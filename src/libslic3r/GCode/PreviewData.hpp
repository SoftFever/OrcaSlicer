#ifndef slic3r_GCode_PreviewData_hpp_
#define slic3r_GCode_PreviewData_hpp_

#include "../libslic3r.h"
#include "../ExtrusionEntity.hpp"
#include "../Point.hpp"

#include <tuple>
#include <array>
#include <vector>
#include <bitset>
#include <cstddef>
#include <algorithm>
#include <string>

#include <float.h>

namespace Slic3r {

// Represents an RGBA color
struct Color
{
    std::array<float,4> rgba;

    Color(const float *argba)
    {
        memcpy(this->rgba.data(), argba, sizeof(float) * 4);
    }
    constexpr Color(float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f) : rgba{r,g,b,a}
    {
        // Intentionally empty
    }

    std::vector<unsigned char> as_bytes() const;
};
Color operator + (const Color& c1, const Color& c2);
Color operator * (float f, const Color& color);

// Default colors for Ranges
constexpr std::array<Color, 10> range_rainbow_colors{
    Color{0.043f, 0.173f, 0.478f, 1.0f},
    Color{0.075f, 0.349f, 0.522f, 1.0f},
    Color{0.110f, 0.533f, 0.569f, 1.0f},
    Color{0.016f, 0.839f, 0.059f, 1.0f},
    Color{0.667f, 0.949f, 0.000f, 1.0f},
    Color{0.988f, 0.975f, 0.012f, 1.0f},
    Color{0.961f, 0.808f, 0.039f, 1.0f},
    Color{0.890f, 0.533f, 0.125f, 1.0f},
    Color{0.820f, 0.408f, 0.188f, 1.0f},
    Color{0.761f, 0.322f, 0.235f, 1.0f}};

class GCodePreviewData
{
public:

    // Color mapping to convert a float into a smooth rainbow of 10 colors.
    class RangeBase
    {
    public:
        virtual void reset() = 0;
        virtual bool empty() const = 0;
        virtual float min() const = 0;
        virtual float max() const = 0;
        
        // Gets the step size using min(), max() and colors
        float step_size() const;
        
        // Gets the color at a value using colors, min(), and max()
        Color get_color_at(float value) const;
    };

    // Color mapping converting a float in a range between a min and a max into a smooth rainbow of 10 colors.
    class Range : public RangeBase
    {
    public:
        Range();

        // RangeBase Overrides
        void reset() override;
        bool empty() const override;
        float min() const override;
        float max() const override;
        
        // Range-specific methods
        void update_from(float value);
        void update_from(const RangeBase& other);

        private:
        float min_val;
        float max_val;
    };

    // Like Range, but stores multiple ranges internally that are used depending on mode.
    // Template param EnumRangeType must be an enum with values for each type of range that needs to be tracked in this MultiRange.
    // The last enum value should be num_values. The numerical values of all enum values should range from 0 to num_values.
    template <typename EnumRangeType>
    class MultiRange : public RangeBase
    {
    public:
        void reset() override
        {
            bounds = decltype(bounds){};
        }

        bool empty() const override
        {
            for (std::size_t i = 0; i < bounds.size(); ++i)
            {
                if (bounds[i].min != bounds[i].max)
                    return false;
            }
            return true;
        }

        float min() const override
        {
            float min = FLT_MAX;
            for (std::size_t i = 0; i < bounds.size(); ++i)
            {
                // Only use bounds[i] if the current mode includes it
                if (mode.test(i))
                {
                    min = std::min(min, bounds[i].min);
                }
            }
            return min;
        }

        float max() const override
        {
            float max = -FLT_MAX;
            for (std::size_t i = 0; i < bounds.size(); ++i)
            {
                // Only use bounds[i] if the current mode includes it
                if (mode.test(i))
                {
                    max = std::max(max, bounds[i].max);
                }
            }
            return max;
        }

        void update_from(const float value, EnumRangeType range_type_value)
        {
            bounds[static_cast<std::size_t>(range_type_value)].update_from(value);
        }

        void update_from(const MultiRange& other)
        {
            for (std::size_t i = 0; i < bounds.size(); ++i)
            {
                bounds[i].update_from(other.bounds[i]);
            }
        }

        void set_mode(const EnumRangeType range_type_value, const bool enable)
        {
            mode.set(static_cast<std::size_t>(range_type_value), enable);
        }

    private:
        // Interval bounds
        struct Bounds
        {
            float min{FLT_MAX};
            float max{-FLT_MAX};
            void update_from(const float value)
            {
                min = std::min(min, value);
                max = std::max(max, value);
            }
            void update_from(const Bounds other_bounds)
            {
                min = std::min(min, other_bounds.min);
                max = std::max(max, other_bounds.max);
            }
        };

        std::array<Bounds, static_cast<std::size_t>(EnumRangeType::num_values)> bounds;
        std::bitset<static_cast<std::size_t>(EnumRangeType::num_values)> mode;
    };

    // Enum distinguishing different kinds of feedrate data
    enum class FeedrateKind
    {
        EXTRUSION = 0,  // values must go from 0 up to num_values
        TRAVEL,
        num_values  //must be last in the list of values
    };

    struct Ranges
    {
        // Color mapping by layer height.
        Range height;
        // Color mapping by extrusion width.
        Range width;
        // Color mapping by feedrate.
        MultiRange<FeedrateKind> feedrate;
        // Color mapping by fan speed.
        Range fan_speed;
        // Color mapping by volumetric extrusion rate.
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
            FanSpeed,
            VolumetricRate,
            Tool,
            ColorPrint,
            Num_View_Types
        };

        static const Color Default_Extrusion_Role_Colors[erCount];
        static const std::string Default_Extrusion_Role_Names[erCount];
        static const EViewType Default_View_Type;

		class Path
		{
		public:
		    Polyline 		polyline;
		    ExtrusionRole 	extrusion_role;
		    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
		    float			mm3_per_mm;
		    // Width of the extrusion, used for visualization purposes.
		    float 			width;
		    // Height of the extrusion, used for visualization purposes.
		    float 			height;
		    // Feedrate of the extrusion, used for visualization purposes.
		    float 			feedrate;
		    // Id of the extruder, used for visualization purposes.
		    uint32_t		extruder_id;
		    // Id of the color, used for visualization purposes in the color printing case.
		    uint32_t	 	cp_color_id;
		    // Fan speed for the extrusion, used for visualization purposes.
		    float 			fan_speed;
		};
		using Paths = std::vector<Path>;

        struct Layer
        {
            float z;
            Paths paths;

            Layer(float z, const Paths& paths);
        };

        typedef std::vector<Layer> LayersList;

        EViewType view_type;
        Color role_colors[erCount];
        std::string role_names[erCount];
        LayersList layers;
        unsigned int role_flags;

        void set_default();
        bool is_role_flag_set(ExtrusionRole role) const;

        // Return an estimate of the memory consumed by the time estimator.
        size_t memory_used() const;

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
        size_t color_print_idx;

        void set_default();

        // Return an estimate of the memory consumed by the time estimator.
        size_t memory_used() const;
    };

    struct Retraction
    {
        static const Color Default_Color;

        struct Position
        {
            Vec3crd position;
            float width;
            float height;

            Position(const Vec3crd& position, float width, float height);
        };

        typedef std::vector<Position> PositionsList;

        PositionsList positions;
        Color color;
        bool is_visible;

        void set_default();

        // Return an estimate of the memory consumed by the time estimator.
        size_t memory_used() const;
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

    Color get_extrusion_role_color(ExtrusionRole role) const;
    Color get_height_color(float height) const;
    Color get_width_color(float width) const;
    Color get_feedrate_color(float feedrate) const;
    Color get_fan_speed_color(float fan_speed) const;
    Color get_volumetric_rate_color(float rate) const;

    void set_extrusion_role_color(const std::string& role_name, float red, float green, float blue, float alpha);
    void set_extrusion_paths_colors(const std::vector<std::string>& colors);

    std::string get_legend_title() const;
    LegendItemsList get_legend_items(const std::vector<float>& tool_colors, const std::vector<std::string>& cp_items) const;

    // Return an estimate of the memory consumed by the time estimator.
    size_t memory_used() const;

    static const std::vector<std::string>& ColorPrintColors();
};

} // namespace Slic3r

#endif /* slic3r_GCode_PreviewData_hpp_ */
