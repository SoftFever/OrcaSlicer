#ifndef IPROGRESSINDICATOR_HPP
#define IPROGRESSINDICATOR_HPP

#include <string>
#include <functional>

namespace Slic3r {

/**
 * @brief Generic progress indication interface.
 */
class ProgressIndicator {
public:
    using CancelFn = std::function<void(void)>; // Cancel function signature.

private:
    float m_state = .0f, m_max = 1.f, m_step;
    CancelFn m_cancelfunc = [](){};

public:

    inline virtual ~ProgressIndicator() {}

    /// Get the maximum of the progress range.
    float max() const { return m_max; }

    /// Get the current progress state
    float state() const { return m_state; }

    /// Set the maximum of the progress range
    virtual void max(float maxval) { m_max = maxval; }

    /// Set the current state of the progress.
    virtual void state(float val)  { m_state = val; }

    /**
     * @brief Number of states int the progress. Can be used instead of giving a
     * maximum value.
     */
    virtual void states(unsigned statenum) {
        m_step = m_max / statenum;
    }

    /// Message shown on the next status update.
    virtual void message(const std::string&) = 0;

    /// Title of the operation.
    virtual void title(const std::string&) = 0;

    /// Formatted message for the next status update. Works just like sprintf.
    virtual void message_fmt(const std::string& fmt, ...);

    /// Set up a cancel callback for the operation if feasible.
    virtual void on_cancel(CancelFn func = CancelFn()) { m_cancelfunc = func; }

    /**
     * Explicitly shut down the progress indicator and call the associated
     * callback.
     */
    virtual void cancel() { m_cancelfunc(); }

    /// Convenience function to call message and status update in one function.
    void update(float st, const std::string& msg) {
        message(msg); state(st);
    }
};

}

#endif // IPROGRESSINDICATOR_HPP
