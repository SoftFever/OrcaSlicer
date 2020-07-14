#ifndef slic3r_PrintBase_hpp_
#define slic3r_PrintBase_hpp_

#include "libslic3r.h"
#include <set>
#include <vector>
#include <string>
#include <functional>

// tbb/mutex.h includes Windows, which in turn defines min/max macros. Convince Windows.h to not define these min/max macros.
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include "tbb/mutex.h"

#include "ObjectID.hpp"
#include "Model.hpp"
#include "PlaceholderParser.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class CanceledException : public std::exception {
public:
   const char* what() const throw() { return "Background processing has been canceled"; }
};

class PrintStateBase {
public:
    enum State {
        INVALID,
        STARTED,
        DONE,
    };

    enum class WarningLevel {
        NON_CRITICAL,
        CRITICAL
    };

    typedef size_t TimeStamp;

    // A new unique timestamp is being assigned to the step every time the step changes its state.
    struct StateWithTimeStamp
    {
        StateWithTimeStamp() : state(INVALID), timestamp(0) {}
        State       state;
        TimeStamp   timestamp;
    };

    struct Warning
    {
        WarningLevel    level;
        std::string     message;
    };

    struct StateWithWarnings : public StateWithTimeStamp
    {
        std::vector<Warning>    warnings;
    };

protected:
    //FIXME last timestamp is shared between Print & SLAPrint,
    // and if multiple Print or SLAPrint instances are executed in parallel, modification of g_last_timestamp
    // is not synchronized!
    static size_t g_last_timestamp;
};

// To be instantiated over PrintStep or PrintObjectStep enums.
template <class StepType, size_t COUNT>
class PrintState : public PrintStateBase
{
public:
    PrintState() {}

    StateWithTimeStamp state_with_timestamp(StepType step, tbb::mutex &mtx) const {
        tbb::mutex::scoped_lock lock(mtx);
        StateWithTimeStamp state = m_state[step];
        return state;
    }

    StateWithWarnings state_with_warnings(StepType step, tbb::mutex &mtx) const {
        tbb::mutex::scoped_lock lock(mtx);
        StateWithWarnings state = m_state[step];
        return state;
    }

    bool is_started(StepType step, tbb::mutex &mtx) const {
        return this->state_with_timestamp(step, mtx).state == STARTED;
    }

    bool is_done(StepType step, tbb::mutex &mtx) const {
        return this->state_with_timestamp(step, mtx).state == DONE;
    }

    StateWithTimeStamp state_with_timestamp_unguarded(StepType step) const { 
        return m_state[step];
    }

    bool is_started_unguarded(StepType step) const {
        return this->state_with_timestamp_unguarded(step).state == STARTED;
    }

    bool is_done_unguarded(StepType step) const {
        return this->state_with_timestamp_unguarded(step).state == DONE;
    }

    // Set the step as started. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // This is necessary to block until the Print::apply() updates its state, which may
    // influence the processing step being entered.
    template<typename ThrowIfCanceled>
    bool set_started(StepType step, tbb::mutex &mtx, ThrowIfCanceled throw_if_canceled) {
        tbb::mutex::scoped_lock lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
#ifndef NDEBUG
// The following test is not necessarily valid after the background processing thread
// is stopped with throw_if_canceled(), as the CanceledException is not being catched
// by the Print or PrintObject to update m_step_active or m_state[...].state.
// This should not be a problem as long as the caller calls set_started() / set_done() /
// active_step_add_warning() consistently. From the robustness point of view it would be
// be better to catch CanceledException and do the updates. From the performance point of view,
// the current implementation is optimal.
//
//        assert(m_step_active == -1);
//        for (int i = 0; i < int(COUNT); ++ i)
//            assert(m_state[i].state != STARTED);
#endif // NDEBUG
        if (m_state[step].state == DONE)
            return false;
        PrintStateBase::StateWithWarnings &state = m_state[step];
        state.state = STARTED;
        state.timestamp = ++ g_last_timestamp;
        state.warnings.clear();
        m_step_active = static_cast<int>(step);
        return true;
    }

    // Set the step as done. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
	template<typename ThrowIfCanceled>
	TimeStamp set_done(StepType step, tbb::mutex &mtx, ThrowIfCanceled throw_if_canceled) {
        tbb::mutex::scoped_lock lock(mtx);
        // If canceled, throw before changing the step state.
        throw_if_canceled();
        assert(m_state[step].state == STARTED);
        assert(m_step_active == static_cast<int>(step));
        PrintStateBase::StateWithWarnings &state = m_state[step];
        state.state = DONE;
        state.timestamp = ++ g_last_timestamp;
        m_step_active = -1;
        return state.timestamp;
    }

    // Make the step invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to m_state.
    // In case the step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate(StepType step, CancelationCallback cancel) {
        bool invalidated = m_state[step].state != INVALID;
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            PrintStateBase::StateWithWarnings &state = m_state[step];
            state.state = INVALID;
            state.timestamp = ++ g_last_timestamp;
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            // Internally the cancel() callback shall unlock the PrintBase::m_status_mutex to let
            // the working thread proceed.
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to clear it.
            state.warnings.clear();
            m_step_active = -1;
        }
        return invalidated;
    }

    template<typename CancelationCallback, typename StepTypeIterator>
    bool invalidate_multiple(StepTypeIterator step_begin, StepTypeIterator step_end, CancelationCallback cancel) {
        bool invalidated = false;
        for (StepTypeIterator it = step_begin; it != step_end; ++ it) {
            StateWithTimeStamp &state = m_state[*it];
            if (state.state != INVALID) {
                invalidated = true;
                state.state = INVALID;
                state.timestamp = ++ g_last_timestamp;
            }
        }
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            // Internally the cancel() callback shall unlock the PrintBase::m_status_mutex to let
            // the working thread to proceed.
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to clear it.
            for (StepTypeIterator it = step_begin; it != step_end; ++ it)
                m_state[*it].warnings.clear();
            m_step_active = -1;
        }
        return invalidated;
    }

    // Make all steps invalid.
    // PrintBase::m_state_mutex should be locked at this point, guarding access to m_state.
    // In case any step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate_all(CancelationCallback cancel) {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++ i) {
            StateWithTimeStamp &state = m_state[i];
            if (state.state != INVALID) {
                invalidated = true;
                state.state = INVALID;
                state.timestamp = ++ g_last_timestamp;
            }
        }
        if (invalidated) {
            cancel();
            // Now the worker thread should be stopped, therefore it cannot write into the warnings field.
            // It is safe to clear it.
            for (size_t i = 0; i < COUNT; ++ i)
                m_state[i].warnings.clear();
            m_step_active = -1;
        }
        return invalidated;
    }

    StepType active_step_add_warning(PrintStateBase::WarningLevel warning_level, const std::string &message, tbb::mutex &mtx)
    {
        tbb::mutex::scoped_lock lock(mtx);
        assert(m_step_active != -1);
        assert(m_state[m_step_active].state == STARTED);
        m_state[m_step_active].warnings.emplace_back(PrintStateBase::Warning{ warning_level, message});
        return static_cast<StepType>(m_step_active);
    }

private:
    StateWithWarnings   m_state[COUNT];
    // Active class StepType or -1 if none is active.
    // If the background processing is canceled, m_step_active may not be resetted
    // to -1, see the comment in this->set_started().
    int                 m_step_active = -1;
};

class PrintBase;

class PrintObjectBase : public ObjectID
{
public:
    const ModelObject*      model_object() const    { return m_model_object; }
    ModelObject*            model_object()          { return m_model_object; }

protected:
    PrintObjectBase(ModelObject *model_object) : m_model_object(model_object) {}
    virtual ~PrintObjectBase() {}
    // Declared here to allow access from PrintBase through friendship.
	static tbb::mutex&            state_mutex(PrintBase *print);
	static std::function<void()>  cancel_callback(PrintBase *print);

    ModelObject                  *m_model_object;
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
class PrintBase : public ObjectID
{
public:
	PrintBase() : m_placeholder_parser(&m_full_print_config) { this->restart(); }
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
    virtual ApplyStatus     apply(const Model &model, DynamicPrintConfig config) = 0;
    const Model&            model() const { return m_model; }

    struct TaskParams {
		TaskParams() : single_model_object(0), single_model_instance_only(false), to_object_step(-1), to_print_step(-1) {}
        // If non-empty, limit the processing to this ModelObject.
        ObjectID                single_model_object;
		// If set, only process single_model_object. Otherwise process everything, but single_model_object first.
		bool					single_model_instance_only;
        // If non-negative, stop processing at the successive object step.
        int                     to_object_step;
        // If non-negative, stop processing at the successive print step.
        int                     to_print_step;
    };
    // After calling the apply() function, call set_task() to limit the task to be processed by process().
    virtual void            set_task(const TaskParams &params) {}
    // Perform the calculation. This is the only method that is to be called at a worker thread.
    virtual void            process() = 0;
    // Clean up after process() finished, either with success, error or if canceled.
    // The adjustments on the Print / PrintObject data due to set_task() are to be reverted here.
    virtual void            finalize() {}

    struct SlicingStatus {
		SlicingStatus(int percent, const std::string &text, unsigned int flags = 0) : percent(percent), text(text), flags(flags) {}
        SlicingStatus(const PrintBase &print, int warning_step) : 
            flags(UPDATE_PRINT_STEP_WARNINGS), warning_object_id(print), warning_step(warning_step) {}
        SlicingStatus(const PrintObjectBase &print_object, int warning_step) : 
            flags(UPDATE_PRINT_OBJECT_STEP_WARNINGS), warning_object_id(print_object), warning_step(warning_step) {}
        int             percent { -1 };
        std::string     text;
        // Bitmap of flags.
        enum FlagBits {
            DEFAULT                             = 0,
            RELOAD_SCENE                        = 1 << 1,
            RELOAD_SLA_SUPPORT_POINTS           = 1 << 2,
            RELOAD_SLA_PREVIEW                  = 1 << 3,
            // UPDATE_PRINT_STEP_WARNINGS is mutually exclusive with UPDATE_PRINT_OBJECT_STEP_WARNINGS.
            UPDATE_PRINT_STEP_WARNINGS          = 1 << 4,
            UPDATE_PRINT_OBJECT_STEP_WARNINGS   = 1 << 5
        };
        // Bitmap of FlagBits
        unsigned int    flags;
        // set to an ObjectID of a Print or a PrintObject based on flags
        // (whether UPDATE_PRINT_STEP_WARNINGS or UPDATE_PRINT_OBJECT_STEP_WARNINGS is set).
        ObjectID        warning_object_id;
        // For which Print or PrintObject step a new warning is beeing issued?
        int             warning_step { -1 };
    };
    typedef std::function<void(const SlicingStatus&)>  status_callback_type;
    // Default status console print out in the form of percent => message.
    void                    set_status_default() { m_status_callback = nullptr; }
    // No status output or callback whatsoever, useful mostly for automatic tests.
    void                    set_status_silent() { m_status_callback = [](const SlicingStatus&){}; }
    // Register a custom status callback.
    void                    set_status_callback(status_callback_type cb) { m_status_callback = cb; }
    // Calls a registered callback to update the status, or print out the default message.
    void                    set_status(int percent, const std::string &message, unsigned int flags = SlicingStatus::DEFAULT) {
		if (m_status_callback) m_status_callback(SlicingStatus(percent, message, flags));
        else printf("%d => %s\n", percent, message.c_str());
    }

    typedef std::function<void()>  cancel_callback_type;
    // Various methods will call this callback to stop the background processing (the Print::process() call)
    // in case a successive change of the Print / PrintObject / PrintRegion instances changed
    // the state of the finished or running calculations.
    void                       set_cancel_callback(cancel_callback_type cancel_callback) { m_cancel_callback = cancel_callback; }
    // Has the calculation been canceled?
	enum CancelStatus {
		// No cancelation, background processing should run.
		NOT_CANCELED = 0,
		// Canceled by user from the user interface (user pressed the "Cancel" button or user closed the application).
		CANCELED_BY_USER = 1,
		// Canceled internally from Print::apply() through the Print/PrintObject::invalidate_step() or ::invalidate_all_steps().
		CANCELED_INTERNAL = 2
	};
    CancelStatus               cancel_status() const { return m_cancel_status; }
    // Has the calculation been canceled?
	bool                       canceled() const { return m_cancel_status != NOT_CANCELED; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                       cancel() { m_cancel_status = CANCELED_BY_USER; }
	void                       cancel_internal() { m_cancel_status = CANCELED_INTERNAL; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                       restart() { m_cancel_status = NOT_CANCELED; }
    // Returns true if the last step was finished with success.
    virtual bool               finished() const = 0;

    const PlaceholderParser&   placeholder_parser() const { return m_placeholder_parser; }
    const DynamicPrintConfig&  full_print_config() const { return m_full_print_config; }

    virtual std::string        output_filename(const std::string &filename_base = std::string()) const = 0;
    // If the filename_base is set, it is used as the input for the template processing. In that case the path is expected to be the directory (may be empty).
    // If filename_set is empty, than the path may be a file or directory. If it is a file, then the macro will not be processed.
    std::string                output_filepath(const std::string &path, const std::string &filename_base = std::string()) const;

protected:
	friend class PrintObjectBase;
    friend class BackgroundSlicingProcess;

    tbb::mutex&            state_mutex() const { return m_state_mutex; }
    std::function<void()>  cancel_callback() { return m_cancel_callback; }
	void				   call_cancel_callback() { m_cancel_callback(); }

    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void                   throw_if_canceled() const { if (m_cancel_status) throw CanceledException(); }

    // To be called by this->output_filename() with the format string pulled from the configuration layer.
    std::string            output_filename(const std::string &format, const std::string &default_ext, const std::string &filename_base, const DynamicConfig *config_override = nullptr) const;
    // Update "scale", "input_filename", "input_filename_base" placeholders from the current printable ModelObjects.
    void                   update_object_placeholders(DynamicConfig &config, const std::string &default_ext) const;

	Model                                   m_model;
	DynamicPrintConfig						m_full_print_config;
    PlaceholderParser                       m_placeholder_parser;

    // Callback to be evoked regularly to update state of the UI thread.
    status_callback_type                    m_status_callback;

private:
    tbb::atomic<CancelStatus>               m_cancel_status;

    // Callback to be evoked to stop the background processing before a state is updated.
    cancel_callback_type                    m_cancel_callback = [](){};

    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    mutable tbb::mutex                      m_state_mutex;
};

template<typename PrintStepEnum, const size_t COUNT>
class PrintBaseWithState : public PrintBase
{
public:
    bool            is_step_done(PrintStepEnum step) const { return m_state.is_done(step, this->state_mutex()); }
	PrintStateBase::StateWithTimeStamp step_state_with_timestamp(PrintStepEnum step) const { return m_state.state_with_timestamp(step, this->state_mutex()); }
    PrintStateBase::StateWithWarnings  step_state_with_warnings(PrintStepEnum step) const { return m_state.state_with_warnings(step, this->state_mutex()); }

protected:
    bool            set_started(PrintStepEnum step) { return m_state.set_started(step, this->state_mutex(), [this](){ this->throw_if_canceled(); }); }
	PrintStateBase::TimeStamp set_done(PrintStepEnum step) { return m_state.set_done(step, this->state_mutex(), [this](){ this->throw_if_canceled(); }); }
    bool            invalidate_step(PrintStepEnum step)
		{ return m_state.invalidate(step, this->cancel_callback()); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) 
        { return m_state.invalidate_multiple(step_begin, step_end, this->cancel_callback()); }
    bool            invalidate_steps(std::initializer_list<PrintStepEnum> il) 
        { return m_state.invalidate_multiple(il.begin(), il.end(), this->cancel_callback()); }
    bool            invalidate_all_steps() 
        { return m_state.invalidate_all(this->cancel_callback()); }

	bool            is_step_started_unguarded(PrintStepEnum step) const { return m_state.is_started_unguarded(step); }
	bool            is_step_done_unguarded(PrintStepEnum step) const { return m_state.is_done_unguarded(step); }

    // Add a slicing warning to the active Print step and send a status notification.
    // This method could be called multiple times between this->set_started() and this->set_done().
    void            active_step_add_warning(PrintStateBase::WarningLevel warning_level, const std::string &message) {
        PrintStepEnum active_step = m_state.active_step_add_warning(warning_level, message, this->state_mutex());
        if (m_status_callback) m_status_callback(SlicingStatus(*this, active_step));
        else printf("print warning: %s\n", message.c_str());
    }

private:
    PrintState<PrintStepEnum, COUNT> m_state;
};

template<typename PrintType, typename PrintObjectStepEnum, const size_t COUNT>
class PrintObjectBaseWithState : public PrintObjectBase
{
public:
    PrintType*       print()         { return m_print; }
    const PrintType* print() const   { return m_print; }

    typedef PrintState<PrintObjectStepEnum, COUNT> PrintObjectState;
    bool            is_step_done(PrintObjectStepEnum step) const { return m_state.is_done(step, PrintObjectBase::state_mutex(m_print)); }
    PrintStateBase::StateWithTimeStamp step_state_with_timestamp(PrintObjectStepEnum step) const { return m_state.state_with_timestamp(step, PrintObjectBase::state_mutex(m_print)); }
    PrintStateBase::StateWithWarnings  step_state_with_warnings(PrintObjectStepEnum step) const { return m_state.state_with_warnings(step, PrintObjectBase::state_mutex(m_print)); }

protected:
	PrintObjectBaseWithState(PrintType *print, ModelObject *model_object) : PrintObjectBase(model_object), m_print(print) {}

    bool            set_started(PrintObjectStepEnum step) 
        { return m_state.set_started(step, PrintObjectBase::state_mutex(m_print), [this](){ this->throw_if_canceled(); }); }
	PrintStateBase::TimeStamp set_done(PrintObjectStepEnum step) 
        { return m_state.set_done(step, PrintObjectBase::state_mutex(m_print), [this](){ this->throw_if_canceled(); }); }

    bool            invalidate_step(PrintObjectStepEnum step)
        { return m_state.invalidate(step, PrintObjectBase::cancel_callback(m_print)); }
    template<typename StepTypeIterator>
    bool            invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) 
        { return m_state.invalidate_multiple(step_begin, step_end, PrintObjectBase::cancel_callback(m_print)); }
    bool            invalidate_steps(std::initializer_list<PrintObjectStepEnum> il) 
        { return m_state.invalidate_multiple(il.begin(), il.end(), PrintObjectBase::cancel_callback(m_print)); }
    bool            invalidate_all_steps() 
        { return m_state.invalidate_all(PrintObjectBase::cancel_callback(m_print)); }

    bool            is_step_started_unguarded(PrintObjectStepEnum step) const { return m_state.is_started_unguarded(step); }
    bool            is_step_done_unguarded(PrintObjectStepEnum step) const { return m_state.is_done_unguarded(step); }

    // Add a slicing warning to the active PrintObject step and send a status notification.
    // This method could be called multiple times between this->set_started() and this->set_done().
    void            active_step_add_warning(PrintStateBase::WarningLevel warning_level, const std::string &message) {
        PrintObjectStepEnum active_step = m_state.active_step_add_warning(warning_level, message, PrintObjectBase::state_mutex(m_print));
        if (m_print.m_status_callback) m_print.m_status_callback(SlicingStatus(*this, active_step));
        else printf("print object warning: %s\n", message.c_str());
    }

protected:
    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void            throw_if_canceled() { if (m_print->canceled()) throw CanceledException(); }

    friend PrintType;
    PrintType                               *m_print;

private:
    PrintState<PrintObjectStepEnum, COUNT>   m_state;
};

} // namespace Slic3r

#endif /* slic3r_PrintBase_hpp_ */
