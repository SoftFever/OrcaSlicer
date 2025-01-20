#include "JusPrinPresetConfigUtils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"


namespace Slic3r { namespace GUI {

nlohmann::json JusPrinPresetConfigUtils::PresetsToJson(const std::vector<std::pair<const Preset*, bool>>& presets)
{
    nlohmann::json j_array = nlohmann::json::array();
    for (const auto& [preset, is_selected] : presets) {
        j_array.push_back(PresetToJson(preset));
    }
    return j_array;
}

nlohmann::json JusPrinPresetConfigUtils::PresetToJson(const Preset* preset)
{
    nlohmann::json j;
    j["name"] = preset->name;
    j["is_default"] = preset->is_default;
    j["config"] = preset->config.to_json(preset->name, "", preset->version.to_string(), preset->custom_defined);
    return j;
}

nlohmann::json JusPrinPresetConfigUtils::GetPresetsJson(Preset::Type type) {
    Tab* tab = wxGetApp().get_tab(type);
    if (!tab) {
        return nlohmann::json::array();
    }

    TabPresetComboBox* combo = tab->get_combo_box();
    std::vector<std::pair<const Preset*, bool>> presets;

    for (unsigned int i = 0; i < combo->GetCount(); i++) {
        std::string preset_name = combo->GetString(i).ToUTF8().data();

        if (preset_name.substr(0, 5) == "-----") continue;   // Skip separator

        // Orca Slicer adds "* " to the preset name to indicate that it has been modified
        if (preset_name.substr(0, 2) == "* ") {
            preset_name = preset_name.substr(2);
        }

        const Preset* preset = tab->m_presets->find_preset(preset_name, false);
        if (preset) {
            presets.push_back({preset, combo->GetSelection() == i});
        }
    }

    return PresetsToJson(presets);
}

nlohmann::json JusPrinPresetConfigUtils::GetEditedPresetJson(Preset::Type type) {
    Tab* tab = wxGetApp().get_tab(type);
    if (!tab) {
        return nlohmann::json::array();
    }
    PresetCollection* presets = tab->get_presets();
    if (!presets) {
        return nlohmann::json::array();
    }

    nlohmann::json j = PresetToJson(&presets->get_edited_preset());

    const bool deep_compare = (type == Preset::TYPE_PRINTER || type == Preset::TYPE_SLA_MATERIAL);
    j["dirty_options"] = presets->current_dirty_options(deep_compare);

    return j;
}


nlohmann::json JusPrinPresetConfigUtils::GetAllPresetJson() {
    nlohmann::json printerPresetsJson = GetPresetsJson(Preset::Type::TYPE_PRINTER);
    nlohmann::json filamentPresetsJson = GetPresetsJson(Preset::Type::TYPE_FILAMENT);
    nlohmann::json printPresetsJson = GetPresetsJson(Preset::Type::TYPE_PRINT);

    return {
        {"printerPresets", printerPresetsJson},
        {"filamentPresets", filamentPresetsJson},
        {"printProcessPresets", printPresetsJson}
    };
}

nlohmann::json JusPrinPresetConfigUtils::GetAllEditedPresetJson() {
    nlohmann::json editedPrinterPresetJson = GetEditedPresetJson(Preset::Type::TYPE_PRINTER);
    nlohmann::json editedFilamentPresetJson = GetEditedPresetJson(Preset::Type::TYPE_FILAMENT);
    nlohmann::json editedPrintProcessPresetJson = GetEditedPresetJson(Preset::Type::TYPE_PRINT);

    return {
        {"editedPrinterPreset", editedPrinterPresetJson},
        {"editedFilamentPreset", editedFilamentPresetJson},
        {"editedPrintProcessPreset", editedPrintProcessPresetJson}
    };
}

nlohmann::json JusPrinPresetConfigUtils::CostItemsToJson(const Slic3r::orientation::CostItems& cost_items) {
    nlohmann::json j;
    j["overhang"] = cost_items.overhang;
    j["bottom"] = cost_items.bottom;
    j["bottom_hull"] = cost_items.bottom_hull;
    j["contour"] = cost_items.contour;
    j["area_laf"] = cost_items.area_laf;
    j["area_projected"] = cost_items.area_projected;
    j["volume"] = cost_items.volume;
    j["area_total"] = cost_items.area_total;
    j["radius"] = cost_items.radius;
    j["height_to_bottom_hull_ratio"] = cost_items.height_to_bottom_hull_ratio;
    j["unprintability"] = cost_items.unprintability;
    return j;
}

nlohmann::json JusPrinPresetConfigUtils::GetModelObjectFeaturesJson(const ModelObject* obj) {
    if (!obj || obj->instances.size() != 1) {
        BOOST_LOG_TRIVIAL(error) << "GetModelObjectFeaturesJson: Not sure why there will be more than one instance of a model object. Skipping for now.";
        return nlohmann::json::object();
    }

    Slic3r::orientation::OrientMesh om = OrientJob::get_orient_mesh(obj->instances[0]);
    Slic3r::orientation::OrientParams params;
    params.min_volume = false;

    Slic3r::orientation::AutoOrienterDelegate orienter(&om, params, {}, {});
    Slic3r::orientation::CostItems features = orienter.get_features(om.orientation.cast<float>(), true);
    return CostItemsToJson(features);
}

nlohmann::json JusPrinPresetConfigUtils::GetPlaterConfigJson()
{
    nlohmann::json j = nlohmann::json::object();
    Plater* plater = wxGetApp().plater();

    j["plateCount"] = plater->get_partplate_list().get_plate_list().size();
    j["modelObjects"] = nlohmann::json::array();

    for (const ModelObject* object : plater->model().objects) {
        auto object_grid_config = &(object->config);

        nlohmann::json obj;
        obj["id"] = std::to_string(object->id().id);
        obj["name"] = object->name;
        obj["features"] = GetModelObjectFeaturesJson(object);

        int extruder_id = -1;  // Default extruder ID
        auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(object_grid_config->option("extruder"));
        if (extruder_id_ptr) {
            extruder_id = *extruder_id_ptr;
        }
        obj["extruderId"] = extruder_id;

        j["modelObjects"].push_back(obj);
    }

    return j;
}

void JusPrinPresetConfigUtils::DiscardCurrentPresetChanges() {
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle) {
        return;
    }
    bundle->printers.discard_current_changes();
    bundle->filaments.discard_current_changes();
    bundle->prints.discard_current_changes();
}

void JusPrinPresetConfigUtils::UpdatePresetTabs() {
    std::array<Preset::Type, 2> preset_types = {Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT};

    for (const auto& preset_type : preset_types) {
        if (Tab* tab = wxGetApp().get_tab(preset_type)) {
            tab->reload_config();
            tab->update();
            tab->update_dirty();
        }
    }
}

void JusPrinPresetConfigUtils::ApplyConfig(const nlohmann::json& item) {
    std::string type = item.value("type", "");
    Preset::Type preset_type;
    if (type == "print") {
        preset_type = Preset::Type::TYPE_PRINT;
    } else if (type == "filament") {
        preset_type = Preset::Type::TYPE_FILAMENT;
    } else {
        BOOST_LOG_TRIVIAL(error) << "ApplyConfig: invalid type parameter";
        return;
    }

    Tab* tab = wxGetApp().get_tab(preset_type);
    if (tab != nullptr) {
        try {
            DynamicPrintConfig* config = tab->get_config();
            if (!config) return;

            ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Enable);
            config->set_deserialize(item.value("key", ""), item["value"], context);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "ApplyConfig: error applying config " << e.what();
        }
    }
}

}} // namespace Slic3r::GUI
