#include "PrintConfig.hpp"

namespace Slic3r {

t_optiondef_map PrintConfigDef::def = PrintConfigDef::build_def();

void
StaticPrintConfig::prepare_extruder_option(const t_config_option_key opt_key, DynamicPrintConfig& other)
{
    // don't apply role-based extruders if their value is zero
    if (other.has(opt_key) && other.option(opt_key)->getInt() == 0)
        other.erase(opt_key);
    
    // only apply default extruder if our role-based value is zero
    // (i.e. default extruder has the lowest priority among all other values)
    if (other.has("extruder")) {
        int extruder = other.option("extruder")->getInt();
        if (extruder > 0) {
            if (!other.has(opt_key) && this->option(opt_key)->getInt() == 0)
                other.option(opt_key, true)->setInt(extruder);
        }
    }
}

void
PrintObjectConfig::apply(const ConfigBase &other, bool ignore_nonexistent) {
    DynamicPrintConfig other_clone;
    other_clone.apply(other);
    
    this->prepare_extruder_option("support_material_extruder", other_clone);
    this->prepare_extruder_option("support_material_interface_extruder", other_clone);
    
    StaticConfig::apply(other_clone, ignore_nonexistent);
};

void
PrintRegionConfig::apply(const ConfigBase &other, bool ignore_nonexistent) {
    DynamicPrintConfig other_clone;
    other_clone.apply(other);
    
    this->prepare_extruder_option("infill_extruder", other_clone);
    this->prepare_extruder_option("perimeter_extruder", other_clone);
    
    StaticConfig::apply(other_clone, ignore_nonexistent);
};

}
