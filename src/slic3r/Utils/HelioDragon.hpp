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
#include "nlohmann/json.hpp"
#include "../GUI/BackgroundSlicingProcess.hpp"
#include "../GUI/NotificationManager.hpp"

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
        unsigned    status;
        std::string error;
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

    static PresignedURLResult create_presigned_url(const std::string helio_api_url, const std::string helio_api_key);
};

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

    std::mutex              m_mutex;
    std::condition_variable m_condition;
    boost::thread           m_thread;
    State                   m_state = STATE_INITIAL;
    std::string             helio_api_key;
    std::string             helio_api_url;

    void helio_threaded_process_start(std::mutex&                                slicing_mutex,
                                      std::condition_variable&                   slicing_condition,
                                      BackgroundSlicingProcess::State&           slicing_state,
                                      std::unique_ptr<GUI::NotificationManager>& notification_manager);

    void helio_thread_start(std::mutex&                                slicing_mutex,
                            std::condition_variable&                   slicing_condition,
                            BackgroundSlicingProcess::State&           slicing_state,
                            std::unique_ptr<GUI::NotificationManager>& notification_manager);

    HelioBackgroundProcess()
    {
        helio_api_url = "https://api2.helioadditive.com/graphql/sdk";
    }

    void set_helio_api_key(std::string api_key);

};
} // namespace Slic3r

#endif
