#include "libslic3r/libslic3r.h"
#include "Mouse3DController.hpp"

#include "Camera.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "AppConfig.hpp"
#include "GLCanvas3D.hpp"

#include <wx/glcanvas.h>

#include <boost/nowide/convert.hpp>
#include <boost/log/trivial.hpp>
#include "I18N.hpp"

#include <bitset>

//unofficial linux lib
//#include <spnav.h>

// WARN: If updating these lists, please also update resources/udev/90-3dconnexion.rules

static const std::vector<int> _3DCONNEXION_VENDORS =
{
    0x046d,  // LOGITECH = 1133 // Logitech (3Dconnexion is made by Logitech)
    0x256F   // 3DCONNECTION = 9583 // 3Dconnexion
};

// See: https://github.com/FreeSpacenav/spacenavd/blob/a9eccf34e7cac969ee399f625aef827f4f4aaec6/src/dev.c#L202
static const std::vector<int> _3DCONNEXION_DEVICES =
{
    0xc603,	/* 50691 spacemouse plus XT */
    0xc605,	/* 50693 cadman */
    0xc606,	/* 50694 spacemouse classic */
    0xc621,	/* 50721 spaceball 5000 */
    0xc623,	/* 50723 space traveller */
    0xc625,	/* 50725 space pilot */
    0xc626,	/* 50726 space navigator *TESTED* */
    0xc627,	/* 50727 space explorer */
    0xc628,	/* 50728 space navigator for notebooks*/
    0xc629,	/* 50729 space pilot pro*/
    0xc62b,	/* 50731 space mouse pro*/
    0xc62e,	/* 50734 spacemouse wireless (USB cable) *TESTED* */
    0xc62f,	/* 50735 spacemouse wireless receiver */
    0xc631,	/* 50737 spacemouse pro wireless *TESTED* */
    0xc632,	/* 50738 spacemouse pro wireless receiver */
    0xc633,	/* 50739 spacemouse enterprise */
    0xc635,	/* 50741 spacemouse compact *TESTED* */
    0xc636,	/* 50742 spacemouse module */
    0xc640,	/* 50752 nulooq */
    0xc652, /* 50770 3Dconnexion universal receiver *TESTED* */
};

namespace Slic3r {
namespace GUI {

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
template<typename T>
void update_maximum(std::atomic<T>& maximum_value, T const& value) noexcept
{
    T prev_value = maximum_value;
    while (prev_value < value && ! maximum_value.compare_exchange_weak(prev_value, value)) ;
}
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

void Mouse3DController::State::append_translation(const Vec3d& translation, size_t input_queue_max_size)
{
	tbb::mutex::scoped_lock lock(m_input_queue_mutex);
    while (m_input_queue.size() >= input_queue_max_size)
        m_input_queue.pop_front();
    m_input_queue.emplace_back(QueueItem::translation(translation));
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    update_maximum(input_queue_max_size_achieved, m_input_queue.size());
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
}

void Mouse3DController::State::append_rotation(const Vec3f& rotation, size_t input_queue_max_size)
{
	tbb::mutex::scoped_lock lock(m_input_queue_mutex);
    while (m_input_queue.size() >= input_queue_max_size)
        m_input_queue.pop_front();
    m_input_queue.emplace_back(QueueItem::rotation(rotation.cast<double>()));
#ifdef WIN32
	if (rotation.x() != 0.0f)
        ++ m_mouse_wheel_counter;
#endif // WIN32
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    update_maximum(input_queue_max_size_achieved, m_input_queue.size());
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
}

void Mouse3DController::State::append_button(unsigned int id, size_t /* input_queue_max_size */)
{
	tbb::mutex::scoped_lock lock(m_input_queue_mutex);
    m_input_queue.emplace_back(QueueItem::buttons(id));
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    update_maximum(input_queue_max_size_achieved, m_input_queue.size());
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
}

#ifdef WIN32
// Filter out mouse scroll events produced by the 3DConnexion driver.
bool Mouse3DController::State::process_mouse_wheel()
{
	tbb::mutex::scoped_lock lock(m_input_queue_mutex);
	if (m_mouse_wheel_counter == 0)
    	// No 3DConnexion rotation has been captured since the last mouse scroll event.
        return false;
    if (std::find_if(m_input_queue.begin(), m_input_queue.end(), [](const QueueItem &item){ return item.is_rotation(); }) != m_input_queue.end()) {
    	// There is a rotation stored in the queue. Suppress one mouse scroll event.
        -- m_mouse_wheel_counter;
        return true;
    }
    m_mouse_wheel_counter = 0;
    return true;
}
#endif // WIN32

bool Mouse3DController::State::apply(const Mouse3DController::Params &params, Camera& camera)
{
    if (! wxGetApp().IsActive())
        return false;

    std::deque<QueueItem> input_queue;
    {
    	// Atomically move m_input_queue to input_queue.
    	tbb::mutex::scoped_lock lock(m_input_queue_mutex);
    	input_queue = std::move(m_input_queue);
        m_input_queue.clear();
    }

    for (const QueueItem &input_queue_item : input_queue) {
    	if (input_queue_item.is_translation()) {
	        const Vec3d& translation = input_queue_item.vector;
	        double zoom_factor = camera.min_zoom() / camera.get_zoom();
	        camera.set_target(camera.get_target() + zoom_factor * params.translation.scale * (translation.x() * camera.get_dir_right() + translation.z() * camera.get_dir_up()));
            if (translation.y() != 0.0)
                camera.update_zoom(params.zoom.scale * translation.y());
        } else if (input_queue_item.is_rotation()) {
	    	Vec3d rot = params.rotation.scale * input_queue_item.vector * (PI / 180.);
	        camera.rotate_local_around_target(Vec3d(rot.x(), - rot.z(), rot.y()));
	        break;
	    } else {
	    	assert(input_queue_item.is_buttons());
	        switch (input_queue_item.type_or_buttons) {
	        case 0: camera.update_zoom(1.0); break;
	        case 1: camera.update_zoom(-1.0); break;
            default: break;
	        }
    	}
    }

    return ! input_queue.empty();
}

// Load the device parameter database from appconfig. To be called on application startup.
void Mouse3DController::load_config(const AppConfig &appconfig)
{
	// We do not synchronize m_params_by_device with the background thread explicitely 
	// as there should be a full memory barrier executed once the background thread is started.
	m_params_by_device.clear();

	for (const std::string &device_name : appconfig.get_mouse_device_names()) {
	    double translation_speed 	= 4.0;
	    float  rotation_speed 		= 4.0;
	    double translation_deadzone = Params::DefaultTranslationDeadzone;
	    float  rotation_deadzone 	= Params::DefaultRotationDeadzone;
	    double zoom_speed 			= 2.0;
	    appconfig.get_mouse_device_translation_speed(device_name, translation_speed);
	    appconfig.get_mouse_device_translation_deadzone(device_name, translation_deadzone);
	    appconfig.get_mouse_device_rotation_speed(device_name, rotation_speed);
	    appconfig.get_mouse_device_rotation_deadzone(device_name, rotation_deadzone);
	    appconfig.get_mouse_device_zoom_speed(device_name, zoom_speed);
	    // clamp to valid values
	    Params params;
	    params.translation.scale = Params::DefaultTranslationScale * std::clamp(translation_speed, 0.1, 10.0);
	    params.translation.deadzone = std::clamp(translation_deadzone, 0.0, Params::MaxTranslationDeadzone);
	    params.rotation.scale = Params::DefaultRotationScale * std::clamp(rotation_speed, 0.1f, 10.0f);
	    params.rotation.deadzone = std::clamp(rotation_deadzone, 0.0f, Params::MaxRotationDeadzone);
	    params.zoom.scale = Params::DefaultZoomScale * std::clamp(zoom_speed, 0.1, 10.0);
	    m_params_by_device[device_name] = std::move(params);
	}
}

// Store the device parameter database back to appconfig. To be called on application closeup.
void Mouse3DController::save_config(AppConfig &appconfig) const
{
	// We do not synchronize m_params_by_device with the background thread explicitely 
	// as there should be a full memory barrier executed once the background thread is stopped.
	for (const std::pair<std::string, Params> &key_value_pair : m_params_by_device) {
		const std::string &device_name = key_value_pair.first;
		const Params      &params      = key_value_pair.second;
	    // Store current device parameters into the config
	    appconfig.set_mouse_device(device_name, params.translation.scale / Params::DefaultTranslationScale, params.translation.deadzone,
	        params.rotation.scale / Params::DefaultRotationScale, params.rotation.deadzone, params.zoom.scale / Params::DefaultZoomScale);
	}
}

bool Mouse3DController::apply(Camera& camera)
{
    // check if the user unplugged the device
    if (! m_connected) {
        // hides the settings dialog if the user un-plug the device
        m_show_settings_dialog = false;
        m_settings_dialog_closed_by_user = false;
    }
    return m_state.apply(m_params, camera);
}

void Mouse3DController::render_settings_dialog(GLCanvas3D& canvas) const
{
    if (! m_show_settings_dialog || ! m_connected)
        return;

    // when the user clicks on [X] or [Close] button we need to trigger
    // an extra frame to let the dialog disappear
    if (m_settings_dialog_closed_by_user)
    {
        m_show_settings_dialog = false;
        m_settings_dialog_closed_by_user = false;
        canvas.request_extra_frame();
        return;
    }

    Params params_copy;
    bool   params_changed = false;
    {
    	tbb::mutex::scoped_lock lock(m_params_ui_mutex);
    	params_copy = m_params_ui;
    }

    Size cnv_size = canvas.get_canvas_size();

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.set_next_window_pos(0.5f * (float)cnv_size.get_width(), 0.5f * (float)cnv_size.get_height(), ImGuiCond_Always, 0.5f, 0.5f);

    static ImVec2 last_win_size(0.0f, 0.0f);
    bool shown = true;
    if (imgui.begin(_(L("3Dconnexion settings")), &shown, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
    {
        if (shown)
        {
            ImVec2 win_size = ImGui::GetWindowSize();
            if ((last_win_size.x != win_size.x) || (last_win_size.y != win_size.y))
            {
                // when the user clicks on [X] button, the next time the dialog is shown 
                // has a dummy size, so we trigger an extra frame to let it have the correct size
                last_win_size = win_size;
                canvas.request_extra_frame();
            }

            const ImVec4& color = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text(_(L("Device:")));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            imgui.text(m_device_str);

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text(_(L("Speed:")));
            ImGui::PopStyleColor();

            float translation_scale = (float)params_copy.translation.scale / Params::DefaultTranslationScale;
            if (imgui.slider_float(_(L("Translation")) + "##1", &translation_scale, 0.1f, 10.0f, "%.1f")) {
            	params_copy.translation.scale = Params::DefaultTranslationScale * (double)translation_scale;
            	params_changed = true;
            }

            float rotation_scale = params_copy.rotation.scale / Params::DefaultRotationScale;
            if (imgui.slider_float(_(L("Rotation")) + "##1", &rotation_scale, 0.1f, 10.0f, "%.1f")) {
            	params_copy.rotation.scale = Params::DefaultRotationScale * rotation_scale;
            	params_changed = true;
            }

            float zoom_scale = params_copy.zoom.scale / Params::DefaultZoomScale;
            if (imgui.slider_float(_(L("Zoom")), &zoom_scale, 0.1f, 10.0f, "%.1f")) {
            	params_copy.zoom.scale = Params::DefaultZoomScale * zoom_scale;
            	params_changed = true;
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text(_(L("Deadzone:")));
            ImGui::PopStyleColor();

            float translation_deadzone = (float)params_copy.translation.deadzone;
            if (imgui.slider_float(_(L("Translation")) + "/" + _(L("Zoom")), &translation_deadzone, 0.0f, (float)Params::MaxTranslationDeadzone, "%.2f")) {
            	params_copy.translation.deadzone = (double)translation_deadzone;
            	params_changed = true;
            }

            float rotation_deadzone = params_copy.rotation.deadzone;
            if (imgui.slider_float(_(L("Rotation")) + "##2", &rotation_deadzone, 0.0f, Params::MaxRotationDeadzone, "%.2f")) {
            	params_copy.rotation.deadzone = rotation_deadzone;
            	params_changed = true;
            }

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            ImGui::Separator();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text("DEBUG:");
            imgui.text("Vectors:");
            ImGui::PopStyleColor();
            Vec3f translation = m_state.get_first_vector_of_type(State::QueueItem::TranslationType).cast<float>();
            Vec3f rotation = m_state.get_first_vector_of_type(State::QueueItem::RotationType).cast<float>();
            ImGui::InputFloat3("Translation##3", translation.data(), "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("Rotation##3", rotation.data(), "%.3f", ImGuiInputTextFlags_ReadOnly);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text("Queue size:");
            ImGui::PopStyleColor();

            int input_queue_size_current[2] = { int(m_state.input_queue_size_current()), int(m_state.input_queue_max_size_achieved) };
            ImGui::InputInt2("Current##4", input_queue_size_current, ImGuiInputTextFlags_ReadOnly);

            int input_queue_size_param = int(params_copy.input_queue_max_size);
            if (ImGui::InputInt("Max size", &input_queue_size_param, 1, 1, ImGuiInputTextFlags_ReadOnly))
            {
                if (input_queue_size_param > 0) {
	            	params_copy.input_queue_max_size = input_queue_size_param;
    	        	params_changed = true;
                }
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            imgui.text("Camera:");
            ImGui::PopStyleColor();
            Vec3f target = wxGetApp().plater()->get_camera().get_target().cast<float>();
            ImGui::InputFloat3("Target", target.data(), "%.3f", ImGuiInputTextFlags_ReadOnly);
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

            ImGui::Separator();
            if (imgui.button(_(L("Close"))))
            {
                // the user clicked on the [Close] button
                m_settings_dialog_closed_by_user = true;
                canvas.set_as_dirty();
            }
        }
        else
        {
            // the user clicked on the [X] button
            m_settings_dialog_closed_by_user = true;
            canvas.set_as_dirty();
        }
    }

    imgui.end();

    if (params_changed) {
    	// Synchronize front end parameters to back end.
    	tbb::mutex::scoped_lock lock(m_params_ui_mutex);
        auto pthis = const_cast<Mouse3DController*>(this);
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        if (params_copy.input_queue_max_size != params_copy.input_queue_max_size)
        	// Reset the statistics counter.
            m_state.input_queue_max_size_achieved = 0;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        pthis->m_params_ui = params_copy;
        pthis->m_params_ui_changed = true;
    }
}

#if __APPLE__

void Mouse3DController::connected(std::string device_name)
{
    assert(! m_connected);
    assert(m_device_str.empty());
	m_device_str = device_name;
    // Copy the parameters for m_device_str into the current parameters.
    if (auto it_params = m_params_by_device.find(m_device_str); it_params != m_params_by_device.end()) {
    	tbb::mutex::scoped_lock lock(m_params_ui_mutex);
    	m_params = m_params_ui = it_params->second;
    }
    m_connected = true;
}

void Mouse3DController::disconnected()
{
    // Copy the current parameters for m_device_str into the parameter database.
    assert(m_connected == ! m_device_str.empty());
    if (m_connected) {
        tbb::mutex::scoped_lock lock(m_params_ui_mutex);
        m_params_by_device[m_device_str] = m_params_ui;
	    m_device_str.clear();
	    m_connected = false;
        wxGetApp().plater()->get_camera().recover_from_free_camera();
        wxGetApp().plater()->set_current_canvas_as_dirty();
        wxWakeUpIdle();
    }
}

bool Mouse3DController::handle_input(const DataPacketAxis& packet)
{
    if (! wxGetApp().IsActive())
        return false;

    {
    	// Synchronize parameters between the UI thread and the background thread.
    	//FIXME is this necessary on OSX? Are these notifications triggered from the main thread or from a worker thread?
    	tbb::mutex::scoped_lock lock(m_params_ui_mutex);
    	if (m_params_ui_changed) {
    		m_params = m_params_ui;
    		m_params_ui_changed = false;
    	}
    }
    
    bool updated = false;
    // translation
    double deadzone = m_params.translation.deadzone;
    Vec3d translation(std::abs(packet[0]) > deadzone ? -packet[0] : 0.0,
                      std::abs(packet[1]) > deadzone ?  packet[1] : 0.0,
                      std::abs(packet[2]) > deadzone ?  packet[2] : 0.0);
    if (! translation.isApprox(Vec3d::Zero())) {
        m_state.append_translation(translation, m_params.input_queue_max_size);
        updated = true;
    }
    // rotation
    deadzone = m_params.rotation.deadzone;
    Vec3f rotation(std::abs(packet[3]) > deadzone ? (float)packet[3] : 0.0,
                   std::abs(packet[4]) > deadzone ? (float)packet[4] : 0.0,
                   std::abs(packet[5]) > deadzone ? (float)packet[5] : 0.0);
    if (! rotation.isApprox(Vec3f::Zero())) {
        m_state.append_rotation(rotation, m_params.input_queue_max_size);
        updated = true;
    }

    if (updated) {
        wxGetApp().plater()->set_current_canvas_as_dirty();
        // ask for an idle event to update 3D scene
        wxWakeUpIdle();
    }
    return updated;
}

#else //__APPLE__

// Initialize the application.
void Mouse3DController::init()
{
	assert(! m_thread.joinable());
    if (! m_thread.joinable()) {
    	m_stop = false;
	    m_thread = std::thread(&Mouse3DController::run, this);
	}
}

// Closing the application.
void Mouse3DController::shutdown()
{
    if (m_thread.joinable()) {
    	// Stop the worker thread, if running.
    	{
    		// Notify the worker thread to cancel wait on detection polling.
			std::lock_guard<std::mutex> lock(m_stop_condition_mutex);
			m_stop = true;
		}
		m_stop_condition.notify_all();
		// Wait for the worker thread to stop.
        m_thread.join();
        m_stop = false;
	}
}

// Main routine of the worker thread.
void Mouse3DController::run()
{
    // Initialize the hidapi library
    int res = hid_init();
    if (res != 0) {
    	// Give up.
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize hidapi library";
        return;
    }

    for (;;) {
        {
        	tbb::mutex::scoped_lock lock(m_params_ui_mutex);
        	if (m_stop)
        		break;
        	if (m_params_ui_changed) {
        		m_params = m_params_ui;
        		m_params_ui_changed = false;
        	}
        }
    	if (m_device == nullptr)
    		// Polls the HID devices, blocks for maximum 2 seconds.
    		m_connected = this->connect_device();
    	else
    		// Waits for 3DConnexion mouse input for maximum 100ms, then repeats.
        	this->collect_input();
    }

    this->disconnect_device();

    // Finalize the hidapi library
    hid_exit();
}

bool Mouse3DController::connect_device()
{
    if (m_stop)
    	return false;

    {
    	// Wait for 2 seconds, but cancellable by m_stop.
    	std::unique_lock<std::mutex> lock(m_stop_condition_mutex);
        m_stop_condition.wait_for(lock, std::chrono::seconds(2), [this]{ return this->m_stop; });
    }

    if (m_stop)
    	return false;

    // Enumerates devices
    hid_device_info* devices = hid_enumerate(0, 0);
    if (devices == nullptr)
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to enumerate HID devices";
        return false;
    }

    // Searches for 1st connected 3Dconnexion device
    struct DeviceData
    {
        std::string path;
        unsigned short usage_page;
        unsigned short usage;

        DeviceData()
            : path(""), usage_page(0), usage(0)
        {}
        DeviceData(const std::string& path, unsigned short usage_page, unsigned short usage)
            : path(path), usage_page(usage_page), usage(usage)
        {}

        bool has_valid_usage() const { return (usage_page == 1) && (usage == 8); }
    };

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    hid_device_info* cur = devices;
    std::cout << std::endl << "======================================================================================================================================" << std::endl;
    std::cout << "Detected devices:" << std::endl;
    while (cur != nullptr)
    {
        std::cout << "\"";
        std::wcout << ((cur->manufacturer_string != nullptr) ? cur->manufacturer_string : L"Unknown");
        std::cout << "/";
        std::wcout << ((cur->product_string != nullptr) ? cur->product_string : L"Unknown");
        std::cout << "\" code: " << cur->vendor_id << "/" << cur->product_id << " (" << std::hex << cur->vendor_id << "/" << cur->product_id << std::dec << ")";
        std::cout << " serial number: '";
        std::wcout << ((cur->serial_number != nullptr) ? cur->serial_number : L"Unknown");
        std::cout << "' usage page: " << cur->usage_page << " usage: " << cur->usage << " interface number: " << cur->interface_number << std::endl;

        cur = cur->next;
    }
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

    // When using 3Dconnexion universal receiver, multiple devices are detected sharing the same vendor_id and product_id.
    // To choose from them the right one we use:
    // On Windows and Mac: usage_page == 1 and usage == 8
    // On Linux: as usage_page and usage are not defined (see hidapi.h) we try all detected devices until one is succesfully open
    // When only a single device is detected, as for wired connections, vendor_id and product_id are enough

    // First we count all the valid devices from the enumerated list,

    hid_device_info* current = devices;
    typedef std::pair<unsigned short, unsigned short> DeviceIds;
    typedef std::vector<DeviceData> DeviceDataList;
    typedef std::map<DeviceIds, DeviceDataList> DetectedDevices;
    DetectedDevices detected_devices;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    std::cout << std::endl << "Detected 3D connexion devices:" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    while (current != nullptr)
    {
        unsigned short vendor_id = 0;
        unsigned short product_id = 0;

        for (size_t i = 0; i < _3DCONNEXION_VENDORS.size(); ++i)
        {
            if (_3DCONNEXION_VENDORS[i] == current->vendor_id)
            {
                vendor_id = current->vendor_id;
                break;
            }
        }

        if (vendor_id != 0)
        {
            for (size_t i = 0; i < _3DCONNEXION_DEVICES.size(); ++i)
            {
                if (_3DCONNEXION_DEVICES[i] == current->product_id)
                {
                    product_id = current->product_id;
                    DeviceIds detected_device(vendor_id, product_id);
                    DetectedDevices::iterator it = detected_devices.find(detected_device);
                    if (it == detected_devices.end())
                        it = detected_devices.insert(DetectedDevices::value_type(detected_device, DeviceDataList())).first;

                    it->second.emplace_back(current->path, current->usage_page, current->usage);

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    std::wcout << "\"" << ((current->manufacturer_string != nullptr) ? current->manufacturer_string : L"Unknown");
                    std::cout << "/";
                    std::wcout << ((current->product_string != nullptr) ? current->product_string : L"Unknown");
                    std::cout << "\" code: " << current->vendor_id << "/" << current->product_id << " (" << std::hex << current->vendor_id << "/" << current->product_id << std::dec << ")";
                    std::cout << " serial number: '";
                    std::wcout << ((current->serial_number != nullptr) ? current->serial_number : L"Unknown");
                    std::cout << "' usage page: " << current->usage_page << " usage: " << current->usage << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                }
            }
        }

        current = current->next;
    }

    // Free enumerated devices
    hid_free_enumeration(devices);

    if (detected_devices.empty())
        return false;

    std::string path = "";
    unsigned short vendor_id = 0;
    unsigned short product_id = 0;

    // Then we'll decide the choosing logic to apply in dependence of the device count and operating system

    for (const DetectedDevices::value_type& device : detected_devices)
    {
        if (device.second.size() == 1)
        {
#if defined(__linux__)
            hid_device* test_device = hid_open(device.first.first, device.first.second, nullptr);
            if (test_device != nullptr)
            {
                hid_close(test_device);
#else
            if (device.second.front().has_valid_usage())
            {
#endif // __linux__ 
                vendor_id = device.first.first;
                product_id = device.first.second;
                break;
            }
        }
        else
        {
            bool found = false;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            for (const DeviceData& data : device.second)
            {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                std::cout << "Test device: " << std::hex << device.first.first << std::dec << "/" << std::hex << device.first.second << std::dec << " \"" << data.path << "\"";
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

#ifdef __linux__
                hid_device* test_device = hid_open_path(data.path.c_str());
                if (test_device != nullptr)
                {
                    path = data.path;
                    vendor_id = device.first.first;
                    product_id = device.first.second;
                    found = true;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    std::cout << "-> PASSED" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    hid_close(test_device);
                    break;
                }
#else // !__linux__
                if (data.has_valid_usage())
                {
                    path = data.path;
                    vendor_id = device.first.first;
                    product_id = device.first.second;
                    found = true;
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    std::cout << "-> PASSED" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                    break;
                }
#endif // __linux__
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
                else
                    std::cout << "-> NOT PASSED" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            }

            if (found)
                break;
        }
    }

    if (path.empty())
    {
        if ((vendor_id != 0) && (product_id != 0))
        {
            // Open the 3Dconnexion device using vendor_id and product_id
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << std::endl << "Opening device: " << std::hex << vendor_id << std::dec << "/" << std::hex << product_id << std::dec << " using hid_open()" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            m_device = hid_open(vendor_id, product_id, nullptr);
        }
        else
            return false;
    }
    else
    {
        // Open the 3Dconnexion device using the device path
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        std::cout << std::endl << "Opening device: " << std::hex << vendor_id << std::dec << "/" << std::hex << product_id << std::dec << "\"" << path << "\" using hid_open_path()" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        m_device = hid_open_path(path.c_str());
    }

    if (m_device != nullptr)
    {
        wchar_t buffer[1024];
        hid_get_manufacturer_string(m_device, buffer, 1024);
        m_device_str = boost::nowide::narrow(buffer);
        // #3479 seems to show that sometimes an extra whitespace is added, so we remove it
        boost::algorithm::trim(m_device_str);

        hid_get_product_string(m_device, buffer, 1024);
        m_device_str += "/" + boost::nowide::narrow(buffer);
        // #3479 seems to show that sometimes an extra whitespace is added, so we remove it
        boost::algorithm::trim(m_device_str);

        BOOST_LOG_TRIVIAL(info) << "Connected 3DConnexion device:";
        BOOST_LOG_TRIVIAL(info) << "Manufacturer/product: " << m_device_str;
        BOOST_LOG_TRIVIAL(info) << "Manufacturer id.....: " << vendor_id << " (" << std::hex << vendor_id << std::dec << ")";
        BOOST_LOG_TRIVIAL(info) << "Product id..........: " << product_id << " (" << std::hex << product_id << std::dec << ")";
        if (!path.empty())
            BOOST_LOG_TRIVIAL(info) << "Path................: '" << path << "'";
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        std::cout << "Opened device." << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        // Copy the parameters for m_device_str into the current parameters.
        if (auto it_params = m_params_by_device.find(m_device_str); it_params != m_params_by_device.end()) {
	    	tbb::mutex::scoped_lock lock(m_params_ui_mutex);
	    	m_params = m_params_ui = it_params->second;
	    }
    }
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    else
    {
        std::cout << std::endl << "Unable to connect to device:" << std::endl;
        std::cout << "Manufacturer/product: " << m_device_str << std::endl;
        std::cout << "Manufacturer id.....: " << vendor_id << " (" << std::hex << vendor_id << std::dec << ")" << std::endl;
        std::cout << "Product id..........: " << product_id << " (" << std::hex << product_id << std::dec << ")" << std::endl;
        std::cout << "Path................: '" << path << "'" << std::endl;
    }
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

    return (m_device != nullptr);
}

void Mouse3DController::disconnect_device()
{
    if (m_device) {
	    hid_close(m_device);
	    m_device = nullptr;
	    BOOST_LOG_TRIVIAL(info) << "Disconnected device: " << m_device_str;
        // Copy the current parameters for m_device_str into the parameter database.
        {
	        tbb::mutex::scoped_lock lock(m_params_ui_mutex);
	        m_params_by_device[m_device_str] = m_params_ui;
	    }
	    m_device_str.clear();
	    m_connected = false;
        wxGetApp().plater()->get_camera().recover_from_free_camera();
        wxGetApp().plater()->set_current_canvas_as_dirty();
        wxWakeUpIdle();
    }
}

void Mouse3DController::collect_input()
{
    DataPacketRaw packet = { 0 };
    // Read packet, block maximum 100 ms. That means when closing the application, closing the application will be delayed by 100 ms.
    int res = hid_read_timeout(m_device, packet.data(), packet.size(), 100);
    if (res < 0) {
        // An error occourred (device detached from pc ?). Close the 3Dconnexion device.
        this->disconnect_device();
    } else
		this->handle_input(packet, res, m_params, m_state);
}

// Unpack raw 3DConnexion HID packet of a wired 3D mouse into m_state. Called by the worker thread.
bool Mouse3DController::handle_input(const DataPacketRaw& packet, const int packet_lenght, const Params &params, State &state_in_out)
{
    if (! wxGetApp().IsActive())
        return false;

    int res = packet_lenght;
    bool updated = false;

    if (res == 7)
        updated = handle_packet(packet, params, state_in_out);
    else if (res == 13)
        updated = handle_wireless_packet(packet, params, state_in_out);
    else if ((res == 3) && (packet[0] == 3))
        // On Mac button packets can be 3 bytes long
        updated = handle_packet(packet, params, state_in_out);
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
    else if (res > 0)
        std::cout << "Got unknown data packet of length: " << res << ", code:" << (int)packet[0] << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT

#if 1
    if (updated) {
        wxGetApp().plater()->set_current_canvas_as_dirty();
        // ask for an idle event to update 3D scene
        wxWakeUpIdle();
    }
#endif
    return updated;
}

// Unpack raw 3DConnexion HID packet of a wired 3D mouse into m_state. Called by handle_input() from the worker thread.
bool Mouse3DController::handle_packet(const DataPacketRaw& packet, const Params &params, State &state_in_out)
{
    switch (packet[0])
    {
    case 1: // Translation
        {
            if (handle_packet_translation(packet, params, state_in_out))
                return true;

            break;
        }
    case 2: // Rotation
        {
            if (handle_packet_rotation(packet, 1, params, state_in_out))
                return true;

            break;
        }
    case 3: // Button
        {
            if (params.buttons_enabled && handle_packet_button(packet, packet.size() - 1, params, state_in_out))
                return true;

            break;
        }
    case 23: // Battery charge
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << "3DConnexion - battery level: " << (int)packet[1] << " percent" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    default:
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << "3DConnexion - Got unknown data packet of code: " << (int)packet[0] << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    }

    return false;
}

// Unpack raw 3DConnexion HID packet of a wireless 3D mouse into m_state. Called by handle_input() from the worker thread.
bool Mouse3DController::handle_wireless_packet(const DataPacketRaw& packet, const Params &params, State &state_in_out)
{
    switch (packet[0])
    {
    case 1: // Translation + Rotation
        {
            bool updated = handle_packet_translation(packet, params, state_in_out);
            updated |= handle_packet_rotation(packet, 7, params, state_in_out);

            if (updated)
                return true;

            break;
        }
    case 3: // Button
        {
            if (params.buttons_enabled && handle_packet_button(packet, 12, params, state_in_out))
                return true;

            break;
        }
    case 23: // Battery charge
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << "3DConnexion - battery level: " << (int)packet[1] << " percent" << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    default:
        {
#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            std::cout << "3DConnexion - Got unknown data packet of code: " << (int)packet[0] << std::endl;
#endif // ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
            break;
        }
    }
    
    return false;
}

// Convert a signed 16bit word from a 3DConnexion mouse HID packet into a double coordinate, apply a dead zone.
static double convert_input(int coord_byte_low, int coord_byte_high, double deadzone)
{
    int value = coord_byte_low | (coord_byte_high << 8);
    if (value >= 32768)
    	value = value - 65536;
    double ret = (double)value / 350.0;
    return (std::abs(ret) > deadzone) ? ret : 0.0;
}

// Unpack raw 3DConnexion HID packet, decode state of translation axes into state_in_out. Called by handle_input() from the worker thread.
bool Mouse3DController::handle_packet_translation(const DataPacketRaw& packet, const Params &params, State &state_in_out)
{
    double deadzone = params.translation.deadzone;
    Vec3d translation(-convert_input(packet[1], packet[2], deadzone),
        convert_input(packet[3], packet[4], deadzone),
        convert_input(packet[5], packet[6], deadzone));

    if (!translation.isApprox(Vec3d::Zero()))
    {
        state_in_out.append_translation(translation, params.input_queue_max_size);
        return true;
    }

    return false;
}

// Unpack raw 3DConnexion HID packet, decode state of rotation axes into state_in_out. Called by the handle_input() from worker thread.
bool Mouse3DController::handle_packet_rotation(const DataPacketRaw& packet, unsigned int first_byte, const Params &params, State &state_in_out)
{
    double deadzone = (double)params.rotation.deadzone;
    Vec3f rotation((float)convert_input(packet[first_byte + 0], packet[first_byte + 1], deadzone),
        (float)convert_input(packet[first_byte + 2], packet[first_byte + 3], deadzone),
        (float)convert_input(packet[first_byte + 4], packet[first_byte + 5], deadzone));

    if (!rotation.isApprox(Vec3f::Zero()))
    {
        state_in_out.append_rotation(rotation, params.input_queue_max_size);
        return true;
    }

    return false;
}

// Unpack raw 3DConnexion HID packet, decode button state into state_in_out. Called by handle_input() from the worker thread.
bool Mouse3DController::handle_packet_button(const DataPacketRaw& packet, unsigned int packet_size, const Params &params, State &state_in_out)
{
    unsigned int data = 0;
    for (unsigned int i = 1; i < packet_size; ++i)
    {
        data |= packet[i] << 8 * (i - 1);
    }

    const std::bitset<32> data_bits{ data };
    for (size_t i = 0; i < data_bits.size(); ++i)
    {
        if (data_bits.test(i))
        {
            state_in_out.append_button((unsigned int)i, params.input_queue_max_size);
            return true;
        }
    }

    return false;
}

#endif //__APPLE__

} // namespace GUI
} // namespace Slic3r
