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
    CancelFn cancelfunc_ = [](){};

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

    /**
     * Explicitly shut down the progress indicator and call the associated
     * callback.
     */
    virtual void cancel() { cancelfunc_(); }

    /// Convinience function to call message and status update in one function.
    void update(float st, const std::string& msg) {
        message(msg); state(st);
    }
};

}

#endif // IPROGRESSINDICATOR_HPP
