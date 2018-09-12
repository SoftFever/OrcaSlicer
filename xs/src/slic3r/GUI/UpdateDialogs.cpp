#include "UpdateDialogs.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/event.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/hyperlink.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "ConfigWizard.hpp"

namespace Slic3r {
namespace GUI {


static const std::string CONFIG_UPDATE_WIKI_URL("https://github.com/prusa3d/Slic3r/wiki/Slic3r-PE-1.40-configuration-update");


// MsgUpdateSlic3r

MsgUpdateSlic3r::MsgUpdateSlic3r(const Semver &ver_current, const Semver &ver_online) :
	MsgDialog(nullptr, _(L("Update available")), _(L("New version of Slic3r PE is available"))),
	ver_current(ver_current),
	ver_online(ver_online)
{
	const auto url = wxString::Format("https://github.com/prusa3d/Slic3r/releases/tag/version_%s", ver_online.to_string());
	auto *link = new wxHyperlinkCtrl(this, wxID_ANY, url, url);

	auto *text = new wxStaticText(this, wxID_ANY, _(L("To download, follow the link below.")));
	const auto link_width = link->GetSize().GetWidth();
	text->Wrap(CONTENT_WIDTH > link_width ? CONTENT_WIDTH : link_width);
	content_sizer->Add(text);
	content_sizer->AddSpacer(VERT_SPACING);

	auto *versions = new wxFlexGridSizer(2, 0, VERT_SPACING);
	versions->Add(new wxStaticText(this, wxID_ANY, _(L("Current version:"))));
	versions->Add(new wxStaticText(this, wxID_ANY, ver_current.to_string()));
	versions->Add(new wxStaticText(this, wxID_ANY, _(L("New version:"))));
	versions->Add(new wxStaticText(this, wxID_ANY, ver_online.to_string()));
	content_sizer->Add(versions);
	content_sizer->AddSpacer(VERT_SPACING);

	content_sizer->Add(link);
	content_sizer->AddSpacer(2*VERT_SPACING);

	cbox = new wxCheckBox(this, wxID_ANY, _(L("Don't notify about new releases any more")));
	content_sizer->Add(cbox);
	content_sizer->AddSpacer(VERT_SPACING);

	Fit();
}

MsgUpdateSlic3r::~MsgUpdateSlic3r() {}

bool MsgUpdateSlic3r::disable_version_check() const
{
	return cbox->GetValue();
}


// MsgUpdateConfig

MsgUpdateConfig::MsgUpdateConfig(const std::unordered_map<std::string, std::string> &updates) :
	MsgDialog(nullptr, _(L("Configuration update")), _(L("Configuration update is available")), wxID_NONE)
{
	auto *text = new wxStaticText(this, wxID_ANY, _(L(
		"Would you like to install it?\n\n"
		"Note that a full configuration snapshot will be created first. It can then be restored at any time "
		"should there be a problem with the new version.\n\n"
		"Updated configuration bundles:"
	)));
	text->Wrap(CONTENT_WIDTH);
	content_sizer->Add(text);
	content_sizer->AddSpacer(VERT_SPACING);

	auto *versions = new wxFlexGridSizer(2, 0, VERT_SPACING);
	for (const auto &update : updates) {
		auto *text_vendor = new wxStaticText(this, wxID_ANY, update.first);
		text_vendor->SetFont(boldfont);
		versions->Add(text_vendor);
		versions->Add(new wxStaticText(this, wxID_ANY, update.second));
	}

	content_sizer->Add(versions);
	content_sizer->AddSpacer(2*VERT_SPACING);

	auto *btn_cancel = new wxButton(this, wxID_CANCEL);
	btn_sizer->Add(btn_cancel);
	btn_sizer->AddSpacer(HORIZ_SPACING);
	auto *btn_ok = new wxButton(this, wxID_OK);
	btn_sizer->Add(btn_ok);
	btn_ok->SetFocus();

	Fit();
}

MsgUpdateConfig::~MsgUpdateConfig() {}


// MsgDataIncompatible

MsgDataIncompatible::MsgDataIncompatible(const std::unordered_map<std::string, wxString> &incompats) :
	MsgDialog(nullptr, _(L("Slic3r incompatibility")), _(L("Slic3r configuration is incompatible")), wxBitmap(from_u8(Slic3r::var("Slic3r_192px_grayscale.png")), wxBITMAP_TYPE_PNG), wxID_NONE)
{
	auto *text = new wxStaticText(this, wxID_ANY, _(L(
		"This version of Slic3r PE is not compatible with currently installed configuration bundles.\n"
		"This probably happened as a result of running an older Slic3r PE after using a newer one.\n\n"

		"You may either exit Slic3r and try again with a newer version, or you may re-run the initial configuration. "
		"Doing so will create a backup snapshot of the existing configuration before installing files compatible with this Slic3r.\n"
	)));
	text->Wrap(CONTENT_WIDTH);
	content_sizer->Add(text);

	auto *text2 = new wxStaticText(this, wxID_ANY, wxString::Format(_(L("This Slic3r PE version: %s")), SLIC3R_VERSION));
	text2->Wrap(CONTENT_WIDTH);
	content_sizer->Add(text2);
	content_sizer->AddSpacer(VERT_SPACING);

	auto *text3 = new wxStaticText(this, wxID_ANY, _(L("Incompatible bundles:")));
	text3->Wrap(CONTENT_WIDTH);
	content_sizer->Add(text3);
	content_sizer->AddSpacer(VERT_SPACING);

	auto *versions = new wxFlexGridSizer(2, 0, VERT_SPACING);
	for (const auto &incompat : incompats) {
		auto *text_vendor = new wxStaticText(this, wxID_ANY, incompat.first);
		text_vendor->SetFont(boldfont);
		versions->Add(text_vendor);
		versions->Add(new wxStaticText(this, wxID_ANY, incompat.second));
	}

	content_sizer->Add(versions);
	content_sizer->AddSpacer(2*VERT_SPACING);

	auto *btn_exit = new wxButton(this, wxID_EXIT, _(L("Exit Slic3r")));
	btn_sizer->Add(btn_exit);
	btn_sizer->AddSpacer(HORIZ_SPACING);
	auto *btn_reconf = new wxButton(this, wxID_REPLACE, _(L("Re-configure")));
	btn_sizer->Add(btn_reconf);
	btn_exit->SetFocus();

	auto exiter = [this](const wxCommandEvent& evt) { this->EndModal(evt.GetId()); };
	btn_exit->Bind(wxEVT_BUTTON, exiter);
	btn_reconf->Bind(wxEVT_BUTTON, exiter);

	Fit();
}

MsgDataIncompatible::~MsgDataIncompatible() {}


// MsgDataLegacy

MsgDataLegacy::MsgDataLegacy() :
	MsgDialog(nullptr, _(L("Configuration update")), _(L("Configuration update")))
{
	auto *text = new wxStaticText(this, wxID_ANY, wxString::Format(
		_(L(
			"Slic3r PE now uses an updated configuration structure.\n\n"

			"So called 'System presets' have been introduced, which hold the built-in default settings for various "
			"printers. These System presets cannot be modified, instead, users now may create their "
			"own presets inheriting settings from one of the System presets.\n"
			"An inheriting preset may either inherit a particular value from its parent or override it with a customized value.\n\n"

			"Please proceed with the %s that follows to set up the new presets "
			"and to choose whether to enable automatic preset updates."
		)),
		ConfigWizard::name()
	));
	text->Wrap(CONTENT_WIDTH);
	content_sizer->Add(text);
	content_sizer->AddSpacer(VERT_SPACING);

	auto *text2 = new wxStaticText(this, wxID_ANY, _(L("For more information please visit our wiki page:")));
	static const wxString url("https://github.com/prusa3d/Slic3r/wiki/Slic3r-PE-1.40-configuration-update");
	// The wiki page name is intentionally not localized:
	auto *link = new wxHyperlinkCtrl(this, wxID_ANY, "Slic3r PE 1.40 configuration update", CONFIG_UPDATE_WIKI_URL);
	content_sizer->Add(text2);
	content_sizer->Add(link);
	content_sizer->AddSpacer(VERT_SPACING);

	Fit();
}

MsgDataLegacy::~MsgDataLegacy() {}


}
}
