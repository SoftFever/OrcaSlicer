#ifndef slic3r_GUI_JusPrinPresetConfigUtils_hpp_
#define slic3r_GUI_JusPrinPresetConfigUtils_hpp_

#include <nlohmann/json.hpp>
#include "libslic3r/Preset.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Orient.hpp"

namespace Slic3r { namespace GUI {

class JusPrinPresetConfigUtils {
public:
    static nlohmann::json PresetsToJson(const std::vector<std::pair<const Preset*, bool>>& presets);
    static nlohmann::json GetPresetsJson(Preset::Type type);
    static nlohmann::json PresetToJson(const Preset* preset);
    static nlohmann::json GetAllPresetJson();
    static nlohmann::json GetAllEditedPresetJson();
    static nlohmann::json GetPlaterConfigJson();
    static nlohmann::json GetModelObjectFeaturesJson(const ModelObject* obj);
    static nlohmann::json CostItemsToJson(const Slic3r::orientation::CostItems& cost_items);
    static void DiscardCurrentPresetChanges();
    static void UpdatePresetTabs();
    static void ApplyConfig(const nlohmann::json& item);
    static nlohmann::json GetEditedPresetJson(Preset::Type type);
};

}} // namespace Slic3r::GUI

#endif