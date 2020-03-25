#include "Analyzer.hpp"
#include "PreviewData.hpp"
#include <I18N.hpp>
#include "Utils.hpp"

#include <boost/format.hpp>

//! macro used to mark string used at localization, 
#define L(s) (s)

namespace Slic3r {

std::vector<unsigned char> Color::as_bytes() const
{
    std::vector<unsigned char> ret;
    for (unsigned int i = 0; i < 4; ++i)
    {
        ret.push_back((unsigned char)(255.0f * rgba[i]));
    }
    return ret;
}

GCodePreviewData::Extrusion::Layer::Layer(float z, const Paths& paths)
    : z(z)
    , paths(paths)
{
}

GCodePreviewData::Travel::Polyline::Polyline(EType type, EDirection direction, float feedrate, unsigned int extruder_id, const Polyline3& polyline)
    : type(type)
    , direction(direction)
    , feedrate(feedrate)
    , extruder_id(extruder_id)
    , polyline(polyline)
{
}

GCodePreviewData::Range::Range()
{
    reset();
}

void GCodePreviewData::Range::reset()
{
    min_val = FLT_MAX;
    max_val = -FLT_MAX;
}

bool GCodePreviewData::Range::empty() const
{
    return min_val >= max_val;
}

void GCodePreviewData::Range::update_from(float value)
{
    min_val = std::min(min_val, value);
    max_val = std::max(max_val, value);
}

void GCodePreviewData::Range::update_from(const RangeBase& other)
{
    min_val = std::min(min_val, other.min());
    max_val = std::max(max_val, other.max());
}

float GCodePreviewData::RangeBase::step_size() const
{
    return (max() - min()) / static_cast<float>(range_rainbow_colors.size() - 1);
}

Color GCodePreviewData::RangeBase::get_color_at(float value) const
{
    // Input value scaled to the color range
    float step = step_size();
    const float global_t = (step != 0.0f) ? std::max(0.0f, value - min()) / step : 0.0f; // lower limit of 0.0f

    constexpr std::size_t color_max_idx = range_rainbow_colors.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const std::size_t color_low_idx = std::clamp(static_cast<std::size_t>(global_t), std::size_t{ 0 }, color_max_idx);
    const std::size_t color_high_idx = std::clamp(color_low_idx + 1, std::size_t{ 0 }, color_max_idx);

    // Compute how far the value is between the low and high colors so that they can be interpolated
    const float local_t = std::min(global_t - static_cast<float>(color_low_idx), 1.0f); // upper limit of 1.0f

    // Interpolate between the low and high colors in RGB space to find exactly which color the input value should get
    Color ret;
    for (unsigned int i = 0; i < 4; ++i)
    {
        ret.rgba[i] = lerp(range_rainbow_colors[color_low_idx].rgba[i], range_rainbow_colors[color_high_idx].rgba[i], local_t);
    }
    return ret;
}

float GCodePreviewData::Range::min() const
{
    return min_val;
}

float GCodePreviewData::Range::max() const
{
    return max_val;
}

GCodePreviewData::LegendItem::LegendItem(const std::string& text, const Color& color)
    : text(text)
    , color(color)
{
}

const Color GCodePreviewData::Extrusion::Default_Extrusion_Role_Colors[erCount] =
{
    Color(0.0f, 0.0f, 0.0f, 1.0f),   // erNone
    Color(1.0f, 0.0f, 0.0f, 1.0f),   // erPerimeter
    Color(0.0f, 1.0f, 0.0f, 1.0f),   // erExternalPerimeter
    Color(0.0f, 0.0f, 1.0f, 1.0f),   // erOverhangPerimeter
    Color(1.0f, 1.0f, 0.0f, 1.0f),   // erInternalInfill
    Color(1.0f, 0.0f, 1.0f, 1.0f),   // erSolidInfill
    Color(0.0f, 1.0f, 1.0f, 1.0f),   // erTopSolidInfill
    Color(0.5f, 0.5f, 0.5f, 1.0f),   // erBridgeInfill
    Color(1.0f, 1.0f, 1.0f, 1.0f),   // erGapFill
    Color(0.5f, 0.0f, 0.0f, 1.0f),   // erSkirt
    Color(0.0f, 0.5f, 0.0f, 1.0f),   // erSupportMaterial
    Color(0.0f, 0.0f, 0.5f, 1.0f),   // erSupportMaterialInterface
    Color(0.7f, 0.89f, 0.67f, 1.0f), // erWipeTower
    Color(1.0f, 1.0f, 0.0f, 1.0f),   // erCustom
    Color(0.0f, 0.0f, 0.0f, 1.0f)    // erMixed
};

const GCodePreviewData::Extrusion::EViewType GCodePreviewData::Extrusion::Default_View_Type = GCodePreviewData::Extrusion::FeatureType;

void GCodePreviewData::Extrusion::set_default()
{
    view_type = Default_View_Type;

    ::memcpy((void*)role_colors, (const void*)Default_Extrusion_Role_Colors, erCount * sizeof(Color));

    for (unsigned int i = 0; i < erCount; ++i)
        role_names[i] = ExtrusionEntity::role_to_string(ExtrusionRole(i));

    role_flags = 0;
    for (unsigned int i = 0; i < erCount; ++i)
        role_flags |= 1 << i;
}

bool GCodePreviewData::Extrusion::is_role_flag_set(ExtrusionRole role) const
{
    return is_role_flag_set(role_flags, role);
}

bool GCodePreviewData::Extrusion::is_role_flag_set(unsigned int flags, ExtrusionRole role)
{
    return GCodeAnalyzer::is_valid_extrusion_role(role) && (flags & (1 << (role - erPerimeter))) != 0;
}

size_t GCodePreviewData::Extrusion::memory_used() const
{
    size_t out = sizeof(*this);
    out += SLIC3R_STDVEC_MEMSIZE(this->layers, Layer);
    for (const Layer &layer : this->layers) {
        out += SLIC3R_STDVEC_MEMSIZE(layer.paths, Path);
        for (const Path &path : layer.paths)
			out += SLIC3R_STDVEC_MEMSIZE(path.polyline.points, Point);
    }
	return out;
}

const float GCodePreviewData::Travel::Default_Width = 0.075f;
const float GCodePreviewData::Travel::Default_Height = 0.075f;
const Color GCodePreviewData::Travel::Default_Type_Colors[Num_Types] =
{
    Color(0.0f, 0.0f, 0.75f, 1.0f), // Move
    Color(0.0f, 0.75f, 0.0f, 1.0f), // Extrude
    Color(0.75f, 0.0f, 0.0f, 1.0f), // Retract
};

void GCodePreviewData::Travel::set_default()
{
    width = Default_Width;
    height = Default_Height;
    ::memcpy((void*)type_colors, (const void*)Default_Type_Colors, Num_Types * sizeof(Color));
    color_print_idx = 0;

    is_visible = false;
}

size_t GCodePreviewData::Travel::memory_used() const
{
    size_t out = sizeof(*this);
    out += SLIC3R_STDVEC_MEMSIZE(this->polylines, Polyline);
	for (const Polyline &polyline : this->polylines)
		out += SLIC3R_STDVEC_MEMSIZE(polyline.polyline.points, Vec3crd);
    return out;
}

const Color GCodePreviewData::Retraction::Default_Color = Color(1.0f, 1.0f, 1.0f, 1.0f);

GCodePreviewData::Retraction::Position::Position(const Vec3crd& position, float width, float height)
    : position(position)
    , width(width)
    , height(height)
{
}

void GCodePreviewData::Retraction::set_default()
{
    color = Default_Color;
    is_visible = false;
}

size_t GCodePreviewData::Retraction::memory_used() const
{
	return sizeof(*this) + SLIC3R_STDVEC_MEMSIZE(this->positions, Position);
}

void GCodePreviewData::Shell::set_default()
{
    is_visible = false;
}

GCodePreviewData::GCodePreviewData()
{
    set_default();
}

void GCodePreviewData::set_default()
{
    extrusion.set_default();
    travel.set_default();
    retraction.set_default();
    unretraction.set_default();
    shell.set_default();
    
    // Configure the color range for feedrate to match the default for travels and to enable extrusions since they are always visible
    ranges.feedrate.set_mode(FeedrateKind::TRAVEL, travel.is_visible);
    ranges.feedrate.set_mode(FeedrateKind::EXTRUSION, true);
}

void GCodePreviewData::reset()
{
    ranges.width.reset();
    ranges.height.reset();
    ranges.feedrate.reset();
    ranges.fan_speed.reset();
    ranges.volumetric_rate.reset();
    extrusion.layers.clear();
    travel.polylines.clear();
    retraction.positions.clear();
    unretraction.positions.clear();
}

bool GCodePreviewData::empty() const
{
    return extrusion.layers.empty() && travel.polylines.empty() && retraction.positions.empty() && unretraction.positions.empty();
}

Color GCodePreviewData::get_extrusion_role_color(ExtrusionRole role) const
{
    return extrusion.role_colors[role];
}

Color GCodePreviewData::get_height_color(float height) const
{
    return ranges.height.get_color_at(height);
}

Color GCodePreviewData::get_width_color(float width) const
{
    return ranges.width.get_color_at(width);
}

Color GCodePreviewData::get_feedrate_color(float feedrate) const
{
    return ranges.feedrate.get_color_at(feedrate);
}

Color GCodePreviewData::get_fan_speed_color(float fan_speed) const
{
    return ranges.fan_speed.get_color_at(fan_speed);
}

Color GCodePreviewData::get_volumetric_rate_color(float rate) const
{
    return ranges.volumetric_rate.get_color_at(rate);
}

void GCodePreviewData::set_extrusion_role_color(const std::string& role_name, float red, float green, float blue, float alpha)
{
    for (unsigned int i = 0; i < erCount; ++i)
    {
        if (role_name == extrusion.role_names[i])
        {
            extrusion.role_colors[i] = Color(red, green, blue, alpha);
            break;
        }
    }
}

void GCodePreviewData::set_extrusion_paths_colors(const std::vector<std::string>& colors)
{
    unsigned int size = (unsigned int)colors.size();

    if (size % 2 != 0)
        return;

    for (unsigned int i = 0; i < size; i += 2)
    {
        const std::string& color_str = colors[i + 1];

        if (color_str.size() == 6)
        {
            bool valid = true;
            for (int c = 0; c < 6; ++c)
            {
                if (::isxdigit(color_str[c]) == 0)
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
            {
                unsigned int color;
                std::stringstream ss;
                ss << std::hex << color_str;
                ss >> color;

                float den = 1.0f / 255.0f;

                float r = (float)((color & 0xFF0000) >> 16) * den;
                float g = (float)((color & 0x00FF00) >> 8) * den;
                float b = (float)(color & 0x0000FF) * den;

                this->set_extrusion_role_color(colors[i], r, g, b, 1.0f);
            }
        }
    }
}

std::string GCodePreviewData::get_legend_title() const
{
    switch (extrusion.view_type)
    {
    case Extrusion::FeatureType:
        return L("Feature type");
    case Extrusion::Height:
        return L("Height (mm)");
    case Extrusion::Width:
        return L("Width (mm)");
    case Extrusion::Feedrate:
        return L("Speed (mm/s)");
    case Extrusion::FanSpeed:
        return L("Fan Speed (%)");
    case Extrusion::VolumetricRate:
        return L("Volumetric flow rate (mm³/s)");
    case Extrusion::Tool:
        return L("Tool");
    case Extrusion::ColorPrint:
        return L("Color Print");
    case Extrusion::Num_View_Types:
        break; // just to supress warning about non-handled value
    }

    return "";
}

GCodePreviewData::LegendItemsList GCodePreviewData::get_legend_items(const std::vector<float>& tool_colors, 
                                                                     const std::vector<std::string>& cp_items) const
{
    struct Helper
    {
        static void FillListFromRange(LegendItemsList& list, const RangeBase& range, unsigned int decimals, float scale_factor)
        {
            list.reserve(range_rainbow_colors.size());

            float step = range.step_size();
            if (step == 0.0f)
            {
                char buf[1024];
                sprintf(buf, "%.*f", decimals, scale_factor * range.min());
                list.emplace_back(buf, range_rainbow_colors[0]);
            }
            else
            {
                for (int i = static_cast<int>(range_rainbow_colors.size()) - 1; i >= 0; --i)
                {
                    char buf[1024];
                    sprintf(buf, "%.*f", decimals, scale_factor * (range.min() + (float)i * step));
                    list.emplace_back(buf, range_rainbow_colors[i]);
                }
            }
        }
    };

    LegendItemsList items;

    switch (extrusion.view_type)
    {
    case Extrusion::FeatureType:
        {
            ExtrusionRole first_valid = erPerimeter;
            ExtrusionRole last_valid = erCustom;

            items.reserve(last_valid - first_valid + 1);
            for (unsigned int i = (unsigned int)first_valid; i <= (unsigned int)last_valid; ++i)
            {
                items.emplace_back(Slic3r::I18N::translate(extrusion.role_names[i]), extrusion.role_colors[i]);
            }

            break;
        }
    case Extrusion::Height:
        {
            Helper::FillListFromRange(items, ranges.height, 3, 1.0f);
            break;
        }
    case Extrusion::Width:
        {
            Helper::FillListFromRange(items, ranges.width, 3, 1.0f);
            break;
        }
    case Extrusion::Feedrate:
        {
            Helper::FillListFromRange(items, ranges.feedrate, 1, 1.0f);
            break;
        }
    case Extrusion::FanSpeed:
        {
            Helper::FillListFromRange(items, ranges.fan_speed, 0, 1.0f);
            break;
        }
    case Extrusion::VolumetricRate:
        {
            Helper::FillListFromRange(items, ranges.volumetric_rate, 3, 1.0f);
            break;
        }
    case Extrusion::Tool:
        {
            unsigned int tools_colors_count = (unsigned int)tool_colors.size() / 4;
            items.reserve(tools_colors_count);
            for (unsigned int i = 0; i < tools_colors_count; ++i)
            {
                Color color;
                ::memcpy((void*)color.rgba.data(), (const void*)(tool_colors.data() + i * 4), 4 * sizeof(float));
                items.emplace_back((boost::format(Slic3r::I18N::translate(L("Extruder %d"))) % (i + 1)).str(), color);
            }

            break;
        }
    case Extrusion::ColorPrint:
        {
            const int color_cnt = (int)tool_colors.size()/4;
            const auto color_print_cnt = (int)cp_items.size();
            if (color_print_cnt == 1) // means "Default print color"
            {
                Color color;
                ::memcpy((void*)color.rgba.data(), (const void*)(tool_colors.data()), 4 * sizeof(float));

                items.emplace_back(cp_items[0], color);
                break;
            }

            if (color_cnt != color_print_cnt)
                break;

            for (int i = 0 ; i < color_print_cnt; ++i)
            {
                Color color;
                ::memcpy((void*)color.rgba.data(), (const void*)(tool_colors.data() + i * 4), 4 * sizeof(float));
                
                items.emplace_back(cp_items[i], color);
            }
            break;
        }
    case Extrusion::Num_View_Types:
        break; // just to supress warning about non-handled value
    }

    return items;
}

// Return an estimate of the memory consumed by the time estimator.
size_t GCodePreviewData::memory_used() const
{
    return 
        this->extrusion.memory_used() + 
        this->travel.memory_used() + 
        this->retraction.memory_used() + 
        this->unretraction.memory_used() + 
        sizeof(shell) + sizeof(ranges);
}

const std::vector<std::string>& GCodePreviewData::ColorPrintColors()
{
    static std::vector<std::string> color_print = {"#C0392B", "#E67E22", "#F1C40F", "#27AE60", "#1ABC9C", "#2980B9", "#9B59B6"};
    return color_print;
}

Color operator + (const Color& c1, const Color& c2)
{
    return Color(std::clamp(c1.rgba[0] + c2.rgba[0], 0.0f, 1.0f),
        std::clamp(c1.rgba[1] + c2.rgba[1], 0.0f, 1.0f),
        std::clamp(c1.rgba[2] + c2.rgba[2], 0.0f, 1.0f),
        std::clamp(c1.rgba[3] + c2.rgba[3], 0.0f, 1.0f));
}

Color operator * (float f, const Color& color)
{
    return Color(std::clamp(f * color.rgba[0], 0.0f, 1.0f),
        std::clamp(f * color.rgba[1], 0.0f, 1.0f),
        std::clamp(f * color.rgba[2], 0.0f, 1.0f),
        std::clamp(f * color.rgba[3], 0.0f, 1.0f));
}

} // namespace Slic3r
