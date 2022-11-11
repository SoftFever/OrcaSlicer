#ifndef slic3r_GUI_SetBedTypeDialog_hpp_
#define slic3r_GUI_SetBedTypeDialog_hpp_

#include "Plater.hpp"
#include "PartPlate.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RadioBox.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_SET_BED_TYPE_CONFIRM, wxCommandEvent);

class SetBedTypeDialog : public DPIDialog
{
public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    SetBedTypeDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION
    );

    ~SetBedTypeDialog();
    void sync_bed_type(BedType type);
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    wxWindow* m_cool_btn;
    wxWindow* m_engineering_btn;
    wxWindow* m_high_temp_btn;
    wxWindow* m_texture_pei_btn;
    Button* m_button_ok;
    Button* m_button_cancel;
    std::vector<RadioBox*> radio_buttons;

    wxWindow *  create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, std::string param);
    void select_curr_radiobox(int btn_idx);
};

}} // namespace Slic3r::GUI

#endif