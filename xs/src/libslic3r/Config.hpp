#ifndef slic3r_Config_hpp_
#define slic3r_Config_hpp_

#include <assert.h>
#include <map>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

// Name of the configuration option.
typedef std::string t_config_option_key;
typedef std::vector<std::string> t_config_option_keys;

extern std::string escape_string_cstyle(const std::string &str);
extern std::string escape_strings_cstyle(const std::vector<std::string> &strs);
extern bool unescape_string_cstyle(const std::string &str, std::string &out);
extern bool unescape_strings_cstyle(const std::string &str, std::vector<std::string> &out);

// A generic value of a configuration option.
class ConfigOption {
public:
    virtual ~ConfigOption() {};
    virtual std::string serialize() const = 0;
    virtual bool deserialize(const std::string &str, bool append = false) = 0;
    virtual void set(const ConfigOption &option) = 0;
    virtual int getInt() const { return 0; };
    virtual double getFloat() const { return 0; };
    virtual bool getBool() const { return false; };
    virtual void setInt(int /* val */) { };
    bool operator==(const ConfigOption &rhs) { return this->serialize().compare(rhs.serialize()) == 0; }
    bool operator!=(const ConfigOption &rhs) { return this->serialize().compare(rhs.serialize()) != 0; }
};

// Value of a single valued option (bool, int, float, string, point, enum)
template <class T>
class ConfigOptionSingle : public ConfigOption {
public:
    T value;
    ConfigOptionSingle(T _value) : value(_value) {};
    operator T() const { return this->value; };
    
    void set(const ConfigOption &option) {
        const ConfigOptionSingle<T>* other = dynamic_cast< const ConfigOptionSingle<T>* >(&option);
        if (other != nullptr) this->value = other->value;
    };
};

// Value of a vector valued option (bools, ints, floats, strings, points)
class ConfigOptionVectorBase : public ConfigOption {
public:
    virtual ~ConfigOptionVectorBase() {};
    // Currently used only to initialize the PlaceholderParser.
    virtual std::vector<std::string> vserialize() const = 0;
};

// Value of a vector valued option (bools, ints, floats, strings, points), template
template <class T>
class ConfigOptionVector : public ConfigOptionVectorBase
{
public:
    virtual ~ConfigOptionVector() {};
    std::vector<T> values;
    
    void set(const ConfigOption &option) {
        const ConfigOptionVector<T>* other = dynamic_cast< const ConfigOptionVector<T>* >(&option);
        if (other != nullptr) this->values = other->values;
    };
    
    T& get_at(size_t i) {
        assert(! this->values.empty());
        return (i < this->values.size()) ? this->values[i] : this->values.front();
    };

    const T& get_at(size_t i) const { return const_cast<ConfigOptionVector<T>*>(this)->get_at(i); }
};

class ConfigOptionFloat : public ConfigOptionSingle<double>
{
public:
    ConfigOptionFloat() : ConfigOptionSingle<double>(0) {};
    ConfigOptionFloat(double _value) : ConfigOptionSingle<double>(_value) {};
    
    double getFloat() const { return this->value; };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value;
        return ss.str();
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        std::istringstream iss(str);
        iss >> this->value;
        return !iss.fail();
    };
};

class ConfigOptionFloats : public ConfigOptionVector<double>
{
public:
    std::string serialize() const {
        std::ostringstream ss;
        for (std::vector<double>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << *it;
        }
        return ss.str();
    };
    
    std::vector<std::string> vserialize() const {
        std::vector<std::string> vv;
        vv.reserve(this->values.size());
        for (std::vector<double>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << *it;
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        if (! append)
            this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            std::istringstream iss(item_str);
            double value;
            iss >> value;
            this->values.push_back(value);
        }
        return true;
    };
};

class ConfigOptionInt : public ConfigOptionSingle<int>
{
public:
    ConfigOptionInt() : ConfigOptionSingle<int>(0) {};
    ConfigOptionInt(double _value) : ConfigOptionSingle<int>(int(floor(_value + 0.5))) {};
    
    int getInt() const { return this->value; };
    void setInt(int val) { this->value = val; };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value;
        return ss.str();
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        std::istringstream iss(str);
        iss >> this->value;
        return !iss.fail();
    };
};

class ConfigOptionInts : public ConfigOptionVector<int>
{
public:
    std::string serialize() const {
        std::ostringstream ss;
        for (std::vector<int>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << *it;
        }
        return ss.str();
    };
    
    std::vector<std::string> vserialize() const {
        std::vector<std::string> vv;
        vv.reserve(this->values.size());
        for (std::vector<int>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << *it;
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        if (! append)
            this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            std::istringstream iss(item_str);
            int value;
            iss >> value;
            this->values.push_back(value);
        }
        return true;
    };
};

class ConfigOptionString : public ConfigOptionSingle<std::string>
{
public:
    ConfigOptionString() : ConfigOptionSingle<std::string>("") {};
    ConfigOptionString(std::string _value) : ConfigOptionSingle<std::string>(_value) {};
    
    std::string serialize() const { 
        return escape_string_cstyle(this->value);
    }

    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        return unescape_string_cstyle(str, this->value);
    };
};

// semicolon-separated strings
class ConfigOptionStrings : public ConfigOptionVector<std::string>
{
public:
    std::string serialize() const {
        return escape_strings_cstyle(this->values);
    };
    
    std::vector<std::string> vserialize() const {
        return this->values;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        if (! append)
            this->values.clear();
        return unescape_strings_cstyle(str, this->values);
    };
};

class ConfigOptionPercent : public ConfigOptionFloat
{
public:
    ConfigOptionPercent() : ConfigOptionFloat(0) {};
    ConfigOptionPercent(double _value) : ConfigOptionFloat(_value) {};
    
    double get_abs_value(double ratio_over) const {
        return ratio_over * this->value / 100;
    };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value;
        std::string s(ss.str());
        s += "%";
        return s;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        // don't try to parse the trailing % since it's optional
        std::istringstream iss(str);
        iss >> this->value;
        return !iss.fail();
    };
};

class ConfigOptionPercents : public ConfigOptionFloats
{
public:    
    std::string serialize() const {
        std::ostringstream ss;
        for (const auto &v : this->values) {
            if (&v != &this->values.front()) ss << ",";
            ss << v << "%";
        }
        std::string str = ss.str();
        return str;
    };
    
    std::vector<std::string> vserialize() const {
        std::vector<std::string> vv;
        vv.reserve(this->values.size());
        for (const auto v : this->values) {
            std::ostringstream ss;
            ss << v;
            std::string sout = ss.str() + "%";
            vv.push_back(sout);
        }
        return vv;
    };

    bool deserialize(const std::string &str, bool append = false) {
        if (! append)
            this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            std::istringstream iss(item_str);
            double value;
            // don't try to parse the trailing % since it's optional
            iss >> value;
            this->values.push_back(value);
        }
        return true;
    };
};

class ConfigOptionFloatOrPercent : public ConfigOptionPercent
{
public:
    bool percent;
    ConfigOptionFloatOrPercent() : ConfigOptionPercent(0), percent(false) {};
    ConfigOptionFloatOrPercent(double _value, bool _percent)
        : ConfigOptionPercent(_value), percent(_percent) {};
    
    void set(const ConfigOption &option) {
        const ConfigOptionFloatOrPercent* other = dynamic_cast< const ConfigOptionFloatOrPercent* >(&option);
        if (other != NULL) {
            this->value = other->value;
            this->percent = other->percent;
        }
    };
    
    double get_abs_value(double ratio_over) const {
        if (this->percent) {
            return ratio_over * this->value / 100;
        } else {
            return this->value;
        }
    };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value;
        std::string s(ss.str());
        if (this->percent) s += "%";
        return s;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        this->percent = str.find_first_of("%") != std::string::npos;
        std::istringstream iss(str);
        iss >> this->value;
        return !iss.fail();
    };
};

class ConfigOptionPoint : public ConfigOptionSingle<Pointf>
{
public:
    ConfigOptionPoint() : ConfigOptionSingle<Pointf>(Pointf(0,0)) {};
    ConfigOptionPoint(Pointf _value) : ConfigOptionSingle<Pointf>(_value) {};
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value.x;
        ss << ",";
        ss << this->value.y;
        return ss.str();
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        std::istringstream iss(str);
        iss >> this->value.x;
        iss.ignore(std::numeric_limits<std::streamsize>::max(), ',');
        iss.ignore(std::numeric_limits<std::streamsize>::max(), 'x');
        iss >> this->value.y;
        return true;
    };
};

class ConfigOptionPoints : public ConfigOptionVector<Pointf>
{
public:
    std::string serialize() const {
        std::ostringstream ss;
        for (Pointfs::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << it->x;
            ss << "x";
            ss << it->y;
        }
        return ss.str();
    };
    
    std::vector<std::string> vserialize() const {
        std::vector<std::string> vv;
        for (Pointfs::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << *it;
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        if (! append)
            this->values.clear();
        std::istringstream is(str);
        std::string point_str;
        while (std::getline(is, point_str, ',')) {
            Pointf point;
            std::istringstream iss(point_str);
            std::string coord_str;
            if (std::getline(iss, coord_str, 'x')) {
                std::istringstream(coord_str) >> point.x;
                if (std::getline(iss, coord_str, 'x')) {
                    std::istringstream(coord_str) >> point.y;
                }
            }
            this->values.push_back(point);
        }
        return true;
    };
};

class ConfigOptionBool : public ConfigOptionSingle<bool>
{
public:
    ConfigOptionBool() : ConfigOptionSingle<bool>(false) {};
    ConfigOptionBool(bool _value) : ConfigOptionSingle<bool>(_value) {};
    
    bool getBool() const { return this->value; };
    
    std::string serialize() const {
        return std::string(this->value ? "1" : "0");
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        this->value = (str.compare("1") == 0);
        return true;
    };
};

class ConfigOptionBools : public ConfigOptionVector<unsigned char>
{
public:
    void set(const ConfigOption &option) {
        const ConfigOptionVector<unsigned char>* other = dynamic_cast<const ConfigOptionVector<unsigned char>*>(&option);
        if (other != nullptr) 
            this->values = other->values;
    };
    
    bool& get_at(size_t i) {
        assert(! this->values.empty());
        return *reinterpret_cast<bool*>(&((i < this->values.size()) ? this->values[i] : this->values.front()));
    };

    bool get_at(size_t i) const { return bool((i < this->values.size()) ? this->values[i] : this->values.front()); }

    std::string serialize() const {
        std::ostringstream ss;
        for (std::vector<unsigned char>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << (*it ? "1" : "0");
        }
        return ss.str();
    };
    
    std::vector<std::string> vserialize() const {
        std::vector<std::string> vv;
        for (std::vector<unsigned char>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << (*it ? "1" : "0");
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(const std::string &str, bool append = false) {
        if (! append)
            this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            this->values.push_back(item_str.compare("1") == 0);
        }
        return true;
    };
};

// Map from an enum name to an enum integer value.
typedef std::map<std::string,int> t_config_enum_values;

template <class T>
class ConfigOptionEnum : public ConfigOptionSingle<T>
{
public:
    // by default, use the first value (0) of the T enum type
    ConfigOptionEnum() : ConfigOptionSingle<T>(static_cast<T>(0)) {};
    ConfigOptionEnum(T _value) : ConfigOptionSingle<T>(_value) {};
    
    std::string serialize() const {
        t_config_enum_values enum_keys_map = ConfigOptionEnum<T>::get_enum_values();
        for (t_config_enum_values::iterator it = enum_keys_map.begin(); it != enum_keys_map.end(); ++it) {
            if (it->second == static_cast<int>(this->value)) return it->first;
        }
        return "";
    };

    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        t_config_enum_values enum_keys_map = ConfigOptionEnum<T>::get_enum_values();
        if (enum_keys_map.count(str) == 0) return false;
        this->value = static_cast<T>(enum_keys_map[str]);
        return true;
    };

    // Map from an enum name to an enum integer value.
    //FIXME The map is called often, it shall be initialized statically.
    static t_config_enum_values get_enum_values();
};

// Generic enum configuration value.
// We use this one in DynamicConfig objects when creating a config value object for ConfigOptionType == coEnum.
// In the StaticConfig, it is better to use the specialized ConfigOptionEnum<T> containers.
class ConfigOptionEnumGeneric : public ConfigOptionInt
{
public:
    const t_config_enum_values* keys_map;
    
    std::string serialize() const {
        for (t_config_enum_values::const_iterator it = this->keys_map->begin(); it != this->keys_map->end(); ++it) {
            if (it->second == this->value) return it->first;
        }
        return "";
    };

    bool deserialize(const std::string &str, bool append = false) {
        UNUSED(append);
        if (this->keys_map->count(str) == 0) return false;
        this->value = (*const_cast<t_config_enum_values*>(this->keys_map))[str];
        return true;
    };
};

// Type of a configuration value.
enum ConfigOptionType {
    coNone,
    // single float
    coFloat,
    // vector of floats
    coFloats,
    // single int
    coInt,
    // vector of ints
    coInts,
    // single string
    coString,
    // vector of strings
    coStrings,
    // percent value. Currently only used for infill.
    coPercent,
    // percents value. Currently used for retract before wipe only.
    coPercents,
    // a fraction or an absolute value
    coFloatOrPercent,
    // single 2d point. Currently not used.
    coPoint,
    // vector of 2d points. Currently used for the definition of the print bed and for the extruder offsets.
    coPoints,
    // single boolean value
    coBool,
    // vector of boolean values
    coBools,
    // a generic enum
    coEnum,
};

// Definition of a configuration value for the purpose of GUI presentation, editing, value mapping and config file handling.
class ConfigOptionDef
{
public:
    // What type? bool, int, string etc.
    ConfigOptionType type;
    // Default value of this option. The default value object is owned by ConfigDef, it is released in its destructor.
    ConfigOption* default_value;

    // Usually empty. 
    // Special values - "i_enum_open", "f_enum_open" to provide combo box for int or float selection,
    // "select_open" - to open a selection dialog (currently only a serial port selection).
    std::string gui_type;
    // Usually empty. Otherwise "serialized" or "show_value"
    // The flags may be combined.
    // "serialized" - vector valued option is entered in a single edit field. Values are separated by a semicolon.
    // "show_value" - even if enum_values / enum_labels are set, still display the value, not the enum label.
    std::string gui_flags;
    // Label of the GUI input field.
    // In case the GUI input fields are grouped in some views, the label defines a short label of a grouped value,
    // while full_label contains a label of a stand-alone field.
    // The full label is shown, when adding an override parameter for an object or a modified object.
    std::string label;
    std::string full_label;
    // Category of a configuration field, from the GUI perspective.
    // One of: "Layers and Perimeters", "Infill", "Support material", "Speed", "Extruders", "Advanced", "Extrusion Width"
    std::string category;
    // A tooltip text shown in the GUI.
    std::string tooltip;
    // Text right from the input field, usually a unit of measurement.
    std::string sidetext;
    // Format of this parameter on a command line.
    std::string cli;
    // Set for type == coFloatOrPercent.
    // It provides a link to a configuration value, of which this option provides a ratio.
    // For example, 
    // For example external_perimeter_speed may be defined as a fraction of perimeter_speed.
    t_config_option_key ratio_over;
    // True for multiline strings.
    bool multiline;
    // For text input: If true, the GUI text box spans the complete page width.
    bool full_width;
    // Not editable. Currently only used for the display of the number of threads.
    bool readonly;
    // Height of a multiline GUI text box.
    int height;
    // Optional width of an input field.
    int width;
    // <min, max> limit of a numeric input.
    // If not set, the <min, max> is set to <INT_MIN, INT_MAX>
    // By setting min=0, only nonnegative input is allowed.
    int min;
    int max;
    // Legacy names for this configuration option.
    // Used when parsing legacy configuration file.
    std::vector<t_config_option_key> aliases;
    // Sometimes a single value may well define multiple values in a "beginner" mode.
    // Currently used for aliasing "solid_layers" to "top_solid_layers", "bottom_solid_layers".
    std::vector<t_config_option_key> shortcut;
    // Definition of values / labels for a combo box.
    // Mostly used for enums (when type == coEnum), but may be used for ints resp. floats, if gui_type is set to "i_enum_open" resp. "f_enum_open".
    std::vector<std::string> enum_values;
    std::vector<std::string> enum_labels;
    // For enums (when type == coEnum). Maps enum_values to enums.
    // Initialized by ConfigOptionEnum<xxx>::get_enum_values()
    t_config_enum_values enum_keys_map;

    ConfigOptionDef() : type(coNone), default_value(NULL),
                        multiline(false), full_width(false), readonly(false),
                        height(-1), width(-1), min(INT_MIN), max(INT_MAX) {};
};

// Map from a config option name to its definition.
// The definition does not carry an actual value of the config option, only its constant default value.
// t_config_option_key is std::string
typedef std::map<t_config_option_key,ConfigOptionDef> t_optiondef_map;

// Definition of configuration values for the purpose of GUI presentation, editing, value mapping and config file handling.
// The configuration definition is static: It does not carry the actual configuration values,
// but it carries the defaults of the configuration values.
class ConfigDef
{
public:
    t_optiondef_map options;
    ~ConfigDef() { for (auto &opt : this->options) delete opt.second.default_value; }
    ConfigOptionDef* add(const t_config_option_key &opt_key, ConfigOptionType type) {
        ConfigOptionDef* opt = &this->options[opt_key];
        opt->type = type;
        return opt;
    }
    bool has(const t_config_option_key &opt_key) const { return this->options.count(opt_key) > 0; }
    const ConfigOptionDef* get(const t_config_option_key &opt_key) const {
        t_optiondef_map::iterator it = const_cast<ConfigDef*>(this)->options.find(opt_key);
        return (it == this->options.end()) ? nullptr : &it->second;
    }
};

// An abstract configuration store.
class ConfigBase
{
public:
    // Definition of configuration values for the purpose of GUI presentation, editing, value mapping and config file handling.
    // The configuration definition is static: It does not carry the actual configuration values,
    // but it carries the defaults of the configuration values.
    // ConfigBase does not own ConfigDef, it only references it.
    const ConfigDef* def;
    
    ConfigBase(const ConfigDef *def = nullptr) : def(def) {};
    virtual ~ConfigBase() {};
    bool has(const t_config_option_key &opt_key) const { return this->option(opt_key) != nullptr; }
    const ConfigOption* option(const t_config_option_key &opt_key) const
        { return const_cast<ConfigBase*>(this)->option(opt_key, false); }
    ConfigOption* option(const t_config_option_key &opt_key, bool create = false)
        { return this->optptr(opt_key, create); }
    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) = 0;
    virtual t_config_option_keys keys() const = 0;
    void apply(const ConfigBase &other, bool ignore_nonexistent = false) { this->apply(other, other.keys(), ignore_nonexistent); }
    void apply(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false);
    bool equals(const ConfigBase &other) const { return this->diff(other).empty(); }
    t_config_option_keys diff(const ConfigBase &other) const;
    std::string serialize(const t_config_option_key &opt_key) const;
    bool set_deserialize(t_config_option_key opt_key, const std::string &str, bool append = false);

    double get_abs_value(const t_config_option_key &opt_key) const;
    double get_abs_value(const t_config_option_key &opt_key, double ratio_over) const;
    void setenv_();
    void load(const std::string &file);
    void load_from_gcode(const std::string &file);
    void save(const std::string &file) const;
};

// Configuration store with dynamic number of configuration values.
// In Slic3r, the dynamic config is mostly used at the user interface layer.
class DynamicConfig : public virtual ConfigBase
{
public:
    DynamicConfig() {};
    DynamicConfig(const DynamicConfig& other) : ConfigBase(other.def) { this->apply(other, false); }
    DynamicConfig& operator= (DynamicConfig other) { this->swap(other); return *this; }
    virtual ~DynamicConfig() { for (auto &opt : this->options) delete opt.second; }
    void swap(DynamicConfig &other) { std::swap(this->def, other.def); std::swap(this->options, other.options); }
    template<class T> T* opt(const t_config_option_key &opt_key, bool create = false);
    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false);
    t_config_option_keys keys() const;
    void erase(const t_config_option_key &opt_key) { this->options.erase(opt_key); }

    std::string&        opt_string(const t_config_option_key &opt_key, bool create = false)     { return dynamic_cast<ConfigOptionString*>(this->optptr(opt_key, create))->value; }
    const std::string&  opt_string(const t_config_option_key &opt_key) const                    { return const_cast<DynamicConfig*>(this)->opt_string(opt_key); }
    std::string&        opt_string(const t_config_option_key &opt_key, unsigned int idx)        { return dynamic_cast<ConfigOptionStrings*>(this->optptr(opt_key))->get_at(idx); }
    const std::string&  opt_string(const t_config_option_key &opt_key, unsigned int idx) const  { return const_cast<DynamicConfig*>(this)->opt_string(opt_key, idx); }

    double&             opt_float(const t_config_option_key &opt_key)                           { return dynamic_cast<ConfigOptionFloat*>(this->optptr(opt_key))->value; }
    const double&       opt_float(const t_config_option_key &opt_key) const                     { return const_cast<DynamicConfig*>(this)->opt_float(opt_key); }
    double&             opt_float(const t_config_option_key &opt_key, unsigned int idx)         { return dynamic_cast<ConfigOptionFloats*>(this->optptr(opt_key))->get_at(idx); }
    const double&       opt_float(const t_config_option_key &opt_key, unsigned int idx) const   { return const_cast<DynamicConfig*>(this)->opt_float(opt_key, idx); }

private:
    typedef std::map<t_config_option_key,ConfigOption*> t_options_map;
    t_options_map options;
};

// Configuration store with a static definition of configuration values.
// In Slic3r, the static configuration stores are during the slicing / g-code generation for efficiency reasons,
// because the configuration values could be accessed directly.
class StaticConfig : public virtual ConfigBase
{
public:
    StaticConfig() : ConfigBase() {};
    // Gets list of config option names for each config option of this->def, which has a static counter-part defined by the derived object
    // and which could be resolved by this->optptr(key) call.
    t_config_option_keys keys() const;
    // Set all statically defined config options to their defaults defined by this->def.
    void set_defaults();
    // The derived class has to implement optptr to resolve a static configuration value.
    // virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) = 0;
};

/// Specialization of std::exception to indicate that an unknown config option has been encountered.
class UnknownOptionException : public std::exception {};

}

#endif
