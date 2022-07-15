#include "HMSPanel.hpp"
#include <slic3r/GUI/Widgets/Label.hpp>
#include <slic3r/GUI/I18N.hpp>
#include "GUI.hpp"

namespace Slic3r {
namespace GUI {


HMSPanel::HMSPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    :wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(238, 238, 238));

    auto m_main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);

    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    m_hms_content = new wxTextCtrl(m_scrolledWindow, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_AUTO_URL | wxTE_MULTILINE);

    m_top_sizer->Add(m_hms_content, 1, wxALL | wxEXPAND, 0);

    m_scrolledWindow->SetSizerAndFit(m_top_sizer);

    m_main_sizer->Add(m_scrolledWindow, 1, wxALIGN_CENTER_HORIZONTAL | wxEXPAND, 0);

    this->SetSizerAndFit(m_main_sizer);


    Layout();
}

HMSPanel::~HMSPanel() {

}

void HMSPanel::update(MachineObject *obj)
{
    if (obj) {
        wxString hms_text;
        for (auto item : obj->hms_list) {
            hms_text += wxString::Format("Module_ID = %s, module_num = %d,part_id = %d, msg level = %s msg code: 0x%x\n",
                    HMSItem::get_module_name(item.module_id),
                    item.module_num,
                    item.part_id,
                    HMSItem::get_hms_msg_level_str(item.msg_level),
                    (unsigned)item.msg_code);
        }
        m_hms_content->SetLabelText(hms_text);
    } else {
        m_hms_content->SetLabelText("");
    }
}

bool HMSPanel::Show(bool show)
{
    return wxPanel::Show(show);
}

}
}