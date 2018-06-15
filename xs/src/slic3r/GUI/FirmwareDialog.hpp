#ifndef slic3r_FirmwareDialog_hpp_
#define slic3r_FirmwareDialog_hpp_

#include <memory>

#include <wx/dialog.h>


namespace Slic3r {


class FirmwareDialog: public wxDialog
{
public:
	FirmwareDialog(wxWindow *parent);
	FirmwareDialog(FirmwareDialog &&) = delete;
	FirmwareDialog(const FirmwareDialog &) = delete;
	FirmwareDialog &operator=(FirmwareDialog &&) = delete;
	FirmwareDialog &operator=(const FirmwareDialog &) = delete;
	~FirmwareDialog();

	static void run(wxWindow *parent);
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}

#endif
