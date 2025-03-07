#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintBase.hpp"
#include <boost/log/trivial.hpp>
#include "../GUI/GUI_App.hpp"
#include "../GUI/Event.hpp"
#include "../GUI/Plater.hpp"
#include "../GUI/NotificationManager.hpp"
#include "wx/app.h"
#include "cstdio"


namespace Slic3r {

HelioQuery::PresignedURLResult HelioQuery::create_presigned_url(const std::string helio_api_url, const std::string helio_api_key)
{
    HelioQuery::PresignedURLResult res;
    std::string                    query_body = R"( {
			"query": "query PresignedURL($fileName: String!) { presignedUrl(fileName: $fileName) { mimeType url key } }",
			"variables": {
				"fileName": "test.gcode"
			}
		} )";

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Helio-Key", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(20)
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("error")) {
                res.error = parsed_obj["error"];
            } else {
                res.key      = parsed_obj["data"]["presignedUrl"]["key"];
                res.mimeType = parsed_obj["data"]["presignedUrl"]["mimeType"];
                res.url      = parsed_obj["data"]["presignedUrl"]["url"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = (boost::format("error: %1%, message: %2%") % error % body).str();
            res.status = status;
        })
        .perform_sync();

    return res;
};

HelioQuery::UploadFileResult HelioQuery::upload_file_to_presigned_url(const std::string file_path_string, const std::string upload_url)
{
    UploadFileResult res;

    Http                    http = Http::put(upload_url);
    boost::filesystem::path file_path(file_path_string);
    http.header("Content-Type", "application/octet-stream");

    http.set_put_body(file_path)
        .on_complete([&res](std::string body, unsigned status) {
            if (status == 200)
                res.success = true;
            else
                res.success = false;
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error   = (boost::format("status: %1%, error: %2%, %3%") % status % body % error).str();
        })
        .perform_sync();

    return res;
}

HelioQuery::CreateGCodeResult HelioQuery::create_gcode(const std::string key,
                                                       const std::string helio_api_url,
                                                       const std::string helio_api_key,
                                                       const std::string printer_id,
                                                       const std::string filament_id)
{
    HelioQuery::CreateGCodeResult res;
    std::string                   query_body_template = R"( {
			"query": "mutation CreateGcode($input: CreateGcodeInput!) { createGcode(input: $input) { success gcode { id name } extraInputsRequired } }",
			"variables": {
			  "input": {
				"name": "%1%",
				"printerId": "%2%",
				"materialId": "%3%",
				"gcodeKey": "%4%"
			  }
			}
		  } )";

    std::vector<std::string> key_split;
    boost::split(key_split, key, boost::is_any_of("/"));

    std::string gcode_name = key_split.back();

    std::string query_body = (boost::format(query_body_template) % gcode_name % printer_id % filament_id % key).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Helio-Key", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(20)
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("errors")) {
                res.error   = parsed_obj["errors"].dump();
                res.success = false;
            } else {
                res.success = true;
                res.id      = parsed_obj["data"]["createGcode"]["gcode"]["id"];
                res.name    = parsed_obj["data"]["createGcode"]["gcode"]["name"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

HelioQuery::CreateSimulationResult HelioQuery::create_simulation(const std::string helio_api_url,
                                                                 const std::string helio_api_key,
                                                                 const std::string gcode_id,
                                                                 const float       initial_room_airtemp,
                                                                 const float       layer_threshold,
                                                                 const float       object_proximity_airtemp)
{
    HelioQuery::CreateSimulationResult res;
    std::string                        query_body_template = R"( {
					"query": "mutation CreateSimulation($simulationSettings: SimulationSettingsInput!) { createSimulation(simulationSettings: $simulationSettings) { id name } }",
					"variables": {
					  "simulationSettings": {
						"name": "%1%",
						"gcodeId": "%2%",
						"initialRoomAirtemp": %3%,
						"layerThreshold": %4%,
						"objectProximityAirtemp": %5%,
						"platformTemperature": null,
						"nozzleTemperature": null,
						"fanSpeed": null
					  }
					}
				} )";

    std::string query_body =
        (boost::format(query_body_template) % gcode_id % gcode_id % initial_room_airtemp % layer_threshold % object_proximity_airtemp).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Helio-Key", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(20)
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("errors")) {
                res.error   = parsed_obj["errors"].dump();
                res.success = false;
            } else {
                res.success = true;
                res.id      = parsed_obj["data"]["createSimulation"]["id"];
                res.name    = parsed_obj["data"]["createSimulation"]["name"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

HelioQuery::CheckSimulationProgressResult HelioQuery::check_simulation_progress(const std::string helio_api_url,
                                                                                const std::string helio_api_key,
                                                                                const std::string simulation_id)
{
    HelioQuery::CheckSimulationProgressResult res;
    std::string                               query_body_template = R"( {
							"query": "query Simulation($id: ID!) { simulation(id: $id) { id name progress status thermalIndexGcodeUrl } }",
							"variables": {
								"id": "%1%"
							}
						} )";

    std::string query_body = (boost::format(query_body_template) % simulation_id).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Helio-Key", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(20)
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("errors")) {
                res.error = parsed_obj["errors"].dump();
            } else {
                res.id          = parsed_obj["data"]["simulation"]["id"];
                res.name        = parsed_obj["data"]["simulation"]["name"];
                res.progress    = parsed_obj["data"]["simulation"]["progress"];
                res.is_finished = parsed_obj["data"]["simulation"]["status"] == "FINISHED";
                if (res.is_finished)
                    res.url = parsed_obj["data"]["simulation"]["thermalIndexGcodeUrl"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

void HelioBackgroundProcess::helio_thread_start(std::mutex&                                slicing_mutex,
                                                std::condition_variable&                   slicing_condition,
                                                BackgroundSlicingProcess::State&           slicing_state,
                                                std::unique_ptr<GUI::NotificationManager>& notification_manager)
{

    m_thread = create_thread([this, &slicing_mutex, &slicing_condition, &slicing_state, &notification_manager] {
        this->helio_threaded_process_start(slicing_mutex, slicing_condition, slicing_state, notification_manager);
    });

}

void HelioBackgroundProcess::helio_threaded_process_start(std::mutex&                                slicing_mutex,
                                                          std::condition_variable&                   slicing_condition,
                                                          BackgroundSlicingProcess::State&           slicing_state,
                                                          std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    std::unique_lock<std::mutex> slicing_lck(slicing_mutex);
    slicing_condition.wait(slicing_lck, [this, &slicing_state]() {
        return slicing_state == BackgroundSlicingProcess::STATE_FINISHED || slicing_state == BackgroundSlicingProcess::STATE_CANCELED;
    });
    slicing_lck.unlock();

	wxPostEvent(GUI::wxGetApp().plater(), GUI::SimpleEvent(GUI::EVT_HELIO_PROCESSING_STARTED));

    Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(0.0, "Helio: Process Started");
    Slic3r::SlicingStatusEvent *evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
    wxQueueEvent(GUI::wxGetApp().plater(), evt);

    if (slicing_state == BackgroundSlicingProcess::STATE_FINISHED) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("url: %1%, key: %2%") % helio_api_url % helio_api_key;

        HelioQuery::PresignedURLResult create_presigned_url_res = HelioQuery::create_presigned_url(helio_api_url, helio_api_key);

        if (create_presigned_url_res.error.empty() && create_presigned_url_res.status == 200) {

			status = Slic3r::PrintBase::SlicingStatus(5, "Helio: Presigned URL Created");
		    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
			wxQueueEvent(GUI::wxGetApp().plater(), evt);

            HelioQuery::UploadFileResult upload_file_res = HelioQuery::upload_file_to_presigned_url(m_gcode_result->filename,
                                                                                                    create_presigned_url_res.url);

            if (upload_file_res.success) {

				status = Slic3r::PrintBase::SlicingStatus(10, "Helio: file succesfully uploaded");
				evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
				wxQueueEvent(GUI::wxGetApp().plater(), evt);

                HelioQuery::CreateGCodeResult create_gcode_res = HelioQuery::create_gcode(create_presigned_url_res.key, helio_api_url,
                                                                                          helio_api_key, printer_id, filament_id);

                create_simulation_step(create_gcode_res, notification_manager);

            } else {
				status = Slic3r::PrintBase::SlicingStatus(100, "Helio: file upload failed");
				evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
				wxQueueEvent(GUI::wxGetApp().plater(), evt);
            }
        } else {

            std::string presigned_url_message = (boost::format("error: %1%") % create_presigned_url_res.error).str();

            if (create_presigned_url_res.status == 401) {
                presigned_url_message += "\n Please make sure you have the corrent API key set in preferences.";
            }

			status = Slic3r::PrintBase::SlicingStatus(100, presigned_url_message);
		    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
			wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
    }
}

void HelioBackgroundProcess::create_simulation_step(
    HelioQuery::CreateGCodeResult create_gcode_res, std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    if (create_gcode_res.success) {
		Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(15, "Helio: GCode created successfully");
		Slic3r::SlicingStatusEvent *evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
		wxQueueEvent(GUI::wxGetApp().plater(), evt);

        auto              print_config             = GUI::wxGetApp().preset_bundle->full_config();
        const std::string gcode_id                 = create_gcode_res.id;
        const float       initial_room_airtemp     = print_config.opt_float("helio_initial_room_air_temp");
        const float       layer_threshold          = print_config.opt_float("helio_layer_threshold");
        const float       object_proximity_airtemp = print_config.opt_float("helio_object_proximity_airtemp");

        HelioQuery::CreateSimulationResult create_simulation_res = HelioQuery::create_simulation(helio_api_url, helio_api_key, gcode_id,
                                                                                                 initial_room_airtemp, layer_threshold,
                                                                                                 object_proximity_airtemp);

        if (create_simulation_res.success) {

			status = Slic3r::PrintBase::SlicingStatus(20, "Helio: simulation successfully created");
			evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
			wxQueueEvent(GUI::wxGetApp().plater(), evt);

            int times_tried            = 0;
            int max_unsuccessful_tries = 5;
            int times_queried          = 0;

            while (true) {
                HelioQuery::CheckSimulationProgressResult check_simulation_progress_res =
                    HelioQuery::check_simulation_progress(helio_api_url, helio_api_key, create_simulation_res.id);

                if (check_simulation_progress_res.status == 200) {
                    times_tried = 0;
                    if (check_simulation_progress_res.error.empty()) {

                        std::string trailing_dots = "";

                        for (int i = 0; i < (times_queried % 3); i++) {
                            trailing_dots += "....";
                        }

						status = Slic3r::PrintBase::SlicingStatus(35 + (80-35) * check_simulation_progress_res.progress, "Helio: simulation working" + trailing_dots);
						evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
						wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        if (check_simulation_progress_res.is_finished) {
                            //notification_manager->push_notification((boost::format("Helio: Simulation finished.")).str());
                            std::string simulated_gcode_path = HelioBackgroundProcess::create_path_for_simulated_gcode(m_gcode_result->filename);
                            HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(check_simulation_progress_res.url,
                                                                                           simulated_gcode_path, notification_manager);
                            break;
                        }
                    } else {
						status = Slic3r::PrintBase::SlicingStatus(100, "Helio: simulation failed");
						evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
						wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        break;
                    }
                } else {
                    times_tried++;
                    //notification_manager->push_notification(
                        //(boost::format("Helio: Simulation check failed, %1% tries left") % (max_unsuccessful_tries - times_tried)).str());

					status = Slic3r::PrintBase::SlicingStatus(35, (boost::format("Helio: Simulation check failed, %1% tries left") % (max_unsuccessful_tries - times_tried)).str());
					evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
					wxQueueEvent(GUI::wxGetApp().plater(), evt);

                    if (times_tried >= max_unsuccessful_tries)
                        break;
                }

                times_queried++;
                boost::this_thread ::sleep_for(boost::chrono::seconds(3));
            }

        } else {
			status = Slic3r::PrintBase::SlicingStatus(100, "Helio: Failed to create simulation");
			evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
			wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }

    } else {
		Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, "Helio: Failed to create GCode");
		Slic3r::SlicingStatusEvent *evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
		wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }

}

void HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(std::string file_download_url, std::string simulated_gcode_path, std::unique_ptr<GUI::NotificationManager>& notification_manager) {

    auto http = Http::get(file_download_url);
    unsigned    response_status = 0;
    std::string downloaded_gcode;
    std::string response_error;

    int number_of_attempts = 0;
    int max_attempts       = 4;
    while (response_status != 200) {
        http.on_complete([&downloaded_gcode, &response_error, &response_status](std::string body, unsigned status) {
                response_status = status;
                if (status == 200) {
                    downloaded_gcode = body;
                } else {
                    response_error = (boost::format("status: %1%, error: %2%") % status % body).str();
                }
            })
            .on_error([&response_error, &response_status](std::string body, std::string error, unsigned status) {
                response_status = status;
                response_error  = (boost::format("status: %1%, error: %2%") % status % body).str();
            })
            .perform_sync();

        if (response_status != 200) {
            number_of_attempts++;
			Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(80, (boost::format("Helio: Could not download file. Attempts left %1%") % (max_attempts - number_of_attempts)).str());
			Slic3r::SlicingStatusEvent *evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
			wxQueueEvent(GUI::wxGetApp().plater(), evt);
            
            boost::this_thread::sleep_for(boost::chrono::seconds(2));
        }

        if (response_status == 200 || number_of_attempts >= max_attempts){
            response_error = "";
            break;
        }
    }

    if (response_error.empty()) {
        FILE* file = fopen(simulated_gcode_path.c_str(), "w");
        fwrite(downloaded_gcode.c_str(), 1, downloaded_gcode.size(), file);
        fclose(file);

		Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, "Helio: GCode downloaded successfully");
		Slic3r::SlicingStatusEvent *evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
		wxQueueEvent(GUI::wxGetApp().plater(), evt);
        HelioBackgroundProcess::load_simulation_to_viwer(simulated_gcode_path);
    } else {
		Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, (boost::format("Helio: GCode download failed: %1%") % response_error).str());
		Slic3r::SlicingStatusEvent *evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
		wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }


}

void HelioBackgroundProcess::load_simulation_to_viwer(std::string file_path) { 
        m_gcode_processor.process_file(file_path);
        GCodeProcessorResult* res = &m_gcode_processor.result();
        m_preview->update_gcode_result(res);
        GUI::SimpleEvent evt = GUI::SimpleEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED);
        wxPostEvent(GUI::wxGetApp().plater(), evt);
    }

void HelioBackgroundProcess::set_helio_api_key(std::string api_key) { helio_api_key = api_key; }
void HelioBackgroundProcess::set_gcode_result(Slic3r::GCodeProcessorResult* gcode_result) { m_gcode_result = gcode_result; }

} // namespace Slic3r
