#include "ExtrusionEntity.hpp"
#include "Print.hpp"
#include "ToolOrdering.hpp"
#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "ParameterUtils.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <cassert>
#include <limits>
#include <algorithm>

#include <libslic3r.h>

namespace Slic3r {

const static bool g_wipe_into_objects = false;


// Shortest hamilton path problem
static std::vector<unsigned int> solve_extruder_order(const std::vector<std::vector<float>>& wipe_volumes, std::vector<unsigned int> all_extruders, std::optional<unsigned int> start_extruder_id) 
{
    bool add_start_extruder_flag = false;

    if (start_extruder_id) {
        auto start_iter = std::find(all_extruders.begin(), all_extruders.end(), start_extruder_id);
        if (start_iter == all_extruders.end())
            all_extruders.insert(all_extruders.begin(), *start_extruder_id), add_start_extruder_flag = true;
        else
            std::swap(*all_extruders.begin(), *start_iter);
    }
    else {
        *start_extruder_id = all_extruders.front();
    }

    unsigned int iterations = (1 << all_extruders.size());
    unsigned int final_state = iterations - 1;
    std::vector<std::vector<float>>cache(iterations, std::vector<float>(all_extruders.size(),0x7fffffff));
    std::vector<std::vector<int>>prev(iterations, std::vector<int>(all_extruders.size(), -1));
    cache[1][0] = 0.;
    for (unsigned int state = 0; state < iterations; ++state) {
        if (state & 1) {
            for (unsigned int target = 0; target < all_extruders.size(); ++target) {
                if (state >> target & 1) {
                    for (unsigned int mid_point = 0; mid_point < all_extruders.size(); ++mid_point) {
                        if(state>>mid_point&1){
                            auto tmp = cache[state - (1 << target)][mid_point] + wipe_volumes[all_extruders[mid_point]][all_extruders[target]];
                            if (cache[state][target] >tmp) {
                                cache[state][target] = tmp;
                                prev[state][target] = mid_point;
                            }
                        }
                    }
                }
            }
        }
    }

    //get res
    float cost = std::numeric_limits<float>::max();
    int final_dst =0;
    for (unsigned int dst = 0; dst < all_extruders.size(); ++dst) {
        if (all_extruders[dst] != start_extruder_id && cost > cache[final_state][dst]) {
            cost = cache[final_state][dst];
            final_dst = dst;
        }
    }

    std::vector<unsigned int>path;
    unsigned int curr_state = final_state;
    int curr_point = final_dst;
    while (curr_point != -1) {
        path.emplace_back(all_extruders[curr_point]);
        auto mid_point = prev[curr_state][curr_point];
        curr_state -= (1 << curr_point);
        curr_point = mid_point;
    };

    if (add_start_extruder_flag)
        path.pop_back();

    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<unsigned int> get_extruders_order(const std::vector<std::vector<float>> &wipe_volumes, std::vector<unsigned int> all_extruders, std::optional<unsigned int>start_extruder_id)
{
#define USE_DP_OPTIMIZE
#ifdef USE_DP_OPTIMIZE
    return solve_extruder_order(wipe_volumes, all_extruders, start_extruder_id);
#else
if (all_extruders.size() > 1) {
        int begin_index = 0;
        auto iter = std::find(all_extruders.begin(), all_extruders.end(), start_extruder_id);
        if (iter != all_extruders.end()) {
            for (int i = 0; i < all_extruders.size(); ++i) {
                if (all_extruders[i] == start_extruder_id) {
                    std::swap(all_extruders[i], all_extruders[0]);
                }
            }
            begin_index = 1;
        }

        std::pair<float, std::vector<unsigned int>> volumes_to_extruder_order;
        volumes_to_extruder_order.first = 10000 * all_extruders.size();
        std::sort(all_extruders.begin() + begin_index, all_extruders.end());
        do {
            float flush_volume = 0;
            for (int i = 0; i < all_extruders.size() - 1; ++i) {
                flush_volume += wipe_volumes[all_extruders[i]][all_extruders[i + 1]];
            }
            if (flush_volume < volumes_to_extruder_order.first) {
                volumes_to_extruder_order = std::pair(flush_volume, all_extruders);
            }
        } while (std::next_permutation(all_extruders.begin() + begin_index, all_extruders.end()));

        if (volumes_to_extruder_order.second.size() > 0)
            return volumes_to_extruder_order.second;
    }
    return all_extruders;

#endif // OPTIMIZE
}

// Returns true in case that extruder a comes before b (b does not have to be present). False otherwise.
bool LayerTools::is_extruder_order(unsigned int a, unsigned int b) const
{
    if (a == b)
        return false;

    for (auto extruder : extruders) {
        if (extruder == a)
            return true;
        if (extruder == b)
            return false;
    }

    return false;
}

// Return a zero based extruder from the region, or extruder_override if overriden.
unsigned int LayerTools::wall_filament(const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().wall_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::sparse_infill_filament(const PrintRegion &region) const
{
	assert(region.config().sparse_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().sparse_infill_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::solid_infill_filament(const PrintRegion &region) const
{
	assert(region.config().solid_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().solid_infill_filament.value : this->extruder_override) - 1;
}

// Returns a zero based extruder this eec should be printed with, according to PrintRegion config or extruder_override if overriden.
unsigned int LayerTools::extruder(const ExtrusionEntityCollection &extrusions, const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	assert(region.config().sparse_infill_filament.value > 0);
	assert(region.config().solid_infill_filament.value > 0);
	// 1 based extruder ID.
    unsigned int extruder = 1;
    if (this->extruder_override == 0) {
        if (extrusions.has_infill()) {
            if (extrusions.has_solid_infill())
                extruder = region.config().solid_infill_filament;
            else
                extruder = region.config().sparse_infill_filament;
        } else
            extruder = region.config().wall_filament.value;
    } else
        extruder = this->extruder_override;

    return (extruder == 0) ? 0 : extruder - 1;
}

static double calc_max_layer_height(const PrintConfig &config, double max_object_layer_height)
{
    double max_layer_height = std::numeric_limits<double>::max();
    for (size_t i = 0; i < config.nozzle_diameter.values.size(); ++ i) {
        double mlh = config.max_layer_height.values[i];
        if (mlh == 0.)
            mlh = 0.75 * config.nozzle_diameter.values[i];
        max_layer_height = std::min(max_layer_height, mlh);
    }
    // The Prusa3D Fast (0.35mm layer height) print profile sets a higher layer height than what is normally allowed
    // by the nozzle. This is a hack and it works by increasing extrusion width. See GH #3919.
    return std::max(max_layer_height, max_object_layer_height);
}

// For the use case when each object is printed separately
// (print->config().print_sequence == PrintSequence::ByObject is true).
ToolOrdering::ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material)
{
    m_is_BBL_printer = object.print()->is_BBL_printer();
    m_print_full_config = &object.print()->full_print_config();
    m_print_object_ptr = &object;
    if (object.layers().empty())
        return;

    // Initialize the print layers for just a single object.
    {
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers().size() + object.support_layers().size());
        for (auto layer : object.layers())
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers())
            zs.emplace_back(layer->print_z);
        this->initialize_layers(zs);
    }
    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    // Collect extruders reuqired to print the layers.
    this->collect_extruders(object, std::vector<std::pair<double, unsigned int>>());

    // BBS
    // Reorder the extruders to minimize tool switches.
    std::vector<unsigned int> first_layer_tool_order;
    if (first_extruder == (unsigned int) -1) {
        first_layer_tool_order = generate_first_layer_tool_order(object);
    }

    if (!first_layer_tool_order.empty()) {
        this->reorder_extruders(first_layer_tool_order);
    } else {
        this->reorder_extruders(first_extruder);
    }

    this->fill_wipe_tower_partitions(object.print()->config(), object.layers().front()->print_z - object.layers().front()->height, max_layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(object.print()->config(), max_layer_height);
}

// For the use case when all objects are printed at once.
// (print->config().print_sequence == PrintSequence::ByObject is false).
ToolOrdering::ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material)
{
    m_is_BBL_printer = print.is_BBL_printer();
    m_print_full_config = &print.full_print_config();
    m_print_config_ptr = &print.config();

    // Initialize the print layers for all objects and all layers.
    coordf_t object_bottom_z = 0.;
    coordf_t max_layer_height = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects()) {
            zs.reserve(zs.size() + object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.emplace_back(layer->print_z);

            // Find first object layer that is not empty and save its print_z
            for (const Layer* layer : object->layers())
                if (layer->has_extrusions()) {
                    object_bottom_z = layer->print_z - layer->height;
                    break;
                }

            max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
        }
        this->initialize_layers(zs);
    }
    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

	// Use the extruder switches from Model::custom_gcode_per_print_z to override the extruder to print the object.
	// Do it only if all the objects were configured to be printed with a single extruder.
	std::vector<std::pair<double, unsigned int>> per_layer_extruder_switches;

    // BBS
	if (auto num_filaments = unsigned(print.config().filament_diameter.size());
		num_filaments > 1 && print.object_extruders().size() == 1 && // the current Print's configuration is CustomGCode::MultiAsSingle
        //BBS: replace model custom gcode with current plate custom gcode
        print.model().get_curr_plate_custom_gcodes().mode == CustomGCode::MultiAsSingle) {
		// Printing a single extruder platter on a printer with more than 1 extruder (or single-extruder multi-material).
		// There may be custom per-layer tool changes available at the model.
        per_layer_extruder_switches = custom_tool_changes(print.model().get_curr_plate_custom_gcodes(), num_filaments);
	}

    // Collect extruders reuqired to print the layers.
    for (auto object : print.objects())
        this->collect_extruders(*object, per_layer_extruder_switches);

    // Reorder the extruders to minimize tool switches.
    std::vector<unsigned int> first_layer_tool_order;
    if (first_extruder == (unsigned int)-1) {
        first_layer_tool_order = generate_first_layer_tool_order(print);
    }

    if (!first_layer_tool_order.empty()) {
        this->reorder_extruders(first_layer_tool_order);
    }
    else {
        this->reorder_extruders(first_extruder);
    }

    this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(print.config(), max_layer_height);
}

static void apply_first_layer_order(const DynamicPrintConfig* config, std::vector<unsigned int>& tool_order) {
    const ConfigOptionInts* first_layer_print_sequence_op = config->option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_layer_print_sequence_op) {
        const std::vector<int>& print_sequence_1st = first_layer_print_sequence_op->values;
        if (print_sequence_1st.size() >= tool_order.size()) {
            std::sort(tool_order.begin(), tool_order.end(), [&print_sequence_1st](int lh, int rh) {
                auto lh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), lh);
                auto rh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), rh);

                if (lh_it == print_sequence_1st.end() || rh_it == print_sequence_1st.end())
                    return false;

                return lh_it < rh_it;
            });
        }
    }
}

// BBS
std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const Print& print)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;

    for (auto object : print.objects()) {
        auto first_layer = object->get_layer(0);
        for (auto layerm : first_layer->regions()) {
            int extruder_id = layerm->region().config().option("wall_filament")->getInt();
            
            for (auto expoly : layerm->raw_slices) {
                const double nozzle_diameter = print.config().nozzle_diameter.get_at(0);
                const coordf_t initial_layer_line_width = print.config().get_abs_value("initial_layer_line_width", nozzle_diameter);

                if (offset_ex(expoly, -0.2 * scale_(initial_layer_line_width)).empty())
                    continue;

                double contour_area = expoly.contour.area();
                auto iter = min_areas_per_extruder.find(extruder_id);
                if (iter == min_areas_per_extruder.end()) {
                    min_areas_per_extruder.insert({ extruder_id, contour_area });
                }
                else {
                    if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                        min_areas_per_extruder[extruder_id] = contour_area;
                    }
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    apply_first_layer_order(m_print_full_config, tool_order);

    return tool_order;
}

std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const PrintObject& object)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;
    auto first_layer = object.get_layer(0);
    for (auto layerm : first_layer->regions()) {
        int extruder_id = layerm->region().config().option("wall_filament")->getInt();
        for (auto expoly : layerm->raw_slices) {
            const double nozzle_diameter = object.print()->config().nozzle_diameter.get_at(0);
            const coordf_t line_width = object.config().get_abs_value("line_width", nozzle_diameter);

            if (offset_ex(expoly, -0.2 * scale_(line_width)).empty())
                continue;

            double contour_area = expoly.contour.area();
            auto iter = min_areas_per_extruder.find(extruder_id);
            if (iter == min_areas_per_extruder.end()) {
                min_areas_per_extruder.insert({ extruder_id, contour_area });
            }
            else {
                if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                    min_areas_per_extruder[extruder_id] = contour_area;
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    apply_first_layer_order(m_print_full_config, tool_order);

    return tool_order;
}

void ToolOrdering::initialize_layers(std::vector<coordf_t> &zs)
{
    sort_remove_duplicates(zs);
    // Merge numerically very close Z values.
    for (size_t i = 0; i < zs.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = zs[i] + EPSILON;
        for (; j < zs.size() && zs[j] <= zmax; ++ j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1])));
        i = j;
    }
}

// Collect extruders reuqired to print layers.
void ToolOrdering::collect_extruders(const PrintObject &object, const std::vector<std::pair<double, unsigned int>> &per_layer_extruder_switches)
{
    // Collect the support extruders.
    for (auto support_layer : object.support_layers()) {
        LayerTools   &layer_tools = this->tools_for_layer(support_layer->print_z);
        ExtrusionRole role = support_layer->support_fills.role();
        bool         has_support        = role == erMixed || role == erSupportMaterial || role == erSupportTransition;
        bool         has_interface      = role == erMixed || role == erSupportMaterialInterface;
        unsigned int extruder_support   = object.config().support_filament.value;
        unsigned int extruder_interface = object.config().support_interface_filament.value;
        if (has_support)
            layer_tools.extruders.push_back(extruder_support);
        if (has_interface)
            layer_tools.extruders.push_back(extruder_interface);
        if (has_support || has_interface) {
            layer_tools.has_support = true;
            layer_tools.wiping_extrusions().is_support_overriddable_and_mark(role, object);
        }
    }

    // Extruder overrides are ordered by print_z.
    std::vector<std::pair<double, unsigned int>>::const_iterator it_per_layer_extruder_override;
	it_per_layer_extruder_override = per_layer_extruder_switches.begin();
    unsigned int extruder_override = 0;

    // BBS: collect first layer extruders of an object's wall, which will be used by brim generator
    int layerCount = 0;
    std::vector<int> firstLayerExtruders;
    firstLayerExtruders.clear();

    // Collect the object extruders.
    for (auto layer : object.layers()) {
        LayerTools &layer_tools = this->tools_for_layer(layer->print_z);

        // Override extruder with the next 
    	for (; it_per_layer_extruder_override != per_layer_extruder_switches.end() && it_per_layer_extruder_override->first < layer->print_z + EPSILON; ++ it_per_layer_extruder_override)
    		extruder_override = (int)it_per_layer_extruder_override->second;

        // Store the current extruder override (set to zero if no overriden), so that layer_tools.wiping_extrusions().is_overridable_and_mark() will use it.
        layer_tools.extruder_override = extruder_override;

        // What extruders are required to print this object layer?
        for (const LayerRegion *layerm : layer->regions()) {
            const PrintRegion &region = layerm->region();

            if (! layerm->perimeters.entities.empty()) {
                bool something_nonoverriddable = true;

                if (m_print_config_ptr) { // in this case print->config().print_sequence != PrintSequence::ByObject (see ToolOrdering constructors)
                    something_nonoverriddable = false;
                    for (const auto& eec : layerm->perimeters.entities) // let's check if there are nonoverriddable entities
                        if (!layer_tools.wiping_extrusions().is_overriddable_and_mark(dynamic_cast<const ExtrusionEntityCollection&>(*eec), *m_print_config_ptr, object, region))
                            something_nonoverriddable = true;
                }

                if (something_nonoverriddable){
               		layer_tools.extruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    if (layerCount == 0) {
                        firstLayerExtruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    }
                }

                layer_tools.has_object = true;
            }

            bool has_infill       = false;
            bool has_solid_infill = false;
            bool something_nonoverriddable = false;
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? erNone : fill->entities.front()->role();
                if (is_solid_infill(role))
                    has_solid_infill = true;
                else if (role != erNone)
                    has_infill = true;

                if (m_print_config_ptr) {
                    if (! layer_tools.wiping_extrusions().is_overriddable_and_mark(*fill, *m_print_config_ptr, object, region))
                        something_nonoverriddable = true;
                }
            }

            if (something_nonoverriddable || !m_print_config_ptr) {
            	if (extruder_override == 0) {
	                if (has_solid_infill)
	                    layer_tools.extruders.emplace_back(region.config().solid_infill_filament);
	                if (has_infill)
	                    layer_tools.extruders.emplace_back(region.config().sparse_infill_filament);
            	} else if (has_solid_infill || has_infill)
            		layer_tools.extruders.emplace_back(extruder_override);
            }
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
        layerCount++;
    }

    sort_remove_duplicates(firstLayerExtruders);
    const_cast<PrintObject&>(object).object_first_layer_wall_extruders = firstLayerExtruders;
    
    for (auto& layer : m_layer_tools) {
        // Sort and remove duplicates
        sort_remove_duplicates(layer.extruders);

        // make sure that there are some tools for each object layer (e.g. tall wiping object will result in empty extruders vector)
        if (layer.extruders.empty() && layer.has_object)
            layer.extruders.emplace_back(0); // 0="dontcare" extruder - it will be taken care of in reorder_extruders
    }
}

// Reorder extruders to minimize layer changes.
void ToolOrdering::reorder_extruders(unsigned int last_extruder_id)
{
    if (m_layer_tools.empty())
        return;

    if (last_extruder_id == (unsigned int)-1) {
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < m_layer_tools.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = m_layer_tools[i];
            for (unsigned int extruder_id : lt.extruders)
                if (extruder_id > 0) {
                    last_extruder_id = extruder_id;
                    break;
                }
        }
        if (last_extruder_id == 0)
            // Nothing to extrude.
            return;
    } else
        // 1 based index
        ++ last_extruder_id;

    for (LayerTools &lt : m_layer_tools) {
        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++ i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }

            if (lt == m_layer_tools[0]) {
                // On first layer with wipe tower, prefer a soluble extruder
                // at the beginning, so it is not wiped on the first layer.
                if (m_print_config_ptr && m_print_config_ptr->enable_prime_tower) {
                    for (size_t i = 0; i<lt.extruders.size(); ++i)
                        if (m_print_config_ptr->filament_soluble.get_at(lt.extruders[i]-1)) { // 1-based...
                            std::swap(lt.extruders[i], lt.extruders.front());
                            break;
                        }
                }

                // Then, if we specified the tool order, apply it now
                apply_first_layer_order(m_print_full_config, lt.extruders);
            }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools &lt : m_layer_tools)
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }

    // reorder the extruders for minimum flush volume
    reorder_extruders_for_minimum_flush_volume();
}

// BBS
void ToolOrdering::reorder_extruders(std::vector<unsigned int> tool_order_layer0)
{
    if (m_layer_tools.empty())
        return;

    if (tool_order_layer0.empty())
        return;

    // Reorder the extruders of first layer
    {
        LayerTools& lt = m_layer_tools[0];
        std::vector<unsigned int> layer0_extruders = lt.extruders;
        lt.extruders.clear();
        for (unsigned int extruder_id : tool_order_layer0) {
            auto iter = std::find(layer0_extruders.begin(), layer0_extruders.end(), extruder_id);
            if (iter != layer0_extruders.end()) {
                lt.extruders.push_back(extruder_id);
                *iter = (unsigned int)-1;
            }
        }

        for (unsigned int extruder_id : layer0_extruders) {
            if (extruder_id == 0)
                continue;

            if (extruder_id != (unsigned int)-1)
                lt.extruders.push_back(extruder_id);
        }

        // all extruders are zero
        if (lt.extruders.empty()) {
            lt.extruders.push_back(tool_order_layer0[0]);
        }
    }

    int last_extruder_id = m_layer_tools[0].extruders.back();
    for (int i = 1; i < m_layer_tools.size(); i++) {
        LayerTools& lt = m_layer_tools[i];

        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools& lt : m_layer_tools)
        for (unsigned int& extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            --extruder_id;
        }

    // reorder the extruders for minimum flush volume
    reorder_extruders_for_minimum_flush_volume();
}

void ToolOrdering::fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : m_layer_tools) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(m_layer_tools.size()) - 2; i >= 0; -- i)
        m_layer_tools[i].wipe_tower_partitions = std::max(m_layer_tools[i + 1].wipe_tower_partitions, m_layer_tools[i].wipe_tower_partitions);

    //FIXME this is a hack to get the ball rolling.
    for (LayerTools &lt : m_layer_tools)
        lt.has_wipe_tower = (lt.has_object && (config.timelapse_type == TimelapseType::tlSmooth || lt.wipe_tower_partitions > 0))
            || lt.print_z < object_bottom_z + EPSILON;

    // Test for a raft, insert additional wipe tower layer to fill in the raft separation gap.
    for (size_t i = 0; i + 1 < m_layer_tools.size(); ++ i) {
        const LayerTools &lt      = m_layer_tools[i];
        const LayerTools &lt_next = m_layer_tools[i + 1];
        if (lt.print_z < object_bottom_z + EPSILON && lt_next.print_z >= object_bottom_z + EPSILON) {
            // lt is the last raft layer. Find the 1st object layer.
            size_t j = i + 1;
            for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_wipe_tower; ++ j);
            if (j < m_layer_tools.size()) {
                const LayerTools &lt_object = m_layer_tools[j];
                coordf_t gap = lt_object.print_z - lt.print_z;
                assert(gap > 0.f);
                if (gap > max_layer_height + EPSILON) {
                    // Insert one additional wipe tower layer between lh.print_z and lt_object.print_z.
                    LayerTools lt_new(0.5f * (lt.print_z + lt_object.print_z));
                    // Find the 1st layer above lt_new.
                    for (j = i + 1; j < m_layer_tools.size() && m_layer_tools[j].print_z < lt_new.print_z - EPSILON; ++ j);
                    if (std::abs(m_layer_tools[j].print_z - lt_new.print_z) < EPSILON) {
						m_layer_tools[j].has_wipe_tower = true;
					} else {
						LayerTools &lt_extra = *m_layer_tools.insert(m_layer_tools.begin() + j, lt_new);
                        //LayerTools &lt_prev  = m_layer_tools[j];
                        LayerTools &lt_next  = m_layer_tools[j + 1];
                        assert(! m_layer_tools[j - 1].extruders.empty() && ! lt_next.extruders.empty());
                        // FIXME: Following assert tripped when running combine_infill.t. I decided to comment it out for now.
                        // If it is a bug, it's likely not critical, because this code is unchanged for a long time. It might
                        // still be worth looking into it more and decide if it is a bug or an obsolete assert.
                        //assert(lt_prev.extruders.back() == lt_next.extruders.front());
                        lt_extra.has_wipe_tower = true;
                        lt_extra.extruders.push_back(lt_next.extruders.front());
                        lt_extra.wipe_tower_partitions = lt_next.wipe_tower_partitions;
                    }
                }
            }
            break;
        }
    }

    // If the model contains empty layers (such as https://github.com/prusa3d/Slic3r/issues/1266), there might be layers
    // that were not marked as has_wipe_tower, even when they should have been. This produces a crash with soluble supports
    // and maybe other problems. We will therefore go through layer_tools and detect and fix this.
    // So, if there is a non-object layer starting with different extruder than the last one ended with (or containing more than one extruder),
    // we'll mark it with has_wipe tower.
    for (unsigned int i=0; i+1<m_layer_tools.size(); ++i) {
        LayerTools& lt = m_layer_tools[i];
        LayerTools& lt_next = m_layer_tools[i+1];
        if (lt.extruders.empty() || lt_next.extruders.empty())
            break;
        if (!lt_next.has_wipe_tower && (lt_next.extruders.front() != lt.extruders.back() || lt_next.extruders.size() > 1))
            lt_next.has_wipe_tower = true;
        // We should also check that the next wipe tower layer is no further than max_layer_height:
        unsigned int j = i+1;
        double last_wipe_tower_print_z = lt_next.print_z;
        while (++j < m_layer_tools.size()-1 && !m_layer_tools[j].has_wipe_tower)
            if (m_layer_tools[j+1].print_z - last_wipe_tower_print_z > max_layer_height + EPSILON) {
                m_layer_tools[j].has_wipe_tower = true;
                last_wipe_tower_print_z = m_layer_tools[j].print_z;
            }
    }

    // Calculate the wipe_tower_layer_height values.
    coordf_t wipe_tower_print_z_last = 0.;
    for (LayerTools &lt : m_layer_tools)
        if (lt.has_wipe_tower) {
            lt.wipe_tower_layer_height = lt.print_z - wipe_tower_print_z_last;
            wipe_tower_print_z_last = lt.print_z;
        }
}

void ToolOrdering::collect_extruder_statistics(bool prime_multi_material)
{
    m_first_printing_extruder = (unsigned int)-1;
    for (const auto &lt : m_layer_tools)
        if (! lt.extruders.empty()) {
            m_first_printing_extruder = lt.extruders.front();
            break;
        }

    m_last_printing_extruder = (unsigned int)-1;
    for (auto lt_it = m_layer_tools.rbegin(); lt_it != m_layer_tools.rend(); ++ lt_it)
        if (! lt_it->extruders.empty()) {
            m_last_printing_extruder = lt_it->extruders.back();
            break;
        }

    m_all_printing_extruders.clear();
    for (const auto &lt : m_layer_tools) {
        append(m_all_printing_extruders, lt.extruders);
        sort_remove_duplicates(m_all_printing_extruders);
    }

    if (prime_multi_material && ! m_all_printing_extruders.empty()) {
        // Reorder m_all_printing_extruders in the sequence they will be primed, the last one will be m_first_printing_extruder.
        // Then set m_first_printing_extruder to the 1st extruder primed.
        m_all_printing_extruders.erase(
            std::remove_if(m_all_printing_extruders.begin(), m_all_printing_extruders.end(),
                [ this ](const unsigned int eid) { return eid == m_first_printing_extruder; }),
            m_all_printing_extruders.end());
        m_all_printing_extruders.emplace_back(m_first_printing_extruder);
        m_first_printing_extruder = m_all_printing_extruders.front();
    }
}

void ToolOrdering::reorder_extruders_for_minimum_flush_volume()
{
    const PrintConfig *print_config = m_print_config_ptr;
    if (!print_config && m_print_object_ptr) {
        print_config = &(m_print_object_ptr->print()->config());
    }

    if (!print_config || m_layer_tools.empty())
        return;

    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> flush_matrix(cast<float>(print_config->flush_volumes_matrix.values));
    const unsigned int number_of_extruders = (unsigned int) (sqrt(flush_matrix.size()) + EPSILON);
    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    if ((print_config->purge_in_prime_tower && print_config->single_extruder_multi_material) || m_is_BBL_printer) {
        for (unsigned int i = 0; i < number_of_extruders; ++i)
            wipe_volumes.push_back( std::vector<float>(flush_matrix.begin() + i * number_of_extruders,
                                                       flush_matrix.begin() + (i + 1) * number_of_extruders));
    } else {
        // populate wipe_volumes with prime_volume
        for (unsigned int i = 0; i < number_of_extruders; ++i)
            wipe_volumes.push_back(std::vector<float>(number_of_extruders, print_config->prime_volume));
    }

    auto extruders_to_hash_key = [](const std::vector<unsigned int>& extruders,
                                    std::optional<unsigned int>      initial_extruder_id) -> uint32_t {
        uint32_t hash_key = 0;
        // high 16 bit define initial extruder ,low 16 bit define extruder set
        if (initial_extruder_id)
            hash_key |= (1 << (16 + *initial_extruder_id));
        for (auto item : extruders)
            hash_key |= (1 << item);
        return hash_key;
    };

    std::vector<LayerPrintSequence> other_layers_seqs;
    const ConfigOptionInts *other_layers_print_sequence_op = print_config->option<ConfigOptionInts>("other_layers_print_sequence");
    const ConfigOptionInt *other_layers_print_sequence_nums_op = print_config->option<ConfigOptionInt>("other_layers_print_sequence_nums");
    if (other_layers_print_sequence_op && other_layers_print_sequence_nums_op) {
        const std::vector<int> &print_sequence = other_layers_print_sequence_op->values;
        int sequence_nums = other_layers_print_sequence_nums_op->value;
        other_layers_seqs = get_other_layers_print_sequence(sequence_nums, print_sequence);
    }

    // other_layers_seq: the layer_idx and extruder_idx are base on 1
    auto get_custom_seq = [&other_layers_seqs](int layer_idx, std::vector<int>& out_seq) -> bool {
        for (size_t idx = other_layers_seqs.size() - 1; idx != size_t(-1); --idx) {
            const auto &other_layers_seq = other_layers_seqs[idx];
            if (layer_idx + 1 >= other_layers_seq.first.first && layer_idx + 1 <= other_layers_seq.first.second) {
                out_seq = other_layers_seq.second;
                return true;
            }
        }
        return false;
    };

    std::optional<unsigned int>current_extruder_id;
    for (int i = 0; i < m_layer_tools.size(); ++i) {
        LayerTools& lt = m_layer_tools[i];
        if (lt.extruders.empty())
            continue;

        std::vector<int> custom_extruder_seq;
        if (get_custom_seq(i, custom_extruder_seq) && !custom_extruder_seq.empty()) {
            std::vector<unsigned int> unsign_custom_extruder_seq;
            for (int extruder : custom_extruder_seq) {
                unsigned int unsign_extruder = static_cast<unsigned int>(extruder) - 1;
                auto it = std::find(lt.extruders.begin(), lt.extruders.end(), unsign_extruder);
                if (it != lt.extruders.end()) {
                    unsign_custom_extruder_seq.emplace_back(unsign_extruder);
                }
            }
            assert(lt.extruders.size() == unsign_custom_extruder_seq.size());
            lt.extruders = unsign_custom_extruder_seq;
            current_extruder_id = lt.extruders.back();
            continue;
        }

        // The algorithm complexity is O(n2*2^n)
        if (i != 0) {
            auto hash_key = extruders_to_hash_key(lt.extruders, current_extruder_id);
            auto iter = m_tool_order_cache.find(hash_key);
            if (iter == m_tool_order_cache.end()) {
                lt.extruders = get_extruders_order(wipe_volumes, lt.extruders, current_extruder_id);
                std::vector<uint8_t> hash_val;
                hash_val.reserve(lt.extruders.size());
                for (auto item : lt.extruders)
                    hash_val.emplace_back(static_cast<uint8_t>(item));
                m_tool_order_cache[hash_key] = hash_val;
            }
            else {
                std::vector<unsigned int>extruder_order;
                extruder_order.reserve(iter->second.size());
                for (auto item : iter->second)
                    extruder_order.emplace_back(static_cast<unsigned int>(item));
                lt.extruders = std::move(extruder_order);
            }
        }
        current_extruder_id = lt.extruders.back();
    }
}

// Layers are marked for infinite skirt aka draft shield. Not all the layers have to be printed.
void ToolOrdering::mark_skirt_layers(const PrintConfig &config, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    if (m_layer_tools.front().extruders.empty()) {
        // Empty first layer, no skirt will be printed.
        //FIXME throw an exception?
        return;
    }

    size_t i = 0;
    for (;;) {
        m_layer_tools[i].has_skirt = true;
        size_t j = i + 1;
        for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_object; ++ j);
        // i and j are two successive layers printing an object.
        if (j == m_layer_tools.size())
            // Don't print skirt above the last object layer.
            break;
        // Mark some printing intermediate layers as having skirt.
        double last_z = m_layer_tools[i].print_z;
        for (size_t k = i + 1; k < j; ++ k) {
            if (m_layer_tools[k + 1].print_z - last_z > max_layer_height + EPSILON) {
                // Layer k is the last one not violating the maximum layer height.
                // Don't extrude skirt on empty layers.
                while (m_layer_tools[k].extruders.empty())
                    -- k;
                if (m_layer_tools[k].has_skirt) {
                    // Skirt cannot be generated due to empty layers, there would be a missing layer in the skirt.
                    //FIXME throw an exception?
                    break;
                }
                m_layer_tools[k].has_skirt = true;
                last_z = m_layer_tools[k].print_z;
            }
        }
        i = j;
    }
}

// Assign a pointer to a custom G-code to the respective ToolOrdering::LayerTools.
// Ignore color changes, which are performed on a layer and for such an extruder, that the extruder will not be printing above that layer.
// If multiple events are planned over a span of a single layer, use the last one.

// BBS: replace model custom gcode with current plate custom gcode
static CustomGCode::Info custom_gcode_per_print_z;
void ToolOrdering::assign_custom_gcodes(const Print &print)
{
	// Only valid for non-sequential print.
	assert(print.config().print_sequence == PrintSequence::ByLayer);

    custom_gcode_per_print_z = print.model().get_curr_plate_custom_gcodes();
	if (custom_gcode_per_print_z.gcodes.empty())
		return;

    // BBS
	auto 						num_filaments = unsigned(print.config().filament_diameter.size());
	CustomGCode::Mode 			mode          =
		(num_filaments == 1) ? CustomGCode::SingleExtruder :
		print.object_extruders().size() == 1 ? CustomGCode::MultiAsSingle : CustomGCode::MultiExtruder;
    CustomGCode::Mode           model_mode    = print.model().get_curr_plate_custom_gcodes().mode;
	std::vector<unsigned char> 	extruder_printing_above(num_filaments, false);
	auto 						custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin();
	// Tool changes and color changes will be ignored, if the model's tool/color changes were entered in mm mode and the print is in non mm mode
	// or vice versa.
	bool 						ignore_tool_and_color_changes = (mode == CustomGCode::MultiExtruder) != (model_mode == CustomGCode::MultiExtruder);
	// If printing on a single extruder machine, make the tool changes trigger color change (M600) events.
	bool 						tool_changes_as_color_changes = mode == CustomGCode::SingleExtruder && model_mode == CustomGCode::MultiAsSingle;

	// From the last layer to the first one:
    coordf_t print_z_above = std::numeric_limits<coordf_t>::lowest();
	for (auto it_lt = m_layer_tools.rbegin(); it_lt != m_layer_tools.rend(); ++ it_lt) {
		LayerTools &lt = *it_lt;
		// Add the extruders of the current layer to the set of extruders printing at and above this print_z.
		for (unsigned int i : lt.extruders)
			extruder_printing_above[i] = true;
		// Skip all custom G-codes above this layer and skip all extruder switches.
		for (; custom_gcode_it != custom_gcode_per_print_z.gcodes.rend() && (
            (print_z_above > lt.print_z && custom_gcode_it->print_z > 0.5 * (lt.print_z + print_z_above))
            || custom_gcode_it->type == CustomGCode::ToolChange); ++ custom_gcode_it);
        print_z_above = lt.print_z;
		if (custom_gcode_it == custom_gcode_per_print_z.gcodes.rend())
			// Custom G-codes were processed.
			break;
		// Some custom G-code is configured for this layer or a layer below.
		const CustomGCode::Item &custom_gcode = *custom_gcode_it;
		// print_z of the layer below the current layer.
		coordf_t print_z_below = 0.;
		if (auto it_lt_below = it_lt; ++ it_lt_below != m_layer_tools.rend())
			print_z_below = it_lt_below->print_z;
        if (custom_gcode.print_z > 0.5 * (print_z_below + lt.print_z)) {
			// The custom G-code applies to the current layer.
			bool color_change = custom_gcode.type == CustomGCode::ColorChange;
			bool tool_change  = custom_gcode.type == CustomGCode::ToolChange;
			bool pause_or_custom_gcode = ! color_change && ! tool_change;
			bool apply_color_change = ! ignore_tool_and_color_changes &&
				// If it is color change, it will actually be useful as the exturder above will print.
                // BBS
				(color_change ? 
					mode == CustomGCode::SingleExtruder || 
						(custom_gcode.extruder <= int(num_filaments) && extruder_printing_above[unsigned(custom_gcode.extruder - 1)]) :
				 	tool_change && tool_changes_as_color_changes);
			if (pause_or_custom_gcode || apply_color_change)
        		lt.custom_gcode = &custom_gcode;
			// Consume that custom G-code event.
			++ custom_gcode_it;
		}
	}
}

const LayerTools& ToolOrdering::tools_for_layer(coordf_t print_z) const
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), LayerTools(print_z - EPSILON));
    assert(it_layer_tools != m_layer_tools.end());
    coordf_t dist_min = std::abs(it_layer_tools->print_z - print_z);
    for (++ it_layer_tools; it_layer_tools != m_layer_tools.end(); ++ it_layer_tools) {
        coordf_t d = std::abs(it_layer_tools->print_z - print_z);
        if (d >= dist_min)
            break;
        dist_min = d;
    }
    -- it_layer_tools;
    assert(dist_min < EPSILON);
    return *it_layer_tools;
}

// This function is called from Print::mark_wiping_extrusions and sets extruder this entity should be printed with (-1 .. as usual)
void WipingExtrusions::set_extruder_override(const ExtrusionEntity* entity, const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;

    auto entity_map_it = (entity_map.emplace(std::make_tuple(entity, object), ExtruderPerCopy())).first; // (add and) return iterator
    ExtruderPerCopy& copies_vector = entity_map_it->second;
    copies_vector.resize(num_of_copies, -1);

    if (copies_vector[copy_id] != -1)
        std::cout << "ERROR: Entity extruder overriden multiple times!!!\n";    // A debugging message - this must never happen.

    copies_vector[copy_id] = extruder;
}

// BBS
void WipingExtrusions::set_support_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_map.emplace(object, extruder);
}

void WipingExtrusions::set_support_interface_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_intf_map.emplace(object, extruder);
}

// Finds first non-soluble extruder on the layer
int WipingExtrusions::first_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.begin(); extruders_it != lt.extruders.end(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Finds last non-soluble extruder on the layer
int WipingExtrusions::last_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.rbegin(); extruders_it != lt.extruders.rend(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Decides whether this entity could be overridden
bool WipingExtrusions::is_overriddable(const ExtrusionEntityCollection& eec, const PrintConfig& print_config, const PrintObject& object, const PrintRegion& region) const
{
    if (print_config.filament_soluble.get_at(m_layer_tools->extruder(eec, region)))
        return false;

    if (object.config().flush_into_objects)
        return true;

    if (!object.config().flush_into_infill || eec.role() != erInternalInfill)
        return false;

    return true;
}

// BBS
bool WipingExtrusions::is_support_overriddable(const ExtrusionRole role, const PrintObject& object) const
{
    if (!object.config().flush_into_support)
        return false;

    if (role == erMixed) {
        return object.config().support_filament == 0 || object.config().support_interface_filament == 0;
    }
    else if (role == erSupportMaterial || role == erSupportTransition) {
        return object.config().support_filament == 0;
    }
    else if (role == erSupportMaterialInterface) {
        return object.config().support_interface_filament == 0;
    }

    return false;
}

// Following function iterates through all extrusions on the layer, remembers those that could be used for wiping after toolchange
// and returns volume that is left to be wiped on the wipe tower.
float WipingExtrusions::mark_wiping_extrusions(const Print& print, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe)
{
    const LayerTools& lt = *m_layer_tools;
    const float min_infill_volume = 0.f; // ignore infill with smaller volume than this

    if (! this->something_overridable || volume_to_wipe <= 0. || print.config().filament_soluble.get_at(old_extruder) || print.config().filament_soluble.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Soluble filament cannot be wiped in a random infill, neither the filament after it

    // BBS
    if (print.config().filament_is_support.get_at(old_extruder) || print.config().filament_is_support.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Support filament cannot be used to print support, infill, wipe_tower, etc.

    // we will sort objects so that dedicated for wiping are at the beginning:
    ConstPrintObjectPtrs object_list = print.objects().vector();
    // BBS: fix the exception caused by not fixed order between different objects
    std::sort(object_list.begin(), object_list.end(), [object_list](const PrintObject* a, const PrintObject* b) {
        if (a->config().flush_into_objects != b->config().flush_into_objects) {
            return a->config().flush_into_objects.getBool();
        }
        else {
            return a->id() < b->id();
        }
    });

    // We will now iterate through
    //  - first the dedicated objects to mark perimeters or infills (depending on infill_first)
    //  - second through the dedicated ones again to mark infills or perimeters (depending on infill_first)
    //  - then all the others to mark infills (in case that !infill_first, we must also check that the perimeter is finished already
    // this is controlled by the following variable:
    bool perimeters_done = false;

    for (int i=0 ; i<(int)object_list.size() + (perimeters_done ? 0 : 1); ++i) {
        if (!perimeters_done && (i==(int)object_list.size() || !object_list[i]->config().flush_into_objects)) { // we passed the last dedicated object in list
            perimeters_done = true;
            i=-1;   // let's go from the start again
            continue;
        }

        const PrintObject* object = object_list[i];

        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;

        size_t num_of_copies = object->instances().size();

        // iterate through copies (aka PrintObject instances) first, so that we mark neighbouring infills to minimize travel moves
        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();

                if (!object->config().flush_into_infill && !object->config().flush_into_objects && !object->config().flush_into_support)
                    continue;
                bool wipe_into_infill_only = !object->config().flush_into_objects && object->config().flush_into_infill;
                bool is_infill_first = region.config().is_infill_first;
                if (is_infill_first != perimeters_done || wipe_into_infill_only) {
                    for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                        if (!is_overriddable(*fill, print.config(), *object, region))
                            continue;

                        if (wipe_into_infill_only && ! is_infill_first)
                            // In this case we must check that the original extruder is used on this layer before the one we are overridding
                            // (and the perimeters will be finished before the infill is printed):
                            if (!lt.is_extruder_order(lt.wall_filament(region), new_extruder))
                                continue;

                        if ((!is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume))
                        {     // this infill will be used to wipe this extruder
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                if (object->config().flush_into_objects && is_infill_first == perimeters_done)
                {
                    for (const ExtrusionEntity* ee : layerm->perimeters.entities) {
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                        if (is_overriddable(*fill, print.config(), *object, region) && !is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume) {
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }
            }

            // BBS
            if (object->config().flush_into_support) {
                auto& object_config = object->config();
                const SupportLayer* this_support_layer = object->get_support_layer_at_printz(lt.print_z, EPSILON);

                do {
                    if (this_support_layer == nullptr)
                        break;

                    bool support_overriddable = object_config.support_filament == 0;
                    bool support_intf_overriddable = object_config.support_interface_filament == 0;
                    if (!support_overriddable && !support_intf_overriddable)
                        break;

                    auto &entities = this_support_layer->support_fills.entities;
                    if (support_overriddable && !is_support_overridden(object) && !(object_config.support_interface_not_for_body.value && !support_intf_overriddable &&(new_extruder==object_config.support_interface_filament-1||old_extruder==object_config.support_interface_filament-1))) {
                        set_support_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterial || ee->role() == erSupportTransition)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }

                    if (support_intf_overriddable && !is_support_interface_overridden(object)) {
                        set_support_interface_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterialInterface)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }
                } while (0);
            }
        }
    }
	// Some purge remains to be done on the Wipe Tower.
    assert(volume_to_wipe > 0.);
    return volume_to_wipe;
}



// Called after all toolchanges on a layer were mark_infill_overridden. There might still be overridable entities,
// that were not actually overridden. If they are part of a dedicated object, printing them with the extruder
// they were initially assigned to might mean violating the perimeter-infill order. We will therefore go through
// them again and make sure we override it.
void WipingExtrusions::ensure_perimeters_infills_order(const Print& print)
{
	if (! this->something_overridable)
		return;

    const LayerTools& lt = *m_layer_tools;
    unsigned int first_nonsoluble_extruder = first_nonsoluble_extruder_on_layer(print.config());
    unsigned int last_nonsoluble_extruder = last_nonsoluble_extruder_on_layer(print.config());

    for (const PrintObject* object : print.objects()) {
        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;
        size_t num_of_copies = object->instances().size();

        for (size_t copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();
                //BBS
                if (!object->config().flush_into_infill && !object->config().flush_into_objects)
                    continue;

                bool is_infill_first = region.config().is_infill_first;
                for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                    if (!is_overriddable(*fill, print.config(), *object, region)
                     || is_entity_overridden(fill, object, copy) )
                        continue;

                    // This infill could have been overridden but was not - unless we do something, it could be
                    // printed before its perimeter, or not be printed at all (in case its original extruder has
                    // not been added to LayerTools
                    // Either way, we will now force-override it with something suitable:
                    //BBS
                    if (is_infill_first
                    //BBS
                    //|| object->config().flush_into_objects  // in this case the perimeter is overridden, so we can override by the last one safely
                    || lt.is_extruder_order(lt.wall_filament(region), last_nonsoluble_extruder    // !infill_first, but perimeter is already printed when last extruder prints
                    || ! lt.has_extruder(lt.sparse_infill_filament(region)))) // we have to force override - this could violate infill_first (FIXME)
                        set_extruder_override(fill, object, copy, (is_infill_first ? first_nonsoluble_extruder : last_nonsoluble_extruder), num_of_copies);
                    else {
                        // In this case we can (and should) leave it to be printed normally.
                        // Force overriding would mean it gets printed before its perimeter.
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                for (const ExtrusionEntity* ee : layerm->perimeters.entities) {                      // iterate through all perimeter Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    if (is_overriddable(*fill, print.config(), *object, region) && ! is_entity_overridden(fill, object, copy))
                        set_extruder_override(fill, object, copy, (is_infill_first ? last_nonsoluble_extruder : first_nonsoluble_extruder), num_of_copies);
                }
            }
        }
    }
}

// Following function is called from GCode::process_layer and returns pointer to vector with information about which extruders should be used for given copy of this entity.
// If this extrusion does not have any override, nullptr is returned.
// Otherwise it modifies the vector in place and changes all -1 to correct_extruder_id (at the time the overrides were created, correct extruders were not known,
// so -1 was used as "print as usual").
// The resulting vector therefore keeps track of which extrusions are the ones that were overridden and which were not. If the extruder used is overridden,
// its number is saved as is (zero-based index). Regular extrusions are saved as -number-1 (unfortunately there is no negative zero).
const WipingExtrusions::ExtruderPerCopy* WipingExtrusions::get_extruder_overrides(const ExtrusionEntity* entity, const PrintObject* object, int correct_extruder_id, size_t num_of_copies)
{
	ExtruderPerCopy *overrides = nullptr;
    auto entity_map_it = entity_map.find(std::make_tuple(entity, object));
    if (entity_map_it != entity_map.end()) {
        overrides = &entity_map_it->second;
    	overrides->resize(num_of_copies, -1);
	    // Each -1 now means "print as usual" - we will replace it with actual extruder id (shifted it so we don't lose that information):
	    std::replace(overrides->begin(), overrides->end(), -1, -correct_extruder_id-1);
	}
    return overrides;
}

// BBS
int WipingExtrusions::get_support_extruder_overrides(const PrintObject* object)
{
    auto iter = support_map.find(object);
    if (iter != support_map.end())
        return iter->second;

    return -1;
}

int WipingExtrusions::get_support_interface_extruder_overrides(const PrintObject* object)
{
    auto iter = support_intf_map.find(object);
    if (iter != support_intf_map.end())
        return iter->second;

    return -1;
}


} // namespace Slic3r
