#include "PlaceholderParser.hpp"


namespace Slic3r {


PlaceholderParser::PlaceholderParser()
{
    // TODO: port these methods to C++, then call them here
    // this->apply_env_variables();
    // this->update_timestamp();
}

PlaceholderParser::~PlaceholderParser()
{
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

        // set placeholders for options with multiple values
        const ConfigOptionDef &def = (*config.def)[key];
        switch (def.type) {
        case coFloats:
            this->set_multiple_from_vector(key,
                        *(ConfigOptionFloats*)config.option(key));
            break;

        case coInts:
            this->set_multiple_from_vector(key,
                        *(ConfigOptionInts*)config.option(key));
            break;

        case coStrings:
            this->set_multiple_from_vector(key,
                        *(ConfigOptionStrings*)config.option(key));
            break;

        case coPoints:
            this->set_multiple_from_vector(key,
                        *(ConfigOptionPoints*)config.option(key));
            break;

        case coBools:
            this->set_multiple_from_vector(key,
                        *(ConfigOptionBools*)config.option(key));
            break;

        case coPoint:
            {
                const ConfigOptionPoint &opt =
                    *(ConfigOptionPoint*)config.option(key);

                this->_single[key] = opt.serialize();

                Pointf val = opt;
                this->_multiple[key + "_X"] = val.x;
                this->_multiple[key + "_Y"] = val.y;
            }

            break;

        default:
            // set single-value placeholders
            this->_single[key] = config.serialize(key);
            break;
        }
    }
}

std::ostream& operator<<(std::ostream &stm, const Pointf &pointf)
{
    return stm << pointf.x << "," << pointf.y;
}

template<class T>
void PlaceholderParser::set_multiple_from_vector(const std::string &key,
    ConfigOptionVector<T> &opt)
{
    const std::vector<T> &vals = opt.values;

    for (size_t i = 0; i < vals.size(); ++i) {
        std::stringstream multikey_stm;
        multikey_stm << key << "_" << i;

        std::stringstream val_stm;
        val_stm << vals[i];

        this->_multiple[multikey_stm.str()] = val_stm.str();
    }

    if (vals.size() > 0) {
        std::stringstream val_stm;
        val_stm << vals[0];
        this->_multiple[key] = val_stm.str();
    }
}

#ifdef SLIC3RXS
REGISTER_CLASS(PlaceholderParser, "GCode::PlaceholderParser");
#endif

}
