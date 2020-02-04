#ifndef slic3r_UpdateDialogs_hpp_
#define slic3r_UpdateDialogs_hpp_

#include <string>
#include <unordered_map>
#include <vector>

#include "libslic3r/Semver.hpp"
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
	wxCheckBox *cbox;
};


// Confirmation dialog informing about configuration update. Lists updated bundles & their versions.
class MsgUpdateConfig : public MsgDialog
{
public:
	struct Update
	{
		std::string vendor;
		Semver version;
		std::string comment;
		std::string changelog_url;

		Update(std::string vendor, Semver version, std::string comment, std::string changelog_url)
			: vendor(std::move(vendor))
			, version(std::move(version))
			, comment(std::move(comment))
			, changelog_url(std::move(changelog_url))
		{}
	};

	MsgUpdateConfig(const std::vector<Update> &updates);
	MsgUpdateConfig(MsgUpdateConfig &&) = delete;
	MsgUpdateConfig(const MsgUpdateConfig &) = delete;
	MsgUpdateConfig &operator=(MsgUpdateConfig &&) = delete;
	MsgUpdateConfig &operator=(const MsgUpdateConfig &) = delete;
	~MsgUpdateConfig();
};

// Informs about currently installed bundles not being compatible with the running Slic3r. Asks about action.
class MsgUpdateForced : public MsgDialog
{
public:
	struct Update
	{
		std::string vendor;
		Semver version;
		std::string comment;
		std::string changelog_url;

		Update(std::string vendor, Semver version, std::string comment, std::string changelog_url)
			: vendor(std::move(vendor))
			, version(std::move(version))
			, comment(std::move(comment))
			, changelog_url(std::move(changelog_url))
		{}
	};

	MsgUpdateForced(const std::vector<Update>& updates);
	MsgUpdateForced(MsgUpdateForced&&) = delete;
	MsgUpdateForced(const MsgUpdateForced&) = delete;
	MsgUpdateForced& operator=(MsgUpdateForced&&) = delete;
	MsgUpdateForced& operator=(const MsgUpdateForced&) = delete;
	~MsgUpdateForced();
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

// Informs about absence of bundles requiring update.
class MsgNoUpdates : public MsgDialog
{
public:
	MsgNoUpdates();
	MsgNoUpdates(MsgNoUpdates&&) = delete;
	MsgNoUpdates(const MsgNoUpdates&) = delete;
	MsgNoUpdates& operator=(MsgNoUpdates&&) = delete;
	MsgNoUpdates& operator=(const MsgNoUpdates&) = delete;
	~MsgNoUpdates();
};

}
}

#endif
