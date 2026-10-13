#include "FlowTypeHelper.hpp"

#include "GUI_App.hpp"
#include "Plater.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/algorithm/string.hpp>

#include <algorithm>

namespace Slic3r { namespace GUI { namespace FlowType {

static size_t nozzle_count()
{
    const auto *diameters = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    return diameters != nullptr && !diameters->values.empty() ? diameters->values.size() : 1;
}

static void notify_plater()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    plater->update_project_dirty_from_presets();
    plater->on_config_change(wxGetApp().preset_bundle->full_config());
}

bool printer_supports_high_flow()
{
    const auto *support = wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionStrings>("printer_flow_support");
    return support != nullptr &&
           std::find(support->values.begin(), support->values.end(), FLOW_MODE_HIGH_FLOW) != support->values.end();
}

bool any_filament_supports_high_flow()
{
    const PresetBundle &bundle = *wxGetApp().preset_bundle;
    for (const std::string &name : bundle.filament_presets) {
        const Preset *preset = bundle.filaments.find_preset(name, false);
        if (preset == nullptr)
            continue;
        const auto *support = preset->config.option<ConfigOptionStrings>("filament_flow_support");
        if (support != nullptr &&
            std::find(support->values.begin(), support->values.end(), FLOW_MODE_HIGH_FLOW) != support->values.end())
            return true;
    }
    return false;
}

std::vector<std::string> nozzle_volume_types()
{
    std::vector<std::string> types;
    if (const auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")) {
        types.reserve(opt->values.size());
        for (int v : opt->values)
            types.push_back(to_string(FilamentVolumeType(v)));
    }
    types.resize(nozzle_count(), FLOW_MODE_STANDARD);
    const bool supported = printer_supports_high_flow();
    for (std::string &t : types)
        if (!supported || t != FLOW_MODE_HIGH_FLOW)
            t = FLOW_MODE_STANDARD;
    return types;
}

size_t distinct_nozzle_flow_type_count()
{
    std::vector<std::string> types = nozzle_volume_types();
    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());
    return types.empty() ? 1 : types.size();
}

bool any_nozzle_high_flow()
{
    const std::vector<std::string> types = nozzle_volume_types();
    return std::find(types.begin(), types.end(), FLOW_MODE_HIGH_FLOW) != types.end();
}

// App-level memory of the per-nozzle flow selection, stored a
// "nozzle_volume_types" section keyed by printer preset name, CSV of
// "Standard"/"High Flow" per nozzle. The project config itself is volatile
// (reset on restart), hence this mirror.
static const char *APP_CONFIG_NOZZLE_FLOW_SECTION = "nozzle_volume_types";
static const char *FLOW_DISPLAY_HIGH_FLOW         = "High Flow";
static const char *FLOW_DISPLAY_STANDARD          = "Standard";

static void save_nozzle_volume_types_to_app_config()
{
    if (wxGetApp().app_config == nullptr)
        return;
    std::vector<std::string> display;
    for (const std::string &t : nozzle_volume_types())
        display.push_back(t == FLOW_MODE_HIGH_FLOW ? FLOW_DISPLAY_HIGH_FLOW : FLOW_DISPLAY_STANDARD);
    wxGetApp().app_config->set(APP_CONFIG_NOZZLE_FLOW_SECTION,
                               wxGetApp().preset_bundle->printers.get_selected_preset_name(),
                               boost::algorithm::join(display, ","));
}

void set_nozzle_volume_type(size_t nozzle_idx, const std::string &volume_type)
{
    std::vector<std::string> types = nozzle_volume_types();
    if (nozzle_idx >= types.size())
        return;
    types[nozzle_idx] = volume_type == FLOW_MODE_HIGH_FLOW ? FLOW_MODE_HIGH_FLOW : FLOW_MODE_STANDARD;
    auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true);
    opt->values.clear();
    for (const std::string &t : types)
        opt->values.push_back(filament_volume_type_from_string(t));
    save_nozzle_volume_types_to_app_config();
    if (Plater *plater = wxGetApp().plater())
        plater->get_partplate_list().set_filament_group_dirty(true);
    notify_plater();
}

void set_nozzle_volume_types(const std::vector<std::string> &volume_types)
{
    const size_t count = nozzle_count();
    if (volume_types.size() != count)
        return;

    auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true);
    opt->values.clear();
    opt->values.reserve(volume_types.size());
    for (const std::string &type : volume_types)
        opt->values.push_back(type == FLOW_MODE_HIGH_FLOW ? fvtHighFlow : fvtStandard);
    save_nozzle_volume_types_to_app_config();
    notify_plater();
}

void restore_nozzle_volume_types_from_app_config()
{
    if (wxGetApp().app_config == nullptr)
        return;
    const std::string stored = wxGetApp().app_config->get(
        APP_CONFIG_NOZZLE_FLOW_SECTION, wxGetApp().preset_bundle->printers.get_selected_preset_name());
    if (stored.empty())
        return; // no memory for this printer yet, keep whatever the project config holds

    std::vector<std::string> tokens;
    boost::split(tokens, stored, boost::is_any_of(","));
    // Normalize like nozzle_volume_types(): unknown values / unsupported printers -> standard.
    const bool supported = printer_supports_high_flow();
    std::vector<std::string> types;
    for (std::string &t : tokens) {
        boost::algorithm::trim(t);
        types.push_back(supported && boost::iequals(t, FLOW_DISPLAY_HIGH_FLOW) ? FLOW_MODE_HIGH_FLOW : FLOW_MODE_STANDARD);
    }
    types.resize(nozzle_count(), FLOW_MODE_STANDARD);

    // Write directly without notify_plater(); callers rebuild the nozzle UI right after.
    auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true);
    opt->values.clear();
    opt->values.reserve(types.size());
    for (const std::string &t : types)
        opt->values.push_back(t == FLOW_MODE_HIGH_FLOW ? fvtHighFlow : fvtStandard);
}

void reset_nozzle_volume_types_to_standard()
{
    auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true);
    opt->values.assign(nozzle_count(), fvtStandard);
    notify_plater();
}

std::string grouping_mode()
{
    const auto *opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionString>("filament_grouping_mode");
    return opt != nullptr && opt->value == FILAMENT_GROUPING_CUSTOM ? FILAMENT_GROUPING_CUSTOM : FILAMENT_GROUPING_STANDARD;
}

void set_grouping_mode(const std::string &mode)
{
    wxGetApp().preset_bundle->project_config.option<ConfigOptionString>("filament_grouping_mode", true)->value =
        mode == FILAMENT_GROUPING_CUSTOM ? FILAMENT_GROUPING_CUSTOM : FILAMENT_GROUPING_STANDARD;
    notify_plater();
}

void apply_custom_mapping(const std::vector<FilamentVolumeType> &mapping)
{
    wxGetApp().preset_bundle->set_filament_volume_types(mapping);
    notify_plater();
}

void sync_filament_volume_types_for_slice()
{
    // Flow type every filament should use when the custom per-filament mapping does
    // not apply: follow the single nozzle type when the nozzles are not mixing types
    // (all standard -> standard, all high flow -> high flow); in standard mode with
    // mixed nozzles, fall back to standard.
    FilamentVolumeType type = fvtStandard;
    if (distinct_nozzle_flow_type_count() < 2) {
        const std::vector<std::string> nozzles = nozzle_volume_types();
        if (!nozzles.empty() && nozzles.front() == FLOW_MODE_HIGH_FLOW)
            type = fvtHighFlow;
    }
    const std::vector<FilamentVolumeType> current = wxGetApp().preset_bundle->get_filament_volume_types();
    if (std::all_of(current.begin(), current.end(), [type](FilamentVolumeType t) { return t == type; }))
        return; // already uniform at the target type
    const size_t n = wxGetApp().preset_bundle->filament_presets.size();
    apply_custom_mapping(std::vector<FilamentVolumeType>(std::max<size_t>(n, size_t(1)), type));
}

}}} // namespace Slic3r::GUI::FlowType
