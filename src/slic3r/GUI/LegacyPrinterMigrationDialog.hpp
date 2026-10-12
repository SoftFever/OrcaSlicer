#pragma once

#include "GUI_Utils.hpp"

#include <vector>

class CheckBox;
class wxWindow;
class wxRect;

namespace Slic3r {

struct LegacyPrinterMigration;

namespace GUI {

// Lists the user filament presets that no longer show on printers their parent listed in an older
// release, and asks whether to migrate them. ShowModal() returns wxID_YES to migrate. With
// offer_dont_remind it also offers to stop the startup reminder for good.
class LegacyPrinterMigrationDialog : public DPIDialog
{
public:
    LegacyPrinterMigrationDialog(wxWindow *parent, const std::vector<LegacyPrinterMigration> &migrations, bool offer_dont_remind);

    bool dont_remind() const;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    ::CheckBox *m_dont_remind{nullptr};
};

}} // namespace Slic3r::GUI
