#ifndef slic3r_Config_hpp_
#define slic3r_Config_hpp_

#include <map>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <myinit.h>
#include "Point.hpp"

namespace Slic3r {

typedef std::string t_config_option_key;
typedef std::vector<std::string> t_config_option_keys;

class ConfigOption {
    public:
    virtual ~ConfigOption() {};
    virtual std::string serialize() const = 0;
    virtual bool deserialize(std::string str) = 0;
    virtual int getInt() const { return 0; };
    virtual void setInt(int val) {};
};

class ConfigOptionVectorBase : public ConfigOption {
    public:
    virtual ~ConfigOptionVectorBase() {};
    virtual std::vector<std::string> vserialize() const = 0;
};

template <class T>
class ConfigOptionVector : public ConfigOptionVectorBase
{
    public:
    virtual ~ConfigOptionVector() {};
    std::vector<T> values;
    
    T get_at(size_t i) const {
        try {
            return this->values.at(i);
        } catch (const std::out_of_range& oor) {
            return this->values.front();
        }
    };
};

class ConfigOptionFloat : public ConfigOption
{
    public:
    double value;  // use double instead of float for preserving compatibility with values coming from Perl
    ConfigOptionFloat() : value(0) {};
    
    operator float() const { return this->value; };
    operator double() const { return this->value; };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value;
        return ss.str();
    };
    
    bool deserialize(std::string str) {
        std::istringstream iss(str);
        return iss >> this->value;
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
        for (std::vector<double>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << *it;
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(std::string str) {
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

class ConfigOptionInt : public ConfigOption
{
    public:
    int value;
    ConfigOptionInt() : value(0) {};
    
    operator int() const { return this->value; };
    int getInt() const { return this->value; };
    void setInt(int val) { this->value = val; };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->value;
        return ss.str();
    };
    
    bool deserialize(std::string str) {
        std::istringstream iss(str);
        return iss >> this->value;
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
        for (std::vector<int>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << *it;
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(std::string str) {
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

class ConfigOptionString : public ConfigOption
{
    public:
    std::string value;
    ConfigOptionString() : value("") {};
    
    operator std::string() const { return this->value; };
    
    std::string serialize() const {
        std::string str = this->value;
        
        // s/\R/\\n/g
        size_t pos = 0;
        while ((pos = str.find("\n", pos)) != std::string::npos || (pos = str.find("\r", pos)) != std::string::npos) {
            str.replace(pos, 1, "\\n");
            pos += 2; // length of "\\n"
        }
        
        return str; 
    };
    
    bool deserialize(std::string str) {
        // s/\\n/\n/g
        size_t pos = 0;
        while ((pos = str.find("\\n", pos)) != std::string::npos) {
            str.replace(pos, 2, "\n");
            pos += 1; // length of "\n"
        }
        
        this->value = str;
        return true;
    };
};

// semicolon-separated strings
class ConfigOptionStrings : public ConfigOptionVector<std::string>
{
    public:
    
    std::string serialize() const {
        std::ostringstream ss;
        for (std::vector<std::string>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ";";
            ss << *it;
        }
        return ss.str();
    };
    
    std::vector<std::string> vserialize() const {
        return this->values;
    };
    
    bool deserialize(std::string str) {
        this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ';')) {
            this->values.push_back(item_str);
        }
        return true;
    };
};

class ConfigOptionPercent : public ConfigOption
{
    public:
    double value;
    ConfigOptionPercent() : value(0) {};
    
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
    
    bool deserialize(std::string str) {
        // don't try to parse the trailing % since it's optional
        std::istringstream iss(str);
        return iss >> this->value;
    };
};

class ConfigOptionFloatOrPercent : public ConfigOption
{
    public:
    double value;
    bool percent;
    ConfigOptionFloatOrPercent() : value(0), percent(false) {};
    
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
    
    bool deserialize(std::string str) {
        this->percent = str.find_first_of("%") != std::string::npos;
        std::istringstream iss(str);
        return iss >> this->value;
    };
};

class ConfigOptionPoint : public ConfigOption
{
    public:
    Pointf point;
    ConfigOptionPoint() : point(Pointf(0,0)) {};
    
    operator Pointf() const { return this->point; };
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << this->point.x;
        ss << ",";
        ss << this->point.y;
        return ss.str();
    };
    
    bool deserialize(std::string str) {
        std::istringstream iss(str);
        iss >> this->point.x;
        iss.ignore(std::numeric_limits<std::streamsize>::max(), ',');
        iss.ignore(std::numeric_limits<std::streamsize>::max(), 'x');
        iss >> this->point.y;
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
    
    bool deserialize(std::string str) {
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

class ConfigOptionBool : public ConfigOption
{
    public:
    bool value;
    ConfigOptionBool() : value(false) {};
    
    operator bool() const { return this->value; };
    
    std::string serialize() const {
        return std::string(this->value ? "1" : "0");
    };
    
    bool deserialize(std::string str) {
        this->value = (str.compare("1") == 0);
        return true;
    };
};

class ConfigOptionBools : public ConfigOptionVector<bool>
{
    public:
    
    std::string serialize() const {
        std::ostringstream ss;
        for (std::vector<bool>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << (*it ? "1" : "0");
        }
        return ss.str();
    };
    
    std::vector<std::string> vserialize() const {
        std::vector<std::string> vv;
        for (std::vector<bool>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            std::ostringstream ss;
            ss << (*it ? "1" : "0");
            vv.push_back(ss.str());
        }
        return vv;
    };
    
    bool deserialize(std::string str) {
        this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            this->values.push_back(item_str.compare("1") == 0);
        }
        return true;
    };
};

typedef std::map<std::string,int> t_config_enum_values;

template <class T>
class ConfigOptionEnum : public ConfigOption
{
    public:
    T value;
    
    operator T() const { return this->value; };
    
    std::string serialize() const {
        t_config_enum_values enum_keys_map = ConfigOptionEnum<T>::get_enum_values();
        for (t_config_enum_values::iterator it = enum_keys_map.begin(); it != enum_keys_map.end(); ++it) {
            if (it->second == static_cast<int>(this->value)) return it->first;
        }
        return "";
    };

    bool deserialize(std::string str) {
        t_config_enum_values enum_keys_map = ConfigOptionEnum<T>::get_enum_values();
        if (enum_keys_map.count(str) == 0) return false;
        this->value = static_cast<T>(enum_keys_map[str]);
        return true;
    };

    static t_config_enum_values get_enum_values();
};

/* We use this one in DynamicConfig objects, otherwise it's better to use
   the specialized ConfigOptionEnum<T> containers. */
class ConfigOptionEnumGeneric : public ConfigOption
{
    public:
    int value;
    t_config_enum_values* keys_map;
    
    operator int() const { return this->value; };
    
    std::string serialize() const {
        for (t_config_enum_values::iterator it = this->keys_map->begin(); it != this->keys_map->end(); ++it) {
            if (it->second == this->value) return it->first;
        }
        return "";
    };

    bool deserialize(std::string str) {
        if (this->keys_map->count(str) == 0) return false;
        this->value = (*this->keys_map)[str];
        return true;
    };
};

enum ConfigOptionType {
    coFloat,
    coFloats,
    coInt,
    coInts,
    coString,
    coStrings,
    coPercent,
    coFloatOrPercent,
    coPoint,
    coPoints,
    coBool,
    coBools,
    coEnum,
};

class ConfigOptionDef
{
    public:
    ConfigOptionType type;
    std::string gui_type;
    std::string gui_flags;
    std::string label;
    std::string full_label;
    std::string category;
    std::string tooltip;
    std::string sidetext;
    std::string cli;
    t_config_option_key ratio_over;
    bool multiline;
    bool full_width;
    bool readonly;
    int height;
    int width;
    int min;
    int max;
    std::vector<t_config_option_key> aliases;
    std::vector<t_config_option_key> shortcut;
    std::vector<std::string> enum_values;
    std::vector<std::string> enum_labels;
    t_config_enum_values enum_keys_map;
    
    ConfigOptionDef() : multiline(false), full_width(false), readonly(false),
                        height(-1), width(-1), min(INT_MIN), max(INT_MAX) {};
};

typedef std::map<t_config_option_key,ConfigOptionDef> t_optiondef_map;

class ConfigBase
{
    public:
    t_optiondef_map* def;
    
    ConfigBase() : def(NULL) {};
    bool has(const t_config_option_key opt_key);
    virtual ConfigOption* option(const t_config_option_key opt_key, bool create = false) = 0;
    virtual const ConfigOption* option(const t_config_option_key opt_key) const = 0;
    virtual void keys(t_config_option_keys *keys) const = 0;
    void apply(const ConfigBase &other, bool ignore_nonexistent = false);
    bool equals(ConfigBase &other);
    t_config_option_keys diff(ConfigBase &other);
    std::string serialize(const t_config_option_key opt_key);
    bool set_deserialize(const t_config_option_key opt_key, std::string str);
    void set_ifndef(t_config_option_key opt_key, SV* value, bool deserialize = false);
    double get_abs_value(const t_config_option_key opt_key);
    double get_abs_value(const t_config_option_key opt_key, double ratio_over);
    
    #ifdef SLIC3RXS
    SV* as_hash();
    SV* get(t_config_option_key opt_key);
    SV* get_at(t_config_option_key opt_key, size_t i);
    bool set(t_config_option_key opt_key, SV* value);
    bool set_deserialize(const t_config_option_key opt_key, SV* str);
    #endif
};

class DynamicConfig : public ConfigBase
{
    public:
    DynamicConfig() {};
    DynamicConfig(const DynamicConfig& other);
    DynamicConfig& operator= (DynamicConfig other);
    void swap(DynamicConfig &other);
    ~DynamicConfig();
    template<class T> T* opt(const t_config_option_key opt_key, bool create = false);
    ConfigOption* option(const t_config_option_key opt_key, bool create = false);
    const ConfigOption* option(const t_config_option_key opt_key) const;
    void keys(t_config_option_keys *keys) const;
    void erase(const t_config_option_key opt_key);
    
    private:
    typedef std::map<t_config_option_key,ConfigOption*> t_options_map;
    t_options_map options;
};

class StaticConfig : public ConfigBase
{
    public:
    void keys(t_config_option_keys *keys) const;
    virtual ConfigOption* option(const t_config_option_key opt_key, bool create = false) = 0;
    const ConfigOption* option(const t_config_option_key opt_key) const;
    
    #ifdef SLIC3RXS
    bool set(t_config_option_key opt_key, SV* value);
    #endif
};

}

#endif
