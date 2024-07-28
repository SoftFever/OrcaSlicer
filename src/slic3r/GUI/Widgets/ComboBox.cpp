#include "ComboBox.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(ComboBox, TextInput)

EVT_LEFT_DOWN(ComboBox::mouseDown)
EVT_LEFT_DCLICK(ComboBox::mouseDown)
//EVT_MOUSEWHEEL(ComboBox::mouseWheelMoved)
EVT_KEY_DOWN(ComboBox::keyDown)

// catch paint events
END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

static wxWindow *GetScrollParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    while (pWin->GetParent()) {
        auto pWin2 = pWin->GetParent();
        if (auto top = dynamic_cast<wxScrollHelper *>(pWin2))
            return dynamic_cast<wxWindow *>(pWin);
        pWin = pWin2;
    }
    return nullptr;
}

ComboBox::ComboBox(wxWindow *parent,
                   wxWindowID      id,
                   const wxString &value,
                   const wxPoint & pos,
                   const wxSize &  size,
                   int             n,
                   const wxString  choices[],
                   long            style)
    : drop(texts, tips, icons)
{
    if (style & wxCB_READONLY)
        style |= wxRIGHT;
    text_off = style & CB_NO_TEXT;
    TextInput::Create(parent, "", value, (style & CB_NO_DROP_ICON) ? "" : "drop_down", pos, size,
                      style | wxTE_PROCESS_ENTER);
    drop.Create(this, style & DD_STYLE_MASK);

    if (style & wxCB_READONLY) {
        GetTextCtrl()->Hide();
        TextInput::SetFont(Label::Body_14);
        TextInput::SetBorderColor(StateColor(std::make_pair(0xDBDBDB, (int) StateColor::Disabled),
            std::make_pair(0x009688, (int) StateColor::Hovered),
            std::make_pair(0xDBDBDB, (int) StateColor::Normal)));
        TextInput::SetBackgroundColor(StateColor(std::make_pair(0xF0F0F1, (int) StateColor::Disabled),
            std::make_pair(0xE5F0EE, (int) StateColor::Focused), // ORCA updated background color for focused item
            std::make_pair(*wxWHITE, (int) StateColor::Normal)));
        TextInput::SetLabelColor(StateColor(
            std::make_pair(0x6B6B6B, (int) StateColor::Disabled), // ORCA: Use same color for disabled text on combo boxes
            std::make_pair(0x262E30, (int) StateColor::Normal)));
    }
    if (auto scroll = GetScrollParent(this))
        scroll->Bind(wxEVT_MOVE, &ComboBox::onMove, this);
    drop.Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        SetSelection(e.GetInt());
        e.SetEventObject(this);
        e.SetId(GetId());
        GetEventHandler()->ProcessEvent(e);
    });
    drop.Bind(EVT_DISMISS, [this](auto &) {
        drop_down = false;
        wxCommandEvent e(wxEVT_COMBOBOX_CLOSEUP);
        GetEventHandler()->ProcessEvent(e);
    });
    for (int i = 0; i < n; ++i) Append(choices[i]);
}

int ComboBox::GetSelection() const { return drop.GetSelection(); }

void ComboBox::SetSelection(int n)
{
    if (n == drop.selection)
        return;
    drop.SetSelection(n);
    SetLabel(drop.GetValue());
    if (drop.selection >= 0 && drop.iconSize.y > 0)
        SetIcon(icons[drop.selection].IsNull() ? create_scaled_bitmap("drop_down", nullptr, 16): icons[drop.selection]); // ORCA fix combo boxes without arrows
}
void ComboBox::SelectAndNotify(int n) { 
    SetSelection(n);
    sendComboBoxEvent();
}

void ComboBox::Rescale()
{
    TextInput::Rescale();
    drop.Rescale();
}

wxString ComboBox::GetValue() const
{
    return drop.GetSelection() >= 0 ? drop.GetValue() : GetLabel();
}

void ComboBox::SetValue(const wxString &value)
{
    drop.SetValue(value);
    SetLabel(value);
    if (drop.selection >= 0 && drop.iconSize.y > 0)
        SetIcon(icons[drop.selection].IsNull() ? create_scaled_bitmap("drop_down", nullptr, 16): icons[drop.selection]); // ORCA fix combo boxes without arrows
}

void ComboBox::SetLabel(const wxString &value)
{
    if (GetTextCtrl()->IsShown() || text_off)
        GetTextCtrl()->SetValue(value);
    else
        TextInput::SetLabel(value);
}

wxString ComboBox::GetLabel() const
{
    if (GetTextCtrl()->IsShown() || text_off)
        return GetTextCtrl()->GetValue();
    else
        return TextInput::GetLabel();
}

void ComboBox::SetTextLabel(const wxString& label)
{
    TextInput::SetLabel(label);
}

wxString ComboBox::GetTextLabel() const
{
    return TextInput::GetLabel();
}

bool ComboBox::SetFont(wxFont const& font)
{
    if (GetTextCtrl() && GetTextCtrl()->IsShown())
        return GetTextCtrl()->SetFont(font);
    else
        return TextInput::SetFont(font);
}

int ComboBox::Append(const wxString &item, const wxBitmap &bitmap)
{
    return Append(item, bitmap, nullptr);
}

int ComboBox::Append(const wxString &item,
                     const wxBitmap &bitmap,
                     void *          clientData)
{
    texts.push_back(item);
    tips.push_back(wxString{});
    icons.push_back(bitmap);
    datas.push_back(clientData);
    types.push_back(wxClientData_None);
    drop.Invalidate();
    return texts.size() - 1;
}

void ComboBox::DoClear()
{
    SetIcon("drop_down");
    texts.clear();
    tips.clear();
    icons.clear();
    datas.clear();
    types.clear();
    drop.Invalidate(true);
}

void ComboBox::DoDeleteOneItem(unsigned int pos)
{
    if (pos >= texts.size()) return;
    texts.erase(texts.begin() + pos);
    tips.erase(tips.begin() + pos);
    icons.erase(icons.begin() + pos);
    datas.erase(datas.begin() + pos);
    types.erase(types.begin() + pos);
    drop.Invalidate(true);
}

unsigned int ComboBox::GetCount() const { return texts.size(); }

wxString ComboBox::GetString(unsigned int n) const
{
    return n < texts.size() ? texts[n] : wxString{};
}

void ComboBox::SetString(unsigned int n, wxString const &value)
{
    if (n >= texts.size()) return;
    texts[n]  = value;
    drop.Invalidate();
    if (n == drop.GetSelection()) SetLabel(value);
}

wxString ComboBox::GetItemTooltip(unsigned int n) const
{
    if (n >= texts.size()) return wxString();
    return tips[n];
}

void ComboBox::SetItemTooltip(unsigned int n, wxString const &value) {
    if (n >= texts.size()) return;
    tips[n] = value;
    if (n == drop.GetSelection()) drop.SetToolTip(value);
}

wxBitmap ComboBox::GetItemBitmap(unsigned int n) { return icons[n]; }

void ComboBox::SetItemBitmap(unsigned int n, wxBitmap const &bitmap)
{
    if (n >= texts.size()) return;
    icons[n] = bitmap;
    drop.Invalidate();
}

int ComboBox::DoInsertItems(const wxArrayStringsAdapter &items,
                            unsigned int                 pos,
                            void **                      clientData,
                            wxClientDataType             type)
{
    if (pos > texts.size()) return -1;
    for (int i = 0; i < items.GetCount(); ++i) {
        texts.insert(texts.begin() + pos, items[i]);
        tips.insert(tips.begin() + pos, wxString{});
        icons.insert(icons.begin() + pos, wxNullBitmap);
        datas.insert(datas.begin() + pos, clientData ? clientData[i] : NULL);
        types.insert(types.begin() + pos, type);
        ++pos;
    }
    drop.Invalidate(true);
    return pos - 1;
}

void *ComboBox::DoGetItemClientData(unsigned int n) const { return n < texts.size() ? datas[n] : NULL; }

void ComboBox::DoSetItemClientData(unsigned int n, void *data)
{
    if (n < texts.size())
        datas[n] = data;
}

void ComboBox::mouseDown(wxMouseEvent &event)
{
    SetFocus();
    if (drop_down) {
        drop.Hide();
    } else if (drop.HasDismissLongTime()) {
        drop.autoPosition();
        drop_down = true;
        drop.Popup(&drop);
        wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
        GetEventHandler()->ProcessEvent(e);
    }
}

void ComboBox::mouseWheelMoved(wxMouseEvent &event)
{
    event.Skip();
    if (drop_down) return;
    auto delta = event.GetWheelRotation() < 0 ? 1 : -1;
    unsigned int n = GetSelection() + delta;
    if (n < GetCount()) {
        SetSelection((int) n);
        sendComboBoxEvent();
    }
}

void ComboBox::keyDown(wxKeyEvent& event)
{
    switch (event.GetKeyCode()) {
        case WXK_RETURN:
        case WXK_SPACE:
            if (drop_down) {
                drop.DismissAndNotify();
            } else if (drop.HasDismissLongTime()) {
                drop.autoPosition();
                drop_down = true;
                drop.Popup();
                wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
                GetEventHandler()->ProcessEvent(e);
            }
            break;
        case WXK_UP:
        case WXK_DOWN:
        case WXK_LEFT:
        case WXK_RIGHT:
            if ((event.GetKeyCode() == WXK_UP || event.GetKeyCode() == WXK_LEFT) && GetSelection() > 0) {
                SetSelection(GetSelection() - 1);
            } else if ((event.GetKeyCode() == WXK_DOWN || event.GetKeyCode() == WXK_RIGHT) && GetSelection() + 1 < texts.size()) {
                SetSelection(GetSelection() + 1);
            } else {
                break;
            }
            sendComboBoxEvent();
            break;
        case WXK_TAB:
            HandleAsNavigationKey(event);
            break;
        default:
            event.Skip();
            break;
    }
}

void ComboBox::onMove(wxMoveEvent &event)
{
    event.Skip();
    drop.Hide();
}

void ComboBox::OnEdit()
{
    auto value = GetTextCtrl()->GetValue();
    SetValue(value);
}

#ifdef __WIN32__

WXLRESULT ComboBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS;
    }
    return TextInput::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

void ComboBox::sendComboBoxEvent()
{
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(drop.GetSelection());
    event.SetString(drop.GetValue());
    GetEventHandler()->ProcessEvent(event);
}
