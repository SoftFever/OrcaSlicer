#include "PrintHostDialogs.hpp"

#include <algorithm>

#include <wx/frame.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dataview.h>
#include <wx/wupdlock.h>
#include <wx/debug.h>

#include "GUI.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "../Utils/PrintHost.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {


PrintHostSendDialog::PrintHostSendDialog(const fs::path &path)
    : MsgDialog(nullptr, _(L("Send G-Code to printer host")), _(L("Upload to Printer Host with the following filename:")), wxID_NONE)
    , txt_filename(new wxTextCtrl(this, wxID_ANY, path.filename().wstring()))
    , box_print(new wxCheckBox(this, wxID_ANY, _(L("Start printing after upload"))))
{
    auto *label_dir_hint = new wxStaticText(this, wxID_ANY, _(L("Use forward slashes ( / ) as a directory separator if needed.")));
    label_dir_hint->Wrap(CONTENT_WIDTH);

    content_sizer->Add(txt_filename, 0, wxEXPAND);
    content_sizer->Add(label_dir_hint);
    content_sizer->AddSpacer(VERT_SPACING);
    content_sizer->Add(box_print, 0, wxBOTTOM, 2*VERT_SPACING);

    btn_sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL));

    txt_filename->SetFocus();
    wxString stem(path.stem().wstring());
    txt_filename->SetSelection(0, stem.Length());

    Fit();
}

fs::path PrintHostSendDialog::filename() const
{
    return fs::path(txt_filename->GetValue().wx_str());
}

bool PrintHostSendDialog::start_print() const
{
    return box_print->GetValue();
}



wxDEFINE_EVENT(EVT_PRINTHOST_PROGRESS, PrintHostQueueDialog::Event);
wxDEFINE_EVENT(EVT_PRINTHOST_ERROR, PrintHostQueueDialog::Event);

PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id)
    : wxEvent(winid, eventType)
    , job_id(job_id)
{}

PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id, int progress)
    : wxEvent(winid, eventType)
    , job_id(job_id)
    , progress(progress)
{}

PrintHostQueueDialog::Event::Event(wxEventType eventType, int winid, size_t job_id, wxString error)
    : wxEvent(winid, eventType)
    , job_id(job_id)
    , error(std::move(error))
{}

wxEvent *PrintHostQueueDialog::Event::Clone() const
{
    return new Event(*this);
}

PrintHostQueueDialog::PrintHostQueueDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _(L("Print host upload queue")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , on_progress_evt(this, EVT_PRINTHOST_PROGRESS, &PrintHostQueueDialog::on_progress, this)
    , on_error_evt(this, EVT_PRINTHOST_ERROR, &PrintHostQueueDialog::on_error, this)
{
    enum { HEIGHT = 800, WIDTH = 400, SPACING = 5 };

    SetMinSize(wxSize(HEIGHT, WIDTH));

    auto *topsizer = new wxBoxSizer(wxVERTICAL);

    job_list = new wxDataViewListCtrl(this, wxID_ANY);
    job_list->AppendTextColumn("ID", wxDATAVIEW_CELL_INERT);
    job_list->AppendProgressColumn("Progress", wxDATAVIEW_CELL_INERT);
    job_list->AppendTextColumn("Status", wxDATAVIEW_CELL_INERT);
    job_list->AppendTextColumn("Host", wxDATAVIEW_CELL_INERT);
    job_list->AppendTextColumn("Filename", wxDATAVIEW_CELL_INERT);

    auto *btnsizer = new wxBoxSizer(wxHORIZONTAL);
    auto *btn_cancel = new wxButton(this, wxID_DELETE, _(L("Cancel selected")));
    auto *btn_close = new wxButton(this, wxID_CANCEL, _(L("Close")));
    btnsizer->Add(btn_cancel, 0, wxRIGHT, SPACING);
    btnsizer->AddStretchSpacer();
    btnsizer->Add(btn_close);

    topsizer->Add(job_list, 1, wxEXPAND | wxBOTTOM, SPACING);
    topsizer->Add(btnsizer, 0, wxEXPAND);
    SetSizer(topsizer);
}

void PrintHostQueueDialog::append_job(const PrintHostJob &job)
{
    wxCHECK_RET(!job.empty(), "PrintHostQueueDialog: Attempt to append an empty job");

    wxVector<wxVariant> fields;
    fields.push_back(wxVariant(wxString::Format("%d", job_list->GetItemCount() + 1)));
    fields.push_back(wxVariant(0));
    fields.push_back(wxVariant(_(L("Enqueued"))));
    fields.push_back(wxVariant(job.printhost->get_host()));
    fields.push_back(wxVariant(job.upload_data.upload_path.string()));
    job_list->AppendItem(fields);
}

void PrintHostQueueDialog::on_progress(Event &evt)
{
    wxCHECK_RET(evt.job_id < job_list->GetItemCount(), "Out of bounds access to job list");

    const wxVariant status(evt.progress < 100 ? _(L("Uploading")) : _(L("Complete")));

    job_list->SetValue(wxVariant(evt.progress), evt.job_id, 1);
    job_list->SetValue(status, evt.job_id, 2);
}

void PrintHostQueueDialog::on_error(Event &evt)
{
    wxCHECK_RET(evt.job_id < job_list->GetItemCount(), "Out of bounds access to job list");

    // TODO
}


}}
