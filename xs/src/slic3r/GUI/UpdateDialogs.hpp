#ifndef slic3r_UpdateDialogs_hpp_
#define slic3r_UpdateDialogs_hpp_

#include <string>
#include <unordered_map>

#include "slic3r/Utils/Semver.hpp"
#include "MsgDialog.hpp"

class wxBoxSizer;
class wxCheckBox;

namespace Slic3r {

namespace GUI {


// A confirmation dialog listing configuration updates
class MsgUpdateSlic3r : public MsgDialog
{
public:
	MsgUpdateSlic3r(const Semver &ver_current, const Semver &ver_online);
	MsgUpdateSlic3r(MsgUpdateSlic3r &&) = delete;
	MsgUpdateSlic3r(const MsgUpdateSlic3r &) = delete;
	MsgUpdateSlic3r &operator=(MsgUpdateSlic3r &&) = delete;
	MsgUpdateSlic3r &operator=(const MsgUpdateSlic3r &) = delete;
	virtual ~MsgUpdateSlic3r();

	// Tells whether the user checked the "don't bother me again" checkbox
	bool disable_version_check() const;

private:
	const Semver &ver_current;
	const Semver &ver_online;
	wxCheckBox *cbox;
};


// Confirmation dialog informing about configuration update. Lists updated bundles & their versions.
class MsgUpdateConfig : public MsgDialog
{
public:
	// updates is a map of "vendor name" -> "version (comment)"
	MsgUpdateConfig(const std::unordered_map<std::string, std::string> &updates);
	MsgUpdateConfig(MsgUpdateConfig &&) = delete;
	MsgUpdateConfig(const MsgUpdateConfig &) = delete;
	MsgUpdateConfig &operator=(MsgUpdateConfig &&) = delete;
	MsgUpdateConfig &operator=(const MsgUpdateConfig &) = delete;
	~MsgUpdateConfig();
};

// Informs about currently installed bundles not being compatible with the running Slic3r. Asks about action.
class MsgDataIncompatible : public MsgDialog
{
public:
	// incompats is a map of "vendor name" -> "version restrictions"
	MsgDataIncompatible(const std::unordered_map<std::string, wxString> &incompats);
	MsgDataIncompatible(MsgDataIncompatible &&) = delete;
	MsgDataIncompatible(const MsgDataIncompatible &) = delete;
	MsgDataIncompatible &operator=(MsgDataIncompatible &&) = delete;
	MsgDataIncompatible &operator=(const MsgDataIncompatible &) = delete;
	~MsgDataIncompatible();
};

// Informs about a legacy data directory - an update from Slic3r PE < 1.40
class MsgDataLegacy : public MsgDialog
{
public:
	MsgDataLegacy();
	MsgDataLegacy(MsgDataLegacy &&) = delete;
	MsgDataLegacy(const MsgDataLegacy &) = delete;
	MsgDataLegacy &operator=(MsgDataLegacy &&) = delete;
	MsgDataLegacy &operator=(const MsgDataLegacy &) = delete;
	~MsgDataLegacy();
};


}
}

#endif
