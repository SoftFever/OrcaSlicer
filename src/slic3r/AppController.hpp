#ifndef APPCONTROLLER_HPP
#define APPCONTROLLER_HPP

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <iostream>

#include "GUI/ProgressIndicator.hpp"

#include <PrintConfig.hpp>

namespace Slic3r {

class Model;
class Print;
class PrintObject;
class PrintConfig;
class ProgressStatusBar;
class DynamicPrintConfig;

/// A Progress indicator object smart pointer
using ProgresIndicatorPtr = std::shared_ptr<ProgressIndicator>;

using FilePath = std::string;
using FilePathList = std::vector<FilePath>;

/// Common runtime issue types
enum class IssueType {
    INFO,
    WARN,
    WARN_Q,     // Warning with a question to continue
    ERR,
    FATAL
};

/**
 * @brief A boilerplate class for creating application logic. It should provide
 * features as issue reporting and progress indication, etc...
 *
 * The lower lever UI independent classes can be manipulated with a subclass
 * of this controller class. We can also catch any exceptions that lower level
 * methods could throw and display appropriate errors and warnings.
 *
 * Note that the outer and the inner interface of this class is free from any
 * UI toolkit dependencies. We can implement it with any UI framework or make it
 * a cli client.
 */
class AppControllerBase {
public:

    using Ptr = std::shared_ptr<AppControllerBase>;

    inline virtual ~AppControllerBase() {}

    /**
     * @brief Query some paths from the user.
     *
     * It should display a file chooser dialog in case of a UI application.
     * @param title Title of a possible query dialog.
     * @param extensions Recognized file extensions.
     * @return Returns a list of paths chosen by the user.
     */
    virtual FilePathList query_destination_paths(
            const std::string& title,
            const std::string& extensions,
            const std::string& functionid = "",
            const std::string& hint = "") const = 0;

    /**
     * @brief Same as query_destination_paths but works for directories only.
     */
    virtual FilePathList query_destination_dirs(
            const std::string& title,
            const std::string& functionid = "",
            const std::string& hint = "") const = 0;

    /**
     * @brief Same as query_destination_paths but returns only one path.
     */
    virtual FilePath query_destination_path(
            const std::string& title,
            const std::string& extensions,
            const std::string& functionid = "",
            const std::string& hint = "") const = 0;

    /**
     * @brief Report an issue to the user be it fatal or recoverable.
     *
     * In a UI app this should display some message dialog.
     *
     * @param issuetype The type of the runtime issue.
     * @param description A somewhat longer description of the issue.
     * @param brief A very brief description. Can be used for message dialog
     * title.
     */
    virtual bool report_issue(IssueType issuetype,
                              const std::string& description,
                              const std::string& brief) = 0;

    /**
     * @brief Return the global progress indicator for the current controller.
     * Can be empty as well.
     *
     * Only one thread should use the global indicator at a time.
     */
    virtual ProgresIndicatorPtr global_progress_indicator() = 0;

    virtual void global_progress_indicator(ProgresIndicatorPtr gpri) = 0;

    /**
     * @brief A predicate telling the caller whether it is the thread that
     * created the AppConroller object itself. This probably means that the
     * execution is in the UI thread. Otherwise it returns false meaning that
     * some worker thread called this function.
     * @return Return true for the same caller thread that created this
     * object and false for every other.
     */
    virtual bool is_main_thread() const = 0;

    /**
     * @brief The frontend supports asynch execution.
     *
     * A Graphic UI will support this, a CLI may not. This can be used in
     * subclass methods to decide whether to start threads for block free UI.
     *
     * Note that even a progress indicator's update called regularly can solve
     * the blocking UI problem in some cases even when an event loop is present.
     * This is how wxWidgets gauge work but creating a separate thread will make
     * the UI even more fluent.
     *
     * @return true if a job or method can be executed asynchronously, false
     * otherwise.
     */
    virtual bool supports_asynch() const = 0;

    virtual void process_events() = 0;

    /**
     * @brief Create a new progress indicator and return a smart pointer to it.
     * @param statenum The number of states for the given procedure.
     * @param title The title of the procedure.
     * @param firstmsg The message for the first subtask to be displayed.
     * @return Smart pointer to the created object.
     */
    virtual ProgresIndicatorPtr create_progress_indicator(
            unsigned statenum,
            const std::string& title,
            const std::string& firstmsg = "") const = 0;
};

/**
 * @brief Implementation of AppControllerBase for the GUI app
 */
class AppControllerGui: public AppControllerBase {
private:
    class PriData;   // Some structure to store progress indication data

    // Pimpl data for thread safe progress indication features
    std::unique_ptr<PriData> m_pri_data;

public:

    AppControllerGui();

    virtual ~AppControllerGui();

    virtual FilePathList query_destination_paths(
            const std::string& title,
            const std::string& extensions,
            const std::string& functionid,
            const std::string& hint) const override;

    virtual FilePathList query_destination_dirs(
            const std::string& /*title*/,
            const std::string& /*functionid*/,
            const std::string& /*hint*/) const override { return {}; }

    virtual FilePath query_destination_path(
            const std::string& title,
            const std::string& extensions,
            const std::string& functionid,
            const std::string& hint) const override;

    virtual bool report_issue(IssueType issuetype,
                              const std::string& description,
                              const std::string& brief = std::string()) override;

    virtual ProgresIndicatorPtr global_progress_indicator() override;

    virtual void global_progress_indicator(ProgresIndicatorPtr gpri) override;

    virtual bool is_main_thread() const override;

    virtual bool supports_asynch() const override;

    virtual void process_events() override;

    virtual ProgresIndicatorPtr create_progress_indicator(
            unsigned statenum,
            const std::string& title,
            const std::string& firstmsg) const override;

protected:

    // This is a global progress indicator placeholder. In the Slic3r UI it can
    // contain the progress indicator on the statusbar.
    ProgresIndicatorPtr m_global_progressind;
};

class AppControllerCli: public AppControllerBase {

    class CliProgress : public ProgressIndicator {
        std::string m_msg, m_title;
    public:
        virtual void message(const std::string& msg) override {
            m_msg = msg;
        }

        virtual void title(const std::string& title) override {
            m_title = title;
        }
    };

public:

    AppControllerCli() {
        std::cout << "Cli AppController ready..." << std::endl;
        m_global_progressind = std::make_shared<CliProgress>();
    }

    virtual ~AppControllerCli() {}

    virtual FilePathList query_destination_paths(
            const std::string& /*title*/,
            const std::string& /*extensions*/,
            const std::string& /*functionid*/,
            const std::string& /*hint*/) const override { return {}; }

    virtual FilePathList query_destination_dirs(
            const std::string& /*title*/,
            const std::string& /*functionid*/,
            const std::string& /*hint*/) const override { return {}; }

    virtual FilePath query_destination_path(
            const std::string& /*title*/,
            const std::string& /*extensions*/,
            const std::string& /*functionid*/,
            const std::string& /*hint*/) const override { return "out.zip"; }

    virtual bool report_issue(IssueType /*issuetype*/,
                              const std::string& description,
                              const std::string& brief) override {
        std::cerr << brief << ": " << description << std::endl;
        return true;
    }

    virtual ProgresIndicatorPtr global_progress_indicator() override {
        return m_global_progressind;
    }

    virtual void global_progress_indicator(ProgresIndicatorPtr) override {}

    virtual bool is_main_thread() const override { return true; }

    virtual bool supports_asynch() const override { return false; }

    virtual void process_events() override {}

    virtual ProgresIndicatorPtr create_progress_indicator(
            unsigned /*statenum*/,
            const std::string& /*title*/,
            const std::string& /*firstmsg*/) const override {
        return std::make_shared<CliProgress>();
    }

protected:

    // This is a global progress indicator placeholder. In the Slic3r UI it can
    // contain the progress indicator on the statusbar.
    ProgresIndicatorPtr m_global_progressind;
};

class Zipper {
    struct Impl;
    std::unique_ptr<Impl> m_impl;
public:

    Zipper(const std::string& zipfilepath);
    ~Zipper();

    void next_entry(const std::string& fname);

    std::string get_name() const;

    std::ostream& stream();

    void close();
};

/**
 * @brief Implementation of the printing logic.
 */
class PrintController {
    Print *m_print = nullptr;
    std::function<void()> m_rempools;
protected:

    // Data structure with the png export input data
    struct PngExportData {
        std::string zippath;                        // output zip file
        unsigned long width_px = 1440;              // resolution - rows
        unsigned long height_px = 2560;             // resolution columns
        double width_mm = 68.0, height_mm = 120.0;  // dimensions in mm
        double exp_time_first_s = 35.0;             // first exposure time
        double exp_time_s = 8.0;                    // global exposure time
        double corr_x = 1.0;                        // offsetting in x
        double corr_y = 1.0;                        // offsetting in y
        double corr_z = 1.0;                        // offsetting in y
    };

    // Should display a dialog with the input fields for printing to png
    PngExportData query_png_export_data(const DynamicPrintConfig&);

    // The previous export data, to pre-populate the dialog
    PngExportData m_prev_expdata;

    void slice(ProgresIndicatorPtr pri);

public:

    // Must be public for perl to use it
    explicit inline PrintController(Print *print): m_print(print) {}

    PrintController(const PrintController&) = delete;
    PrintController(PrintController&&) = delete;

    using Ptr = std::unique_ptr<PrintController>;

    inline static Ptr create(Print *print) {
        return PrintController::Ptr( new PrintController(print) );
    }

    /**
     * @brief Slice the loaded print scene.
     */
    void slice();

    /**
     * @brief Slice the print into zipped png files.
     */
    void slice_to_png();

    const PrintConfig& config() const;
};

/**
 * @brief Top level controller.
 */
class AppController {
    Model *m_model = nullptr;
    PrintController::Ptr printctl;
    std::atomic<bool> m_arranging;
public:

    /**
     * @brief Get the print controller object.
     *
     * @return Return a raw pointer instead of a smart one for perl to be able
     * to use this function and access the print controller.
     */
    PrintController * print_ctl() { return printctl.get(); }

    /**
     * @brief Set a model object.
     *
     * @param model A raw pointer to the model object. This can be used from
     * perl.
     */
    void set_model(Model *model) { m_model = model; }

    /**
     * @brief Set the print object from perl.
     *
     * This will create a print controller that will then be accessible from
     * perl.
     * @param print A print object which can be a perl-ish extension as well.
     */
    void set_print(Print *print) {
        printctl = PrintController::create(print);
    }

    /**
     * @brief Set up a global progress indicator.
     *
     * In perl we have a progress indicating status bar on the bottom of the
     * window which is defined and created in perl. We can pass the ID-s of the
     * gauge and the statusbar id and make a wrapper implementation of the
     * ProgressIndicator interface so we can use this GUI widget from C++.
     *
     * This function should be called from perl.
     *
     * @param gauge_id The ID of the gague widget of the status bar.
     * @param statusbar_id The ID of the status bar.
     */
    void set_global_progress_indicator(ProgressStatusBar *prs);

    void arrange_model();
};

}

#endif // APPCONTROLLER_HPP
