#ifndef slic3r_GUI_PlateSettingsDialog_hpp_
#define slic3r_GUI_PlateSettingsDialog_hpp_

#include "Plater.hpp"
#include "PartPlate.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "DragCanvas.hpp"

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
        const wxString& title = wxEmptyString,
        bool only_first_layer_seq = false,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION
    );

    ~PlateSettingsDialog();
    void sync_bed_type(BedType type);
    void sync_print_seq(int print_seq = 0);
    void sync_first_layer_print_seq(int selection, const std::vector<int>& seq = std::vector<int>());
    void sync_spiral_mode(bool spiral_mode, bool as_global);
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

    int get_first_layer_print_seq_choice() {
        int choice = 0;
        if (m_first_layer_print_seq_choice != nullptr)
            choice = m_first_layer_print_seq_choice->GetSelection();
        return choice;
    };

    std::vector<int> get_first_layer_print_seq();

    int get_spiral_mode_choice() {
        int choice = 0;
        if (m_spiral_mode_choice != nullptr)
            choice = m_spiral_mode_choice->GetSelection();
        return choice;
    };

    bool get_spiral_mode(){
        return false;
    }

protected:
    DragCanvas* m_drag_canvas;
    ComboBox* m_first_layer_print_seq_choice { nullptr };
    ComboBox* m_print_seq_choice { nullptr };
    ComboBox* m_bed_type_choice { nullptr };
    ComboBox* m_spiral_mode_choice { nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    TextInput *m_ti_plate_name;
};

class PlateNameEditDialog : public DPIDialog
{
public:
    enum ButtonStyle { ONLY_CONFIRM = 0, CONFIRM_AND_CANCEL = 1, MAX_STYLE_NUM = 2 };
    PlateNameEditDialog(wxWindow *      parent,
                        wxWindowID      id    = wxID_ANY,
                        const wxString &title = wxEmptyString,
                        const wxPoint & pos   = wxDefaultPosition,
                        const wxSize &  size  = wxDefaultSize,
                        long            style = wxCLOSE_BOX | wxCAPTION);

    ~PlateNameEditDialog();
    void     on_dpi_changed(const wxRect &suggested_rect) override;

    wxString get_plate_name() const;
    void     set_plate_name(const wxString &name);

protected:
    Button *   m_button_ok;
    Button *   m_button_cancel;
    TextInput *m_ti_plate_name;
};
}} // namespace Slic3r::GUI

#endif