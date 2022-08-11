#ifndef slic3r_BonjourDialog_hpp_
#define slic3r_BonjourDialog_hpp_

#include <memory>

#include <wx/dialog.h>

#include "libslic3r/PrintConfig.hpp"

class wxListView;
class wxStaticText;
class wxTimer;
class wxTimerEvent;


namespace Slic3r {

class Bonjour;
class BonjourReplyEvent;
class ReplySet;


class BonjourDialog: public wxDialog
{
public:
	BonjourDialog(wxWindow *parent, Slic3r::PrinterTechnology);
	BonjourDialog(BonjourDialog &&) = delete;
	BonjourDialog(const BonjourDialog &) = delete;
	BonjourDialog &operator=(BonjourDialog &&) = delete;
	BonjourDialog &operator=(const BonjourDialog &) = delete;
	~BonjourDialog();

	bool show_and_lookup();
	wxString get_selected() const;
private:
	wxListView *list;
	std::unique_ptr<ReplySet> replies;
	wxStaticText *label;
	std::shared_ptr<Bonjour> bonjour;
	std::unique_ptr<wxTimer> timer;
	unsigned timer_state;
	Slic3r::PrinterTechnology tech;

	void on_reply(BonjourReplyEvent &);
	void on_timer(wxTimerEvent &);
    void on_timer_process();
};



}

#endif
