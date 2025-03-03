#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <boost/log/trivial.hpp>

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
            res.error  = error;
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

    if (slicing_state == BackgroundSlicingProcess::STATE_FINISHED) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("url: %1%, key: %2%") % helio_api_url % helio_api_key;

        HelioQuery::PresignedURLResult create_presigned_url_res = HelioQuery::create_presigned_url(helio_api_url, helio_api_key);

        if (create_presigned_url_res.error.empty()) {
            notification_manager->push_notification("Helio: Presigned URL Created");
            HelioQuery::UploadFileResult upload_file_res = HelioQuery::upload_file_to_presigned_url(m_gcode_result->filename, create_presigned_url_res.url);

            if (upload_file_res.success) {
                notification_manager->push_notification("Helio: file successfully uploaded");
            } else {
                notification_manager->push_notification("Helio: file upload failed");
            }
        } else {
            notification_manager->push_notification((boost::format("error: %1%") % create_presigned_url_res.error).str());
        }
    }

    slicing_lck.unlock();
}

void HelioBackgroundProcess::set_helio_api_key(std::string api_key) { helio_api_key = api_key; }
void HelioBackgroundProcess::set_gcode_result(Slic3r::GCodeProcessorResult* gcode_result) { m_gcode_result = gcode_result; }

} // namespace Slic3r
