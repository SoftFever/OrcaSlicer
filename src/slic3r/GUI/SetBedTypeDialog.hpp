#ifndef slic3r_GUI_SetBedTypeDialog_hpp_
#define slic3r_GUI_SetBedTypeDialog_hpp_

#include "Plater.hpp"
#include "PartPlate.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RadioBox.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_SET_BED_TYPE_CONFIRM, wxCommandEvent);

class BedTypeRadioBox : public RadioBox
{
public:
    BedTypeRadioBox(wxWindow* parent, BedType bed_type) : RadioBox(parent), m_bed_type(bed_type) {}

    void SetBedType(BedType bed_type) { m_bed_type = bed_type; }
    BedType GetBedType() { return m_bed_type; }

private:
    BedType m_bed_type{ BedType::btCount };
};

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
    BedTypeRadioBox* m_rb_default_plate{ nullptr };
    BedTypeRadioBox* m_rb_cool_plate{ nullptr };
    BedTypeRadioBox* m_rb_eng_plate{ nullptr };
    BedTypeRadioBox* m_rb_high_temp_plate{ nullptr };
    BedTypeRadioBox* m_rb_texture_pei_plate{ nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    std::vector<BedTypeRadioBox*> radio_buttons;

    BedTypeRadioBox* create_item_radiobox(wxString title, wxWindow* parent, wxString tooltip, int padding_left, BedType bed_type);
    void select_curr_radiobox(int btn_idx);
};

}} // namespace Slic3r::GUI

#endif