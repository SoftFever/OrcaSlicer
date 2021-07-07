#include "SLAImportJob.hpp"

#include "libslic3r/Format/SL1.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/filepicker.h>

namespace Slic3r { namespace GUI {

enum class Sel { modelAndProfile, profileOnly, modelOnly};
    
class ImportDlg: public wxDialog {
    wxFilePickerCtrl *m_filepicker;
    wxComboBox *m_import_dropdown, *m_quality_dropdown;
    
public:
    ImportDlg(Plater *plater)
        : wxDialog{plater, wxID_ANY, "Import SLA archive"}
    {
        auto szvert = new wxBoxSizer{wxVERTICAL};
        auto szfilepck = new wxBoxSizer{wxHORIZONTAL};
        
        m_filepicker = new wxFilePickerCtrl(this, wxID_ANY,
                                            from_u8(wxGetApp().app_config->get_last_dir()), _(L("Choose SLA archive:")),
                                            "SL1 archive files (*.sl1, *.zip)|*.sl1;*.SL1;*.zip;*.ZIP",
                                            wxDefaultPosition, wxDefaultSize, wxFLP_DEFAULT_STYLE | wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        
        szfilepck->Add(new wxStaticText(this, wxID_ANY, _L("Import file") + ": "), 0, wxALIGN_CENTER);
        szfilepck->Add(m_filepicker, 1);
        szvert->Add(szfilepck, 0, wxALL | wxEXPAND, 5);
        
        auto szchoices = new wxBoxSizer{wxHORIZONTAL};
        
        static const std::vector<wxString> inp_choices = {
            _(L("Import model and profile")),
            _(L("Import profile only")),
            _(L("Import model only"))
        };
        
        m_import_dropdown = new wxComboBox(
            this, wxID_ANY, inp_choices[0], wxDefaultPosition, wxDefaultSize,
            inp_choices.size(), inp_choices.data(), wxCB_READONLY | wxCB_DROPDOWN);
        
        szchoices->Add(m_import_dropdown);
        szchoices->Add(new wxStaticText(this, wxID_ANY, _L("Quality") + ": "), 0, wxALIGN_CENTER | wxALL, 5);
        
        static const std::vector<wxString> qual_choices = {
            _(L("Accurate")),
            _(L("Balanced")),
            _(L("Quick"))
        };
        
        m_quality_dropdown = new wxComboBox(
            this, wxID_ANY, qual_choices[0], wxDefaultPosition, wxDefaultSize,
            qual_choices.size(), qual_choices.data(), wxCB_READONLY | wxCB_DROPDOWN);
        szchoices->Add(m_quality_dropdown);
        
        m_import_dropdown->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
            if (get_selection() == Sel::profileOnly)
                m_quality_dropdown->Disable();
            else m_quality_dropdown->Enable();
        });
        
        szvert->Add(szchoices, 0, wxALL, 5);
        szvert->AddStretchSpacer(1);
        auto szbtn = new wxBoxSizer(wxHORIZONTAL);
        szbtn->Add(new wxButton{this, wxID_CANCEL});
        szbtn->Add(new wxButton{this, wxID_OK});
        szvert->Add(szbtn, 0, wxALIGN_RIGHT | wxALL, 5);
        
        SetSizerAndFit(szvert);
    }
    
    Sel get_selection() const
    {
        int sel = m_import_dropdown->GetSelection();
        return Sel(std::min(int(Sel::modelOnly), std::max(0, sel)));
    }
    
    Vec2i get_marchsq_windowsize() const
    {
        enum { Accurate, Balanced, Fast};
        
        switch(m_quality_dropdown->GetSelection())
        {
        case Fast: return {8, 8};
        case Balanced: return {4, 4};
        default:
        case Accurate:
            return {2, 2};
        }
    }
    
    wxString get_path() const
    {
        return m_filepicker->GetPath();
    }
};

class SLAImportJob::priv {
public:
    Plater *plater;
    
    Sel sel = Sel::modelAndProfile;

    indexed_triangle_set mesh;
    DynamicPrintConfig   profile;
    wxString             path;
    Vec2i                win = {2, 2};
    std::string          err;

    priv(Plater *plt) : plater{plt} {}
};

SLAImportJob::SLAImportJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
    : PlaterJob{std::move(pri), plater}, p{std::make_unique<priv>(plater)}
{}

SLAImportJob::~SLAImportJob() = default;

void SLAImportJob::process()
{
    auto progr = [this](int s) {
        if (s < 100)
            update_status(int(s), _(L("Importing SLA archive")));
        return !was_canceled();
    };
    
    if (p->path.empty()) return;
    
    std::string path = p->path.ToUTF8().data();
    ConfigSubstitutions config_substitutions;
    try {
        switch (p->sel) {
        case Sel::modelAndProfile:
            config_substitutions = import_sla_archive(path, p->win, p->mesh, p->profile, progr);
            break;
        case Sel::modelOnly:
            config_substitutions = import_sla_archive(path, p->win, p->mesh, progr);
            break;
        case Sel::profileOnly:
            config_substitutions = import_sla_archive(path, p->profile);
            break;
        }
        
    } catch (std::exception &ex) {
        p->err = ex.what();
    }

    if (! config_substitutions.empty()) {
        //FIXME Add reporting here "Loading profiles found following incompatibilities."
    }
    
    update_status(100, was_canceled() ? _(L("Importing canceled.")) :
                                        _(L("Importing done.")));
}

void SLAImportJob::reset()
{
    p->sel     = Sel::modelAndProfile;
    p->mesh    = {};
    p->profile = {};
    p->win     = {2, 2};
    p->path.Clear();
}

void SLAImportJob::prepare()
{
    reset();
    
    ImportDlg dlg{p->plater};
    
    if (dlg.ShowModal() == wxID_OK) {
        auto path = dlg.get_path();
        auto nm = wxFileName(path);
        p->path = !nm.Exists(wxFILE_EXISTS_REGULAR) ? "" : nm.GetFullPath();
        p->sel  = dlg.get_selection();
        p->win  = dlg.get_marchsq_windowsize();
    } else {
        p->path = "";
    }
}

void SLAImportJob::finalize()
{
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;
    
    if (!p->err.empty()) {
        show_error(p->plater, p->err);
        p->err = "";
        return;
    }
    
    std::string name = wxFileName(p->path).GetName().ToUTF8().data();
    
    if (!p->profile.empty()) {
        const ModelObjectPtrs& objects = p->plater->model().objects;
        for (auto object : objects)
            if (object->volumes.size() > 1)
            {
                Slic3r::GUI::show_info(nullptr,
                                       _(L("You cannot load SLA project with a multi-part object on the bed")) + "\n\n" +
                                       _(L("Please check your object list before preset changing.")),
                                       _(L("Attention!")) );
                return;
            }
        
        DynamicPrintConfig config = {};
        config.apply(SLAFullPrintConfig::defaults());
        config += std::move(p->profile);
        
        wxGetApp().preset_bundle->load_config_model(name, std::move(config));
        wxGetApp().load_current_presets();
    }
    
    if (!p->mesh.empty()) {
        bool is_centered = false;
        p->plater->sidebar().obj_list()->load_mesh_object(TriangleMesh{p->mesh},
                                                          name, is_centered);
    }
    
    reset();
}

}}
