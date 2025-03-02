#ifndef slic3r_HelioDragon_hpp_
#define slic3r_HelioDragon_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include <condition_variable>
#include <mutex>
#include <boost/thread.hpp>
#include <wx/event.h>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "../GUI/BackgroundSlicingProcess.hpp"
#include "../GUI/NotificationManager.hpp"
//#include "Preferences.hpp"

namespace Slic3r {

class DynamicPrintConfig;
class Http;
class AppConfig;

class HelioQuery
{
public:
    struct PresignedURLResult
    {
        std::string key;
        std::string mimeType;
        std::string url;
    };

    struct CreateGCodeQuery
    {
        std::string name;
        std::string materialID;
        std::string printerID;
        std::string gcodeKey;
    };

    struct CreateSimulationQuery
    {
        std::string name;
        std::string gcodeID;
        float       initialRoomAirtemp;
        float       layerThreshold;
        float       objectProximityAirtemp;
    };

    static PresignedURLResult create_presigned_url(std::string helio_api_key);

    //static const std::string helio_api_url;
};

//const std::string HelioQuery::helio_api_url = "https://api2.helioadditive.com/graphql/sdk";

class HelioBackgroundProcess
{
public:
    enum State {
        // m_thread  is not running yet, or it did not reach the STATE_IDLE yet (it does not wait on the condition yet).
        STATE_INITIAL = 0,
        // m_thread is waiting for the task to execute.
        STATE_IDLE,
        STATE_STARTED,
        // m_thread is executing a task.
        STATE_RUNNING,
        // m_thread finished executing a task, and it is waiting until the UI thread picks up the results.
        STATE_FINISHED,
        // m_thread finished executing a task, the task has been canceled by the UI thread, therefore the UI thread will not be notified.
        STATE_CANCELED,
        // m_thread exited the loop and it is going to finish. The UI thread should join on m_thread.
        STATE_EXIT,
        STATE_EXITED,
    };

    std::mutex                                 m_mutex;
    std::condition_variable                    m_condition;
    boost::thread                              m_thread;
    State                                      m_state = STATE_INITIAL;
    std::unique_ptr<GUI::NotificationManager>& m_notification_manager;
    //std::string                                helio_api_key;

    void helio_threaded_process_start(std::mutex&                                slicing_mutex,
                                      std::condition_variable&                   slicing_condition,
                                      BackgroundSlicingProcess::State&           slicing_state,
                                      std::unique_ptr<GUI::NotificationManager>& notification_manager);

    void helio_thread_start(std::mutex&                      slicing_mutex,
                            std::condition_variable&         slicing_condition,
                            BackgroundSlicingProcess::State& slicing_state);

    void init_notification_manager()
    {
        if (!m_notification_manager)
            return;
        m_notification_manager->init();

        auto cancel_callback = [this]() { return true; };
        m_notification_manager->init_slicing_progress_notification(cancel_callback);
        m_notification_manager->set_fff(true);
        m_notification_manager->init_progress_indicator();
    }

    HelioBackgroundProcess(std::unique_ptr<GUI::NotificationManager>& notification_manager) : m_notification_manager(notification_manager)
    {
        //helio_api_key = wxGetApp().app_config->get("helio_api_key");
    }
};
} // namespace Slic3r

#endif
