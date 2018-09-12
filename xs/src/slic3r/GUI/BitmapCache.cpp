#include "BitmapCache.hpp"

#if ! defined(WIN32) && ! defined(__APPLE__)
#define BROKEN_ALPHA
#endif

#ifdef BROKEN_ALPHA
    #include <wx/mstream.h>
    #include <wx/rawbmp.h>
#endif /* BROKEN_ALPHA */

namespace Slic3r { namespace GUI {

void BitmapCache::clear()
{
    for (std::pair<const std::string, wxBitmap*> &bitmap : m_map)
        delete bitmap.second;
}

static wxBitmap wxImage_to_wxBitmap_with_alpha(wxImage &&image)
{
#ifdef BROKEN_ALPHA
    wxMemoryOutputStream stream;
    image.SaveFile(stream, wxBITMAP_TYPE_PNG);
    wxStreamBuffer *buf = stream.GetOutputStreamBuffer();
    return wxBitmap::NewFromPNGData(buf->GetBufferStart(), buf->GetBufferSize());
#else
    return wxBitmap(std::move(image));
#endif
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, size_t width, size_t height)
{
    wxBitmap *bitmap = nullptr;
    auto      it     = m_map.find(bitmap_key);
    if (it == m_map.end()) {
        bitmap = new wxBitmap(width, height);
        m_map[bitmap_key] = bitmap;
    } else {
        bitmap = it->second;
        if (bitmap->GetWidth() != width || bitmap->GetHeight() != height)
            bitmap->Create(width, height);
    }
#ifndef BROKEN_ALPHA
    bitmap->UseAlpha();
#endif
    return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap &bmp)
{
    wxBitmap *bitmap = nullptr;
    auto      it     = m_map.find(bitmap_key);
    if (it == m_map.end()) {
        bitmap = new wxBitmap(bmp);
        m_map[bitmap_key] = bitmap;
    } else {
        bitmap = it->second;
        *bitmap = bmp;
    }
    return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap &bmp, const wxBitmap &bmp2)
{
    // Copying the wxBitmaps is cheap as the bitmap's content is reference counted.
    const wxBitmap bmps[2] = { bmp, bmp2 };
    return this->insert(bitmap_key, bmps, bmps + 2);
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap &bmp, const wxBitmap &bmp2, const wxBitmap &bmp3)
{
    // Copying the wxBitmaps is cheap as the bitmap's content is reference counted.
    const wxBitmap bmps[3] = { bmp, bmp2, bmp3 };
    return this->insert(bitmap_key, bmps, bmps + 3);
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, const wxBitmap *begin, const wxBitmap *end)
{
    size_t width  = 0;
    size_t height = 0;
    for (const wxBitmap *bmp = begin; bmp != end; ++ bmp) {
        width += bmp->GetWidth();
        height = std::max<size_t>(height, bmp->GetHeight());
    }

#ifdef BROKEN_ALPHA

    wxImage image(width, height);
    image.InitAlpha();
    // Fill in with a white color.
    memset(image.GetData(), 0x0ff, width * height * 3);
    // Fill in with full transparency.
    memset(image.GetAlpha(),    0, width * height);
    size_t x = 0;
    for (const wxBitmap *bmp = begin; bmp != end; ++ bmp) {
        if (bmp->GetWidth() > 0) {
            if (bmp->GetDepth() == 32) {
                wxAlphaPixelData data(*const_cast<wxBitmap*>(bmp));
                data.UseAlpha();
                if (data) {
                    for (int r = 0; r < bmp->GetHeight(); ++ r) {
                        wxAlphaPixelData::Iterator src(data);
                        src.Offset(data, 0, r);
                        unsigned char *dst_pixels = image.GetData()  + (x + r * width) * 3;
                        unsigned char *dst_alpha  = image.GetAlpha() +  x + r * width;
                        for (int c = 0; c < bmp->GetWidth(); ++ c, ++ src) {
                            *dst_pixels ++ = src.Red();
                            *dst_pixels ++ = src.Green();
                            *dst_pixels ++ = src.Blue();
                            *dst_alpha  ++ = src.Alpha();
                        }
                    }
                }
            } else if (bmp->GetDepth() == 24) {
                wxNativePixelData data(*const_cast<wxBitmap*>(bmp));
                if (data) {
                    for (int r = 0; r < bmp->GetHeight(); ++ r) {
                        wxNativePixelData::Iterator src(data);
                        src.Offset(data, 0, r);
                        unsigned char *dst_pixels = image.GetData()  + (x + r * width) * 3;
                        unsigned char *dst_alpha  = image.GetAlpha() +  x + r * width;
                        for (int c = 0; c < bmp->GetWidth(); ++ c, ++ src) {
                            *dst_pixels ++ = src.Red();
                            *dst_pixels ++ = src.Green();
                            *dst_pixels ++ = src.Blue();
                            *dst_alpha  ++ = wxALPHA_OPAQUE;
                        }
                    }
                }
            }
        }
        x += bmp->GetWidth();
    }
    return this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image)));

#else

    wxBitmap *bitmap = this->insert(bitmap_key, width, height);
    wxMemoryDC memDC;
    memDC.SelectObject(*bitmap);
    memDC.SetBackground(*wxTRANSPARENT_BRUSH);
    memDC.Clear();
    size_t x = 0;
    for (const wxBitmap *bmp = begin; bmp != end; ++ bmp) {
        if (bmp->GetWidth() > 0)
            memDC.DrawBitmap(*bmp, x, 0, true);
        x += bmp->GetWidth();
    }
    memDC.SelectObject(wxNullBitmap);
    return bitmap;

#endif
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
    return wxImage_to_wxBitmap_with_alpha(std::move(image));
}

} // namespace GUI
} // namespace Slic3r
