#include "BitmapCache.hpp"

#include "libslic3r/Utils.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <boost/filesystem.hpp>

#ifdef __WXGTK2__
    // Broken alpha workaround
    #include <wx/mstream.h>
    #include <wx/rawbmp.h>
#endif /* __WXGTK2__ */

#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

namespace Slic3r { namespace GUI {

BitmapCache::BitmapCache()
{
#ifdef __APPLE__
    // Note: win->GetContentScaleFactor() is not used anymore here because it tends to
    // return bogus results quite often (such as 1.0 on Retina or even 0.0).
    // We're using the max scaling factor across all screens because it's very likely to be good enough.
    m_scale = mac_max_scaling_factor();
#endif
}

void BitmapCache::clear()
{
    for (std::pair<const std::string, wxBitmap*> &bitmap : m_map)
        delete bitmap.second;

    m_map.clear();
}

static wxBitmap wxImage_to_wxBitmap_with_alpha(wxImage &&image, float scale = 1.0f)
{
#ifdef __WXGTK2__
    // Broken alpha workaround
    wxMemoryOutputStream stream;
    image.SaveFile(stream, wxBITMAP_TYPE_PNG);
    wxStreamBuffer *buf = stream.GetOutputStreamBuffer();
    return wxBitmap::NewFromPNGData(buf->GetBufferStart(), buf->GetBufferSize());
#else
#ifdef __APPLE__
    // This is a c-tor native to Mac OS. We need to let the Mac OS wxBitmap implementation
    // know that the image may already be scaled appropriately for Retina,
    // and thereby that it's not supposed to upscale it.
    // Contrary to intuition, the `scale` argument isn't "please scale this to such and such"
    // but rather "the wxImage is sized for backing scale such and such".
    return wxBitmap(std::move(image), -1, scale);
#else
    return wxBitmap(std::move(image));
#endif
#endif
}

wxBitmapBundle* BitmapCache::insert_bndl(const std::string& name, const std::vector<wxBitmapBundle*>& bmps)
{
    wxVector<wxBitmap> bitmaps;

    std::set<double> scales = {1.0};
#ifndef __linux__

#ifdef __APPLE__
    scales.emplace(m_scale);
#else
    size_t disp_cnt = wxDisplay::GetCount();
    for (size_t disp = 0; disp < disp_cnt; ++disp)
        scales.emplace(wxDisplay(disp).GetScaleFactor());
#endif

#endif // !__linux__

    for (double scale : scales) {
        size_t width = 0;
        size_t height = 0;
        for (const wxBitmapBundle* bmp_bndl : bmps) {
#ifdef __APPLE__
            wxSize size = bmp_bndl->GetDefaultSize();
#else
            wxSize size = bmp_bndl->GetPreferredBitmapSizeAtScale(scale);
#endif
            width += size.GetWidth();
            height = std::max<size_t>(height, size.GetHeight());
        }

        std::string bitmap_key = name + "," +float_to_string_decimal_point(scale);

#ifdef __WXGTK2__
        // Broken alpha workaround
        wxImage image(width, height);
        image.InitAlpha();
        // Fill in with a white color.
        memset(image.GetData(), 0x0ff, width * height * 3);
        // Fill in with full transparency.
        memset(image.GetAlpha(), 0, width * height);
        size_t x = 0;
        for (const wxBitmapBundle* bmp_bndl : bmps) {
            wxBitmap bmp = bmp_bndl->GetBitmap(bmp_bndl->GetDefaultSize());
            if (bmp.GetWidth() > 0) {
                if (bmp.GetDepth() == 32) {
                    wxAlphaPixelData data(bmp);
                    //FIXME The following method is missing from wxWidgets 3.1.1.
                    // It looks like the wxWidgets 3.0.3 called the wrapped bitmap's UseAlpha().
                    //data.UseAlpha();
                    if (data) {
                        for (int r = 0; r < bmp.GetHeight(); ++r) {
                            wxAlphaPixelData::Iterator src(data);
                            src.Offset(data, 0, r);
                            unsigned char* dst_pixels = image.GetData() + (x + r * width) * 3;
                            unsigned char* dst_alpha = image.GetAlpha() + x + r * width;
                            for (int c = 0; c < bmp.GetWidth(); ++c, ++src) {
                                *dst_pixels++ = src.Red();
                                *dst_pixels++ = src.Green();
                                *dst_pixels++ = src.Blue();
                                *dst_alpha++ = src.Alpha();
                            }
                        }
                    }
                }
                else if (bmp.GetDepth() == 24) {
                    wxNativePixelData data(bmp);
                    if (data) {
                        for (int r = 0; r < bmp.GetHeight(); ++r) {
                            wxNativePixelData::Iterator src(data);
                            src.Offset(data, 0, r);
                            unsigned char* dst_pixels = image.GetData() + (x + r * width) * 3;
                            unsigned char* dst_alpha = image.GetAlpha() + x + r * width;
                            for (int c = 0; c < bmp.GetWidth(); ++c, ++src) {
                                *dst_pixels++ = src.Red();
                                *dst_pixels++ = src.Green();
                                *dst_pixels++ = src.Blue();
                                *dst_alpha++ = wxALPHA_OPAQUE;
                            }
                        }
                    }
                }
            }
            x += bmp.GetScaledWidth();
        }

        bitmaps.push_back(* this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image))));

#else

        wxBitmap* bitmap = this->insert(bitmap_key, width, height, scale);
        wxMemoryDC memDC;
        memDC.SelectObject(*bitmap);
        memDC.SetBackground(*wxTRANSPARENT_BRUSH);
        memDC.Clear();
        size_t x = 0;
        for (const wxBitmapBundle* bmp_bndl : bmps) {
            wxBitmap bmp = bmp_bndl->GetBitmap(bmp_bndl->GetPreferredBitmapSizeAtScale(scale));

            if (bmp.GetWidth() > 0)
                memDC.DrawBitmap(bmp, x, 0, true);
            // we should "move" with step equal to non-scaled width
#ifdef __APPLE__
            x += bmp.GetScaledWidth();
#else
            x += bmp.GetWidth();
#endif
        }
        memDC.SelectObject(wxNullBitmap);
        bitmaps.push_back(*bitmap);

#endif
    }

    return insert_bndl(name, bitmaps);
}

wxBitmapBundle* BitmapCache::insert_bndl(const std::string &bitmap_key, const char* data, size_t width, size_t height)
{
    wxBitmapBundle* bndl = nullptr;
    auto it = m_bndl_map.find(bitmap_key);
    if (it == m_bndl_map.end()) {
        bndl = new wxBitmapBundle(wxBitmapBundle::FromSVG(data, wxSize(width, height)));
        m_bndl_map[bitmap_key] = bndl;
    }
    else {
        bndl = it->second;
        *bndl = wxBitmapBundle::FromSVG(data, wxSize(width, height));
    }
    return bndl;
}

wxBitmapBundle* BitmapCache::insert_bndl(const std::string& bitmap_key, const wxBitmapBundle& bmp)
{
    wxBitmapBundle* bndl = nullptr;
    auto it = m_bndl_map.find(bitmap_key);
    if (it == m_bndl_map.end()) {
        bndl = new wxBitmapBundle(bmp);
        m_bndl_map[bitmap_key] = bndl;
    }
    else {
        bndl = it->second;
        *bndl = wxBitmapBundle(bmp);
    }
    return bndl;
}

wxBitmapBundle* BitmapCache::insert_bndl(const std::string& bitmap_key, const wxVector<wxBitmap>& bmps)
{
    wxBitmapBundle* bndl = nullptr;
    auto it = m_bndl_map.find(bitmap_key);
    if (it == m_bndl_map.end()) {
        bndl = new wxBitmapBundle(wxBitmapBundle::FromBitmaps(bmps));
        m_bndl_map[bitmap_key] = bndl;
    }
    else {
        bndl = it->second;
        *bndl = wxBitmapBundle::FromBitmaps(bmps);
    }
    return bndl;
}

wxBitmap* BitmapCache::insert(const std::string &bitmap_key, size_t width, size_t height, double scale/* = -1.0*/)
{
    wxBitmap *bitmap = nullptr;
    auto      it     = m_map.find(bitmap_key);
    if (it == m_map.end()) {
        bitmap = new wxBitmap(width, height
#ifdef __WXGTK3__
            , 32
#endif
            );
#ifdef __APPLE__
        // Contrary to intuition, the `scale` argument isn't "please scale this to such and such"
        // but rather "the wxImage is sized for backing scale such and such".
        // So, We need to let the Mac OS wxBitmap implementation
        // know that the image may already be scaled appropriately for Retina,
        // and thereby that it's not supposed to upscale it.
        bitmap->CreateScaled(width, height, -1, scale < 0.0 ? m_scale : scale);
#endif
        m_map[bitmap_key] = bitmap;
    } else {
        bitmap = it->second;
        if (size_t(bitmap->GetWidth()) != width || size_t(bitmap->GetHeight()) != height)
            bitmap->Create(width, height);
    }
#if defined(WIN32) || defined(__APPLE__)
    // Not needed or harmful for GTK2 and GTK3.
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

wxBitmap* BitmapCache::insert_raw_rgba(const std::string &bitmap_key, unsigned width, unsigned height, const unsigned char *raw_data, const bool grayscale/* = false*/)
{
    wxImage image(width, height);
    image.InitAlpha();
    unsigned char *rgb   = image.GetData();
    unsigned char *alpha = image.GetAlpha();
    unsigned int pixels = width * height;
    for (unsigned int i = 0; i < pixels; ++ i) {
        *rgb   ++ = *raw_data ++;
        *rgb   ++ = *raw_data ++;
        *rgb   ++ = *raw_data ++;
        *alpha ++ = *raw_data ++;
    }

    if (grayscale)
        image = image.ConvertToGreyscale(m_gs, m_gs, m_gs);

    return this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image), m_scale));
}

wxBitmap* BitmapCache::load_png(const std::string &bitmap_name, unsigned width, unsigned height,
    const bool grayscale/* = false*/, const float scale_in_center/* = 0*/) // BBS: support resize by fill border
{
    std::string bitmap_key = bitmap_name + ( height !=0 ? 
                                           "-h" + std::to_string(height) : 
                                           "-w" + std::to_string(width))
                                         + (grayscale ? "-gs" : "");

    auto it = m_map.find(bitmap_key);
    if (it != m_map.end())
        return it->second;

    wxImage image;
    if (! image.LoadFile(Slic3r::GUI::from_u8(Slic3r::var(bitmap_name + ".png")), wxBITMAP_TYPE_PNG) ||
        image.GetWidth() == 0 || image.GetHeight() == 0)
        return nullptr;

    if (height == 0 && width == 0)
        height = image.GetHeight();

    if (height != 0 && unsigned(image.GetHeight()) != height)
        width   = unsigned(0.5f + float(image.GetWidth()) * height / image.GetHeight());
    else if (width != 0 && unsigned(image.GetWidth()) != width)
        height  = unsigned(0.5f + float(image.GetHeight()) * width / image.GetWidth());

    if (height != 0 && width != 0) {
        // BBS: support resize by fill border
        if (scale_in_center > 0)
            image.Resize({ (int)width, (int)height }, { (int)(width - image.GetWidth()) / 2, (int)(height - image.GetHeight()) / 2 });
        else
            image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);
    }

    if (grayscale)
        image = image.ConvertToGreyscale(m_gs, m_gs, m_gs);

    return this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image)));
}

NSVGimage* BitmapCache::nsvgParseFromFileWithReplace(const char* filename, const char* units, float dpi, const std::map<std::string, std::string>& replaces)
{
    std::string str;
    FILE* fp = NULL;
    size_t size;
    char* data = NULL;
    NSVGimage* image = NULL;

    fp = boost::nowide::fopen(filename, "rb");
    if (!fp) goto error;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = (char*)malloc(size + 1);
    if (data == NULL) goto error;
    if (fread(data, 1, size, fp) != size) goto error;
    data[size] = '\0';	// Must be null terminated.
    fclose(fp);

    if (replaces.empty())
        image = nsvgParse(data, units, dpi);
    else {
        str.assign(data);
        for (auto val : replaces)
            boost::replace_all(str, val.first, val.second);
        image = nsvgParse(str.data(), units, dpi);
    }
    free(data);
    return image;

error:
    if (fp) fclose(fp);
    if (data) free(data);
    if (image) nsvgDelete(image);
    return NULL;
}

void BitmapCache::nsvgGetDataFromFileWithReplace(const char* filename, std::string& data_str, const std::map<std::string, std::string>& replaces)
{
    FILE* fp = NULL;
    size_t size;
    char* data = NULL;

    fp = boost::nowide::fopen(filename, "rb");
    if (!fp) goto error;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = (char*)malloc(size + 1);
    if (data == NULL) goto error;
    if (fread(data, 1, size, fp) != size) goto error;
    data[size] = '\0';	// Must be null terminated.
    fclose(fp);

    data_str.assign(data);
    for (auto val : replaces)
        boost::replace_all(data_str, val.first, val.second);

    free(data);
    return;

error:
    if (fp) fclose(fp);
    if (data) free(data);
    return;
}

wxBitmapBundle* BitmapCache::from_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height,
                                      const bool dark_mode, const std::string& new_color /*= ""*/)
{
    if (target_width == 0)
        target_width = target_height;
    std::string bitmap_key = bitmap_name + (target_height != 0 ?
        "-h" + std::to_string(target_height) :
        "-w" + std::to_string(target_width))
        + (dark_mode ? "-dm" : "")
        + new_color;

    auto it = m_bndl_map.find(bitmap_key);
    if (it != m_bndl_map.end())
        return it->second;

    // map of color replaces
    //Orca: use replaces from load_svg function
    std::map<std::string, std::string> replaces;
    replaces["\"#0x00AE42\""] = "\"#009688\"";
    replaces["\"#00FF00\""] = "\"#52c7b8\"";
    if (dark_mode) {
        replaces["\"#262E30\""] = "\"#EFEFF0\"";
        replaces["\"#323A3D\""] = "\"#B3B3B5\"";
        replaces["\"#808080\""] = "\"#818183\"";
        replaces["\"#CECECE\""] = "\"#54545B\"";
        replaces["\"#6B6B6B\""] = "\"#818182\"";
        replaces["\"#909090\""] = "\"#FFFFFF\"";
        replaces["\"#00FF00\""] = "\"#FF0000\"";
        replaces["\"#009688\""] = "\"#00675b\"";
    }

    std::string str;
    nsvgGetDataFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), str, replaces);
    if (str.empty())
        return nullptr;

    return insert_bndl(bitmap_key, str.data(), target_width, target_height);
}

wxBitmapBundle* BitmapCache::from_png(const std::string& bitmap_name, unsigned width, unsigned height)
{
    std::string bitmap_key = bitmap_name + (height != 0 ?
        "-h" + std::to_string(height) :
        "-w" + std::to_string(width));

    auto it = m_bndl_map.find(bitmap_key);
    if (it != m_bndl_map.end())
        return it->second;

    wxImage image;
    if (!image.LoadFile(Slic3r::GUI::from_u8(Slic3r::var(bitmap_name + ".png")), wxBITMAP_TYPE_PNG) ||
        image.GetWidth() == 0 || image.GetHeight() == 0)
        return nullptr;

    if (height != 0 && unsigned(image.GetHeight()) != height)
        width = unsigned(0.5f + float(image.GetWidth()) * height / image.GetHeight());
    else if (width != 0 && unsigned(image.GetWidth()) != width)
        height = unsigned(0.5f + float(image.GetHeight()) * width / image.GetWidth());

    if (height != 0 && width != 0)
        image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);

    return this->insert_bndl(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image)));
}

wxBitmap* BitmapCache::load_svg(const std::string &bitmap_name, unsigned target_width, unsigned target_height,
    const bool grayscale/* = false*/, const bool dark_mode/* = false*/, const std::string& new_color /*= ""*/, const float scale_in_center/* = 0*/)
{
    std::string bitmap_key = bitmap_name + ( target_height !=0 ? 
                                           "-h" + std::to_string(target_height) : 
                                           "-w" + std::to_string(target_width))
                                         + (m_scale != 1.0f ? "-s" + float_to_string_decimal_point(m_scale) : "")
                                         + (dark_mode ? "-dm" : "")
                                         + (grayscale ? "-gs" : "")
                                         + new_color;

    auto it = m_map.find(bitmap_key);
    if (it != m_map.end())
        return it->second;

    // map of color replaces
    std::map<std::string, std::string> replaces;
    replaces["\"#0x00AE42\""] = "\"#009688\"";
    replaces["\"#00FF00\""] = "\"#52c7b8\"";
    if (dark_mode) {
        replaces["\"#262E30\""] = "\"#EFEFF0\"";
        replaces["\"#323A3D\""] = "\"#B3B3B5\"";
        replaces["\"#808080\""] = "\"#818183\"";
        //replaces["\"#ACACAC\""] = "\"#54545A\"";
        replaces["\"#CECECE\""] = "\"#54545B\"";
        replaces["\"#6B6B6B\""] = "\"#818182\"";
        replaces["\"#909090\""] = "\"#FFFFFF\"";
        replaces["\"#00FF00\""] = "\"#FF0000\"";
        replaces["\"#009688\""] = "\"#00675b\"";
    }
    //if (!new_color.empty())
    //    replaces["\"#ED6B21\""] = "\"" + new_color + "\"";

     NSVGimage *image = nullptr;
    if (strstr(bitmap_name.c_str(), "printer_thumbnail") == NULL) {
        image =  nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, replaces);
    }
    else {
        std::map<std::string, std::string> temp_replaces;
        image =  nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, temp_replaces);
    }

    if (image == nullptr)
        return nullptr;

    if (target_height == 0 && target_width == 0)
        target_height = image->height;

    target_height != 0 ? target_height *= m_scale : target_width *= m_scale;

    float svg_scale = target_height != 0 ? 
                  (float)target_height / image->height  : target_width != 0 ?
                  (float)target_width / image->width    : 1;

    int   width    = (int)(svg_scale * image->width + 0.5f);
    int   height   = (int)(svg_scale * image->height + 0.5f);
    int   n_pixels = width * height;
    if (n_pixels <= 0) {
        ::nsvgDelete(image);
        return nullptr;
    }

    NSVGrasterizer *rast = ::nsvgCreateRasterizer();
    if (rast == nullptr) {
        ::nsvgDelete(image);
        return nullptr;
    }

    std::vector<unsigned char> data(n_pixels * 4, 0);
    // BBS: support resize by fill border
    if (scale_in_center > 0 && scale_in_center < svg_scale) {
        int w = (int)(image->width * scale_in_center);
        int h = (int)(image->height * scale_in_center);
        ::nsvgRasterize(rast, image, 0, 0, scale_in_center, data.data() + int(height - h) / 2 * width * 4 + int(width - w) / 2 * 4, w, h, width * 4);
    } else
        ::nsvgRasterize(rast, image, 0, 0, svg_scale, data.data(), width, height, width * 4);
    ::nsvgDeleteRasterizer(rast);
    ::nsvgDelete(image);

    return this->insert_raw_rgba(bitmap_key, width, height, data.data(), grayscale);
}
/*
//we make scaled solid bitmaps only for the cases, when its will be used with scaled SVG icon in one output bitmap
wxBitmap BitmapCache::mksolid(size_t width, size_t height, unsigned char r, unsigned char g, unsigned char b, unsigned char transparency, bool suppress_scaling/* = false* /, size_t border_width /*= 0* /, bool dark_mode/* = false* /)
{
    double scale = suppress_scaling ? 1.0f : m_scale;
    width  *= scale;
    height *= scale;

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

    // Add border, make white/light spools easier to see
    if (border_width > 0) {

        // Restrict to width of image
        if (border_width > height) border_width = height - 1;
        if (border_width > width) border_width = width - 1;

        auto px_data = (uint8_t*)image.GetData();
        auto a_data = (uint8_t*)image.GetAlpha();

        for (size_t x = 0; x < width; ++x) {
            for (size_t y = 0; y < height; ++y) {
                if (x < border_width || y < border_width ||
                    x >= (width - border_width) || y >= (height - border_width)) {
                    const size_t idx = (x + y * width);
                    const size_t idx_rgb = (x + y * width) * 3;
                    px_data[idx_rgb] = px_data[idx_rgb + 1] = px_data[idx_rgb + 2] = dark_mode ? 245u : 110u;
                    a_data[idx] = 255u;
                }
            }
        }
    }

    return wxImage_to_wxBitmap_with_alpha(std::move(image), scale);
}
*/
//we make scaled solid bitmaps only for the cases, when its will be used with scaled SVG icon in one output bitmap
wxBitmapBundle BitmapCache::mksolid(size_t width_in, size_t height_in, unsigned char r, unsigned char g, unsigned char b, unsigned char transparency, size_t border_width /*= 0*/, bool dark_mode/* = false*/)
{
    wxVector<wxBitmap> bitmaps;

    std::set<double> scales = { 1.0 };
#ifndef __linux__

#ifdef __APPLE__
    scales.emplace(m_scale);
#else
    size_t disp_cnt = wxDisplay::GetCount();
    for (size_t disp = 0; disp < disp_cnt; ++disp)
        scales.emplace(wxDisplay(disp).GetScaleFactor());
#endif

#endif // !__linux__

    for (double scale : scales) {
        size_t width = width_in * scale;
        size_t height = height_in * scale;

        wxImage image(width, height);
        image.InitAlpha();
        unsigned char* imgdata = image.GetData();
        unsigned char* imgalpha = image.GetAlpha();
        for (size_t i = 0; i < width * height; ++i) {
            *imgdata++ = r;
            *imgdata++ = g;
            *imgdata++ = b;
            *imgalpha++ = transparency;
        }

        // Add border, make white/light spools easier to see
        if (border_width > 0) {

            // Restrict to width of image
            if (border_width > height) border_width = height - 1;
            if (border_width > width) border_width = width - 1;

            auto px_data = (uint8_t*)image.GetData();
            auto a_data = (uint8_t*)image.GetAlpha();

            for (size_t x = 0; x < width; ++x) {
                for (size_t y = 0; y < height; ++y) {
                    if (x < border_width || y < border_width ||
                        x >= (width - border_width) || y >= (height - border_width)) {
                        const size_t idx = (x + y * width);
                        const size_t idx_rgb = (x + y * width) * 3;
                        px_data[idx_rgb] = px_data[idx_rgb + 1] = px_data[idx_rgb + 2] = dark_mode ? 245u : 110u;
                        a_data[idx] = 255u;
                    }
                }
            }
        }

        bitmaps.push_back(wxImage_to_wxBitmap_with_alpha(std::move(image), scale));
    }
    return wxBitmapBundle::FromBitmaps(bitmaps);
}

wxBitmapBundle* BitmapCache::mksolid_bndl(size_t width, size_t height, const std::string& color, size_t border_width, bool dark_mode)
{
    std::string bitmap_key = (color.empty() ? "empty" : color) + "-h" + std::to_string(height) + "-w" + std::to_string(width) + (dark_mode ? "-dm" : "");

    wxBitmapBundle* bndl = nullptr;
    auto it = m_bndl_map.find(bitmap_key);
    if (it == m_bndl_map.end()) {
        if (color.empty())
            bndl = new wxBitmapBundle(mksolid(width, height, 0, 0, 0, wxALPHA_TRANSPARENT, size_t(0)));
        else {
            ColorRGB rgb;// [3]
            decode_color(color, rgb);
            bndl = new wxBitmapBundle(mksolid(width, height, rgb.r_uchar(), rgb.g_uchar(), rgb.b_uchar(), wxALPHA_OPAQUE, border_width, dark_mode));
        }
        m_bndl_map[bitmap_key] = bndl;
    }
    else
        return it->second;

    return bndl;
}

bool BitmapCache::parse_color(const std::string& scolor, unsigned char* rgb_out)
{
    if (scolor.size() == 9) {
        unsigned char rgba[4];
        parse_color4(scolor, rgba);
        rgb_out[0] = rgba[0];
        rgb_out[1] = rgba[1];
        rgb_out[2] = rgba[2];
        return true;
    }
    rgb_out[0] = rgb_out[1] = rgb_out[2] = 0;
    if (scolor.size() != 7 || scolor.front() != '#')
        return false;
    const char* c = scolor.data() + 1;
    for (size_t i = 0; i < 3; ++i) {
        int digit1 = hex_digit_to_int(*c++);
        int digit2 = hex_digit_to_int(*c++);
        if (digit1 == -1 || digit2 == -1)
            return false;
        rgb_out[i] = (unsigned char)(digit1 * 16 + digit2);
    }

    return true;
}

bool BitmapCache::parse_color4(const std::string& scolor, unsigned char* rgba_out)
{
    rgba_out[0] = rgba_out[1] = rgba_out[2] = 0; rgba_out[3] = 255;
    if ((scolor.size() != 7 && scolor.size() != 9) || scolor.front() != '#')
        return false;
    const char* c = scolor.data() + 1;
    for (size_t i = 0; i < scolor.size() / 2; ++i) {
        int digit1 = hex_digit_to_int(*c++);
        int digit2 = hex_digit_to_int(*c++);
        if (digit1 == -1 || digit2 == -1)
            return false;
        rgba_out[i] = (unsigned char)(digit1 * 16 + digit2);
    }
    return true;
}

} // namespace GUI
} // namespace Slic3r
