#ifndef slic3r_GUI_JusPrinPlateUtils_hpp_
#define slic3r_GUI_JusPrinPlateUtils_hpp_

#include <nlohmann/json.hpp>
#include <GL/glew.h>

#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Orient.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

namespace Slic3r { namespace GUI {

class JusPrinPlateUtils {
public:
    static nlohmann::json RenderPlateView(const nlohmann::json& params);
    static nlohmann::json GetPlates(const nlohmann::json& params);
    static nlohmann::json GetAllModelObjectsJson();
    static nlohmann::json GetProjectInfo(const nlohmann::json& params);
private:
    static void RenderThumbnail(ThumbnailData& thumbnail_data,
        const Vec3d& camera_position, const Vec3d& target);
    static nlohmann::json GetModelObjectFeaturesJson(const ModelObject* obj);
    static nlohmann::json CostItemsToJson(const Slic3r::orientation::CostItems& cost_items);
};

}} // namespace Slic3r::GUI

#endif
