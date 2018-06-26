#ifndef IPROGRESSINDICATOR_HPP
#define IPROGRESSINDICATOR_HPP

#include <string>
#include <functional>

namespace Slic3r {

/**
 * @brief Generic progress indication interface.
 */
class IProgressIndicator {
public:
    using CancelFn = std::function<void(void)>; // Cancel functio signature.

private:
    float state_ = .0f, max_ = 1.f, step_;
    std::function<void(void)> cancelfunc_ = [](){};
    unsigned proc_count_ = 1;

public:

    inline virtual ~IProgressIndicator() {}

    /// Get the maximum of the progress range.
    float max() const { return max_; }

    /// Get the current progress state
    float state() const { return state_; }

    /// Set the maximum of hte progress range
    virtual void max(float maxval) { max_ = maxval; }

    /// Set the current state of the progress.
    virtual void state(float val)  { state_ = val; }

    /**
     * @brief Number of states int the progress. Can be used insted of giving a
     * maximum value.
     */
    virtual void states(unsigned statenum) {
        step_ = max_ / statenum;
    }

    /// Message shown on the next status update.
    virtual void message(const std::string&) = 0;

    /// Title of the operaton.
    virtual void title(const std::string&) = 0;

    /// Formatted message for the next status update. Works just like sprinf.
    virtual void message_fmt(const std::string& fmt, ...);

    /// Set up a cancel callback for the operation if feasible.
    inline void on_cancel(CancelFn func) { cancelfunc_ = func; }

    /// Call a previously specified cancel callback.
    inline void on_cancel() { cancelfunc_(); }

    /**
     * \brief Set up how many subprocedures does the whole operation contain.
     *
     * This was neccesary from practical reasons. If the progress indicator is
     * a dialog and we want to show the progress of a few sub operations than
     * the dialog wont be closed and reopened each time a new sub operation is
     * started. This is not a mandatory feature and can be ignored completely.
     */
    inline void procedure_count(unsigned pc) { proc_count_ = pc; }

    /// Get the current procedure count
    inline unsigned procedure_count() const { return proc_count_; }

    /// Convinience function to call message and status update in one function.
    void update(float st, const std::string& msg) {
        message(msg); state(st);
    }
};

}

#endif // IPROGRESSINDICATOR_HPP
