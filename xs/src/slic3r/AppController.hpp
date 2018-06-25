#ifndef APPCONTROLLER_HPP
#define APPCONTROLLER_HPP

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <thread>
#include <unordered_map>

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

        virtual void messageFmt(const std::string& fmt, ...);

        template<class T>
        void update(T st, const std::string& msg) {
            message(msg); state(st);
        }
    };

    using ProgresIndicatorPtr = std::shared_ptr<ProgressIndicator>;

    inline void progressIndicator(ProgresIndicatorPtr progrind) {
        progressind_[std::this_thread::get_id()] = progrind;
    }

    inline void progressIndicator(unsigned statenum,
                                  const std::string& title,
                                  const std::string& firstmsg = "") {
        progressind_[std::this_thread::get_id()] =
                createProgressIndicator(statenum, title, firstmsg);
    }

    inline ProgresIndicatorPtr progressIndicator() {

        ProgresIndicatorPtr ret;
        if( progressind_.find(std::this_thread::get_id()) == progressind_.end())
            progressind_[std::this_thread::get_id()] = ret =
                    = createProgressIndicator(100, "Progress");

        return ret;
    }

protected:

    ProgresIndicatorPtr createProgressIndicator(
            unsigned statenum,
            const std::string& title,
            const std::string& firstmsg = "") const;

private:
    std::unordered_map<std::thread::id, ProgresIndicatorPtr> progressind_;
};

class PrintController: public AppControllerBoilerplate {
    Print *print_ = nullptr;
protected:

    void make_skirt();
    void make_brim();
    void make_wipe_tower();

    void make_perimeters(PrintObject *pobj);
    void infill(PrintObject *pobj);
    void gen_support_material(PrintObject *pobj);

public:

    using Ptr = std::unique_ptr<PrintController>;

    explicit inline PrintController(Print *print): print_(print) {}

    inline static Ptr create(Print *print) {
        return std::make_unique<PrintController>(print);
    }

    void slice(PrintObject *pobj);

    void slice();
    void slice_to_png();
};

class AppController: protected AppControllerBoilerplate {
    Model *model_ = nullptr;
    PrintController::Ptr printctl;
public:

    PrintController * print_ctl() { return printctl.get(); }

    void set_model(Model *model) { model_ = model; }

    void set_print(Print *print) {
        printctl = PrintController::create(print);
        printctl->progressIndicator(progressIndicator());
    }

    void set_global_progress_indicator_id(unsigned gauge_id,
                                          unsigned statusbar_id);
};

}

#endif // APPCONTROLLER_HPP
