#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <boost/log/trivial.hpp>

namespace Slic3r {

HelioQuery::PresignedURLResult create_presigned_url(std::string helio_api_key)
{
    /* Http http = Http::post(HelioQuery::helio_api_url);

    std::string query_body = R"( {
			"query": "query PresignedURL($fileName: String!) 
            { presignedUrl(fileName: $fileName) { mimeType url key } }",
			"variables": {
				"fileName": "test.gcode"
			}
		} )";

    http.header("Content-Type", "application/json");
    http.header("Helio-Key", helio_api_key);
    http.set_post_body(query_body);
    http.on_complete([&](std::string body, unsigned status) {
			BOOST_LOG_TRIVIAL(debug) << boost::format("%1% created file, key") % body; })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1% could not create file, error") % error;
        })
        .perform_sync();*/

        std::string                a        = "";
    HelioQuery::PresignedURLResult ret_item = {a, a, a};
    return ret_item;
};

void HelioBackgroundProcess::helio_thread_start(std::mutex&                      slicing_mutex,
                                                std::condition_variable&         slicing_condition,
                                                BackgroundSlicingProcess::State& slicing_state)
{
    m_thread = create_thread([this, &slicing_mutex, &slicing_condition, &slicing_state] {
        this->helio_threaded_process_start(slicing_mutex, slicing_condition, slicing_state, this->m_notification_manager);
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
//    HelioQuery::create_presigned_url(helio_api_key);
    notification_manager->push_notification(GUI::format(_L("Stuff Happened")));
    slicing_lck.unlock();
}
} // namespace Slic3r
