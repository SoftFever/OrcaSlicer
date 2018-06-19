#ifndef APPCONTROLLER_HPP
#define APPCONTROLLER_HPP

#include <string>
#include <vector>
#include <memory>

namespace Slic3r {

class Model;
class Print;
class PrintObject;

class AppControllerBoilerplate {
public:

    using Path = std::string;
    using PathList = std::vector<Path>;

    enum class IssueType {
        INFO,
        WARN,
        ERR,
        FATAL
    };

    PathList query_destination_paths(
            const std::string& title,
            const std::string& extensions) const;

    PathList query_destination_dirs(
            const std::string& title) const;

    Path query_destination_path(
            const std::string& title,
            const std::string& extensions) const;

    void report_issue(IssueType issuetype,
                      const std::string& description,
                      const std::string& brief = "");

    class ProgressIndicator {
        float state_ = .0f, max_ = 1.f, step_;
    public:

        inline virtual ~ProgressIndicator() {}

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

        template<class T>
        void update(T st, const std::string& msg) {
            message(msg); state(st);
        }
    };

    using ProgresIndicatorPtr = std::shared_ptr<ProgressIndicator>;

    ProgresIndicatorPtr createProgressIndicator(
            unsigned statenum,
            const std::string& title,
            const std::string& firstmsg = "") const;

    inline void globalProgressIndicator(ProgresIndicatorPtr progrind) {
        glob_progressind_ = progrind;
    }

    inline ProgresIndicatorPtr globalProgressIndicator() {
        if(!glob_progressind_)
            glob_progressind_ = createProgressIndicator(100, "Progress");

        return glob_progressind_;
    }

private:
    ProgresIndicatorPtr glob_progressind_;
};

class AppController: protected AppControllerBoilerplate {
    Model *model_ = nullptr; Print *print_ = nullptr;

    void sliceObject(PrintObject *pobj);

public:

    void slice();

    void slice_to_png();

    void set_model(Model *model) { model_ = model; }

    void set_print(Print *print) { print_ = print; }

    void set_global_progress_indicator_id(unsigned gauge_id,
                                          unsigned statusbar_id);
};

}

#endif // APPCONTROLLER_HPP
