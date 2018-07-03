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
class PrintConfig;


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
class AppControllerBoilerplate {
public:

    /// A Progress indicator object smart pointer
    using ProgresIndicatorPtr = std::shared_ptr<IProgressIndicator>;

private:
    class PriData;   // Some structure to store progress indication data

    // Pimpl data for thread safe progress indication features
    std::unique_ptr<PriData> pri_data_;

public:

    AppControllerBoilerplate();
    ~AppControllerBoilerplate();

    using Path = string;
    using PathList = std::vector<Path>;

    /// Common runtime issue types
    enum class IssueType {
        INFO,
        WARN,
        WARN_Q,     // Warning with a question to continue
        ERR,
        FATAL
    };

    /**
     * @brief Query some paths from the user.
     *
     * It should display a file chooser dialog in case of a UI application.
     * @param title Title of a possible query dialog.
     * @param extensions Recognized file extensions.
     * @return Returns a list of paths choosed by the user.
     */
    PathList query_destination_paths(
            const string& title,
            const std::string& extensions) const;

    /**
     * @brief Same as query_destination_paths but works for directories only.
     */
    PathList query_destination_dirs(
            const string& title) const;

    /**
     * @brief Same as query_destination_paths but returns only one path.
     */
    Path query_destination_path(
            const string& title,
            const std::string& extensions,
            const std::string& hint = "") const;

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
    bool report_issue(IssueType issuetype,
                      const string& description,
                      const string& brief);

    bool report_issue(IssueType issuetype,
                      const string& description);

    /**
     * @brief Return the global progress indicator for the current controller.
     * Can be empty as well.
     *
     * Only one thread should use the global indicator at a time.
     */
    ProgresIndicatorPtr global_progress_indicator();

    void global_progress_indicator(ProgresIndicatorPtr gpri);

    /**
     * @brief A predicate telling the caller whether it is the thread that
     * created the AppConroller object itself. This probably means that the
     * execution is in the UI thread. Otherwise it returns false meaning that
     * some worker thread called this function.
     * @return Return true for the same caller thread that created this
     * object and false for every other.
     */
    bool is_main_thread() const;

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
    bool supports_asynch() const;

    void process_events();

protected:

    /**
     * @brief Create a new progress indicator and return a smart pointer to it.
     * @param statenum The number of states for the given procedure.
     * @param title The title of the procedure.
     * @param firstmsg The message for the first subtask to be displayed.
     * @return Smart pointer to the created object.
     */
    ProgresIndicatorPtr create_progress_indicator(
            unsigned statenum,
            const string& title,
            const string& firstmsg) const;

    ProgresIndicatorPtr create_progress_indicator(
            unsigned statenum,
            const string& title) const;

    // This is a global progress indicator placeholder. In the Slic3r UI it can
    // contain the progress indicator on the statusbar.
    ProgresIndicatorPtr global_progressind_;
};

/**
 * @brief Implementation of the printing logic.
 */
class PrintController: public AppControllerBoilerplate {
    Print *print_ = nullptr;
protected:

    void make_skirt();
    void make_brim();
    void make_wipe_tower();

    void make_perimeters(PrintObject *pobj);
    void infill(PrintObject *pobj);
    void gen_support_material(PrintObject *pobj);

    /**
     * @brief Slice one pront object.
     * @param pobj The print object.
     */
    void slice(PrintObject *pobj);

    void slice(ProgresIndicatorPtr pri);

public:

    // Must be public for perl to use it
    explicit inline PrintController(Print *print): print_(print) {}

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

    const PrintConfig& config() const;
};

/**
 * @brief Top level controller.
 */
class AppController: public AppControllerBoilerplate {
    Model *model_ = nullptr;
    PrintController::Ptr printctl;
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
    void set_model(Model *model) { model_ = model; }

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
     * IProgressIndicator interface so we can use this GUI widget from C++.
     *
     * This function should be called from perl.
     *
     * @param gauge_id The ID of the gague widget of the status bar.
     * @param statusbar_id The ID of the status bar.
     */
    void set_global_progress_indicator(unsigned gauge_id,
                                          unsigned statusbar_id);

    void arrange_model();
};

}

#endif // APPCONTROLLER_HPP
