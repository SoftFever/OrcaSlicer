#include "../GCode.hpp"
#include "CoolingBuffer.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>
#include <float.h>

#if 0
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

CoolingBuffer::CoolingBuffer(GCode &gcodegen) : m_gcodegen(gcodegen), m_current_extruder(0)
{
    this->reset();
}

void CoolingBuffer::reset()
{
    m_current_pos.assign(5, 0.f);
    Pointf3 pos = m_gcodegen.writer().get_position();
    m_current_pos[0] = float(pos.x);
    m_current_pos[1] = float(pos.y);
    m_current_pos[2] = float(pos.z);
    m_current_pos[4] = float(m_gcodegen.config().travel_speed.value);
}

struct CoolingLine
{
    enum Type {
        TYPE_SET_TOOL           = 1 << 0,
        TYPE_EXTRUDE_END        = 1 << 1,
        TYPE_BRIDGE_FAN_START   = 1 << 2,
        TYPE_BRIDGE_FAN_END     = 1 << 3,
        TYPE_G0                 = 1 << 4,
        TYPE_G1                 = 1 << 5,
        TYPE_ADJUSTABLE         = 1 << 6,
        TYPE_EXTERNAL_PERIMETER = 1 << 7,
        // The line sets a feedrate.
        TYPE_HAS_F              = 1 << 8,
        TYPE_WIPE               = 1 << 9,
        TYPE_G4                 = 1 << 10,
        TYPE_G92                = 1 << 11,
    };

    CoolingLine(unsigned int type, size_t  line_start, size_t  line_end) :
        type(type), line_start(line_start), line_end(line_end),
        length(0.f), time(0.f), time_max(0.f), slowdown(false) {}

    bool adjustable(bool slowdown_external_perimeters) const {
        return (this->type & TYPE_ADJUSTABLE) && 
               (! (this->type & TYPE_EXTERNAL_PERIMETER) || slowdown_external_perimeters) &&
               this->time < this->time_max;
    }

    bool adjustable() const {
        return (this->type & TYPE_ADJUSTABLE) && this->time < this->time_max;
    }

    size_t  type;
    // Start of this line at the G-code snippet.
    size_t  line_start;
    // End of this line at the G-code snippet.
    size_t  line_end;
    // XY Euclidian length of this segment.
    float   length;
    // Current feedrate, possibly adjusted.
    float   feedrate;
    // Current duration of this segment.
    float   time;
    // Maximum duration of this segment.
    float   time_max;
    // If marked with the "slowdown" flag, the line has been slowed down.
    bool    slowdown;
};

// Calculate the required per extruder time stretches.
struct PerExtruderAdjustments 
{
    // Calculate the total elapsed time per this extruder, adjusted for the slowdown.
    float elapsed_time_total() {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            time_total += line.time;
        return time_total;
    }
    // Calculate the maximum time when slowing down.
    float maximum_time(bool slowdown_external_perimeters) {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (line.adjustable(slowdown_external_perimeters)) {
                if (line.time_max == FLT_MAX)
                    return FLT_MAX;
                else
                    time_total += line.time_max;
            } else
                time_total += line.time;
        return time_total;
    }
    // Calculate the non-adjustable part of the total time.
    float non_adjustable_time(bool slowdown_external_perimeters) {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (! line.adjustable(slowdown_external_perimeters))
                time_total += line.time;
        return time_total;
    }
    float slow_down_maximum(bool slowdown_external_perimeters) {
        float time_total = 0.f;
        for (CoolingLine &line : lines) {
            if (line.adjustable(slowdown_external_perimeters)) {
                assert(line.time_max >= 0.f && line.time_max < FLT_MAX);
                line.slowdown = true;
                line.time     = line.time_max;
                line.feedrate = line.length / line.time;
            }
            time_total += line.time;
        }
        return time_total;
    }
    float slow_down_proportional(float factor, bool slowdown_external_perimeters) {
        assert(factor >= 1.f);
        float time_total = 0.f;
        for (CoolingLine &line : lines) {
            if (line.adjustable(slowdown_external_perimeters)) {
                line.slowdown = true;
                line.time     = std::min(line.time_max, line.time * factor);
                line.feedrate = line.length / line.time;
            }
            time_total += line.time;
        }
        return time_total;
    }

    bool operator<(const PerExtruderAdjustments &rhs) const { return this->extruder_id < rhs.extruder_id; }

    // Sort the lines, adjustable first, higher feedrate first.
    void sort_lines_by_decreasing_feedrate() {
        std::sort(lines.begin(), lines.end(), [](const CoolingLine &l1, const CoolingLine &l2) {
            bool adj1 = l1.adjustable();
            bool adj2 = l2.adjustable();
            return (adj1 == adj2) ? l1.feedrate > l2.feedrate : adj1;
        });
        for (n_lines_adjustable = 0; n_lines_adjustable < lines.size(); ++ n_lines_adjustable)
            if ((this->lines[n_lines_adjustable].type & CoolingLine::TYPE_ADJUSTABLE) == 0)
                break;
        time_non_adjustable = 0.f;
        for (size_t i = n_lines_adjustable; i < lines.size(); ++ i)
            time_non_adjustable += lines[i].time;
    }

    // Calculate the maximum time when slowing down.
    float time_stretch_when_slowing_down_to(float min_feedrate) {
        float time_stretch = 0.f;
        if (this->min_print_speed < min_feedrate + EPSILON) {
            for (size_t i = 0; i < n_lines_adjustable; ++ i) {
                const CoolingLine &line = lines[i];
                if (line.feedrate > min_feedrate)
                    time_stretch += line.time * (line.feedrate / min_feedrate - 1.f);
            }
        }
        return time_stretch;
    }

    void slow_down_to(float min_feedrate) {
        if (this->min_print_speed < min_feedrate + EPSILON) {
            for (size_t i = 0; i < n_lines_adjustable; ++ i) {
                CoolingLine &line = lines[i];
                if (line.feedrate > min_feedrate) {
                    line.time *= std::max(1.f, line.feedrate / min_feedrate);
                    line.feedrate = min_feedrate;
                    line.slowdown = true;
                }
            }
        }
    }

    // Extruder, for which the G-code will be adjusted.
    unsigned int                extruder_id         = 0;
    // Minimum print speed allowed for this extruder.
    float                       min_print_speed     = 0.f;

    // Parsed lines.
    std::vector<CoolingLine>    lines;
    // The following two values are set by sort_lines_by_decreasing_feedrate():
    // Number of adjustable lines, at the start of lines.
    size_t                      n_lines_adjustable  = 0;
    // Non-adjustable time of lines starting with n_lines_adjustable. 
    float                       time_non_adjustable = 0;

    // Temporaries for processing the slow down. Both thresholds go from 0 to n_lines_adjustable.
    size_t                      idx_line_begin      = 0;
    size_t                      idx_line_end        = 0;
};

#define EXTRUDER_CONFIG(OPT) config.OPT.get_at(m_current_extruder)

std::string CoolingBuffer::process_layer(const std::string &gcode, size_t layer_id)
{
    const FullPrintConfig       &config        = m_gcodegen.config();
    const std::vector<Extruder> &extruders     = m_gcodegen.writer().extruders();
    const size_t                 num_extruders = extruders.size();

    std::vector<PerExtruderAdjustments> per_extruder_adjustments(num_extruders);
    std::vector<size_t>                 map_extruder_to_per_extruder_adjustment(num_extruders, 0);
    for (size_t i = 0; i < num_extruders; ++ i) {
        unsigned int extruder_id = extruders[i].id();
        per_extruder_adjustments[i].extruder_id = extruder_id;
        per_extruder_adjustments[i].min_print_speed = config.min_print_speed.get_at(i);
        map_extruder_to_per_extruder_adjustment[extruder_id] = i;
    }
    const std::string     toolchange_prefix = m_gcodegen.writer().toolchange_prefix();
    // Parse the layer G-code for the moves, which could be adjusted.
    {
        PerExtruderAdjustments *adjustment  = &per_extruder_adjustments[map_extruder_to_per_extruder_adjustment[m_current_extruder]];
        unsigned int      initial_extruder  = m_current_extruder;
		const char       *line_start = gcode.c_str();
		const char		 *line_end   = line_start;
        const char        extrusion_axis = config.get_extrusion_axis()[0];
        // Index of an existing CoolingLine of the current adjustment, which holds the feedrate setting command
        // for a sequence of extrusion moves.
        size_t            active_speed_modifier = size_t(-1);
		for (; *line_start != 0; line_start = line_end) {
            while (*line_end != '\n' && *line_end != 0)
                ++ line_end;
            // sline will not contain the trailing '\n'.
            std::string sline(line_start, line_end);
            // CoolingLine will contain the trailing '\n'.
            if (*line_end == '\n')
                ++ line_end;
            CoolingLine line(0, line_start - gcode.c_str(), line_end - gcode.c_str());
            if (boost::starts_with(sline, "G0 "))
                line.type = CoolingLine::TYPE_G0;
            else if (boost::starts_with(sline, "G1 "))
                line.type = CoolingLine::TYPE_G1;
            else if (boost::starts_with(sline, "G92 "))
                line.type = CoolingLine::TYPE_G92;
            if (line.type) {
                // G0, G1 or G92
                // Parse the G-code line.
                std::vector<float> new_pos(m_current_pos);
                const char *c = sline.data() + 3;
                for (;;) {
                    // Skip whitespaces.
                    for (; *c == ' ' || *c == '\t'; ++ c);
                    if (*c == 0 || *c == ';')
                        break;
                    // Parse the axis.
                    size_t axis = (*c >= 'X' && *c <= 'Z') ? (*c - 'X') :
                                  (*c == extrusion_axis) ? 3 : (*c == 'F') ? 4 : size_t(-1);
					if (axis != size_t(-1)) {
						new_pos[axis] = float(atof(++c));
						if (axis == 4) {
							// Convert mm/min to mm/sec.
							new_pos[4] /= 60.f;
                            if ((line.type & CoolingLine::TYPE_G92) == 0)
                                // This is G0 or G1 line and it sets the feedrate. This mark is used for reducing the duplicate F calls.
                                line.type |= CoolingLine::TYPE_HAS_F;
                        }
					}
                    // Skip this word.
                    for (; *c != ' ' && *c != '\t' && *c != 0; ++ c);
                }
                bool external_perimeter = boost::contains(sline, ";_EXTERNAL_PERIMETER");
                bool wipe               = boost::contains(sline, ";_WIPE");
                if (external_perimeter)
                    line.type |= CoolingLine::TYPE_EXTERNAL_PERIMETER;
                if (wipe)
                    line.type |= CoolingLine::TYPE_WIPE;
                if (boost::contains(sline, ";_EXTRUDE_SET_SPEED") && ! wipe) {
                    line.type |= CoolingLine::TYPE_ADJUSTABLE;
                    active_speed_modifier = adjustment->lines.size();
                }
                if ((line.type & CoolingLine::TYPE_G92) == 0) {
                    // G0 or G1. Calculate the duration.
                    if (config.use_relative_e_distances.value)
                        // Reset extruder accumulator.
                        m_current_pos[3] = 0.f;
                    float dif[4];
                    for (size_t i = 0; i < 4; ++ i)
                        dif[i] = new_pos[i] - m_current_pos[i];
                    float dxy2 = dif[0] * dif[0] + dif[1] * dif[1];
                    float dxyz2 = dxy2 + dif[2] * dif[2];
                    if (dxyz2 > 0.f) {
                        // Movement in xyz, calculate time from the xyz Euclidian distance.
                        line.length = sqrt(dxyz2);
                    } else if (std::abs(dif[3]) > 0.f) {
                        // Movement in the extruder axis.
                        line.length = std::abs(dif[3]);
                    }
                    if (line.length > 0) {
                        line.feedrate = new_pos[4]; // current F
                        line.time   = line.length / line.feedrate;
                    }
                    line.time_max = line.time;
					if ((line.type & CoolingLine::TYPE_ADJUSTABLE) || active_speed_modifier != size_t(-1))
                        line.time_max = (adjustment->min_print_speed == 0.f) ? FLT_MAX : std::max(line.time, line.length / adjustment->min_print_speed);
					if (active_speed_modifier < adjustment->lines.size() && (line.type & CoolingLine::TYPE_G1)) {
                        // Inside the ";_EXTRUDE_SET_SPEED" blocks, there must not be a G1 Fxx entry.
                        assert((line.type & CoolingLine::TYPE_HAS_F) == 0);
						CoolingLine &sm = adjustment->lines[active_speed_modifier];
						sm.length   += line.length;
						sm.time     += line.time;
						if (sm.time_max != FLT_MAX) {
							if (line.time_max == FLT_MAX)
								sm.time_max = FLT_MAX;
							else
								sm.time_max += line.time_max;
						}
						// Don't store this line.
						line.type = 0;
					}
				}
                m_current_pos = std::move(new_pos);
            } else if (boost::starts_with(sline, ";_EXTRUDE_END")) {
                line.type = CoolingLine::TYPE_EXTRUDE_END;
                active_speed_modifier = size_t(-1);
            } else if (boost::starts_with(sline, toolchange_prefix)) {
                // Switch the tool.
                line.type = CoolingLine::TYPE_SET_TOOL;
                unsigned int new_extruder = (unsigned int)atoi(sline.c_str() + toolchange_prefix.size());
                if (new_extruder != m_current_extruder) {
                    m_current_extruder = new_extruder;
                    adjustment         = &per_extruder_adjustments[map_extruder_to_per_extruder_adjustment[m_current_extruder]];
                }
            } else if (boost::starts_with(sline, ";_BRIDGE_FAN_START")) {
                line.type = CoolingLine::TYPE_BRIDGE_FAN_START;
            } else if (boost::starts_with(sline, ";_BRIDGE_FAN_END")) {
                line.type = CoolingLine::TYPE_BRIDGE_FAN_END;
            } else if (boost::starts_with(sline, "G4 ")) {
                // Parse the wait time.
                line.type = CoolingLine::TYPE_G4;
                size_t pos_S = sline.find('S', 3);
                size_t pos_P = sline.find('P', 3);
                line.time = line.time_max = float(
                    (pos_S > 0) ? atof(sline.c_str() + pos_S + 1) :
                    (pos_P > 0) ? atof(sline.c_str() + pos_P + 1) * 0.001 : 0.);
            }
            if (line.type != 0)
                adjustment->lines.emplace_back(std::move(line));
		}
        m_current_extruder = initial_extruder;
    }

    // Sort the extruders by the increasing slowdown_below_layer_time.
    std::vector<size_t> extruder_by_slowdown_time;
    extruder_by_slowdown_time.reserve(num_extruders);
    // Only insert entries, which are adjustable (have cooling enabled and non-zero stretchable time).
    // Collect total print time of non-adjustable extruders.
    float elapsed_time_total_non_adjustable = 0.f;
    for (size_t i = 0; i < num_extruders; ++ i) {
        if (config.cooling.get_at(extruders[i].id())) {
            extruder_by_slowdown_time.emplace_back(i);
            per_extruder_adjustments[i].sort_lines_by_decreasing_feedrate();
        } else
            elapsed_time_total_non_adjustable += per_extruder_adjustments[i].elapsed_time_total();
    }
    std::sort(extruder_by_slowdown_time.begin(), extruder_by_slowdown_time.end(),
        [&config, &extruders](const size_t idx1, const size_t idx2){
            return config.slowdown_below_layer_time.get_at(extruders[idx1].id()) < 
                   config.slowdown_below_layer_time.get_at(extruders[idx2].id());
        });

    // Elapsed time after adjustment.
    float elapsed_time_total = 0.f;
    {
        // Elapsed time for the already adjusted extruders.
		float elapsed_time_total0 = elapsed_time_total_non_adjustable;
        for (size_t i_extruder_by_slowdown_time = 0; i_extruder_by_slowdown_time < extruder_by_slowdown_time.size(); ++ i_extruder_by_slowdown_time) {
            // Idx in per_extruder_adjustments.
            size_t idx = extruder_by_slowdown_time[i_extruder_by_slowdown_time];
            // Macro to sum or adjust all sections starting with i_extruder_by_slowdown_time.
            #define FORALL_UNPROCESSED(ACCUMULATOR, ACTION) \
                ACCUMULATOR = elapsed_time_total0;\
                for (size_t j = i_extruder_by_slowdown_time; j < extruder_by_slowdown_time.size(); ++ j) \
                    ACCUMULATOR += per_extruder_adjustments[extruder_by_slowdown_time[j]].ACTION
            // Calculate the current adjusted elapsed_time_total over the non-finalized extruders.
            float        total;
            FORALL_UNPROCESSED(total, elapsed_time_total());
            float        slowdown_below_layer_time = float(config.slowdown_below_layer_time.get_at(per_extruder_adjustments[idx].extruder_id)) * 1.001f;
            if (total > slowdown_below_layer_time) {
                // The current total time is above the minimum threshold of the rest of the extruders, don't adjust anything.
            } else {
                // Adjust this and all the following (higher config.slowdown_below_layer_time) extruders.
                // Sum maximum slow down time as if everything was slowed down including the external perimeters.
                float max_time;
                FORALL_UNPROCESSED(max_time, maximum_time(true));
                if (max_time > slowdown_below_layer_time) {
                    // By slowing every possible movement, the layer time could be reached.
#if 0
                    // Now decide, whether the external perimeters shall be slowed down as well.
                    float max_time_nep;
                    FORALL_UNPROCESSED(max_time_nep, maximum_time(false));
                    if (max_time_nep > slowdown_below_layer_time) {
                        // It is sufficient to slow down the non-external perimeter moves to reach the target layer time.
                        // Slow down the non-external perimeters proportionally.
                        float non_adjustable_time;
                        FORALL_UNPROCESSED(non_adjustable_time, non_adjustable_time(false));
                        // The following step is a linear programming task due to the minimum movement speeds of the print moves.
                        // Run maximum 5 iterations until a good enough approximation is reached.
                        for (size_t iter = 0; iter < 5; ++ iter) {
							float factor = (slowdown_below_layer_time - non_adjustable_time) / (total - non_adjustable_time);
                            assert(factor > 1.f);
                            FORALL_UNPROCESSED(total, slow_down_proportional(factor, false));
                            if (total > 0.95f * slowdown_below_layer_time)
                                break;
                        }
                    } else {
                        // Slow down everything. First slow down the non-external perimeters to maximum.
                        FORALL_UNPROCESSED(total, slow_down_maximum(false));
                        // Slow down the external perimeters proportionally.
                        float non_adjustable_time;
                        FORALL_UNPROCESSED(non_adjustable_time, non_adjustable_time(true));
                        for (size_t iter = 0; iter < 5; ++ iter) {
							float factor = (slowdown_below_layer_time - non_adjustable_time) / (total - non_adjustable_time);
                            assert(factor > 1.f);
                            FORALL_UNPROCESSED(total, slow_down_proportional(factor, true));
                            if (total > 0.95f * slowdown_below_layer_time)
                                break;
                        }
                    }
#else
                    // Slow down. Try to equalize the feedrates.
                    std::vector<PerExtruderAdjustments*> by_min_print_speed;
                    by_min_print_speed.reserve(extruder_by_slowdown_time.size() - i_extruder_by_slowdown_time);
                    for (size_t j = i_extruder_by_slowdown_time; j < extruder_by_slowdown_time.size(); ++ j)
                        by_min_print_speed.emplace_back(&per_extruder_adjustments[extruder_by_slowdown_time[j]]);
                    // Find the next highest adjustable feedrate among the extruders.
                    float feedrate = 0;
                    for (PerExtruderAdjustments *adj : by_min_print_speed)
                        if (adj->idx_line_begin < adj->n_lines_adjustable && adj->lines[adj->idx_line_begin].feedrate > feedrate)
                            feedrate = adj->lines[adj->idx_line_begin].feedrate;
                    if (feedrate == 0)
                        // No adjustable line is left.
                        break;
                    // Sort by min_print_speed, maximum speed first.
                    std::sort(by_min_print_speed.begin(), by_min_print_speed.end(), 
                        [](const PerExtruderAdjustments *p1, const PerExtruderAdjustments *p2){ return p1->min_print_speed > p2->min_print_speed; });
                    // Slow down, fast moves first.
                    for (;;) {
                        // For each extruder, find the span of lines with a feedrate close to feedrate.
                        for (PerExtruderAdjustments *adj : by_min_print_speed) {
                            for (adj->idx_line_end = adj->idx_line_begin;
                                adj->idx_line_end < adj->n_lines_adjustable && adj->lines[adj->idx_line_end].feedrate > feedrate - EPSILON;
                                 ++ adj->idx_line_end) ;
                        }
                        // Find the next highest adjustable feedrate among the extruders.
                        float feedrate_next = 0.f;
                        for (PerExtruderAdjustments *adj : by_min_print_speed)
                            if (adj->idx_line_end < adj->n_lines_adjustable && adj->lines[adj->idx_line_end].feedrate > feedrate_next)
                                feedrate_next = adj->lines[adj->idx_line_end].feedrate;
                        // Slow down, limited by max(feedrate_next, min_print_speed).
                        for (auto adj = by_min_print_speed.begin(); adj != by_min_print_speed.end();) {
                            float feedrate_limit = std::max(feedrate_next, (*adj)->min_print_speed);
                            float time_stretch = slowdown_below_layer_time - total;
                            float time_stretch_max = 0.f;
                            std::pair<float, float> time_stretched(0.f, 0.f);
                            for (auto it = adj; it != by_min_print_speed.end(); ++ it)
                                time_stretch_max += (*it)->time_stretch_when_slowing_down_to(feedrate_limit);
							bool done = false;
							if (time_stretch_max > time_stretch) {
								feedrate_limit = feedrate - (feedrate - feedrate_limit) * time_stretch / time_stretch_max;
								done = true;
							}
                            for (auto it = adj; it != by_min_print_speed.end(); ++ it)
                                (*it)->slow_down_to(feedrate_limit);
							if (done) {
								// Break from two levels of loops.
								feedrate_next = 0.f;
								break;
							}
                            // Skip the other extruders with nearly the same min_print_speed, as they have been processed already.
                            auto next = adj;
                            for (++ next; next != by_min_print_speed.end() && (*next)->min_print_speed > (*adj)->min_print_speed - EPSILON; ++ next);
                            adj = next;
                        }
                        if (feedrate_next == 0.f)
                            // There are no other extrusions available for slow down.
                            break;
                        for (PerExtruderAdjustments *adj : by_min_print_speed) {
                            adj->idx_line_begin = adj->idx_line_end;
                            feedrate = feedrate_next;
                        }
                    }
#endif
                } else {
                    // Slow down to maximum possible.
                    FORALL_UNPROCESSED(total, slow_down_maximum(true));
                }
            }
            #undef FORALL_UNPROCESSED
            // Sum the final elapsed time for all extruders up to i_extruder_by_slowdown_time.
            if (i_extruder_by_slowdown_time + 1 == extruder_by_slowdown_time.size())
                // Optimization for single extruder prints.
                elapsed_time_total0 = total;
            else
                elapsed_time_total0 += per_extruder_adjustments[idx].elapsed_time_total();
        }
        elapsed_time_total = elapsed_time_total0;
    }

    // Transform the G-code.
    // First sort the adjustment lines by their position in the source G-code.
    std::vector<const CoolingLine*> lines;
    {
        size_t n_lines = 0;
        for (const PerExtruderAdjustments &adj : per_extruder_adjustments)
            n_lines += adj.lines.size();
        lines.reserve(n_lines);
        for (const PerExtruderAdjustments &adj : per_extruder_adjustments)
            for (const CoolingLine &line : adj.lines)
                lines.emplace_back(&line);
        std::sort(lines.begin(), lines.end(), [](const CoolingLine *ln1, const CoolingLine *ln2) { return ln1->line_start < ln2->line_start; } );
    }
    // Second generate the adjusted G-code.
    std::string new_gcode;
    new_gcode.reserve(gcode.size() * 2);
    int  fan_speed          = -1;
    bool bridge_fan_control = false;
    int  bridge_fan_speed   = 0;
    auto change_extruder_set_fan = [ this, layer_id, elapsed_time_total, &new_gcode, &fan_speed, &bridge_fan_control, &bridge_fan_speed ]() {
        const FullPrintConfig &config = m_gcodegen.config();
        int min_fan_speed = EXTRUDER_CONFIG(min_fan_speed);
        int fan_speed_new = EXTRUDER_CONFIG(fan_always_on) ? min_fan_speed : 0;
        if (layer_id >= EXTRUDER_CONFIG(disable_fan_first_layers)) {
            int   max_fan_speed             = EXTRUDER_CONFIG(max_fan_speed);
            float slowdown_below_layer_time = float(EXTRUDER_CONFIG(slowdown_below_layer_time));
            float fan_below_layer_time      = float(EXTRUDER_CONFIG(fan_below_layer_time));
            if (EXTRUDER_CONFIG(cooling)) {
                if (elapsed_time_total < slowdown_below_layer_time) {
                    // Layer time very short. Enable the fan to a full throttle.
                    fan_speed_new = max_fan_speed;
                } else if (elapsed_time_total < fan_below_layer_time) {
                    // Layer time quite short. Enable the fan proportionally according to the current layer time.
                    assert(elapsed_time_total >= slowdown_below_layer_time);
                    double t = (elapsed_time_total - slowdown_below_layer_time) / (fan_below_layer_time - slowdown_below_layer_time);
                    fan_speed_new = int(floor(t * min_fan_speed + (1. - t) * max_fan_speed) + 0.5);
                }
            }
            bridge_fan_speed   = EXTRUDER_CONFIG(bridge_fan_speed);
            bridge_fan_control = bridge_fan_speed > fan_speed_new;
        } else {
            bridge_fan_control = false;
            bridge_fan_speed   = 0;
            fan_speed_new      = 0;
        }
        if (fan_speed_new != fan_speed) {
            fan_speed = fan_speed_new;
            new_gcode += m_gcodegen.writer().set_fan(fan_speed);
        }
    };
	change_extruder_set_fan();

    const char *pos              = gcode.c_str();
    int         current_feedrate = 0;
    for (const CoolingLine *line : lines) {
        const char *line_start  = gcode.c_str() + line->line_start;
        const char *line_end    = gcode.c_str() + line->line_end;
        if (line_start > pos)
            new_gcode.append(pos, line_start - pos);
        if (line->type & CoolingLine::TYPE_SET_TOOL) {
            unsigned int new_extruder = (unsigned int)atoi(line_start + toolchange_prefix.size());
            if (new_extruder != m_current_extruder) {
                m_current_extruder = new_extruder;
                change_extruder_set_fan();
            }
            new_gcode.append(line_start, line_end - line_start);
        } else if (line->type & CoolingLine::TYPE_BRIDGE_FAN_START) {
            if (bridge_fan_control)
                new_gcode += m_gcodegen.writer().set_fan(bridge_fan_speed, true);
        } else if (line->type & CoolingLine::TYPE_BRIDGE_FAN_END) {
            if (bridge_fan_control)
                new_gcode += m_gcodegen.writer().set_fan(fan_speed, true);
        } else if (line->type & CoolingLine::TYPE_EXTRUDE_END) {
            // Just remove this comment.
        } else if (line->type & (CoolingLine::TYPE_ADJUSTABLE | CoolingLine::TYPE_EXTERNAL_PERIMETER | CoolingLine::TYPE_WIPE | CoolingLine::TYPE_HAS_F)) {
            // Find the start of a comment, or roll to the end of line.
			const char *end = line_start;
			for (; end < line_end && *end != ';'; ++ end);
			// Find the 'F' word.
            const char *fpos            = strstr(line_start + 2, " F") + 2;
            int         new_feedrate    = current_feedrate;
            bool        modify          = false;
            assert(fpos != nullptr);
            if (line->slowdown) {
                modify       = true;
                new_feedrate = int(floor(60. * line->feedrate + 0.5));
            } else {
                new_feedrate = atoi(fpos);
                if (new_feedrate != current_feedrate) {
                    // Append the line without the comment.
                    new_gcode.append(line_start, end - line_start);
                    current_feedrate = new_feedrate;
                } else if ((line->type & (CoolingLine::TYPE_ADJUSTABLE | CoolingLine::TYPE_EXTERNAL_PERIMETER | CoolingLine::TYPE_WIPE)) || line->length == 0.) {
                    // Feedrate does not change and this line does not move the print head. Skip the complete G-code line including the G-code comment.
                    end = line_end;
                } else {
                    // Remove the feedrate from the G0/G1 line.
                    modify = true;
                }
            }
            if (modify) {
                if (new_feedrate != current_feedrate) {
                    // Replace the feedrate.
                    new_gcode.append(line_start, fpos - line_start);
                    current_feedrate = new_feedrate;
                    char buf[64];
                    sprintf(buf, "%d", int(current_feedrate));
                    new_gcode += buf;
                } else {
                    // Remove the feedrate word.
                    const char *f = fpos;
                    // Roll the pointer before the 'F' word.
                    for (f -= 2; f > line_start && (*f == ' ' || *f == '\t'); -- f);
                    // Append up to the F word, without the trailing whitespace.
                    new_gcode.append(line_start, f - line_start + 1);
                }
                // Skip the non-whitespaces of the F parameter up the comment or end of line.
				for (; fpos != end && *fpos != ' ' && *fpos != ';' && *fpos != '\n'; ++fpos);
                // Append the rest of the line without the comment.
                if (fpos < end)
                    new_gcode.append(fpos, end - fpos);
                // There should never be an empty G1 statement emited by the filter. Such lines should be removed completely.
                assert(new_gcode.size() < 4 || new_gcode.substr(new_gcode.size() - 4) != "G1 \n");
            }
            // Process the rest of the line.
            if (end < line_end) {
                if (line->type & (CoolingLine::TYPE_ADJUSTABLE | CoolingLine::TYPE_EXTERNAL_PERIMETER | CoolingLine::TYPE_WIPE)) {
					// Process comments, remove ";_EXTRUDE_SET_SPEED", ";_EXTERNAL_PERIMETER", ";_WIPE"
					std::string comment(end, line_end);
					boost::replace_all(comment, ";_EXTRUDE_SET_SPEED", "");
                    if (line->type & CoolingLine::TYPE_EXTERNAL_PERIMETER)
                        boost::replace_all(comment, ";_EXTERNAL_PERIMETER", "");
                    if (line->type & CoolingLine::TYPE_WIPE)
                        boost::replace_all(comment, ";_WIPE", "");
					new_gcode += comment;
				} else {
					// Just attach the rest of the source line.
					new_gcode.append(end, line_end - end);
				}
            }
        } else {
			new_gcode.append(line_start, line_end - line_start);
        }
        pos = line_end;
    }
    const char *gcode_end = gcode.c_str() + gcode.size();
    if (pos < gcode_end)
        new_gcode.append(pos, gcode_end - pos);

    return new_gcode;
}

} // namespace Slic3r
