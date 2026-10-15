#include "PluginHostBindings.hpp"
#include "PluginHostUi.hpp"
#include "slic3r/plugin/PluginBindingUtils.hpp"

#include <pybind11/pytypes.h>
#include <pybind11/cast.h>
#include <pybind11/pybind11.h>

#include <cstddef>
#include <libslic3r/AppConfig.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/PresetBundle.hpp>

#include <memory>

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/MainFrame.hpp>
#include <slic3r/GUI/Plater.hpp>
#include <slic3r/GUI/PresetComboBoxes.hpp> // TabPresetComboBox::set_filament_idx
#include <slic3r/GUI/Tab.hpp>

#include <pybind11/stl.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

namespace Slic3r {
namespace {

py::list current_filament_presets(PresetBundle& bundle)
{
    py::list presets;
    for (const std::string& preset_name : bundle.filament_presets) {
        Preset* preset = bundle.filaments.find_preset(preset_name);
        if (preset == nullptr)
            presets.append(py::none());
        else
            presets.append(py::cast(preset, py::return_value_policy::reference));
    }
    return presets;
}

PresetCollection& printer_presets(PresetBundle& bundle)
{
    return static_cast<PresetCollection&>(bundle.printers);
}

// Handle to one category of currently edited preset. It is a category, not a snapshot: it always
// resolves the preset the matching tab edits now, so it stays valid across preset switches.
struct PresetEditor
{
    Preset::Type type = Preset::TYPE_INVALID;
};

const char* preset_tab_name(Preset::Type type)
{
    switch (type) {
    case Preset::TYPE_PRINT:    return "process";
    case Preset::TYPE_FILAMENT: return "filament";
    case Preset::TYPE_PRINTER:  return "printer";
    default:                    return "preset";
    }
}

void update_edited_preset(Preset::Type type, const std::string& key, const std::string& value)
{
    PluginHostUi::run_on_ui_thread([&] {
        GUI::GUI_App& app = GUI::wxGetApp();
        if (app.is_closing())
            throw std::runtime_error("OrcaSlicer is closing");
        if (app.preset_bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        if (app.plater() == nullptr)
            throw std::runtime_error("OrcaSlicer main window is not available");

        GUI::Tab* tab = app.get_tab(type);
        if (tab == nullptr)
            throw std::runtime_error(std::string("The ") + preset_tab_name(type) + " preset tab is not available");
        // The tab for this preset type exists, but the selected printer's technology may not use it
        // (the print tab no-ops under SLA); refuse instead of writing a value the UI cannot apply.
        if (!tab->supports_printer_technology(app.preset_bundle->printers.get_selected_preset().printer_technology()))
            throw std::runtime_error(std::string("The ") + preset_tab_name(type)
                                     + " preset is not available for the selected printer technology");

        tab->apply_preset_config_value(key, value);
    });
}

// Slot-addressed filament update. `slot_number` is 1-based, matching the GUI's filament slots and
// the values PresetBundle::filament_presets holds. The slot's assigned preset is resolved to its
// internal name; when that is not the preset the filament tab edits, the tab is switched through
// Tab::select_preset() - the normal UI preselection flow, so a dirty edited preset still prompts
// the user and a declined prompt cancels the call with a clear error and no change.
void update_filament_slot(int slot_number, const std::string& key, const std::string& value)
{
    PluginHostUi::run_on_ui_thread([&] {
        GUI::GUI_App& app = GUI::wxGetApp();
        if (app.is_closing())
            throw std::runtime_error("OrcaSlicer is closing");
        if (app.preset_bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        if (app.plater() == nullptr)
            throw std::runtime_error("OrcaSlicer main window is not available");

        PresetBundle& bundle = *app.preset_bundle;
        if (slot_number < 1 || static_cast<size_t>(slot_number) > bundle.filament_presets.size())
            throw std::out_of_range("Filament slot " + std::to_string(slot_number) + " is out of range; slot numbers are 1-based and this printer has "
                                    + std::to_string(bundle.filament_presets.size()) + " filament slot(s)");

        const std::string& assigned_name = bundle.filament_presets[static_cast<size_t>(slot_number) - 1];
        if (assigned_name.empty())
            throw std::runtime_error("Filament slot " + std::to_string(slot_number) + " has no assigned filament preset");

        // Resolve the assigned name the way the app does, so a renamed/aliased entry maps to the
        // internal preset it now identifies. Copy the name out before selecting anything: the
        // collection can be reallocated while switching.
        Preset* slot_preset = bundle.filaments.find_preset(assigned_name);
        if (slot_preset == nullptr)
            throw std::runtime_error("Filament slot " + std::to_string(slot_number) + " is assigned \"" + assigned_name
                                     + "\", which does not resolve to a filament preset");
        const std::string target_name = slot_preset->name;

        GUI::Tab* tab = app.get_tab(Preset::TYPE_FILAMENT);
        if (tab == nullptr)
            throw std::runtime_error("The filament preset tab is not available");
        if (!tab->supports_printer_technology(bundle.printers.get_selected_preset().printer_technology()))
            throw std::runtime_error("The filament preset is not available for the selected printer technology");

        // Switch to the slot's preset through the normal preset selection, never a forced one, so the
        // user's unsaved-changes prompt is honoured. A declined prompt returns false here.
        if (bundle.filaments.get_edited_preset().name != target_name) {
            if (!tab->select_preset(target_name))
                throw std::runtime_error("Switching the edited filament preset to \"" + target_name + "\" for filament slot "
                                         + std::to_string(slot_number) + " was canceled; the setting was not changed");
        }

        // select_preset() can still leave a different preset edited (e.g. the target disappeared), so
        // re-check before writing anything.
        if (bundle.filaments.get_edited_preset().name != target_name)
            throw std::runtime_error("The edited filament preset is \"" + bundle.filaments.get_edited_preset().name
                                     + "\", not \"" + target_name + "\"; the setting was not changed");

        // Match PlaterPresetComboBox::switch_to_tab(): record which 0-based filament slot this tab
        // combobox now edits, so the GUI shows the slot the value was written for.
        if (GUI::TabPresetComboBox* combo = tab->get_combo_box())
            combo->set_filament_idx(slot_number - 1);

        tab->apply_preset_config_value(key, value);
    });
}

void reload_local_bundle(const std::string& bundle_id)
{
    bool        reloaded = false;
    std::string error;
    PluginHostUi::run_on_ui_thread([&] {
        GUI::GUI_App& app = GUI::wxGetApp();
        if (app.is_closing())
            throw std::runtime_error("OrcaSlicer is closing");
        if (app.preset_bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        if (app.app_config == nullptr)
            throw std::runtime_error("Application configuration is not available");
        if (app.mainframe == nullptr)
            throw std::runtime_error("OrcaSlicer main window is not available");

        reloaded = app.preset_bundle->reload_local_bundle(app.app_config->get("preset_folder"), bundle_id, &error);
        if (!reloaded)
            return;

        app.mainframe->update_side_preset_ui();
        for (GUI::Tab* tab : app.tabs_list) {
            tab->reload_config();
            tab->update_changed_ui();
        }
        if (GUI::Plater* plater = app.plater())
            plater->sidebar().update_all_preset_comboboxes();
    });
    if (!reloaded)
        throw std::runtime_error(error.empty() ? "Local bundle was not reloaded" : error);
}

std::vector<std::string> list_bundle_ids()
{
    std::vector<std::string> ids;
    PluginHostUi::run_on_ui_thread([&] {
        GUI::GUI_App& app = GUI::wxGetApp();
        if (app.is_closing())
            throw std::runtime_error("OrcaSlicer is closing");
        if (app.preset_bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        if (app.app_config == nullptr)
            throw std::runtime_error("Application configuration is not available");

        ids = app.preset_bundle->list_local_bundle_ids(app.app_config->get("preset_folder"));
    });
    return ids;
}

} // namespace

void host_bindings::register_presets(py::module_& host)
{
    auto preset = host.def_submodule("preset", "Preset operations");
    auto bundles = preset.def_submodule("bundles", "Local preset bundle operations");
    bundles.def("reload_local", &reload_local_bundle, py::arg("bundle_id"),
                "Reload one local preset bundle from the active user's _local directory.");
    bundles.def("list_ids", &list_bundle_ids,
                "IDs of local preset bundles in the active user's _local directory that reload_local() accepts, "
                "including bundles added after startup.");

    py::enum_<Preset::Type>(host, "PresetType")
        .value("Invalid", Preset::TYPE_INVALID)
        .value("Print", Preset::TYPE_PRINT)
        .value("SlaPrint", Preset::TYPE_SLA_PRINT)
        .value("Filament", Preset::TYPE_FILAMENT)
        .value("SlaMaterial", Preset::TYPE_SLA_MATERIAL)
        .value("Printer", Preset::TYPE_PRINTER)
        .value("PhysicalPrinter", Preset::TYPE_PHYSICAL_PRINTER)
        .value("Plate", Preset::TYPE_PLATE)
        .value("Model", Preset::TYPE_MODEL);

    py::class_<PresetEditor>(preset, "PresetEditor",
                             "Handle to a category of currently edited preset. It is not a snapshot: it always "
                             "resolves the preset the matching tab edits now, so it stays usable across preset "
                             "switches. update() edits the preset in place; the change is not saved to disk.")
        .def_readonly("type", &PresetEditor::type)
        .def(
            "update",
            [](const PresetEditor& editor, const std::string& key, const std::string& value) {
                update_edited_preset(editor.type, key, value);
            },
            py::arg("key"), py::arg("value"),
            "Set one setting of the currently edited preset and run the same committed-change path as "
            "editing the setting in the GUI; the edited preset becomes dirty until saved/exported. "
            "key must be a setting of this preset category and value its serialized string form, "
            "otherwise ValueError is raised and nothing is written. A setting edited through a field "
            "runs that field's validation even before its page has been opened; one the GUI edits "
            "outside a field (such as compatible printers) also recomputes preset compatibility.")
        .def(
            "update",
            [](const PresetEditor& editor, int slot_number, const std::string& key, const std::string& value) {
                if (editor.type != Preset::TYPE_FILAMENT)
                    throw std::invalid_argument("The slot_number form of update() is only available on "
                                                "orca.host.preset.filament; call update(key, value) on this editor");
                update_filament_slot(slot_number, key, value);
            },
            py::arg("slot_number"), py::arg("key"), py::arg("value"),
            "Set one setting of the filament preset assigned to a 1-based filament slot. When that preset "
            "is not the one currently edited the call switches to it through the normal preset-selection "
            "flow, so an unsaved edited preset still prompts the user and a declined prompt cancels the "
            "call with an error and no change. An out-of-range, empty or unresolved slot is rejected with "
            "a clear error. Other preset editors accept only update(key, value); this form raises "
            "ValueError on them.");

    // The three preset categories edited by the main tabs. `process` is the print/process preset,
    // `printer` the machine preset and `filament` the currently edited filament preset (its
    // 3-argument form addresses a 1-based filament slot); each only accepts its own settings, so a
    // printer setting passed to `process` is rejected instead of written silently.
    preset.attr("process")  = py::cast(PresetEditor{Preset::TYPE_PRINT});
    preset.attr("printer")  = py::cast(PresetEditor{Preset::TYPE_PRINTER});
    preset.attr("filament") = py::cast(PresetEditor{Preset::TYPE_FILAMENT});

    py::class_<Preset, std::unique_ptr<Preset, py::nodelete>>(host, "Preset")
        .def_readonly("type", &Preset::type)
        .def_readonly("name", &Preset::name)
        .def_readonly("alias", &Preset::alias)
        .def_readonly("file", &Preset::file)
        .def_readonly("is_default", &Preset::is_default)
        .def_readonly("is_external", &Preset::is_external)
        .def_readonly("is_system", &Preset::is_system)
        .def_readonly("is_visible", &Preset::is_visible)
        .def_readonly("is_dirty", &Preset::is_dirty)
        .def_readonly("is_compatible", &Preset::is_compatible)
        .def_readonly("is_project_embedded", &Preset::is_project_embedded)
        .def_readonly("bundle_id", &Preset::bundle_id)
        .def("is_user", &Preset::is_user)
        .def("is_from_bundle", &Preset::is_from_bundle)
        .def("label", &Preset::label, py::arg("no_alias") = false)
        .def("config_keys", [](const Preset& preset) { return preset.config.keys(); })
        .def("config_value", [](const Preset& preset, const std::string& key) {
            return config_value_or_none(preset.config, key);
        });

    py::class_<PresetCollection, std::unique_ptr<PresetCollection, py::nodelete>>(host, "PresetCollection")
        .def("size", &PresetCollection::size)
        .def("get_selected_preset", [](PresetCollection& collection) -> Preset& {
            return collection.get_selected_preset();
        }, py::return_value_policy::reference_internal)
        .def("selected_preset", [](PresetCollection& collection) -> Preset& {
            return collection.get_selected_preset();
        }, py::return_value_policy::reference_internal)
        .def("get_selected_preset_name", &PresetCollection::get_selected_preset_name)
        .def("selected_preset_name", &PresetCollection::get_selected_preset_name)
        .def("get_edited_preset", [](PresetCollection& collection) -> Preset& {
            return collection.get_edited_preset();
        }, py::return_value_policy::reference_internal)
        .def("edited_preset", [](PresetCollection& collection) -> Preset& {
            return collection.get_edited_preset();
        }, py::return_value_policy::reference_internal)
        .def("preset", [](PresetCollection& collection, size_t index) -> Preset& {
            if (index >= collection.size())
                throw py::index_error("preset index out of range");
            return collection.preset(index);
        }, py::return_value_policy::reference_internal)
        .def("find_preset", [](PresetCollection& collection, const std::string& name) -> Preset* {
            return collection.find_preset(name);
        }, py::return_value_policy::reference_internal)
        .def("preset_names", [](const PresetCollection& collection) {
            std::vector<std::string> names;
            names.reserve(collection.get_presets().size());
            for (const Preset& preset : collection.get_presets())
                names.push_back(preset.name);
            return names;
        });

    py::class_<PresetBundle, std::unique_ptr<PresetBundle, py::nodelete>>(host, "PresetBundle")
        .def_property_readonly("prints", [](PresetBundle& bundle) -> PresetCollection& {
            return bundle.prints;
        }, py::return_value_policy::reference_internal)
        .def_property_readonly("printers", &printer_presets, py::return_value_policy::reference_internal)
        .def_property_readonly("filaments", [](PresetBundle& bundle) -> PresetCollection& {
            return bundle.filaments;
        }, py::return_value_policy::reference_internal)
        .def_property_readonly("sla_prints", [](PresetBundle& bundle) -> PresetCollection& {
            return bundle.sla_prints;
        }, py::return_value_policy::reference_internal)
        .def_property_readonly("sla_materials", [](PresetBundle& bundle) -> PresetCollection& {
            return bundle.sla_materials;
        }, py::return_value_policy::reference_internal)
        .def("current_process_preset", [](PresetBundle& bundle) -> Preset& {
            return bundle.prints.get_edited_preset();
        }, py::return_value_policy::reference_internal)
        .def("current_print_preset", [](PresetBundle& bundle) -> Preset& {
            return bundle.prints.get_edited_preset();
        }, py::return_value_policy::reference_internal)
        .def("current_printer_preset", [](PresetBundle& bundle) -> Preset& {
            return bundle.printers.get_edited_preset();
        }, py::return_value_policy::reference_internal)
        .def("current_filament_preset_names", [](PresetBundle& bundle) {
            return bundle.filament_presets;
        })
        .def("current_filament_presets", &current_filament_presets)
        .def("full_config_keys", [](const PresetBundle& bundle) {
            return bundle.full_config().keys();
        })
        .def("full_config_value", [](const PresetBundle& bundle, const std::string& key) {
            return config_value_or_none(bundle.full_config(), key);
        });
}

} // namespace Slic3r
