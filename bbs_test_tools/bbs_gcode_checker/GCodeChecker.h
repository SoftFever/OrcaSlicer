#ifndef _GCodeChecker_H_
#define _GCodeChecker_H_

#include <iostream>
#include <string>
#include <vector>
#include <array>

namespace BambuStudio {

enum class GCodeCheckResult : unsigned char
{
    Success,
    ParseFailed,
    CheckFailed,
    Count
};

enum class EMoveType : unsigned char
{
    Noop,
    Retract,
    Unretract,
    Tool_change,
    Color_change,
    Pause_Print,
    Custom_GCode,
    Travel,
    Wipe,
    Extrude,
    Count
};

enum Axis {
    X=0,
    Y,
    Z,
    E,
    F,
    I,
    J,
    P,
    NUM_AXES,
    UNKNOWN_AXIS = NUM_AXES,
};

enum ExtrusionRole : uint8_t {
    erNone,
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erTopSolidInfill,
    erBottomSurface,
    erIroning,
    erBridgeInfill,
    erGapFill,
    erSkirt,
    erBrim,
    erSupportMaterial,
    erSupportMaterialInterface,
    erSupportTransition,
    erWipeTower,
    erCustom,
    // Extrusion role for a collection with multiple extrusion roles.
    erMixed,
    erCount
};

class GCodeChecker {
public:
    class GCodeLine {
    public:
        GCodeLine() {}
        const std::string cmd() const {
            const char *cmd = GCodeChecker::skip_whitespaces(m_raw.c_str());
            return std::string(cmd, GCodeChecker::skip_word(cmd) - cmd);
        }

        bool has(Axis axis) const { return (m_mask & (1 << int(axis))) != 0;  }
        double get(Axis axis) const { return m_axis[int(axis)]; }

        std::string   m_raw;
        double        m_axis[NUM_AXES] = { 0.0f };
        uint32_t      m_mask = 0;
    };

    enum class EPositioningType : unsigned char
    {
        Absolute,
        Relative
    };

    GCodeChecker() {}
    GCodeCheckResult parse_file(const std::string& path);

private:
    bool include_chinese(const char* str);
    GCodeCheckResult parse_line(const std::string& line);

    GCodeCheckResult parse_command(GCodeLine& gcode_line);
    GCodeCheckResult parse_axis(GCodeLine& gcode_line);
    GCodeCheckResult parse_G0_G1(GCodeLine& gcode_line);
    GCodeCheckResult parse_G2_G3(GCodeLine& gcode_line);
    GCodeCheckResult parse_G90(const GCodeLine& gcode_line);
    GCodeCheckResult parse_G91(const GCodeLine& gcode_line);
    GCodeCheckResult parse_G92(GCodeLine& gcode_line);
    GCodeCheckResult parse_M82(const GCodeLine& gcode_line);
    GCodeCheckResult parse_M83(const GCodeLine& gcode_line);
    GCodeCheckResult parse_M104_M109(const GCodeLine &gcode_line);

    GCodeCheckResult parse_comment(GCodeLine& gcode_line);

    GCodeCheckResult check_line_width(const GCodeLine& gcode_line);
    GCodeCheckResult check_G0_G1_width(const GCodeLine& gcode_line);
    GCodeCheckResult check_G2_G3_width(const GCodeLine& gcode_line);

    double calculate_G1_width(const std::array<double, 3>& source,
                             const std::array<double, 3>& target,
                             double e, double height, bool is_bridge) const;
    double calculate_G2_G3_width(const std::array<double, 2>& source,
                                const std::array<double, 2>& target,
                                const std::array<double, 2>& center,
                                bool is_ccw, double e, double height, bool is_bridge) const;

public:
    static bool is_whitespace(char c) { return c == ' ' || c == '\t'; }
    static bool is_end_of_line(char c) { return c == '\r' || c == '\n' || c == 0; }
    static bool is_comment_line(char c) { return c == ';'; }
    static bool is_end_of_gcode_line(char c) { return is_comment_line(c) || is_end_of_line(c); }
    static bool is_end_of_word(char c) { return is_whitespace(c) || is_end_of_gcode_line(c); }
    static const char*  skip_word(const char *c) { 
        for (; ! is_end_of_word(*c); ++ c)
            ; // silence -Wempty-body
        return c;
    }
    static const char*  skip_whitespaces(const char *c) { 
        for (; is_whitespace(*c); ++ c)
            ; // silence -Wempty-body
        return c;
    }
    static bool is_single_gcode_word(const char* c) {
        c = skip_word(c);
        c = skip_whitespaces(c);
        return is_end_of_gcode_line(*c);
    }
    static bool starts_with(const std::string &comment, const std::string &tag) {
        size_t tag_len = tag.size();
        return comment.size() >= tag_len && comment.substr(0, tag_len) == tag;
    }
    static ExtrusionRole string_to_role(const std::string& role);
    //BBS: Returns true if the number was parsed correctly into out and the number spanned the whole input string.
    static bool parse_double_from_str(const std::string& input, double& out) {
        size_t read = 0;
        try {
            out = std::stod(input, &read);
            return input.size() == read;
        } catch (...) {
            return false;
        }
    }

    static bool parse_double_from_str(const std::string &input, std::vector<double> &out)
    {

        std::string cmd=input;
        size_t read = 0;

        while (cmd.size() >= 5)
        {
            int pt = 0;
            for (pt = 0; pt < cmd.size(); pt++) {
                char temp = cmd[pt];
                if (temp == ',')
                {
                    try {
                        double num = std::stod(cmd.substr(0, pt), &read);

                        out.push_back(num);
                        cmd =  cmd.substr(pt+1);
                        break;
                    } catch (...) {
                        return false;
                    }
                }
            }
        }

        double num = std::stod(cmd, &read);
        out.push_back(num);

        return true;
    }

private:
    EPositioningType m_global_positioning_type = EPositioningType::Absolute;
    EPositioningType m_e_local_positioning_type = EPositioningType::Absolute;

    std::array<double, 4> m_start_position = { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, 4> m_end_position = { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, 4> m_origin = { 0.0, 0.0, 0.0, 0.0 };

    //BBS: use these value to save information from comment
    ExtrusionRole m_role = erNone;
    bool m_wiping = false;
    int m_layer_num = 0;
    double m_height = 0.0;
    double m_width = 0.0;
    double z_height=0.0f;
    double initial_layer_height=0.0f;
    int                 filament_id;
    double              flow_ratio  = 0;
    double              nozzle_temp = 0.0f;
    std::vector<double> filament_flow_ratio;
    std::vector<double> nozzle_temperature;
    std::vector<double> nozzle_temperature_initial_layer;
};

}

#endif
