#ifndef SLIC3R_GUI_BITMAP_CACHE_HPP
#define SLIC3R_GUI_BITMAP_CACHE_HPP

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Config.hpp"

#include "GUI.hpp"

namespace Slic3r { namespace GUI {

class BitmapCache
{
public:
	BitmapCache() {}
	~BitmapCache() { clear(); }
	void 			clear();

	wxBitmap* 		find(const std::string &name) 		{ auto it = m_map.find(name); return (it == m_map.end()) ? nullptr : it->second; }
	const wxBitmap* find(const std::string &name) const { return const_cast<BitmapCache*>(this)->find(name); }

	wxBitmap*       insert(const std::string &name, size_t width, size_t height);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp, const wxBitmap &bmp2);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp, const wxBitmap &bmp2, const wxBitmap &bmp3);
	wxBitmap* 		insert(const std::string &name, const std::vector<wxBitmap> &bmps) { return this->insert(name, &bmps.front(), &bmps.front() + bmps.size()); }
	wxBitmap* 		insert(const std::string &name, const wxBitmap *begin, const wxBitmap *end);

	static wxBitmap mksolid(size_t width, size_t height, unsigned char r, unsigned char g, unsigned char b, unsigned char transparency);
	static wxBitmap mksolid(size_t width, size_t height, const unsigned char rgb[3]) { return mksolid(width, height, rgb[0], rgb[1], rgb[2], wxALPHA_OPAQUE); }
	static wxBitmap mkclear(size_t width, size_t height) { return mksolid(width, height, 0, 0, 0, wxALPHA_TRANSPARENT); }

private:
    std::map<std::string, wxBitmap*>	m_map;
};

} // GUI
} // Slic3r

#endif /* SLIC3R_GUI_BITMAP_CACHE_HPP */
