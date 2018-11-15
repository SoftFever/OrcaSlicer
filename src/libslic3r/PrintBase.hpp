#ifndef slic3r_PrintBase_hpp_
#define slic3r_PrintBase_hpp_

#include "libslic3r.h"
#include <atomic>
#include <set>
#include <vector>
#include <string>
#include <functional>

#include "tbb/atomic.h"
// tbb/mutex.h includes Windows, which in turn defines min/max macros. Convince Windows.h to not define these min/max macros.
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include "tbb/mutex.h"

#include "Model.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class CanceledException : public std::exception {
public:
   const char* what() const throw() { return "Background processing has been canceled"; }
};

// To be instantiated over PrintStep or PrintObjectStep enums.
template <class StepType, size_t COUNT>
class PrintState
{
public:
    PrintState() { for (size_t i = 0; i < COUNT; ++ i) m_state[i].store(INVALID, std::memory_order_relaxed); }

    enum State {
        INVALID,
        STARTED,
        DONE,
    };
    
    // With full memory barrier.
    bool is_done(StepType step) const { return m_state[step] == DONE; }

    // Set the step as started. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // This is necessary to block until the Print::apply_config() updates its state, which may
    // influence the processing step being entered.
    void set_started(StepType step, tbb::mutex &mtx) {
        mtx.lock();
        m_state[step].store(STARTED, std::memory_order_relaxed);
        mtx.unlock();
    }

    // Set the step as done. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    void set_done(StepType step, tbb::mutex &mtx) { 
        mtx.lock();
        m_state[step].store(DONE, std::memory_order_relaxed);
        mtx.unlock();
    }

    // Make the step invalid.
    // The provided mutex should be locked at this point, guarding access to m_state.
    // In case the step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate(StepType step, tbb::mutex &mtx, CancelationCallback cancel) {
        bool invalidated = m_state[step].load(std::memory_order_relaxed) != INVALID;
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            mtx.unlock();
            cancel();
            m_state[step] = INVALID;
            mtx.lock();
        }
        return invalidated;
    }

    template<typename CancelationCallback, typename StepTypeIterator>
    bool invalidate_multiple(StepTypeIterator step_begin, StepTypeIterator step_end, tbb::mutex &mtx, CancelationCallback cancel) {
        bool invalidated = false;
        for (StepTypeIterator it = step_begin; ! invalidated && it != step_end; ++ it)
            invalidated = m_state[*it].load(std::memory_order_relaxed) != INVALID;
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            mtx.unlock();
            cancel();
            for (StepTypeIterator it = step_begin; it != step_end; ++ it)
                m_state[*it] = INVALID;
            mtx.lock();
        }
        return invalidated;
    }

    // Make all steps invalid.
    // The provided mutex should be locked at this point, guarding access to m_state.
    // In case any step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate_all(tbb::mutex &mtx, CancelationCallback cancel) {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++ i)
            if (m_state[i].load(std::memory_order_relaxed) != INVALID) {
                invalidated = true;
                break;
            }
        if (invalidated) {
            mtx.unlock();
            cancel();
            for (size_t i = 0; i < COUNT; ++ i)
                m_state[i].store(INVALID, std::memory_order_relaxed);
            mtx.lock();
        }
        return invalidated;
    }

private:
    std::atomic<State>          m_state[COUNT];
};

class PrintBase;

class PrintObjectBase
{
protected:
    virtual ~PrintObjectBase() {}
    // Declared here to allow access from PrintBase through friendship.
	static tbb::mutex&            cancel_mutex(PrintBase *print);
	static std::function<void()>  cancel_callback(PrintBase *print);
};

/**
 * @brief Printing involves slicing and export of device dependent instructions.
 *
 * Every technology has a potentially different set of requirements for
 * slicing, support structures and output print instructions. The pipeline
 * however remains roughly the same:
 *      slice -> convert to instructions -> send to printer
 *
 * The PrintBase class will abstract this flow for different technologies.
 *
 */
class PrintBase
{
public:
	PrintBase() { this->restart(); }
    inline virtual ~PrintBase() {}

    virtual PrinterTechnology technology() const noexcept = 0;

    // Reset the print status including the copy of the Model / ModelObject hierarchy.
    virtual void            clear() = 0;
    // The Print is empty either after clear() or after apply() over an empty model,
    // or after apply() over a model, where no object is printable (all outside the print volume).
    virtual bool            empty() const = 0;

    // Validate the print, return empty string if valid, return error if process() cannot (or should not) be started.
    virtual std::string     validate() const { return std::string(); }

    enum ApplyStatus {
        // No change after the Print::apply() call.
        APPLY_STATUS_UNCHANGED,
        // Some of the Print / PrintObject / PrintObjectInstance data was changed,
        // but no result was invalidated (only data influencing not yet calculated results were changed).
        APPLY_STATUS_CHANGED,
        // Some data was changed, which in turn invalidated already calculated steps.
        APPLY_STATUS_INVALIDATED,
    };
    virtual ApplyStatus     apply(const Model &model, const DynamicPrintConfig &config) = 0;

    virtual void            process() = 0;

    typedef std::function<void(int, const std::string&)>  status_callback_type;
    // Default status console print out in the form of percent => message.
    void                    set_status_default() { m_status_callback = nullptr; }
    // No status output or callback whatsoever, useful mostly for automatic tests.
    void                    set_status_silent() { m_status_callback = [](int, const std::string&){}; }
    // Register a custom status callback.
    void                    set_status_callback(status_callback_type cb) { m_status_callback = cb; }
    // Calls a registered callback to update the status, or print out the default message.
    void                    set_status(int percent, const std::string &message) { 
        if (m_status_callback) m_status_callback(percent, message);
        /*else */printf("%d => %s\n", percent, message.c_str());
    }

    typedef std::function<void()>  cancel_callback_type;
    // Various methods will call this callback to stop the background processing (the Print::process() call)
    // in case a successive change of the Print / PrintObject / PrintRegion instances changed
    // the state of the finished or running calculations.
    void                    set_cancel_callback(cancel_callback_type cancel_callback) { m_cancel_callback = cancel_callback; }
    // Has the calculation been canceled?
	enum CancelStatus {
		// No cancelation, background processing should run.
		NOT_CANCELED = 0,
		// Canceled by user from the user interface (user pressed the "Cancel" button or user closed the application).
		CANCELED_BY_USER = 1,
		// Canceled internally from Print::apply() through the Print/PrintObject::invalidate_step() or ::invalidate_all_steps().
		CANCELED_INTERNAL = 2
	};
    CancelStatus            cancel_status() const { return m_cancel_status; }
    // Has the calculation been canceled?
	bool                   canceled() const { return m_cancel_status != NOT_CANCELED; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                   cancel() { m_cancel_status = CANCELED_BY_USER; }
	void                   cancel_internal() { m_cancel_status = CANCELED_INTERNAL; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                   restart() { m_cancel_status = NOT_CANCELED; }

protected:
	friend class PrintObjectBase;

    tbb::mutex&            cancel_mutex() { return m_cancel_mutex; }
    std::function<void()>  cancel_callback() { return m_cancel_callback; }
	void				   call_cancell_callback() { m_cancel_callback(); }

    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void                   throw_if_canceled() const { if (m_cancel_status) throw CanceledException(); }

private:
    tbb::atomic<CancelStatus>               m_cancel_status;
    // Callback to be evoked regularly to update state of the UI thread.
    status_callback_type                    m_status_callback;

    // Callback to be evoked to stop the background processing before a state is updated.
    cancel_callback_type                    m_cancel_callback = [](){};

    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    mutable tbb::mutex                      m_cancel_mutex;
};

template<typename PrintStepEnum, const size_t COUNT>
class PrintBaseWithState : public PrintBase
{
public:
    bool            is_step_done(PrintStepEnum step) const { return m_state.is_done(step); }

protected:
    void            set_started(PrintStepEnum step) { m_state.set_started(step, this->cancel_mutex()); throw_if_canceled(); }
    void            set_done(PrintStepEnum step) { m_state.set_done(step, this->cancel_mutex()); throw_if_canceled(); }
    bool            invalidate_step(PrintStepEnum step)
		{ return m_state.invalidate(step, this->cancel_mutex(), this->cancel_callback()); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) 
        { return m_state.invalidate_multiple(step_begin, step_end, this->cancel_mutex(), this->cancel_callback()); }
    bool            invalidate_steps(std::initializer_list<PrintStepEnum> il) 
        { return m_state.invalidate_multiple(il.begin(), il.end(), this->cancel_mutex(), this->cancel_callback()); }
    bool            invalidate_all_steps() 
        { return m_state.invalidate_all(this->cancel_mutex(), this->cancel_callback()); }

private:
    PrintState<PrintStepEnum, COUNT> m_state;
};

template<typename PrintType, typename PrintObjectStepEnum, const size_t COUNT>
class PrintObjectBaseWithState : public PrintObjectBase
{
public:
    PrintType*       print()         { return m_print; }
    const PrintType* print() const   { return m_print; }

    bool            is_step_done(PrintObjectStepEnum step) const { return m_state.is_done(step); }

protected:
	PrintObjectBaseWithState(PrintType *print) : m_print(print) {}

    void            set_started(PrintObjectStepEnum step) { m_state.set_started(step, PrintObjectBase::cancel_mutex(m_print)); }
    void            set_done(PrintObjectStepEnum step) { m_state.set_done(step, PrintObjectBase::cancel_mutex(m_print)); }

    bool            invalidate_step(PrintObjectStepEnum step)
        { return m_state.invalidate(step, PrintObjectBase::cancel_mutex(m_print), PrintObjectBase::cancel_callback(m_print)); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) 
        { return m_state.invalidate_multiple(step_begin, step_end, PrintObjectBase::cancel_mutex(m_print), PrintObjectBase::cancel_callback(m_print)); }
    bool            invalidate_steps(std::initializer_list<PrintObjectStepEnum> il) 
        { return m_state.invalidate_multiple(il.begin(), il.end(), PrintObjectBase::cancel_mutex(m_print), PrintObjectBase::cancel_callback(m_print)); }
    bool            invalidate_all_steps() { return m_state.invalidate_all(PrintObjectBase::cancel_mutex(m_print), PrintObjectBase::cancel_callback(m_print)); }

protected:
    friend PrintType;
    PrintType                               *m_print;

private:
    PrintState<PrintObjectStepEnum, COUNT>   m_state;
};

} // namespace Slic3r

#endif /* slic3r_PrintBase_hpp_ */
