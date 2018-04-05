#ifndef slic3r_GCode_PreviewData_hpp_
#define slic3r_GCode_PreviewData_hpp_

#include "../libslic3r.h"
#include "../ExtrusionEntity.hpp"

#include "Point.hpp"

namespace Slic3r {

class GCodePreviewData
{
public:
    struct Color
    {
        float rgba[4];

        Color();
        Color(float r, float g, float b, float a);

        std::vector<unsigned char> as_bytes() const;

        static const Color Dummy;
    };

    struct Range
    {
        static const unsigned int Colors_Count = 10;
        static const Color Default_Colors[Colors_Count];

        Color colors[Colors_Count];
        float min;
        float max;

        Range();

        void reset();
        bool empty() const;
        void update_from(float value);
        void update_from(const Range& other);
        void set_from(const Range& other);
        float step_size() const;

        const Color& get_color_at(float value) const;
        const Color& get_color_at_max() const;
    };

    struct Ranges
    {
        Range height;
        Range width;
        Range feedrate;
        Range volumetric_rate;
    };

    struct LegendItem
    {
        std::string text;
        Color color;

        LegendItem(const std::string& text, const Color& color);
    };

    typedef std::vector<LegendItem> LegendItemsList;

    struct Extrusion
    {
        enum EViewType : unsigned char
        {
            FeatureType,
            Height,
            Width,
            Feedrate,
            VolumetricRate,
            Tool,
            Num_View_Types
        };

        static const unsigned int Num_Extrusion_Roles = (unsigned int)erMixed + 1;
        static const Color Default_Extrusion_Role_Colors[Num_Extrusion_Roles];
        static const std::string Default_Extrusion_Role_Names[Num_Extrusion_Roles];
        static const EViewType Default_View_Type;

        struct Layer
        {
            float z;
            ExtrusionPaths paths;

            Layer(float z, const ExtrusionPaths& paths);
        };

        typedef std::vector<Layer> LayersList;

        EViewType view_type;
        Color role_colors[Num_Extrusion_Roles];
        std::string role_names[Num_Extrusion_Roles];
        LayersList layers;
        unsigned int role_flags;

        void set_default();
        bool is_role_flag_set(ExtrusionRole role) const;

        static bool is_role_flag_set(unsigned int flags, ExtrusionRole role);
    };

    struct Travel
    {
        enum EType : unsigned char
        {
            Move,
            Extrude,
            Retract,
            Num_Types
        };

        static const float Default_Width;
        static const float Default_Height;
        static const Color Default_Type_Colors[Num_Types];

        struct Polyline
        {
            enum EDirection
            {
                Vertical,
                Generic,
                Num_Directions
            };

            EType type;
            EDirection direction;
            float feedrate;
            unsigned int extruder_id;
            Polyline3 polyline;

            Polyline(EType type, EDirection direction, float feedrate, unsigned int extruder_id, const Polyline3& polyline);
        };

        typedef std::vector<Polyline> PolylinesList;

        PolylinesList polylines;
        float width;
        float height;
        Color type_colors[Num_Types];
        bool is_visible;

        void set_default();
    };

    struct Retraction
    {
        static const Color Default_Color;

        struct Position
        {
            Point3 position;
            float width;
            float height;

            Position(const Point3& position, float width, float height);
        };

        typedef std::vector<Position> PositionsList;

        PositionsList positions;
        Color color;
        bool is_visible;

        void set_default();
    };

    struct Shell
    {
        bool is_visible;

        void set_default();
    };

    Extrusion extrusion;
    Travel travel;
    Retraction retraction;
    Retraction unretraction;
    Shell shell;
    Ranges ranges;

    GCodePreviewData();

    void set_default();
    void reset();
    bool empty() const;

    const Color& get_extrusion_role_color(ExtrusionRole role) const;
    const Color& get_height_color(float height) const;
    const Color& get_width_color(float width) const;
    const Color& get_feedrate_color(float feedrate) const;
    const Color& get_volumetric_rate_color(float rate) const;

    void set_extrusion_role_color(const std::string& role_name, float red, float green, float blue, float alpha);
    void set_extrusion_paths_colors(const std::vector<std::string>& colors);

    std::string get_legend_title() const;
    LegendItemsList get_legend_items(const std::vector<float>& tool_colors) const;
};

GCodePreviewData::Color operator + (const GCodePreviewData::Color& c1, const GCodePreviewData::Color& c2);
GCodePreviewData::Color operator * (float f, const GCodePreviewData::Color& color);

} // namespace Slic3r

#endif /* slic3r_GCode_PreviewData_hpp_ */
