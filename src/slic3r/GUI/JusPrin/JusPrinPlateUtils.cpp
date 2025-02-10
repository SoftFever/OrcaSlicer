#include "JusPrinPlateUtils.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include <boost/beast/core/detail/base64.hpp>

namespace Slic3r { namespace GUI {

namespace {
    nlohmann::json bbox_to_json(const BoundingBoxf3& bbox) {
        return {
            {"min", {
                {"x", bbox.min.x()},
                {"y", bbox.min.y()},
                {"z", bbox.min.z()}
            }},
            {"max", {
                {"x", bbox.max.x()},
                {"y", bbox.max.y()},
                {"z", bbox.max.z()}
            }}
        };
    }
}

static void z_debug_output_thumbnail(const ThumbnailData& thumbnail_data, std::string file_name)
{
    // debug export of generated image
    wxImage image(thumbnail_data.width, thumbnail_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < thumbnail_data.height; ++r)
    {
        unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
        for (unsigned int c = 0; c < thumbnail_data.width; ++c)
        {
            unsigned char* px = (unsigned char*)thumbnail_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    std::string file_name_path = "/Users/kenneth/Desktop/" + file_name + ".png";
    image.SaveFile(file_name_path, wxBITMAP_TYPE_PNG);
}

static std::string encode_thumbnail_to_base64(const ThumbnailData& thumbnail_data, bool use_png = true) {
    // Create wxImage from thumbnail data
    wxImage image(thumbnail_data.width, thumbnail_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < thumbnail_data.height; ++r) {
        unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
        for (unsigned int c = 0; c < thumbnail_data.width; ++c) {
            unsigned char* px = (unsigned char*)thumbnail_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    // Convert wxImage to memory stream
    wxMemoryOutputStream stream;
    if (use_png) {
        image.SaveFile(stream, wxBITMAP_TYPE_PNG);
    } else {
        image.SaveFile(stream, wxBITMAP_TYPE_JPEG);
    }

    // Get the binary data
    const size_t data_size = stream.GetSize();
    std::vector<unsigned char> buffer(data_size);
    stream.CopyTo(buffer.data(), data_size);

    // Convert to base64
    std::string base64_data;
    base64_data.resize(boost::beast::detail::base64::encoded_size(data_size));
    boost::beast::detail::base64::encode(base64_data.data(), buffer.data(), data_size);

    // Add appropriate data URI prefix
    std::string data_uri = "data:image/";
    data_uri += (use_png ? "png" : "jpeg");
    data_uri += ";base64,";
    data_uri += base64_data;

    return data_uri;
}

nlohmann::json JusPrinPlateUtils::RenderPlateView(const nlohmann::json& params) {
    nlohmann::json payload = params.value("payload", nlohmann::json::object());
    if (payload.is_null() ||
        payload.value("plate_index", -1) == -1 ||
        !payload.contains("views")) {
        BOOST_LOG_TRIVIAL(error) << "RenderPlateView: missing required parameters";
        throw std::runtime_error("Missing required parameters");
    }

    int plate_index = payload.value("plate_index", -1);
    auto views = payload["views"];

    if (!views.is_array()) {
        BOOST_LOG_TRIVIAL(error) << "RenderPlateView: views must be an array";
        throw std::runtime_error("Views must be an array");
    }

    nlohmann::json result = nlohmann::json::array();

    // Generate thumbnails for each view
    for (const auto& view : views) {
        if (!view.contains("camera_position") || !view.contains("target")) {
            BOOST_LOG_TRIVIAL(error) << "RenderPlateView: each view must contain camera_position and target";
            throw std::runtime_error("Invalid view format");
        }

        auto camera_pos_json = view["camera_position"];
        auto target_json = view["target"];

        // Look at Camera::select_view for how to calculate camera_position
        Vec3d camera_position(
            camera_pos_json.value("x", 0.0),
            camera_pos_json.value("y", 0.0),
            camera_pos_json.value("z", 0.0)
        );

        // Good choice is at the center of the plate. But it may be beneficial to target a little bit above the plate center.
        Vec3d target(
            target_json.value("x", 0.0),
            target_json.value("y", 0.0),
            target_json.value("z", 0.0)
        );

        ThumbnailData data;
        data.set(512, 512);
        RenderThumbnail(data, camera_position, target);

        // Convert to base64-encoded image
        std::string base64_image = encode_thumbnail_to_base64(data, false); // true for PNG, false for JPEG

        result.push_back({
            {"base64", base64_image}
        });
    }

    return result;
}

void JusPrinPlateUtils::RenderThumbnail(ThumbnailData& thumbnail_data,
    const Vec3d& camera_position, const Vec3d& target)
{
    const Camera::EType camera_type = Camera::EType::Perspective;  // Fixed camera type
    const ThumbnailsParams thumbnail_params = { {}, false, true, true, true, 0};  // Fixed params

    GLShaderProgram* shader = wxGetApp().get_shader("thumbnail");
    if (shader == nullptr) {
        BOOST_LOG_TRIVIAL(info) << "RenderThumbnail: shader is null, returning directly";
        return;
    }

    ModelObjectPtrs& model_objects = GUI::wxGetApp().model().objects;
    std::vector<ColorRGBA> extruder_colors = ::get_extruders_colors();
    auto canvas3D = wxGetApp().plater()->canvas3D();
    const GLVolumeCollection& volumes = canvas3D->get_volumes();
    PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(0);

    bool ban_light = false;
    static ColorRGBA curr_color;

    // Calculate visible volumes
    GLVolumePtrs visible_volumes;
    int plate_idx = thumbnail_params.plate_id;
    BoundingBoxf3 plate_build_volume = plate->get_plate_box();
    plate_build_volume.min -= Vec3d(1,1,1) * Slic3r::BuildVolume::SceneEpsilon;
    plate_build_volume.max += Vec3d(1,1,1) * Slic3r::BuildVolume::SceneEpsilon;

    auto is_visible = [plate_idx, plate_build_volume](const GLVolume& v) {
        bool ret = v.printable;
        if (plate_idx >= 0) {
            BoundingBoxf3 plate_bbox = plate_build_volume;
            plate_bbox.min(2) = -1e10;
            const BoundingBoxf3& volume_bbox = v.transformed_convex_hull_bounding_box();
            ret &= plate_bbox.contains(volume_bbox) && (volume_bbox.max(2) > 0);
        } else {
            ret &= (!v.shader_outside_printer_detection_enabled || !v.is_outside);
        }
        return ret;
    };

    for (const GLVolume* vol : volumes.volumes) {
        if (!vol->is_modifier && !vol->is_wipe_tower && (!thumbnail_params.parts_only || vol->composite_id.volume_id >= 0)) {
            if (is_visible(*vol)) {
                visible_volumes.emplace_back(const_cast<GLVolume*>(vol));
            }
        }
    }

    // Calculate volumes bounding box
    BoundingBoxf3 volumes_box;
    volumes_box.min.z() = volumes_box.max.z() = 0;
    if (!visible_volumes.empty()) {
        for (const GLVolume* vol : visible_volumes) {
            volumes_box.merge(vol->transformed_bounding_box());
        }
        // Add padding
        Vec3d size = volumes_box.size();
        volumes_box.min -= size * 0.01;
        volumes_box.max += size * 0.01;
        volumes_box.min.z() = -Slic3r::BuildVolume::SceneEpsilon;
    }

    // Setup camera
    Camera camera;
    camera.set_type(camera_type);
    camera.set_scene_box(plate_build_volume);
    camera.set_viewport(0, 0, thumbnail_data.width, thumbnail_data.height);
    camera.apply_viewport();

    plate_build_volume.min.z() = plate_build_volume.max.z() = 0.0;
    camera.zoom_to_box(plate_build_volume, 1.0);
    camera.look_at(camera_position, target, Vec3d::UnitZ());

    const Transform3d& view_matrix = camera.get_view_matrix();
    camera.apply_projection(plate_build_volume);
    const Transform3d& projection_matrix = camera.get_projection_matrix();

    // Clear background
    glsafe(::glClearColor(0.f, 0.f, 0.f, 0.f));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    if (ban_light) {
        glsafe(::glDisable(GL_BLEND));
    }

    // Render plate grid
    GLShaderProgram* flat_shader = wxGetApp().get_shader("flat");
    if (flat_shader != nullptr) {
        flat_shader->start_using();
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        flat_shader->set_uniform("view_model_matrix", view_matrix);
        flat_shader->set_uniform("projection_matrix", projection_matrix);
        plate->render_grid(true);  // true for bottom
        glsafe(::glDisable(GL_BLEND));
        flat_shader->stop_using();
    }

    // Render volumes
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    shader->set_uniform("ban_light", false);

    for (GLVolume* vol : visible_volumes) {
        curr_color = vol->color;
        ColorRGBA new_color = adjust_color_for_rendering(curr_color);
        vol->model.set_color(new_color);

        const bool is_active = vol->is_active;
        vol->is_active = true;

        const Transform3d model_matrix = vol->world_matrix();
        shader->set_uniform("volume_world_matrix", model_matrix);
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
            model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        vol->simple_render(shader, model_objects, extruder_colors, false);
        vol->is_active = is_active;
    }

    shader->stop_using();
    glsafe(::glDisable(GL_DEPTH_TEST));

    // Read pixels
    glsafe(::glReadPixels(0, 0, thumbnail_data.width, thumbnail_data.height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));

    // Debug output
    // std::string file_name = "zzh_" +
    //     std::to_string(int(camera_position.x()*100)) + "_" +
    //     std::to_string(int(camera_position.y()*100)) + "_" +
    //     std::to_string(int(camera_position.z()*100));
    // z_debug_output_thumbnail(thumbnail_data, file_name);

    BOOST_LOG_TRIVIAL(info) << "RenderThumbnail: finished";
}

nlohmann::json JusPrinPlateUtils::GetPlates(const nlohmann::json& params) {
    nlohmann::json j = nlohmann::json::array();

    for (const auto& plate : wxGetApp().plater()->get_partplate_list().get_plate_list()) {
        nlohmann::json plate_info;
        plate_info["name"] = plate->get_plate_name();
        plate_info["index"] = plate->get_index();
        plate_info["bounding_box"] = bbox_to_json(plate->get_plate_box());

        // Loop through each ModelObject
        nlohmann::json objects_info = nlohmann::json::array();
        for (const auto& obj : plate->get_objects_on_this_plate()) {
            nlohmann::json object_info;
            object_info["id"] = std::to_string(obj->id().id);
            object_info["name"] = obj->name;

            auto object_grid_config = &(obj->config);
            int extruder_id = -1;  // Default extruder ID
            auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(object_grid_config->option("extruder"));
            if (extruder_id_ptr) {
                extruder_id = *extruder_id_ptr;
            }
            object_info["extruderId"] = extruder_id;

            objects_info.push_back(object_info);
        }
        plate_info["modelObjects"] = objects_info;

        j.push_back(plate_info);
    }

    return j;
}

}} // namespace Slic3r::GUI
