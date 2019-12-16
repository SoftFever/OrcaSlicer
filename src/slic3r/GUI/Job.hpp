#ifndef JOB_HPP
#define JOB_HPP

#include <atomic>

#include <slic3r/Utils/Thread.hpp>
#include <slic3r/GUI/I18N.hpp>
#include <slic3r/GUI/ProgressIndicator.hpp>

#include <wx/event.h>

#include <boost/thread.hpp>

namespace Slic3r { namespace GUI {

// A class to handle UI jobs like arranging and optimizing rotation.
// These are not instant jobs, the user has to be informed about their
// state in the status progress indicator. On the other hand they are
// separated from the background slicing process. Ideally, these jobs should
// run when the background process is not running.
//
// TODO: A mechanism would be useful for blocking the plater interactions:
// objects would be frozen for the user. In case of arrange, an animation
// could be shown, or with the optimize orientations, partial results
// could be displayed.
class Job : public wxEvtHandler
{
    int               m_range = 100;
    boost::thread     m_thread;
    std::atomic<bool> m_running{false}, m_canceled{false};
    bool              m_finalized = false;
    std::shared_ptr<ProgressIndicator> m_progress;
    
    void run()
    {
        m_running.store(true);
        process();
        m_running.store(false);
        
        // ensure to call the last status to finalize the job
        update_status(status_range(), "");
    }
    
protected:
    // status range for a particular job
    virtual int status_range() const { return 100; }
    
    // status update, to be used from the work thread (process() method)
    void update_status(int st, const wxString &msg = "")
    {
        auto evt = new wxThreadEvent();
        evt->SetInt(st);
        evt->SetString(msg);
        wxQueueEvent(this, evt);
    }
    
    bool        was_canceled() const { return m_canceled.load(); }
    
    // Launched just before start(), a job can use it to prepare internals
    virtual void prepare() {}
    
    // Launched when the job is finished. It refreshes the 3Dscene by def.
    virtual void finalize() { m_finalized = true; }
    
    bool is_finalized() const { return m_finalized; }
    
public:
    Job(std::shared_ptr<ProgressIndicator> pri) : m_progress(pri)
    {
        Bind(wxEVT_THREAD, [this](const wxThreadEvent &evt) {
            auto msg = evt.GetString();
            if (!msg.empty())
                m_progress->set_status_text(msg.ToUTF8().data());
            
            if (m_finalized) return;
            
            m_progress->set_progress(evt.GetInt());
            if (evt.GetInt() == status_range()) {
                // set back the original range and cancel callback
                m_progress->set_range(m_range);
                m_progress->set_cancel_callback();
                wxEndBusyCursor();
                
                finalize();
                
                // dont do finalization again for the same process
                m_finalized = true;
            }
        });
    }
    
    Job(const Job &) = delete;
    Job(Job &&)      = delete;
    Job &operator=(const Job &) = delete;
    Job &operator=(Job &&) = delete;
    
    virtual void process() = 0;
    
    void start()
    { // Start the job. No effect if the job is already running
        if (!m_running.load()) {
            prepare();
            
            // Save the current status indicatior range and push the new one
            m_range = m_progress->get_range();
            m_progress->set_range(status_range());
            
            // init cancellation flag and set the cancel callback
            m_canceled.store(false);
            m_progress->set_cancel_callback(
                [this]() { m_canceled.store(true); });
            
            m_finalized = false;
            
            // Changing cursor to busy
            wxBeginBusyCursor();
            
            try { // Execute the job
                m_thread = create_thread([this] { this->run(); });
            } catch (std::exception &) {
                update_status(status_range(),
                              _(L("ERROR: not enough resources to "
                                  "execute a new job.")));
            }
            
            // The state changes will be undone when the process hits the
            // last status value, in the status update handler (see ctor)
        }
    }
    
    // To wait for the running job and join the threads. False is
    // returned if the timeout has been reached and the job is still
    // running. Call cancel() before this fn if you want to explicitly
    // end the job.
    bool join(int timeout_ms = 0)
    {
        if (!m_thread.joinable()) return true;
        
        if (timeout_ms <= 0)
            m_thread.join();
        else if (!m_thread.try_join_for(boost::chrono::milliseconds(timeout_ms)))
            return false;
        
        return true;
    }
    
    bool is_running() const { return m_running.load(); }
    void cancel() { m_canceled.store(true); }
};

}
}

#endif // JOB_HPP
