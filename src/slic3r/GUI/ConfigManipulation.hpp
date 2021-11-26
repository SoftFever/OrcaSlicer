#ifndef slic3r_ConfigManipulation_hpp_
#define slic3r_ConfigManipulation_hpp_

/*	 Class for validation config options
 *	 and update (enable/disable) IU components
 *	 
 *	 Used for config validation for global config (Print Settings Tab)
 *	 and local config (overrides options on sidebar)
 * */

#include "libslic3r/PrintConfig.hpp"
#include "Field.hpp"

namespace Slic3r {

class ModelConfig;

namespace GUI {

class ConfigManipulation
{
    bool                is_msg_dlg_already_exist{ false };

    bool                m_is_initialized_support_material_overhangs_queried{ false };
    bool                m_support_material_overhangs_queried{ false };

    // function to loading of changed configuration 
    std::function<void()>                                       load_config = nullptr;
    std::function<void (const std::string&, bool toggle, int opt_index)>   cb_toggle_field = nullptr;
    // callback to propagation of changed value, if needed 
    std::function<void(const std::string&, const boost::any&)>  cb_value_change = nullptr;
    ModelConfig* local_config = nullptr;

public:
    ConfigManipulation(std::function<void()> load_config,
        std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_field,
        std::function<void(const std::string&, const boost::any&)>  cb_value_change,
        ModelConfig* local_config = nullptr) :
        load_config(load_config),
        cb_toggle_field(cb_toggle_field),
        cb_value_change(cb_value_change),
        local_config(local_config) {}
    ConfigManipulation() {}

    ~ConfigManipulation() {
        load_config = nullptr;
        cb_toggle_field = nullptr;
        cb_value_change = nullptr;
    }

    void    apply(DynamicPrintConfig* config, DynamicPrintConfig* new_config);
    void    toggle_field(const std::string& field_key, const bool toggle, int opt_index = -1);

    // FFF print
    void    update_print_fff_config(DynamicPrintConfig* config, const bool is_global_config = false);
    void    toggle_print_fff_options(DynamicPrintConfig* config);

    // SLA print
    void    update_print_sla_config(DynamicPrintConfig* config, const bool is_global_config = false);
    void    toggle_print_sla_options(DynamicPrintConfig* config);

    bool    is_initialized_support_material_overhangs_queried() { return m_is_initialized_support_material_overhangs_queried; }
    void    initialize_support_material_overhangs_queried(bool queried)
    {
        m_is_initialized_support_material_overhangs_queried = true;
        m_support_material_overhangs_queried = queried;
    }
};

} // GUI
} // Slic3r

#endif /* slic3r_ConfigManipulation_hpp_ */
