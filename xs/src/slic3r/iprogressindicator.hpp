#ifndef IPROGRESSINDICATOR_HPP
#define IPROGRESSINDICATOR_HPP

#include <string>
#include <functional>

namespace Slic3r {

class IProgressIndicator {
public:
    using CancelFn = std::function<void(void)>;

private:
    float state_ = .0f, max_ = 1.f, step_;
    std::function<void(void)> cancelfunc_ = [](){};

public:

    inline virtual ~IProgressIndicator() {}

    float max() const { return max_; }
    float state() const { return state_; }

    virtual void max(float maxval) { max_ = maxval; }
    virtual void state(float val)  { state_ = val; }
    virtual void state(unsigned st) { state_ = st * step_; }
    virtual void states(unsigned statenum) {
        step_ = max_ / statenum;
    }

    virtual void message(const std::string&) = 0;
    virtual void title(const std::string&) = 0;

    virtual void message_fmt(const std::string& fmt, ...);

    inline void on_cancel(CancelFn func) { cancelfunc_ = func; }
    inline void on_cancel() { cancelfunc_(); }

    template<class T> void update(T st, const std::string& msg) {
        message(msg); state(st);
    }
};

}

#endif // IPROGRESSINDICATOR_HPP
