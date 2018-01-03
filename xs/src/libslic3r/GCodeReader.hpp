#ifndef slic3r_GCodeReader_hpp_
#define slic3r_GCodeReader_hpp_

#include "libslic3r.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include "PrintConfig.hpp"

namespace Slic3r {

class GCodeReader {
public:
    class GCodeLine {
    public:
        GCodeLine() { reset(); }
        void reset() { m_mask = 0; memset(m_axis, 0, sizeof(m_axis)); m_raw.clear(); }

        const std::string&  raw() const { return m_raw; }
        const std::string   cmd() const { 
            const char *cmd = GCodeReader::skip_whitespaces(m_raw.c_str());
            return std::string(cmd, GCodeReader::skip_word(cmd));
        }
        const std::string   comment() const
            { size_t pos = m_raw.find(';'); return (pos == std::string::npos) ? "" : m_raw.substr(pos + 1); }

        bool  has(Axis axis) const { return (m_mask & (1 << int(axis))) != 0; }
        float value(Axis axis) const { return m_axis[axis]; }
        bool  has_value(char axis, float &value) const;
        float new_Z(const GCodeReader &reader) const { return this->has(Z) ? this->z() : reader.z(); }
        float new_E(const GCodeReader &reader) const { return this->has(E) ? this->e() : reader.e(); }
        float new_F(const GCodeReader &reader) const { return this->has(F) ? this->f() : reader.f(); }
        float dist_X(const GCodeReader &reader) const { return this->has(X) ? (this->x() - reader.x()) : 0; }
        float dist_Y(const GCodeReader &reader) const { return this->has(Y) ? (this->y() - reader.y()) : 0; }
        float dist_Z(const GCodeReader &reader) const { return this->has(Z) ? (this->z() - reader.z()) : 0; }
        float dist_E(const GCodeReader &reader) const { return this->has(E) ? (this->e() - reader.e()) : 0; }
        float dist_XY(const GCodeReader &reader) const {
            float x = this->has(X) ? (this->x() - reader.x()) : 0;
            float y = this->has(Y) ? (this->y() - reader.y()) : 0;
            return sqrt(x*x + y*y);
        }
        bool cmd_is(const char *cmd_test) const {
            const char *cmd = GCodeReader::skip_whitespaces(m_raw.c_str());
            int len = strlen(cmd_test); 
            return strncmp(cmd, cmd_test, len) == 0 && GCodeReader::is_end_of_word(cmd[len]);
        }
        bool extruding(const GCodeReader &reader)  const { return this->cmd_is("G1") && this->dist_E(reader) > 0; }
        bool retracting(const GCodeReader &reader) const { return this->cmd_is("G1") && this->dist_E(reader) < 0; }
        bool travel()     const { return this->cmd_is("G1") && ! this->has(E); }
        void set(const GCodeReader &reader, const Axis axis, const float new_value, const int decimal_digits = 3);

        bool  has_x() const { return this->has(X); }
        bool  has_y() const { return this->has(Y); }
        bool  has_z() const { return this->has(Z); }
        bool  has_e() const { return this->has(E); }
        bool  has_f() const { return this->has(F); }
        float x() const { return m_axis[X]; }
        float y() const { return m_axis[Y]; }
        float z() const { return m_axis[Z]; }
        float e() const { return m_axis[E]; }
        float f() const { return m_axis[F]; }

    private:
        std::string      m_raw;
        float            m_axis[NUM_AXES];
        uint32_t         m_mask;
        friend class GCodeReader;
    };

    typedef std::function<void(GCodeReader&, const GCodeLine&)> callback_t;
    
    GCodeReader() : m_verbose(false), m_extrusion_axis('E') { memset(m_position, 0, sizeof(m_position)); }
    void apply_config(const GCodeConfig &config);
    void apply_config(const DynamicPrintConfig &config);

    template<typename Callback>
    void parse_buffer(const std::string &buffer, Callback callback)
    {
        const char *ptr = buffer.c_str();
        GCodeLine gline;
        while (*ptr != 0) {
            gline.reset();
            ptr = this->parse_line(ptr, gline, callback);
        }
    }

    void parse_buffer(const std::string &buffer)
        { this->parse_buffer(buffer, [](GCodeReader&, const GCodeReader::GCodeLine&){}); }

    template<typename Callback>
    const char* parse_line(const char *ptr, GCodeLine &gline, Callback &callback)
    {
        std::pair<const char*, const char*> cmd;
        const char *end = parse_line_internal(ptr, gline, cmd);
        callback(*this, gline);
        update_coordinates(gline, cmd);
        return end;
    }

    template<typename Callback>
    void parse_line(const std::string &line, Callback callback)
        { GCodeLine gline; this->parse_line(line.c_str(), gline, callback); }

    void parse_file(const std::string &file, callback_t callback);

    float& x()       { return m_position[X]; }
    float  x() const { return m_position[X]; }
    float& y()       { return m_position[Y]; }
    float  y() const { return m_position[Y]; }
    float& z()       { return m_position[Z]; }
    float  z() const { return m_position[Z]; }
    float& e()       { return m_position[E]; }
    float  e() const { return m_position[E]; }
    float& f()       { return m_position[F]; }
    float  f() const { return m_position[F]; }

    char   extrusion_axis() const { return m_extrusion_axis; }

private:
    const char* parse_line_internal(const char *ptr, GCodeLine &gline, std::pair<const char*, const char*> &command);
    void        update_coordinates(GCodeLine &gline, std::pair<const char*, const char*> &command);

    static bool         is_whitespace(char c)           { return c == ' ' || c == '\t'; }
    static bool         is_end_of_line(char c)          { return c == '\r' || c == '\n' || c == 0; }
    static bool         is_end_of_gcode_line(char c)    { return c == ';' || is_end_of_line(c); }
    static bool         is_end_of_word(char c)          { return is_whitespace(c) || is_end_of_gcode_line(c); }
    static const char*  skip_whitespaces(const char *c) { for (; is_whitespace(*c); ++ c); return c; }
    static const char*  skip_word(const char *c)        { for (; ! is_end_of_word(*c); ++ c); return c; }

    GCodeConfig m_config;
    char        m_extrusion_axis;
    float       m_position[NUM_AXES];
    bool        m_verbose;
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeReader_hpp_ */
