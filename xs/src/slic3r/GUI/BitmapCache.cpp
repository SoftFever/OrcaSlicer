#include "BitmapCache.hpp"

namespace Slic3r { namespace GUI {

void BitmapCache::clear()
{
    for (std::pair<const std::string, wxBitmap*> &bitmap : m_map)
        delete bitmap.second;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, size_t width, size_t height)
{
	wxBitmap *bitmap = nullptr;
	auto 	  it     = m_map.find(bitmap_key);
	if (it == m_map.end()) {
		bitmap = new wxBitmap(width, height);
    	m_map[bitmap_key] = bitmap;
	} else {
		bitmap = it->second;
		if (bitmap->GetWidth() != width || bitmap->GetHeight() != height)
			bitmap->Create(width, height);
	}
#if defined(__APPLE__) || defined(_MSC_VER)
    bitmap->UseAlpha();
#endif
    return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap &bmp)
{
	wxBitmap *bitmap = this->insert(bitmap_key, bmp.GetWidth(), bmp.GetHeight());

    wxMemoryDC memDC;
    memDC.SelectObject(*bitmap);
    memDC.SetBackground(*wxTRANSPARENT_BRUSH);
    memDC.Clear();
    memDC.DrawBitmap(bmp, 0, 0, true);
    memDC.SelectObject(wxNullBitmap);

	return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap &bmp, const wxBitmap &bmp2)
{
	wxBitmap *bitmap = this->insert(bitmap_key, bmp.GetWidth() + bmp2.GetWidth(), std::max(bmp.GetHeight(), bmp2.GetHeight()));

    wxMemoryDC memDC;
    memDC.SelectObject(*bitmap);
    memDC.SetBackground(*wxTRANSPARENT_BRUSH);
    memDC.Clear();
    if (bmp.GetWidth() > 0)
    	memDC.DrawBitmap(bmp,  0, 				0, true);
    if (bmp2.GetWidth() > 0)
	    memDC.DrawBitmap(bmp2, bmp.GetWidth(), 	0, true);
    memDC.SelectObject(wxNullBitmap);

	return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap &bmp, const wxBitmap &bmp2, const wxBitmap &bmp3)
{
	wxBitmap *bitmap = this->insert(bitmap_key, bmp.GetWidth() + bmp2.GetWidth() + bmp3.GetWidth(), std::max(std::max(bmp.GetHeight(), bmp2.GetHeight()), bmp3.GetHeight()));

    wxMemoryDC memDC;
    memDC.SelectObject(*bitmap);
    memDC.SetBackground(*wxTRANSPARENT_BRUSH);
    memDC.Clear();
    if (bmp.GetWidth() > 0)
	    memDC.DrawBitmap(bmp,  0, 									0, true);
    if (bmp2.GetWidth() > 0)
    	memDC.DrawBitmap(bmp2, bmp.GetWidth(), 						0, true);
    if (bmp3.GetWidth() > 0)
	    memDC.DrawBitmap(bmp3, bmp.GetWidth() + bmp2.GetWidth(), 	0, true);
    memDC.SelectObject(wxNullBitmap);

	return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, std::vector<wxBitmap> &bmps)
{
	size_t width  = 0;
	size_t height = 0;
	for (wxBitmap &bmp : bmps) {
		width += bmp.GetWidth();
		height = std::max<size_t>(height, bmp.GetHeight());
	}
	wxBitmap *bitmap = this->insert(bitmap_key, width, height);

    wxMemoryDC memDC;
    memDC.SelectObject(*bitmap);
    memDC.SetBackground(*wxTRANSPARENT_BRUSH);
    memDC.Clear();
    size_t x = 0;
	for (wxBitmap &bmp : bmps) {
	    if (bmp.GetWidth() > 0)
		    memDC.DrawBitmap(bmp, x, 0, true);
		x += bmp.GetWidth();
	}
    memDC.SelectObject(wxNullBitmap);

	return bitmap;
}

wxBitmap BitmapCache::mksolid(size_t width, size_t height, unsigned char r, unsigned char g, unsigned char b, unsigned char transparency)
{
    wxImage image(width, height);
    image.InitAlpha();
    unsigned char* imgdata = image.GetData();
    unsigned char* imgalpha = image.GetAlpha();
    for (size_t i = 0; i < width * height; ++ i) {
        *imgdata ++ = r;
        *imgdata ++ = g;
        *imgdata ++ = b;
        *imgalpha ++ = transparency;
    }
    return wxBitmap(std::move(image));
}

} // namespace GUI
} // namespace Slic3r
