#include "FontUtils.hpp"
#include "imgui/imstb_truetype.h"
#include "libslic3r/Utils.hpp"
#include <boost/log/trivial.hpp>

#if defined(__APPLE__)
#include <CoreText/CTFont.h>
#include <wx/uri.h>
#include <wx/fontutil.h> // wxNativeFontInfo
#include <wx/osx/core/cfdictionary.h>
#elif defined(__linux__)
#include "FontConfigHelp.hpp"
#endif

namespace Slic3r {

#ifdef __APPLE__
bool is_valid_ttf(std::string_view file_path)
{
    if (file_path.empty()) return false;
    auto const pos_point = file_path.find_last_of('.');
    if (pos_point == std::string_view::npos) return false;

    // use point only after last directory delimiter
    auto const pos_directory_delimiter = file_path.find_last_of("/\\");
    if (pos_directory_delimiter != std::string_view::npos && pos_point < pos_directory_delimiter) return false; // point is before directory delimiter

    // check count of extension chars
    size_t extension_size = file_path.size() - pos_point;
    if (extension_size >= 5) return false; // a lot of symbols for extension
    if (extension_size <= 1) return false; // few letters for extension

    std::string_view extension = file_path.substr(pos_point + 1, extension_size);

    // Because of MacOs - Courier, Geneva, Monaco
    if (extension == std::string_view("dfont")) return false;

    return true;
}

// get filepath from wxFont on Mac OsX
std::string get_file_path(const wxFont &font)
{
    const wxNativeFontInfo *info = font.GetNativeFontInfo();
    if (info == nullptr) return {};
    CTFontDescriptorRef descriptor = info->GetCTFontDescriptor();
    CFURLRef            typeref    = (CFURLRef) CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute);
    if (typeref == NULL) return {};
    ScopeGuard  sg([&typeref]() { CFRelease(typeref); });
    CFStringRef url = CFURLGetString(typeref);
    if (url == NULL) return {};
    wxString        file_uri(wxCFStringRef::AsString(url));
    wxURI           uri(file_uri);
    const wxString &path           = uri.GetPath();
    wxString        path_unescaped = wxURI::Unescape(path);
    std::string     path_str       = path_unescaped.ToUTF8().data();
    BOOST_LOG_TRIVIAL(trace) << "input uri(" << file_uri.c_str() << ") convert to path(" << path.c_str() << ") string(" << path_str << ").";
    return path_str;
}
#endif // __APPLE__
    
using fontinfo_opt = std::optional<stbtt_fontinfo>;

std::string get_human_readable_name(const wxFont &font)
{
    if (!font.IsOk()) return "Font is NOT ok.";
    // Face name is optional in wxFont
    if (!font.GetFaceName().empty()) {
        return std::string(font.GetFaceName().c_str());
    } else {
        return std::string((font.GetFamilyString() + " " + font.GetStyleString() + " " + font.GetWeightString()).c_str());
    }
}

fontinfo_opt load_font_info(const unsigned char *data, unsigned int index)
{
    int font_offset = stbtt_GetFontOffsetForIndex(data, index);
    if (font_offset < 0) {
        assert(false);
        // "Font index(" << index << ") doesn't exist.";
        return {};
    }
    stbtt_fontinfo font_info;
    if (stbtt_InitFont(&font_info, data, font_offset) == 0) {
        // Can't initialize font.
        assert(false);
        return {};
    }
    return font_info;
}

std::unique_ptr<FontFile> create_font_file(std::unique_ptr<std::vector<unsigned char>> data)
{
    int collection_size = stbtt_GetNumberOfFonts(data->data());
    // at least one font must be inside collection
    if (collection_size < 1) {
        assert(false);
        // There is no font collection inside font data
        return nullptr;
    }

    unsigned int                c_size = static_cast<unsigned int>(collection_size);
    std::vector<FontFile::Info> infos;
    infos.reserve(c_size);
    for (unsigned int i = 0; i < c_size; ++i) {
        auto font_info = load_font_info(data->data(), i);
        if (!font_info.has_value()) return nullptr;

        const stbtt_fontinfo *info = &(*font_info);
        // load information about line gap
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &linegap);

        float pixels       = 1000.; // value is irelevant
        float em_pixels    = stbtt_ScaleForMappingEmToPixels(info, pixels);
        int   units_per_em = static_cast<int>(std::round(pixels / em_pixels));

        infos.emplace_back(FontFile::Info{ascent, descent, linegap, units_per_em});
    }
    return std::make_unique<FontFile>(std::move(data), std::move(infos));
}

std::unique_ptr<FontFile> create_font_file(const char *file_path)
{
    FILE *file = std::fopen(file_path, "rb");
    if (file == nullptr) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Couldn't open " << file_path << " for reading.";
        return nullptr;
    }
    ScopeGuard sg([&file]() { std::fclose(file); });

    // find size of file
    if (fseek(file, 0L, SEEK_END) != 0) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Couldn't fseek file " << file_path << " for size measure.";
        return nullptr;
    }
    size_t size = ftell(file);
    if (size == 0) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Size of font file is zero. Can't read.";
        return nullptr;
    }
    rewind(file);
    auto   buffer             = std::make_unique<std::vector<unsigned char>>(size);
    size_t count_loaded_bytes = fread((void *) &buffer->front(), 1, size, file);
    if (count_loaded_bytes != size) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Different loaded(from file) data size.";
        return nullptr;
    }
    return create_font_file(std::move(buffer));
}

#ifdef _WIN32
bool load_hfont(void *hfont, DWORD &dwTable, DWORD &dwOffset, size_t &size, HDC hdc = nullptr)
{
    bool del_hdc = false;
    if (hdc == nullptr) {
        del_hdc = true;
        hdc     = ::CreateCompatibleDC(NULL);
        if (hdc == NULL) return false;
    }

    // To retrieve the data from the beginning of the file for TrueType
    // Collection files specify 'ttcf' (0x66637474).
    dwTable  = 0x66637474;
    dwOffset = 0;

    ::SelectObject(hdc, hfont);
    size = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
    if (size == GDI_ERROR) {
        // HFONT is NOT TTC(collection)
        dwTable = 0;
        size    = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
    }

    if (size == 0 || size == GDI_ERROR) {
        if (del_hdc) ::DeleteDC(hdc);
        return false;
    }
    return true;
}

std::unique_ptr<FontFile> create_font_file(void *hfont)
{
    HDC hdc = ::CreateCompatibleDC(NULL);
    if (hdc == NULL) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Can't create HDC by CreateCompatibleDC(NULL).";
        return nullptr;
    }

    DWORD  dwTable = 0, dwOffset = 0;
    size_t size;
    if (!load_hfont(hfont, dwTable, dwOffset, size, hdc)) {
        ::DeleteDC(hdc);
        return nullptr;
    }
    auto   buffer      = std::make_unique<std::vector<unsigned char>>(size);
    size_t loaded_size = ::GetFontData(hdc, dwTable, dwOffset, buffer->data(), size);
    ::DeleteDC(hdc);
    if (size != loaded_size) {
        assert(false);
        BOOST_LOG_TRIVIAL(error) << "Different loaded(from HFONT) data size.";
        return nullptr;
    }
    return create_font_file(std::move(buffer));
}
#endif

std::unique_ptr<FontFile> create_font_file(const wxFont &font)
{
#ifdef _WIN32
    return create_font_file(font.GetHFONT());
#elif defined(__APPLE__)
    std::string file_path = get_file_path(font);
    if (!is_valid_ttf(file_path)) {
        BOOST_LOG_TRIVIAL(error) << "Can not process font('" << get_human_readable_name(font) << "'), "
                                 << "file in path('" << file_path << "') is not valid TTF.";
        return nullptr;
    }
    return create_font_file(file_path.c_str());
#elif defined(__linux__)
    std::string font_path = Slic3r::GUI::get_font_path(font);
    if (font_path.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Can not read font('" << get_human_readable_name(font) << "'), "
                                 << "file path is empty.";
        return nullptr;
    }
    return create_font_file(font_path.c_str());
#else
    // HERE is place to add implementation for another platform
    // to convert wxFont to font data as windows or font file path as linux
    return nullptr;
#endif
}

bool can_generate_text_shape_from_font(const stbtt_fontinfo &font_info)
{
    const float flatness    = 0.0125f; // [in mm]
    wchar_t letter = 'A';
    int unicode_letter = static_cast<int>(letter);

    int glyph_index = stbtt_FindGlyphIndex(&font_info, unicode_letter);
    if (glyph_index == 0) {
        return false;
    }

    int advance_width=0, left_side_bearing=0;
    stbtt_GetGlyphHMetrics(&font_info, glyph_index, &advance_width, &left_side_bearing);

    stbtt_vertex *vertices;
    int           num_verts = stbtt_GetGlyphShape(&font_info, glyph_index, &vertices);
    if (num_verts <= 0)
        return false;
 
    return true;
}

bool can_generate_text_shape(const std::string& font_name) {
    wxFont wx_font(wxFontInfo().FaceName(font_name.c_str()).Encoding(wxFontEncoding::wxFONTENCODING_SYSTEM));
    std::unique_ptr<FontFile> font = create_font_file(wx_font);
    if (!font)
        return false;

    fontinfo_opt font_info_opt = load_font_info(font->data->data(), 0);
    if (!font_info_opt.has_value())
        return false;
    
    return can_generate_text_shape_from_font(*font_info_opt);
}

bool can_load(const wxFont &font)
{
#ifdef _WIN32
    DWORD  dwTable = 0, dwOffset = 0;
    size_t size  = 0;
    void * hfont = font.GetHFONT();
    if (!load_hfont(hfont, dwTable, dwOffset, size)) return false;
    return hfont != nullptr;
#elif defined(__APPLE__)
    return true;
#elif defined(__linux__)
    return true;
#endif
    return false;
}

}
