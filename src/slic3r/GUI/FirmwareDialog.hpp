#ifndef slic3r_FirmwareDialog_hpp_
#define slic3r_FirmwareDialog_hpp_

#include <memory>

#include <wx/dialog.h>
#include "GUI_Utils.hpp"


namespace Slic3r {


class FirmwareDialog: public GUI::DPIDialog
{
public:
	FirmwareDialog(wxWindow *parent);
	FirmwareDialog(FirmwareDialog &&) = delete;
	FirmwareDialog(const FirmwareDialog &) = delete;
	FirmwareDialog &operator=(FirmwareDialog &&) = delete;
	FirmwareDialog &operator=(const FirmwareDialog &) = delete;
	~FirmwareDialog();

	static void run(wxWindow *parent);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override{;}
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}

#endif
