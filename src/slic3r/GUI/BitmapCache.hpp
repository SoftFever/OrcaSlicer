#ifndef SLIC3R_GUI_BITMAP_CACHE_HPP
#define SLIC3R_GUI_BITMAP_CACHE_HPP

#include <map>
#include <vector>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

namespace Slic3r { namespace GUI {

class BitmapCache
{
public:
	BitmapCache();
	~BitmapCache() { clear(); }
	void 			clear();
	double			scale() { return m_scale; }

	wxBitmap* 		find(const std::string &name) 		{ auto it = m_map.find(name); return (it == m_map.end()) ? nullptr : it->second; }
	const wxBitmap* find(const std::string &name) const { return const_cast<BitmapCache*>(this)->find(name); }

	wxBitmap*       insert(const std::string &name, size_t width, size_t height);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp, const wxBitmap &bmp2);
	wxBitmap* 		insert(const std::string &name, const wxBitmap &bmp, const wxBitmap &bmp2, const wxBitmap &bmp3);
	wxBitmap* 		insert(const std::string &name, const std::vector<wxBitmap> &bmps) { return this->insert(name, &bmps.front(), &bmps.front() + bmps.size()); }
	wxBitmap* 		insert(const std::string &name, const wxBitmap *begin, const wxBitmap *end);
	wxBitmap* 		insert_raw_rgba(const std::string &bitmap_key, unsigned width, unsigned height, const unsigned char *raw_data, const bool grayscale = false);

	// Load png from resources/icons. bitmap_key is given without the .png suffix. Bitmap will be rescaled to provided height/width if nonzero.
    wxBitmap* 		load_png(const std::string &bitmap_key, unsigned width = 0, unsigned height = 0, const bool grayscale = false);
	// Load svg from resources/icons. bitmap_key is given without the .svg suffix. SVG will be rasterized to provided height/width.
    wxBitmap* 		load_svg(const std::string &bitmap_key, unsigned width = 0, unsigned height = 0, const bool grayscale = false, const bool dark_mode = false);

	/*static */wxBitmap mksolid(size_t width, size_t height, unsigned char r, unsigned char g, unsigned char b, unsigned char transparency, bool suppress_scaling = false);
	/*static */wxBitmap mksolid(size_t width, size_t height, const unsigned char rgb[3], bool suppress_scaling = false) { return mksolid(width, height, rgb[0], rgb[1], rgb[2], wxALPHA_OPAQUE, suppress_scaling); }
	/*static */wxBitmap mkclear(size_t width, size_t height) { return mksolid(width, height, 0, 0, 0, wxALPHA_TRANSPARENT); }

	static bool                 parse_color(const std::string& scolor, unsigned char* rgb_out);

private:
    std::map<std::string, wxBitmap*>	m_map;
    double	m_gs	= 0.2;	// value, used for image.ConvertToGreyscale(m_gs, m_gs, m_gs)
	double	m_scale = 1.0;	// value, used for correct scaling of SVG icons on Retina display
};

} // GUI
} // Slic3r

#endif /* SLIC3R_GUI_BITMAP_CACHE_HPP */
