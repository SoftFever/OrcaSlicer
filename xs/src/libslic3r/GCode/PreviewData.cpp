#include "Analyzer.hpp"
#include "PreviewData.hpp"
#include <float.h>
#include <wx/intl.h> 
#include "slic3r/GUI/GUI.hpp"

namespace Slic3r {

const GCodePreviewData::Color GCodePreviewData::Color::Dummy(0.0f, 0.0f, 0.0f, 0.0f);

GCodePreviewData::Color::Color()
{
    rgba[0] = 1.0f;
    rgba[1] = 1.0f;
    rgba[2] = 1.0f;
    rgba[3] = 1.0f;
}

GCodePreviewData::Color::Color(float r, float g, float b, float a)
{
    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = a;
}

std::vector<unsigned char> GCodePreviewData::Color::as_bytes() const
{
    std::vector<unsigned char> ret;
    for (unsigned int i = 0; i < 4; ++i)
    {
        ret.push_back((unsigned char)(255.0f * rgba[i]));
    }
    return ret;
}

GCodePreviewData::Extrusion::Layer::Layer(float z, const ExtrusionPaths& paths)
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

const GCodePreviewData::Color GCodePreviewData::Range::Default_Colors[Colors_Count] =
{
    Color(0.043f, 0.173f, 0.478f, 1.0f),
    Color(0.075f, 0.349f, 0.522f, 1.0f),
    Color(0.110f, 0.533f, 0.569f, 1.0f),
    Color(0.016f, 0.839f, 0.059f, 1.0f),
    Color(0.667f, 0.949f, 0.000f, 1.0f),
    Color(0.988f, 0.975f, 0.012f, 1.0f),
    Color(0.961f, 0.808f, 0.039f, 1.0f),
    Color(0.890f, 0.533f, 0.125f, 1.0f),
    Color(0.820f, 0.408f, 0.188f, 1.0f),
    Color(0.761f, 0.322f, 0.235f, 1.0f)
};

GCodePreviewData::Range::Range()
{
    reset();
}

void GCodePreviewData::Range::reset()
{
    min = FLT_MAX;
    max = -FLT_MAX;
}

bool GCodePreviewData::Range::empty() const
{
    return min == max;
}

void GCodePreviewData::Range::update_from(float value)
{
    min = std::min(min, value);
    max = std::max(max, value);
}

void GCodePreviewData::Range::update_from(const Range& other)
{
    min = std::min(min, other.min);
    max = std::max(max, other.max);
}

void GCodePreviewData::Range::set_from(const Range& other)
{
    min = other.min;
    max = other.max;
}

float GCodePreviewData::Range::step_size() const
{
    return (max - min) / (float)(Colors_Count - 1);
}

GCodePreviewData::Color GCodePreviewData::Range::get_color_at(float value) const
{
    if (empty())
        return Color::Dummy;

    float global_t = (value - min) / step_size();

    unsigned int low = (unsigned int)global_t;
    unsigned int high = clamp((unsigned int)0, Colors_Count - 1, low + 1);

    Color color_low = colors[low];
    Color color_high = colors[high];

    float local_t = global_t - (float)low;

    // interpolate in RGB space
    Color ret;
    for (unsigned int i = 0; i < 4; ++i)
    {
        ret.rgba[i] = lerp(color_low.rgba[i], color_high.rgba[i], local_t);
    }
    return ret;
}

GCodePreviewData::LegendItem::LegendItem(const std::string& text, const GCodePreviewData::Color& color)
    : text(text)
    , color(color)
{
}

const GCodePreviewData::Color GCodePreviewData::Extrusion::Default_Extrusion_Role_Colors[Num_Extrusion_Roles] =
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

// todo: merge with Slic3r::ExtrusionRole2String() from GCode.cpp
const std::string GCodePreviewData::Extrusion::Default_Extrusion_Role_Names[Num_Extrusion_Roles]
{
    L("None"),
    L("Perimeter"),
    L("External perimeter"),
    L("Overhang perimeter"),
    L("Internal infill"),
    L("Solid infill"),
    L("Top solid infill"),
    L("Bridge infill"),
    L("Gap fill"),
    L("Skirt"),
    L("Support material"),
    L("Support material interface"),
    L("Wipe tower"),
    L("Custom"),
    L("Mixed")
};

const GCodePreviewData::Extrusion::EViewType GCodePreviewData::Extrusion::Default_View_Type = GCodePreviewData::Extrusion::FeatureType;

void GCodePreviewData::Extrusion::set_default()
{
    view_type = Default_View_Type;

    ::memcpy((void*)role_colors, (const void*)Default_Extrusion_Role_Colors, Num_Extrusion_Roles * sizeof(Color));

    for (unsigned int i = 0; i < Num_Extrusion_Roles; ++i)
    {
        role_names[i] = Default_Extrusion_Role_Names[i];
    }

    role_flags = 0;
    for (unsigned int i = 0; i < Num_Extrusion_Roles; ++i)
    {
        role_flags |= 1 << i;
    }
}

bool GCodePreviewData::Extrusion::is_role_flag_set(ExtrusionRole role) const
{
    return is_role_flag_set(role_flags, role);
}

bool GCodePreviewData::Extrusion::is_role_flag_set(unsigned int flags, ExtrusionRole role)
{
    return GCodeAnalyzer::is_valid_extrusion_role(role) && (flags & (1 << (role - erPerimeter))) != 0;
}

const float GCodePreviewData::Travel::Default_Width = 0.075f;
const float GCodePreviewData::Travel::Default_Height = 0.075f;
const GCodePreviewData::Color GCodePreviewData::Travel::Default_Type_Colors[Num_Types] =
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

    is_visible = false;
}

const GCodePreviewData::Color GCodePreviewData::Retraction::Default_Color = GCodePreviewData::Color(1.0f, 1.0f, 1.0f, 1.0f);

GCodePreviewData::Retraction::Position::Position(const Point3& position, float width, float height)
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
    ::memcpy((void*)ranges.height.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));
    ::memcpy((void*)ranges.width.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));
    ::memcpy((void*)ranges.feedrate.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));
    ::memcpy((void*)ranges.volumetric_rate.colors, (const void*)Range::Default_Colors, Range::Colors_Count * sizeof(Color));

    extrusion.set_default();
    travel.set_default();
    retraction.set_default();
    unretraction.set_default();
    shell.set_default();
}

void GCodePreviewData::reset()
{
    ranges.width.reset();
    ranges.height.reset();
    ranges.feedrate.reset();
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

GCodePreviewData::Color GCodePreviewData::get_extrusion_role_color(ExtrusionRole role) const
{
    return extrusion.role_colors[role];
}

GCodePreviewData::Color GCodePreviewData::get_height_color(float height) const
{
    return ranges.height.get_color_at(height);
}

GCodePreviewData::Color GCodePreviewData::get_width_color(float width) const
{
    return ranges.width.get_color_at(width);
}

GCodePreviewData::Color GCodePreviewData::get_feedrate_color(float feedrate) const
{
    return ranges.feedrate.get_color_at(feedrate);
}

GCodePreviewData::Color GCodePreviewData::get_volumetric_rate_color(float rate) const
{
    return ranges.volumetric_rate.get_color_at(rate);
}

void GCodePreviewData::set_extrusion_role_color(const std::string& role_name, float red, float green, float blue, float alpha)
{
    for (unsigned int i = 0; i < Extrusion::Num_Extrusion_Roles; ++i)
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
    case Extrusion::VolumetricRate:
        return L("Volumetric flow rate (mm3/s)");
    case Extrusion::Tool:
        return L("Tool");
    }

    return "";
}

GCodePreviewData::LegendItemsList GCodePreviewData::get_legend_items(const std::vector<float>& tool_colors) const
{
    struct Helper
    {
        static void FillListFromRange(LegendItemsList& list, const Range& range, unsigned int decimals, float scale_factor)
        {
            list.reserve(Range::Colors_Count);

            float step = range.step_size();
            for (int i = Range::Colors_Count - 1; i >= 0; --i)
            {
                char buf[1024];
                sprintf(buf, "%.*f", decimals, scale_factor * (range.min + (float)i * step));
                list.emplace_back(buf, range.colors[i]);
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
                items.emplace_back(_CHB(extrusion.role_names[i].c_str()).data(), extrusion.role_colors[i]);
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
    case Extrusion::VolumetricRate:
        {
            Helper::FillListFromRange(items, ranges.volumetric_rate, 3, 1.0f);
            break;
        }
    case Extrusion::Tool:
        {
            unsigned int tools_colors_count = tool_colors.size() / 4;
            items.reserve(tools_colors_count);
            for (unsigned int i = 0; i < tools_colors_count; ++i)
            {
				char buf[MIN_BUF_LENGTH_FOR_L];
                sprintf(buf, _CHB(L("Extruder %d")), i + 1);

                GCodePreviewData::Color color;
                ::memcpy((void*)color.rgba, (const void*)(tool_colors.data() + i * 4), 4 * sizeof(float));

                items.emplace_back(buf, color);
            }

            break;
        }
    }

    return items;
}

} // namespace Slic3r
