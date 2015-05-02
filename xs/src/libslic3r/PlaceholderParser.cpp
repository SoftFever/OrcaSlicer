#include "PlaceholderParser.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Slic3r {

PlaceholderParser::PlaceholderParser()
{
    this->_single["version"] = SLIC3R_VERSION;
    // TODO: port these methods to C++, then call them here
    // this->apply_env_variables();
    this->update_timestamp();
}

void
PlaceholderParser::update_timestamp()
{
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    
    {
        std::ostringstream ss;
        ss << (1900 + timeinfo->tm_year);
        ss << std::setw(2) << std::setfill('0') << (1 + timeinfo->tm_mon);
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_mday;
        ss << "-";
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_hour;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_min;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_sec;
        this->set("timestamp", ss.str());
    }
    this->set("year",   1900 + timeinfo->tm_year);
    this->set("month",  1 + timeinfo->tm_mon);
    this->set("day",    timeinfo->tm_mday);
    this->set("hour",   timeinfo->tm_hour);
    this->set("minute", timeinfo->tm_min);
    this->set("second", timeinfo->tm_sec);
}

void PlaceholderParser::apply_config(DynamicPrintConfig &config)
{
    // options that are set and aren't text-boxes
    t_config_option_keys opt_keys;
    for (t_optiondef_map::iterator i = config.def->begin();
        i != config.def->end(); ++i)
    {
        const t_config_option_key &key = i->first;
        const ConfigOptionDef &def = i->second;

        if (config.has(key) && !def.multiline) {
            opt_keys.push_back(key);
        }
    }

    for (t_config_option_keys::iterator i = opt_keys.begin();
        i != opt_keys.end(); ++i)
    {
        const t_config_option_key &key = *i;
        const ConfigOption* opt = config.option(key);
        
        if (const ConfigOptionVectorBase* optv = dynamic_cast<const ConfigOptionVectorBase*>(opt)) {
            // set placeholders for options with multiple values
            this->set(key, optv->vserialize());
        } else if (const ConfigOptionPoint* optp = dynamic_cast<const ConfigOptionPoint*>(opt)) {
            this->_single[key] = optp->serialize();
            
            Pointf val = *optp;
            this->_multiple[key + "_X"] = val.x;
            this->_multiple[key + "_Y"] = val.y;
        } else {
            // set single-value placeholders
            this->_single[key] = opt->serialize();
        }
    }
}

void
PlaceholderParser::set(const std::string &key, const std::string &value)
{
    this->_single[key] = value;
    this->_multiple.erase(key);
}

void
PlaceholderParser::set(const std::string &key, int value)
{
    std::ostringstream ss;
    ss << value;
    this->set(key, ss.str());
}

void
PlaceholderParser::set(const std::string &key, const std::vector<std::string> &values)
{
    for (std::vector<std::string>::const_iterator v = values.begin(); v != values.end(); ++v) {
        std::stringstream ss;
        ss << key << "_" << (v - values.begin());
        
        this->_multiple[ ss.str() ] = *v;
        if (v == values.begin()) {
            this->_multiple[key] = *v;
        }
    }
    this->_single.erase(key);
}

#ifdef SLIC3RXS
REGISTER_CLASS(PlaceholderParser, "GCode::PlaceholderParser");
#endif

}
