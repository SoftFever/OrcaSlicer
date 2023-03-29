#ifndef slic3r_GUI_PlateSettingsDialog_hpp_
#define slic3r_GUI_PlateSettingsDialog_hpp_

#include "Plater.hpp"
#include "PartPlate.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/ComboBox.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_SET_BED_TYPE_CONFIRM, wxCommandEvent);

class PlateSettingsDialog : public DPIDialog
{
public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    PlateSettingsDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION
    );

    ~PlateSettingsDialog();
    void sync_bed_type(BedType type);
    void sync_print_seq(int print_seq = 0);
    wxString to_bed_type_name(BedType bed_type);
    wxString to_print_sequence_name(PrintSequence print_seq);
    void on_dpi_changed(const wxRect& suggested_rect) override;

    int get_print_seq_choice() {
        int choice = 0;
        if (m_print_seq_choice != nullptr)
            choice =  m_print_seq_choice->GetSelection();
        return choice;
    };

    int get_bed_type_choice() {
        int choice = 0;
        if (m_bed_type_choice != nullptr)
            choice =  m_bed_type_choice->GetSelection();
        return choice;
    };

    wxString get_plate_name() const;
    void set_plate_name(const wxString& name);

protected:
    ComboBox* m_print_seq_choice { nullptr };
    ComboBox* m_bed_type_choice { nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    TextInput *m_ti_plate_name;
};

}} // namespace Slic3r::GUI

#endif