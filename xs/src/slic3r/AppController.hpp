#ifndef APPCONTROLLER_HPP
#define APPCONTROLLER_HPP

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <iostream>

#include "IProgressIndicator.hpp"

namespace Slic3r {

class Model;
class Print;
class PrintObject;

class AppControllerBoilerplate {
    class PriMap;

public:
    using ProgresIndicatorPtr = std::shared_ptr<IProgressIndicator>;

private:
    std::unique_ptr<PriMap> progressind_;

public:

    AppControllerBoilerplate();
    ~AppControllerBoilerplate();

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
            const std::string& extensions,
            const std::string& hint = "") const;

    void report_issue(IssueType issuetype,
                      const std::string& description,
                      const std::string& brief = "");

    void progress_indicator(ProgresIndicatorPtr progrind);

    void progress_indicator(unsigned statenum,
                                  const std::string& title,
                                  const std::string& firstmsg = "");

    ProgresIndicatorPtr progress_indicator();

protected:

    ProgresIndicatorPtr create_progress_indicator(
            unsigned statenum,
            const std::string& title,
            const std::string& firstmsg = "") const;

    bool is_main_thread() const;

    ProgresIndicatorPtr global_progressind_;
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
        printctl->progress_indicator(progress_indicator());
    }

    void set_global_progress_indicator_id(unsigned gauge_id,
                                          unsigned statusbar_id);
};

}

#endif // APPCONTROLLER_HPP
