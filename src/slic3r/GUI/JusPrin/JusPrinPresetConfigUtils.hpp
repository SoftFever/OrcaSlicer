#ifndef slic3r_GUI_JusPrinPresetConfigUtils_hpp_
#define slic3r_GUI_JusPrinPresetConfigUtils_hpp_

#include <nlohmann/json.hpp>
#include "libslic3r/Preset.hpp"

namespace Slic3r { namespace GUI {

class JusPrinPresetConfigUtils {
public:
    static nlohmann::json PresetToJson(const Preset* preset, bool is_selected);
    static nlohmann::json PresetsToJson(const std::vector<std::pair<const Preset*, bool>>& presets);
    static nlohmann::json GetPresetsJson(Preset::Type type);
    static nlohmann::json GetAllPresetJson();
    static nlohmann::json GetAllEditedPresetJson();
    static nlohmann::json GetEditedPresetJson(Preset::Type type);
    static void DiscardCurrentPresetChanges();
    static void UpdatePresetTabs();
    static void ApplyConfig(const nlohmann::json& item);
};

}} // namespace Slic3r::GUI

#endif