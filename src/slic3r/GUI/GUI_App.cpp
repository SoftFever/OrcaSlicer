#include "libslic3r/Technologies.hpp"
#include "GUI_App.hpp"
#include "GUI_Init.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "format.hpp"
#include "I18N.hpp"

#include <algorithm>
#include <iterator>
#include <exception>
#include <cstdlib>
#include <regex>
#include <thread>
#include <string_view>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/richmsgdlg.h>
#include <wx/log.h>
#include <wx/intl.h>

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/splash.h>
#include <wx/fontutil.h>

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Utils.hpp"

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "3DScene.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Process.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/Http.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "SysInfoDialog.hpp"
#include "UpdateDialogs.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "NotificationManager.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "SendSystemInfoDialog.hpp"
#include "ParamsDialog.hpp"
#include "KBShortcutsDialog.hpp"
#include "DownloadProgressDialog.hpp"

#include "BitmapCache.hpp"
#include "Notebook.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressDialog.hpp"

//BBS: DailyTip and UserGuide Dialog
#include "WebDownPluginDlg.hpp"
#include "WebGuideDialog.hpp"
#include "WebUserLoginDialog.hpp"
#include "ReleaseNote.hpp"
#include "ModelMall.hpp"

//#ifdef WIN32
//#include "BaseException.h"
//#endif


#ifdef __WXMSW__
#include <dbt.h>
#include <shlobj.h>

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#include "wx/headerctrl.h"
#include "wx/msw/headerctrl.h"
#endif // _MSW_DARK_MODE
#endif // __WINDOWS__

#endif
#ifdef _WIN32
#include <boost/dll/runtime_symbol_info.hpp>
#endif

#ifdef WIN32
#include "BaseException.h"
#endif

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
#include <boost/beast/core/detail/base64.hpp>
#include <boost/nowide/fstream.hpp>
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

// Needed for forcing menu icons back under gtk2 and gtk3
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <gtk/gtk.h>
#endif

using namespace std::literals;
namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

class MainFrame;


std::string VersionInfo::convert_full_version(std::string short_version)
{
    std::string result = "";
    std::vector<std::string> items;
    boost::split(items, short_version, boost::is_any_of("."));
    if (items.size() == VERSION_LEN) {
        for (int i = 0; i < VERSION_LEN; i++) {
            std::stringstream ss;
            ss << std::setw(2) << std::setfill('0') << items[i];
            result += ss.str();
            if (i != VERSION_LEN - 1)
                result += ".";
        }
        return result;
    }
    return result;
}

std::string VersionInfo::convert_short_version(std::string full_version)
{
    full_version.erase(std::remove(full_version.begin(), full_version.end(), '0'), full_version.end());
    return full_version;
}

static std::string convert_studio_language_to_api(std::string lang_code)
{
    boost::replace_all(lang_code, "_", "-");
    return lang_code;

    /*if (lang_code == "zh_CN")
        return "zh-hans";
    else if (lang_code == "zh_TW")
        return "zh-hant";
    else
        return "en";*/
}

#ifdef _WIN32
bool is_associate_files(std::wstring extend)
{
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_id             = L" Bambu.Studio.1";
    std::wstring reg_base            = L"Software\\Classes";
    std::wstring reg_extension       = reg_base + L"\\." + extend;

    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(HKEY_CURRENT_USER, reg_extension.c_str(), nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if (!bDidntExist && ::wcscmp(szValueCurrent, prog_id.c_str()) == 0)
        return true;

    return false;
}
#endif

class BBLSplashScreen : public wxSplashScreen
{
public:
    BBLSplashScreen(const wxBitmap& bitmap, long splashStyle, int milliseconds, wxPoint pos = wxDefaultPosition)
        : wxSplashScreen(bitmap, splashStyle, milliseconds, static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, wxDefaultPosition, wxDefaultSize,
#ifdef __APPLE__
            wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#else
            wxBORDER_NONE | wxFRAME_NO_TASKBAR
#endif // !__APPLE__
        )
    {
        int init_dpi = get_dpi_for_window(this);
        this->SetPosition(pos);
        this->CenterOnScreen();
        int new_dpi = get_dpi_for_window(this);

        m_scale = (float)(new_dpi) / (float)(init_dpi);

        m_main_bitmap = bitmap;

        scale_bitmap(m_main_bitmap, m_scale);

        // init constant texts and scale fonts
        m_constant_text.init(Label::Body_16);
        scale_font(m_constant_text.title_font, 2.0f);
        scale_font(m_constant_text.version_font, 1.2f);

        // this font will be used for the action string
        m_action_font = m_constant_text.credits_font;

        // draw logo and constant info text
        Decorate(m_main_bitmap);
        wxGetApp().UpdateFrameDarkUI(this);
    }

    void SetText(const wxString& text)
    {
        set_bitmap(m_main_bitmap);
        if (!text.empty()) {
            wxBitmap bitmap(m_main_bitmap);

            wxMemoryDC memDC;
            memDC.SelectObject(bitmap);
            memDC.SetFont(m_action_font);
            memDC.SetTextForeground(StateColor::darkModeColorFor(wxColour(144, 144, 144)));
            int width = bitmap.GetWidth();
            int text_height = memDC.GetTextExtent(text).GetHeight();
            int text_width = memDC.GetTextExtent(text).GetWidth();
            wxRect text_rect(wxPoint(0, m_action_line_y_position), wxPoint(width, m_action_line_y_position + text_height));
            memDC.DrawLabel(text, text_rect, wxALIGN_CENTER);

            memDC.SelectObject(wxNullBitmap);
            set_bitmap(bitmap);
#ifdef __WXOSX__
            // without this code splash screen wouldn't be updated under OSX
            wxYield();
#endif
        }
    }

    void Decorate(wxBitmap& bmp)
    {
        if (!bmp.IsOk())
            return;

        // use a memory DC to draw directly onto the bitmap
        wxMemoryDC memDc(bmp);

        int top_margin = FromDIP(75 * m_scale);
        int width = bmp.GetWidth();

        // draw title and version
        int text_padding = FromDIP(3 * m_scale);
        memDc.SetFont(m_constant_text.title_font);
        int title_height = memDc.GetTextExtent(m_constant_text.title).GetHeight();
        int title_width = memDc.GetTextExtent(m_constant_text.title).GetWidth();
        memDc.SetFont(m_constant_text.version_font);
        int version_height = memDc.GetTextExtent(m_constant_text.version).GetHeight();
        int version_width = memDc.GetTextExtent(m_constant_text.version).GetWidth();
        int split_width = (width + title_width - version_width) / 2;
        wxRect title_rect(wxPoint(0, top_margin), wxPoint(split_width - text_padding, top_margin + title_height));
        memDc.SetTextForeground(StateColor::darkModeColorFor(wxColour(38, 46, 48)));
        memDc.SetFont(m_constant_text.title_font);
        memDc.DrawLabel(m_constant_text.title, title_rect, wxALIGN_RIGHT | wxALIGN_BOTTOM);
        //BBS align bottom of title and version text
        wxRect version_rect(wxPoint(split_width + text_padding, top_margin), wxPoint(width, top_margin + title_height - text_padding));
        memDc.SetFont(m_constant_text.version_font);
        memDc.SetTextForeground(StateColor::darkModeColorFor(wxColor(134, 134, 134)));
        memDc.DrawLabel(m_constant_text.version, version_rect, wxALIGN_LEFT | wxALIGN_BOTTOM);

#if BBL_INTERNAL_TESTING
        wxSize text_rect = memDc.GetTextExtent("Internal Version");
        int start_x = (title_rect.GetLeft() + version_rect.GetRight()) / 2 - text_rect.GetWidth();
        int start_y = version_rect.GetBottom() + 10;
        wxRect internal_sign_rect(wxPoint(start_x, start_y), wxSize(text_rect));
        memDc.SetFont(m_constant_text.title_font);
        memDc.DrawLabel("Internal Version", internal_sign_rect, wxALIGN_TOP | wxALIGN_LEFT);
#endif

        // load bitmap for logo
        BitmapCache bmp_cache;
        int logo_margin = FromDIP(72 * m_scale);
        int logo_size = FromDIP(122 * m_scale);
        int logo_width = FromDIP(94 * m_scale);
        wxBitmap logo_bmp = *bmp_cache.load_svg("splash_logo", logo_size, logo_size);
        int logo_y = top_margin + title_rect.GetHeight() + logo_margin;
        memDc.DrawBitmap(logo_bmp, (width - logo_width) / 2, logo_y, true);

        // calculate position for the dynamic text
        int text_margin = FromDIP(80 * m_scale);
        m_action_line_y_position = logo_y + logo_size + text_margin;
    }

    static wxBitmap MakeBitmap()
    {
        int width = FromDIP(480, nullptr);
        int height = FromDIP(480, nullptr);

        wxImage image(width, height);
        wxBitmap new_bmp(image);

        wxMemoryDC memDC;
        memDC.SelectObject(new_bmp);
        memDC.SetBrush(StateColor::darkModeColorFor(*wxWHITE));
        memDC.DrawRectangle(-1, -1, width + 2, height + 2);
        memDC.DrawBitmap(new_bmp, 0, 0, true);
        return new_bmp;
    }

    void set_bitmap(wxBitmap& bmp)
    {
        m_window->SetBitmap(bmp);
        m_window->Refresh();
        m_window->Update();
    }

    void scale_bitmap(wxBitmap& bmp, float scale)
    {
        if (scale == 1.0)
            return;

        wxImage image = bmp.ConvertToImage();
        if (!image.IsOk() || image.GetWidth() == 0 || image.GetHeight() == 0)
            return;

        int width   = int(scale * image.GetWidth());
        int height  = int(scale * image.GetHeight());
        image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);

        bmp = wxBitmap(std::move(image));
    }

    void scale_font(wxFont& font, float scale)
    {
#ifdef __WXMSW__
        // Workaround for the font scaling in respect to the current active display,
        // not for the primary display, as it's implemented in Font.cpp
        // See https://github.com/wxWidgets/wxWidgets/blob/master/src/msw/font.cpp
        // void wxNativeFontInfo::SetFractionalPointSize(float pointSizeNew)
        wxNativeFontInfo nfi= *font.GetNativeFontInfo();
        float pointSizeNew  = scale * font.GetPointSize();
        nfi.lf.lfHeight     = nfi.GetLogFontHeightAtPPI(pointSizeNew, get_dpi_for_window(this));
        nfi.pointSize       = pointSizeNew;
        font = wxFont(nfi);
#else
        font.Scale(scale);
#endif //__WXMSW__
    }


private:
    wxStaticText* m_staticText_slicer_name;
    wxStaticText* m_staticText_slicer_version;
    wxStaticBitmap* m_bitmap;
    wxStaticText* m_staticText_loading;

    wxBitmap    m_main_bitmap;
    wxFont      m_action_font;
    int         m_action_line_y_position;
    float       m_scale {1.0};

    struct ConstantText
    {
        wxString title;
        wxString version;
        wxString credits;

        wxFont   title_font;
        wxFont   version_font;
        wxFont   credits_font;

        void init(wxFont init_font)
        {
            // title
            title = wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME;

            // dynamically get the version to display
            version = _L("V") + " " + GUI_App::format_display_version();

            // credits infornation
            credits = "";

            title_font = Label::Head_16;
            version_font = Label::Body_16;
            credits_font = init_font;
        }
    }
    m_constant_text;
};

class SplashScreen : public wxSplashScreen
{
public:
    SplashScreen(const wxBitmap& bitmap, long splashStyle, int milliseconds, wxPoint pos = wxDefaultPosition)
        : wxSplashScreen(bitmap, splashStyle, milliseconds, static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, wxDefaultPosition, wxDefaultSize,
#ifdef __APPLE__
            wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#else
            wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR
#endif // !__APPLE__
        )
    {
        wxASSERT(bitmap.IsOk());

        int init_dpi = get_dpi_for_window(this);
        this->SetPosition(pos);
        this->CenterOnScreen();
        int new_dpi = get_dpi_for_window(this);

        m_scale         = (float)(new_dpi) / (float)(init_dpi);

        m_main_bitmap   = bitmap;

        scale_bitmap(m_main_bitmap, m_scale);

        // init constant texts and scale fonts
        init_constant_text();

        // this font will be used for the action string
        m_action_font = m_constant_text.credits_font.Bold();

        // draw logo and constant info text
        Decorate(m_main_bitmap);
    }

    void SetText(const wxString& text)
    {
        set_bitmap(m_main_bitmap);
        if (!text.empty()) {
            wxBitmap bitmap(m_main_bitmap);

            wxMemoryDC memDC;
            memDC.SelectObject(bitmap);

            memDC.SetFont(m_action_font);
            memDC.SetTextForeground(wxColour(237, 107, 33));
            memDC.DrawText(text, int(m_scale * 60), m_action_line_y_position);

            memDC.SelectObject(wxNullBitmap);
            set_bitmap(bitmap);
#ifdef __WXOSX__
            // without this code splash screen wouldn't be updated under OSX
            wxYield();
#endif
        }
    }

    static wxBitmap MakeBitmap(wxBitmap bmp)
    {
        if (!bmp.IsOk())
            return wxNullBitmap;

        // create dark grey background for the splashscreen
        // It will be 5/3 of the weight of the bitmap
        int width = lround((double)5 / 3 * bmp.GetWidth());
        int height = bmp.GetHeight();

        wxImage image(width, height);
        unsigned char* imgdata_ = image.GetData();
        for (int i = 0; i < width * height; ++i) {
            *imgdata_++ = 51;
            *imgdata_++ = 51;
            *imgdata_++ = 51;
        }

        wxBitmap new_bmp(image);

        wxMemoryDC memDC;
        memDC.SelectObject(new_bmp);
        memDC.DrawBitmap(bmp, width - bmp.GetWidth(), 0, true);

        return new_bmp;
    }

    void Decorate(wxBitmap& bmp)
    {
        if (!bmp.IsOk())
            return;

        // draw text to the box at the left of the splashscreen.
        // this box will be 2/5 of the weight of the bitmap, and be at the left.
        int width = lround(bmp.GetWidth() * 0.4);

        // load bitmap for logo
        BitmapCache bmp_cache;
        int logo_size = lround(width * 0.25);
        wxBitmap logo_bmp = *bmp_cache.load_svg(wxGetApp().logo_name(), logo_size, logo_size);

        wxCoord margin = int(m_scale * 20);

        wxRect banner_rect(wxPoint(0, logo_size), wxPoint(width, bmp.GetHeight()));
        banner_rect.Deflate(margin, 2 * margin);

        // use a memory DC to draw directly onto the bitmap
        wxMemoryDC memDc(bmp);

        // draw logo
        memDc.DrawBitmap(logo_bmp, margin, margin, true);

        // draw the (white) labels inside of our black box (at the left of the splashscreen)
        memDc.SetTextForeground(wxColour(255, 255, 255));

        memDc.SetFont(m_constant_text.title_font);
        memDc.DrawLabel(m_constant_text.title,   banner_rect, wxALIGN_TOP | wxALIGN_LEFT);

        int title_height = memDc.GetTextExtent(m_constant_text.title).GetY();
        banner_rect.SetTop(banner_rect.GetTop() + title_height);
        banner_rect.SetHeight(banner_rect.GetHeight() - title_height);

        memDc.SetFont(m_constant_text.version_font);
        memDc.DrawLabel(m_constant_text.version, banner_rect, wxALIGN_TOP | wxALIGN_LEFT);
        int version_height = memDc.GetTextExtent(m_constant_text.version).GetY();

        memDc.SetFont(m_constant_text.credits_font);
        memDc.DrawLabel(m_constant_text.credits, banner_rect, wxALIGN_BOTTOM | wxALIGN_LEFT);
        int credits_height = memDc.GetMultiLineTextExtent(m_constant_text.credits).GetY();
        int text_height    = memDc.GetTextExtent("text").GetY();

        // calculate position for the dynamic text
        int logo_and_header_height = margin + logo_size + title_height + version_height;
        m_action_line_y_position = logo_and_header_height + 0.5 * (bmp.GetHeight() - margin - credits_height - logo_and_header_height - text_height);
    }

private:
    wxBitmap    m_main_bitmap;
    wxFont      m_action_font;
    int         m_action_line_y_position;
    float       m_scale {1.0};

    struct ConstantText
    {
        wxString title;
        wxString version;
        wxString credits;

        wxFont   title_font;
        wxFont   version_font;
        wxFont   credits_font;

        void init(wxFont init_font)
        {
            // title
            title = wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME;

            // dynamically get the version to display
            auto version_text = GUI_App::format_display_version();
#if BBL_INTERNAL_TESTING
            version = _L("Internal Version") + " " + std::string(version_text);
#else
            version = _L("Version") + " " + std::string(version_text);
#endif

            // credits infornation
            credits =   title;

            title_font = version_font = credits_font = init_font;
        }
    }
    m_constant_text;

    void init_constant_text()
    {
        m_constant_text.init(get_default_font(this));

        // As default we use a system font for current display.
        // Scale fonts in respect to banner width

        int text_banner_width = lround(0.4 * m_main_bitmap.GetWidth()) - roundl(m_scale * 50); // banner_width - margins

        float title_font_scale = (float)text_banner_width / GetTextExtent(m_constant_text.title).GetX();
        scale_font(m_constant_text.title_font, title_font_scale > 3.5f ? 3.5f : title_font_scale);

        float version_font_scale = (float)text_banner_width / GetTextExtent(m_constant_text.version).GetX();
        scale_font(m_constant_text.version_font, version_font_scale > 2.f ? 2.f : version_font_scale);

        // The width of the credits information string doesn't respect to the banner width some times.
        // So, scale credits_font in the respect to the longest string width
        int   longest_string_width = word_wrap_string(m_constant_text.credits);
        float font_scale = (float)text_banner_width / longest_string_width;
        scale_font(m_constant_text.credits_font, font_scale);
    }

    void set_bitmap(wxBitmap& bmp)
    {
        m_window->SetBitmap(bmp);
        m_window->Refresh();
        m_window->Update();
    }

    void scale_bitmap(wxBitmap& bmp, float scale)
    {
        if (scale == 1.0)
            return;

        wxImage image = bmp.ConvertToImage();
        if (!image.IsOk() || image.GetWidth() == 0 || image.GetHeight() == 0)
            return;

        int width   = int(scale * image.GetWidth());
        int height  = int(scale * image.GetHeight());
        image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);

        bmp = wxBitmap(std::move(image));
    }

    void scale_font(wxFont& font, float scale)
    {
#ifdef __WXMSW__
        // Workaround for the font scaling in respect to the current active display,
        // not for the primary display, as it's implemented in Font.cpp
        // See https://github.com/wxWidgets/wxWidgets/blob/master/src/msw/font.cpp
        // void wxNativeFontInfo::SetFractionalPointSize(float pointSizeNew)
        wxNativeFontInfo nfi= *font.GetNativeFontInfo();
        float pointSizeNew  = scale * font.GetPointSize();
        nfi.lf.lfHeight     = nfi.GetLogFontHeightAtPPI(pointSizeNew, get_dpi_for_window(this));
        nfi.pointSize       = pointSizeNew;
        font = wxFont(nfi);
#else
        font.Scale(scale);
#endif //__WXMSW__
    }

    // wrap a string for the strings no longer then 55 symbols
    // return extent of the longest string
    int word_wrap_string(wxString& input)
    {
        size_t line_len = 55;// count of symbols in one line
        int idx = -1;
        size_t cur_len = 0;

        wxString longest_sub_string;
        auto get_longest_sub_string = [input](wxString &longest_sub_str, size_t cur_len, size_t i) {
            if (cur_len > longest_sub_str.Len())
                longest_sub_str = input.SubString(i - cur_len + 1, i);
        };

        for (size_t i = 0; i < input.Len(); i++)
        {
            cur_len++;
            if (input[i] == ' ')
                idx = i;
            if (input[i] == '\n')
            {
                get_longest_sub_string(longest_sub_string, cur_len, i);
                idx = -1;
                cur_len = 0;
            }
            if (cur_len >= line_len && idx >= 0)
            {
                get_longest_sub_string(longest_sub_string, cur_len, i);
                input[idx] = '\n';
                cur_len = i - static_cast<size_t>(idx);
            }
        }

        return GetTextExtent(longest_sub_string).GetX();
    }
};


#ifdef __linux__
bool static check_old_linux_datadir(const wxString& app_name) {
    // If we are on Linux and the datadir does not exist yet, look into the old
    // location where the datadir was before version 2.3. If we find it there,
    // tell the user that he might wanna migrate to the new location.
    // (https://github.com/prusa3d/PrusaSlicer/issues/2911)
    // To be precise, the datadir should exist, it is created when single instance
    // lock happens. Instead of checking for existence, check the contents.

    namespace fs = boost::filesystem;

    std::string new_path = Slic3r::data_dir();

    wxString dir;
    if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
        dir = wxFileName::GetHomeDir() + wxS("/.config");
    std::string default_path = (dir + "/" + app_name).ToUTF8().data();

    if (new_path != default_path) {
        // This happens when the user specifies a custom --datadir.
        // Do not show anything in that case.
        return true;
    }

    fs::path data_dir = fs::path(new_path);
    if (! fs::is_directory(data_dir))
        return true; // This should not happen.

    int file_count = std::distance(fs::directory_iterator(data_dir), fs::directory_iterator());

    if (file_count <= 1) { // just cache dir with an instance lock
        // BBS
    } else {
        // If the new directory exists, be silent. The user likely already saw the message.
    }
    return true;
}
#endif

struct FileWildcards {
    std::string_view              title;
    std::vector<std::string_view> file_extensions;
};

static const FileWildcards file_wildcards_by_type[FT_SIZE] = {
    /* FT_STEP */    { "STEP files"sv,      { ".stp"sv, ".step"sv } },
    /* FT_STL */     { "STL files"sv,       { ".stl"sv } },
    /* FT_OBJ */     { "OBJ files"sv,       { ".obj"sv } },
    /* FT_AMF */     { "AMF files"sv,       { ".amf"sv, ".zip.amf"sv, ".xml"sv } },
    /* FT_3MF */     { "3MF files"sv,       { ".3mf"sv } },
    /* FT_GCODE */   { "G-code files"sv,    { ".gcode"sv } },
    /* FT_MODEL */   {"Supported files"sv,  {".3mf"sv, ".stl"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv }},
    /* FT_PROJECT */ { "Project files"sv,   { ".3mf"sv} },
    /* FT_GALLERY */ { "Known files"sv,     { ".stl"sv, ".obj"sv } },

    /* FT_INI */     { "INI files"sv,       { ".ini"sv } },
    /* FT_SVG */     { "SVG files"sv,       { ".svg"sv } },
    /* FT_TEX */     { "Texture"sv,         { ".png"sv, ".svg"sv } },
    /* FT_SL1 */     { "Masked SLA files"sv, { ".sl1"sv, ".sl1s"sv } },
};

// This function produces a Win32 file dialog file template mask to be consumed by wxWidgets on all platforms.
// The function accepts a custom extension parameter. If the parameter is provided, the custom extension
// will be added as a fist to the list. This is important for a "file save" dialog on OSX, which strips
// an extension from the provided initial file name and substitutes it with the default extension (the first one in the template).
wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    const FileWildcards& data = file_wildcards_by_type[file_type];
    std::string title;
    std::string mask;
    std::string custom_ext_lower;

    if (! custom_extension.empty()) {
        // Generate an extension into the title mask and into the list of extensions.
        custom_ext_lower = boost::to_lower_copy(custom_extension);
        const std::string custom_ext_upper = boost::to_upper_copy(custom_extension);
        if (custom_ext_lower == custom_extension) {
            // Add a lower case version.
            title = std::string("*") + custom_ext_lower;
            mask = title;
            // Add an upper case version.
            mask  += ";*";
            mask  += custom_ext_upper;
        } else if (custom_ext_upper == custom_extension) {
            // Add an upper case version.
            title = std::string("*") + custom_ext_upper;
            mask = title;
            // Add a lower case version.
            mask += ";*";
            mask += custom_ext_lower;
        } else {
            // Add the mixed case version only.
            title = std::string("*") + custom_extension;
            mask = title;
        }
    }

    for (const std::string_view &ext : data.file_extensions)
        // Only add an extension if it was not added first as the custom extension.
        if (ext != custom_ext_lower) {
            if (title.empty()) {
                title = "*";
                title += ext;
                mask  = title;
            } else {
                title += ", *";
                title += ext;
                mask  += ";*";
                mask  += ext;
            }
            mask += ";*";
            mask += boost::to_upper_copy(std::string(ext));
        }
    return GUI::format_wxstr("%s (%s)|%s", data.title, title, mask);
}

static std::string libslic3r_translate_callback(const char *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data(); }

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
static void register_win32_dpi_event()
{
    enum { WM_DPICHANGED_ = 0x02e0 };

    wxWindow::MSWRegisterMessageHandler(WM_DPICHANGED_, [](wxWindow *win, WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
        const int dpi = wParam & 0xffff;
        const auto rect = reinterpret_cast<PRECT>(lParam);
        const wxRect wxrect(wxPoint(rect->top, rect->left), wxPoint(rect->bottom, rect->right));

        DpiChangedEvent evt(EVT_DPI_CHANGED_SLICER, dpi, wxrect);
        win->GetEventHandler()->AddPendingEvent(evt);

        return true;
    });
}
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

static GUID GUID_DEVINTERFACE_HID = { 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };

static void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(WM_DEVICECHANGE, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		        plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME) {
//					printf("DBT_DEVICEARRIVAL %d - Media has arrived: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
			        plater->GetEventHandler()->AddPendingEvent(HIDDeviceAttachedEvent(EVT_HID_DEVICE_ATTACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
            break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME)
//					printf("DBT_DEVICEARRIVAL %d - Media was removed: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        			plater->GetEventHandler()->AddPendingEvent(HIDDeviceDetachedEvent(EVT_HID_DEVICE_DETACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
			break;
        default:
            break;
        }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(MainFrame::WM_USER_MEDIACHANGED, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
        wchar_t sPath[MAX_PATH];
        if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
            struct _ITEMIDLIST* pidl = *reinterpret_cast<struct _ITEMIDLIST**>(wParam);
            if (! SHGetPathFromIDList(pidl, sPath)) {
                BOOST_LOG_TRIVIAL(error) << "MediaInserted: SHGetPathFromIDList failed";
                return false;
            }
        }
        switch (lParam) {
        case SHCNE_MEDIAINSERTED:
        {
            //printf("SHCNE_MEDIAINSERTED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
            break;
        }
        case SHCNE_MEDIAREMOVED:
        {
            //printf("SHCNE_MEDIAREMOVED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
            break;
        }
	    default:
//          printf("Unknown\n");
            break;
	    }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(WM_INPUT, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(Slic3r::GUI::find_toplevel_parent(win));
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
//        if (wParam == RIM_INPUTSINK && plater != nullptr && main_frame->IsActive()) {
        if (wParam == RIM_INPUT && plater != nullptr && main_frame->IsActive()) {
        RAWINPUT raw;
			UINT rawSize = sizeof(RAWINPUT);
			::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
			if (raw.header.dwType == RIM_TYPEHID && plater->get_mouse3d_controller().handle_raw_input_win32(raw.data.hid.bRawData, raw.data.hid.dwSizeHid))
				return true;
		}
        return false;
    });

	//wxWindow::MSWRegisterMessageHandler(WM_COPYDATA, [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
	//	COPYDATASTRUCT* copy_data_structure = { 0 };
	//	copy_data_structure = (COPYDATASTRUCT*)lParam;
	//	if (copy_data_structure->dwData == 1) {
	//		LPCWSTR arguments = (LPCWSTR)copy_data_structure->lpData;
	//		Slic3r::GUI::wxGetApp().other_instance_message_handler()->handle_message(boost::nowide::narrow(arguments));
	//	}
	//	return true;
	//	});
}
#endif // WIN32

static void generic_exception_handle()
{
    // Note: Some wxWidgets APIs use wxLogError() to report errors, eg. wxImage
    // - see https://docs.wxwidgets.org/3.1/classwx_image.html#aa249e657259fe6518d68a5208b9043d0
    //
    // wxLogError typically goes around exception handling and display an error dialog some time
    // after an error is logged even if exception handling and OnExceptionInMainLoop() take place.
    // This is why we use wxLogError() here as well instead of a custom dialog, because it accumulates
    // errors if multiple have been collected and displays just one error message for all of them.
    // Otherwise we would get multiple error messages for one missing png, for example.
    //
    // If a custom error message window (or some other solution) were to be used, it would be necessary
    // to turn off wxLogError() usage in wx APIs, most notably in wxImage
    // - see https://docs.wxwidgets.org/trunk/classwx_image.html#aa32e5d3507cc0f8c3330135bc0befc6a
/*#ifdef WIN32
    //LPEXCEPTION_POINTERS exception_pointers = nullptr;
    __try {
        throw;
    }
    __except (CBaseException::UnhandledExceptionFilter2(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {
    //__except (exception_pointers = GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER) {
    //    if (exception_pointers) {
    //        CBaseException::UnhandledExceptionFilter(exception_pointers);
    //    }
    //    else
            throw;
    }
#else*/
    try {
        throw;
    } catch (const std::bad_alloc& ex) {
        // bad_alloc in main thread is most likely fatal. Report immediately to the user (wxLogError would be delayed)
        // and terminate the app so it is at least certain to happen now.
        wxString errmsg = wxString::Format(_L("BambuStudio will terminate because of running out of memory."
                                              "It may be a bug. It will be appreciated if you report the issue to our team."));
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Fatal error"), wxOK | wxICON_ERROR);
        BOOST_LOG_TRIVIAL(error) << boost::format("std::bad_alloc exception: %1%") % ex.what();

        std::terminate();
        //throw;
     } catch (const boost::io::bad_format_string& ex) {
        wxString errmsg = _L("BambuStudio will terminate because of a localization error. "
                             "It will be appreciated if you report the specific scenario this issue happened.");
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Critical error"), wxOK | wxICON_ERROR);
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        std::terminate();
        //throw;
    } catch (const std::exception& ex) {
        wxLogError(format_wxstr(_L("BambuStudio got an unhandled exception: %1%"), ex.what()));
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        throw;
    }
//#endif
}

void GUI_App::post_init()
{
    assert(initialized());
    if (! this->initialized())
        throw Slic3r::RuntimeError("Calling post_init() while not yet initialized");

    bool switch_to_3d = false;
    if (!this->init_params->input_files.empty()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", init with input files, size %1%, input_gcode %2%")
            %this->init_params->input_files.size() %this->init_params->input_gcode;
        switch_to_3d = true;
        if (this->init_params->input_gcode) {
            mainframe->select_tab(size_t(MainFrame::tp3DEditor));
            plater_->select_view_3D("3D");
            this->plater()->load_gcode(from_u8(this->init_params->input_files.front()));
        }
        else {
            mainframe->select_tab(size_t(MainFrame::tp3DEditor));
            plater_->select_view_3D("3D");
            const std::vector<size_t> res = this->plater()->load_files(this->init_params->input_files);
            if (!res.empty()) {
                if (this->init_params->input_files.size() == 1) {
                    // Update application titlebar when opening a project file
                    const std::string& filename = this->init_params->input_files.front();
                    //BBS: remove amf logic as project
                    if (boost::algorithm::iends_with(filename, ".3mf"))
                        this->plater()->set_project_filename(from_u8(filename));
                }
            }
        }
    }
//#if BBL_HAS_FIRST_PAGE
    bool slow_bootup = false;
    if (app_config->get("slow_bootup") == "true") {
        slow_bootup = true;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", slow bootup, won't render gl here.";
    }
    if (!switch_to_3d) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", begin load_gl_resources";
        mainframe->Freeze();
        plater_->canvas3D()->enable_render(false);
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
        plater_->select_view_3D("3D");
        //BBS init the opengl resource here
        Size canvas_size = plater_->canvas3D()->get_canvas_size();
        wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to init opengl";
        wxGetApp().init_opengl();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init opengl";
        plater_->canvas3D()->init();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init canvas3D";
        wxGetApp().imgui()->new_frame();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init imgui frame";
        plater_->canvas3D()->enable_render(true);

        if (!slow_bootup) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to render a first frame for test";
            plater_->canvas3D()->render(false);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished rendering a first frame for test";
        }
        if (is_editor())
            mainframe->select_tab(size_t(0));
        mainframe->Thaw();
        plater_->trigger_restore_project(1);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", end load_gl_resources";
    }
//#endif

    //BBS: remove GCodeViewer as seperate APP logic
    /*if (this->init_params->start_as_gcodeviewer) {
        if (! this->init_params->input_files.empty())
            this->plater()->load_gcode(wxString::FromUTF8(this->init_params->input_files[0].c_str()));
    }
    else
    {
        if (! this->init_params->preset_substitutions.empty())
            show_substitutions_info(this->init_params->preset_substitutions);

#if 0
        // Load the cummulative config over the currently active profiles.
        //FIXME if multiple configs are loaded, only the last one will have an effect.
        // We need to decide what to do about loading of separate presets (just print preset, just filament preset etc).
        // As of now only the full configs are supported here.
        if (!m_print_config.empty())
            this->gui->mainframe->load_config(m_print_config);
#endif
        if (! this->init_params->load_configs.empty())
            // Load the last config to give it a name at the UI. The name of the preset may be later
            // changed by loading an AMF or 3MF.
            //FIXME this is not strictly correct, as one may pass a print/filament/printer profile here instead of a full config.
            this->mainframe->load_config_file(this->init_params->load_configs.back());
        // If loading a 3MF file, the config is loaded from the last one.
        if (!this->init_params->input_files.empty()) {
            const std::vector<size_t> res = this->plater()->load_files(this->init_params->input_files);
            if (!res.empty() && this->init_params->input_files.size() == 1) {
                // Update application titlebar when opening a project file
                const std::string& filename = this->init_params->input_files.front();
                //BBS: remove amf logic as project
                if (boost::algorithm::iends_with(filename, ".3mf"))
                    this->plater()->set_project_filename(filename);
            }
        }
        if (! this->init_params->extra_config.empty())
            this->mainframe->load_config(this->init_params->extra_config);
    }*/

    // BBS: to be checked
#if 1
    // show "Did you know" notification
    if (app_config->get("show_hints") == "true" && !is_gcode_viewer()) {
        plater_->get_notification_manager()->push_hint_notification(false);
    }
#endif

    if (m_networking_need_update) {
        //updating networking
        int ret = updating_bambu_networking();
        if (!ret) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<":networking plugin updated successfully";
            //restart_networking();
        }
        else {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<<":networking plugin updated failed";
        }
    }

    // The extra CallAfter() is needed because of Mac, where this is the only way
    // to popup a modal dialog on start without screwing combo boxes.
    // This is ugly but I honestly found no better way to do it.
    // Neither wxShowEvent nor wxWindowCreateEvent work reliably.
    if (this->preset_updater) { // G-Code Viewer does not initialize preset_updater.
        BOOST_LOG_TRIVIAL(info) << "before check_updates";
        this->check_updates(false);
        BOOST_LOG_TRIVIAL(info) << "after check_updates";
        CallAfter([this] {
            bool cw_showed = this->config_wizard_startup();

            std::string http_url = get_http_url(app_config->get_country_code());
            std::string language = GUI::into_u8(current_language_code());
            std::string network_ver = Slic3r::NetworkAgent::get_version();
            this->preset_updater->sync(http_url, language, network_ver, preset_bundle);

            //BBS: check new version
            this->check_new_version();
        });
    }

    if(!m_networking_need_update && m_agent) {
        m_agent->set_on_ssdp_msg_fn(
            [this](std::string json_str) {
                if (m_is_closing) {
                    return;
                }
                GUI::wxGetApp().CallAfter([this, json_str] {
                    if (m_device_manager) {
                        m_device_manager->on_machine_alive(json_str);
                    }
                    });
            }
        );
        m_agent->set_on_http_error_fn([this](unsigned int status, std::string body) {
            this->handle_http_error(status, body);
        });
        m_agent->start_discovery(true, false);
    }

    //update the plugin tips
    CallAfter([this] {
            mainframe->refresh_plugin_tips();
        });

    // update hms info
    CallAfter([this] {
            if (hms_query)
                hms_query->check_hms_info();
        });

    std::string functional_config_file = Slic3r::resources_dir() + "/config.json";
    DeviceManager::load_functional_config(encode_path(functional_config_file.c_str()));

    // remove old log files over LOG_FILES_MAX_NUM
    std::string log_addr = data_dir();
    if (!log_addr.empty()) {
        auto log_folder = boost::filesystem::path(log_addr) / "log";
        if (boost::filesystem::exists(log_folder)) {
           std::vector<std::pair<time_t, std::string>> files_vec;
           for (auto& it : boost::filesystem::directory_iterator(log_folder)) {
               auto temp_path = it.path();
               try {
                   std::time_t lw_t = boost::filesystem::last_write_time(temp_path) ;
                   files_vec.push_back({ lw_t, temp_path.filename().string() });
               } catch (const std::exception &ex) {
               }
           }
           std::sort(files_vec.begin(), files_vec.end(), [](
               std::pair<time_t, std::string> &a, std::pair<time_t, std::string> &b) {
               return a.first > b.first;
           });

           while (files_vec.size() > LOG_FILES_MAX_NUM) {
               auto full_path = log_folder / boost::filesystem::path(files_vec[files_vec.size() - 1].second);
               BOOST_LOG_TRIVIAL(info) << "delete log file over " << LOG_FILES_MAX_NUM << ", filename: "<< files_vec[files_vec.size() - 1].second;
               try {
                   boost::filesystem::remove(full_path);
               }
               catch (const std::exception& ex) {
                   BOOST_LOG_TRIVIAL(error) << "failed to delete log file: "<< files_vec[files_vec.size() - 1].second << ". Error: " << ex.what();
               }
               files_vec.pop_back();
           }
        }
    }
    BOOST_LOG_TRIVIAL(info) << "finished post_init";
//BBS: remove the single instance currently
/*#ifdef _WIN32
    // Sets window property to mainframe so other instances can indentify it.
    OtherInstanceMessageHandler::init_windows_properties(mainframe, m_instance_hash_int);
#endif //WIN32*/
}

wxDEFINE_EVENT(EVT_ENTER_FORCE_UPGRADE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_NO_NEW_VERSION, wxCommandEvent);
wxDEFINE_EVENT(EVT_SHOW_DIALOG, wxCommandEvent);
wxDEFINE_EVENT(EVT_CONNECT_LAN_MODE_PRINT, wxCommandEvent);
IMPLEMENT_APP(GUI_App)

//BBS: remove GCodeViewer as seperate APP logic
//GUI_App::GUI_App(EAppMode mode)
GUI_App::GUI_App()
    : wxApp()
    //, m_app_mode(mode)
    , m_app_mode(EAppMode::Editor)
    , m_em_unit(10)
    , m_imgui(new ImGuiWrapper())
    , hms_query(new HMSQuery())
	, m_removable_drive_manager(std::make_unique<RemovableDriveManager>())
	//, m_other_instance_message_handler(std::make_unique<OtherInstanceMessageHandler>())
{
	//app config initializes early becasuse it is used in instance checking in BambuStudio.cpp
    this->init_app_config();
    this->init_download_path();

    reset_to_active();
}

void GUI_App::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown enter";

	if (m_removable_drive_manager) {
		removable_drive_manager()->shutdown();
	}

    if (m_is_recreating_gui) return;
    m_is_closing = true;
    stop_sync_user_preset();

    if (m_device_manager) {
        delete m_device_manager;
        m_device_manager = nullptr;
    }

    if (m_agent) {
        //BBS avoid a crash on mac platform
#ifdef __WINDOWS__
        m_agent->start_discovery(false, false);
#endif
        delete m_agent;
        m_agent = nullptr;
    }
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown exit";
}


std::string GUI_App::get_http_url(std::string country_code)
{
    std::string url;
    if (country_code == "US") {
        url = "https://api.bambulab.com/";
    }
    else if (country_code == "CN") {
        url = "https://api.bambulab.cn/";
    }
    else if (country_code == "ENV_CN_DEV") {
        url = "https://api-dev.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_QA") {
        url = "https://api-qa.bambu-lab.com/";
    }
    else if (country_code == "ENV_CN_PRE") {
        url = "https://api-pre.bambu-lab.com/";
    }
    else {
        url = "https://api.bambulab.com/";
    }

    url += "v1/iot-service/api/slicer/resource";
    return url;
}

std::string GUI_App::get_plugin_url(std::string name, std::string country_code)
{
    std::string url = get_http_url(country_code);

    std::string curr_version = SLIC3R_VERSION;
    std::string using_version = curr_version.substr(0, 9) + "00";
    url += (boost::format("?slicer/%1%/cloud=%2%") % name % using_version).str();
    //url += (boost::format("?slicer/plugins/cloud=%1%") % "01.01.00.00").str();
    return url;
}

static std::string decode(std::string const& extra, std::string const& path = {}) {
    char const* p = extra.data();
    char const* e = p + extra.length();
    while (p + 4 < e) {
        boost::uint16_t len = ((boost::uint16_t)p[2]) | ((boost::uint16_t)p[3] << 8);
        if (p[0] == '\x75' && p[1] == '\x70' && len >= 5 && p + 4 + len < e && p[4] == '\x01') {
            return std::string(p + 9, p + 4 + len);
        }
        else {
            p += 4 + len;
        }
    }
    return Slic3r::decode_path(path.c_str());
}

int GUI_App::download_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    int result = 0;
    // get country_code
    AppConfig* app_config = wxGetApp().app_config;
    if (!app_config)
        return -1;

    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: enter";
    m_networking_cancel_update = false;
    // get temp path
    fs::path target_file_path = (fs::temp_directory_path() / package_name);
    fs::path tmp_path = target_file_path;
    tmp_path += format(".%1%%2%", get_current_pid(), ".tmp");

    // get_url
    std::string  url = get_plugin_url(name, app_config->get_country_code());
    std::string download_url;
    Slic3r::Http http_url = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: check the plugin from " << url;
    http_url.timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete(
        [&download_url](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            Semver version;
                            std::string url;
                            std::string type;
                            std::string vendor;
                            std::string description;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    type = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(info) << "[download_plugin]: get version of settings's type, " << sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "version")) {
                                    version = *(Semver::parse(sub_iter.value()));
                                }
                                else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: get type " << type << ", version " << version.to_string() << ", url " << url;
                            download_url = url;
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: get version of plugin failed, body=" << body;
                }
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[download_plugin 1]: catch unknown exception";
                ;
            }
        }).on_error(
            [&result](std::string body, std::string error, unsigned int status) {
                BOOST_LOG_TRIVIAL(error) << "[download_plugin 1] on_error: " << error<<", body = " << body;
                result = -1;
        }).perform_sync();

    bool cancel = false;
    if (result < 0) {
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return result;
    }


    if (download_url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "[download_plugin 1]: no availaible plugin found for this app version: " << SLIC3R_VERSION;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return -1;
    }
    else if (pro_fn) {
        pro_fn(InstallStatusNormal, 5, cancel);
    }

    if (m_networking_cancel_update || cancel) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[download_plugin 1]: %1%, cancelled by user") % __LINE__;
        return -1;
    }
    BOOST_LOG_TRIVIAL(info) << "[download_plugin] get_url = " << download_url;

    // download
    Slic3r::Http http = Slic3r::Http::get(download_url);
    int reported_percent = 0;
    http.on_progress(
        [this, &pro_fn, cancel_fn, &result, &reported_percent](Slic3r::Http::Progress progress, bool& cancel) {
            int percent = 0;
            if (progress.dltotal != 0)
                percent = progress.dlnow * 50 / progress.dltotal;
            bool was_cancel = false;
            if (pro_fn && ((percent - reported_percent) >= 10)) {
                pro_fn(InstallStatusNormal, percent, was_cancel);
                reported_percent = percent;
                BOOST_LOG_TRIVIAL(info) << "[download_plugin 2] progress: " << reported_percent;
            }
            cancel = m_networking_cancel_update || was_cancel;
            if (cancel_fn)
                if (cancel_fn())
                    cancel = true;

            if (cancel)
                result = -1;
        })
        .on_complete([&pro_fn, tmp_path, target_file_path](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "[download_plugin 2] completed";
            bool cancel = false;
            int percent = 0;
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_file_path);
            if (pro_fn) pro_fn(InstallStatusDownloadCompleted, 80, cancel);
            })
        .on_error([&pro_fn, &result](std::string body, std::string error, unsigned int status) {
            bool cancel = false;
            if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
            BOOST_LOG_TRIVIAL(error) << "[download_plugin 2] on_error: " << error<<", body = " << body;
            result = -1;
        });
    http.perform_sync();
    return result;
}

int GUI_App::install_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    bool cancel = false;
    std::string target_file_path = (fs::temp_directory_path() / package_name).string();

    BOOST_LOG_TRIVIAL(info) << "[install_plugin] enter";
    // get plugin folder
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / name;
    //auto plugin_folder = boost::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data()) / "plugins";
    auto backup_folder = plugin_folder/"backup";
    if (!boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << "[install_plugin] will create directory "<<plugin_folder.string();
        boost::filesystem::create_directory(plugin_folder);
    }
    if (!boost::filesystem::exists(backup_folder)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", will create directory %1%")%backup_folder.string();
        boost::filesystem::create_directory(backup_folder);
    }

    if (m_networking_cancel_update) {
        BOOST_LOG_TRIVIAL(info) << boost::format("[install_plugin]: %1%, cancelled by user")%__LINE__;
        return -1;
    }
    if (pro_fn) {
        pro_fn(InstallStatusNormal, 50, cancel);
    }
    // unzip
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_reader(&archive, target_file_path)) {
        BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, open zip file failed")%__LINE__;
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return InstallStatusUnzipFailed;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);
    mz_zip_archive_file_stat stat;
    BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, got %2% files")%__LINE__ %num_entries;
    for (mz_uint i = 0; i < num_entries; i++) {
        if (m_networking_cancel_update || cancel) {
            BOOST_LOG_TRIVIAL(info) << boost::format("[install_plugin]: %1%, cancelled by user")%__LINE__;
            return -1;
        }
        if (mz_zip_reader_file_stat(&archive, i, &stat)) {
            if (stat.m_uncomp_size > 0) {
                std::string dest_file;
                if (stat.m_is_utf8) {
                    dest_file = stat.m_filename;
                }
                else {
                    std::string extra(1024, 0);
                    size_t n = mz_zip_reader_get_extra(&archive, stat.m_file_index, extra.data(), extra.size());
                    dest_file = decode(extra.substr(0, n), stat.m_filename);
                }
                auto dest_file_path = boost::filesystem::path(dest_file);
                dest_file = dest_file_path.filename().string();
                auto dest_path = boost::filesystem::path(plugin_folder.string() + "/" + dest_file);
                std::string dest_zip_file = encode_path(dest_path.string().c_str());
                try {
                    if (fs::exists(dest_path))
                        fs::remove(dest_path);
                    mz_bool res = mz_zip_reader_extract_to_file(&archive, stat.m_file_index, dest_zip_file.c_str(), 0);
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", extract  %1% from plugin zip %2%\n") % dest_file % stat.m_filename;
                    if (res == 0) {
                        mz_zip_error zip_error = mz_zip_get_last_error(&archive);
                        BOOST_LOG_TRIVIAL(error) << "[install_plugin]Archive read error:" << mz_zip_get_error_string(zip_error) << std::endl;
                        close_zip_reader(&archive);
                        if (pro_fn) {
                            pro_fn(InstallStatusUnzipFailed, 0, cancel);
                        }
                        return InstallStatusUnzipFailed;
                    }
                    else {
                        if (pro_fn) {
                            pro_fn(InstallStatusNormal, 50 + i/num_entries, cancel);
                        }
                        try {
                            auto backup_path = boost::filesystem::path(backup_folder.string() + "/" + dest_file);
                            if (fs::exists(backup_path))
                                fs::remove(backup_path);
                            std::string error_message;
                            CopyFileResult cfr = copy_file(dest_path.string(), backup_path.string(), error_message, false);
                            if (cfr != CopyFileResult::SUCCESS) {
                                BOOST_LOG_TRIVIAL(error) << "Copying to backup failed(" << cfr << "): " << error_message;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            BOOST_LOG_TRIVIAL(error) << "Copying to backup failed: " << e.what();
                            //continue
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    // ensure the zip archive is closed and rethrow the exception
                    close_zip_reader(&archive);
                    BOOST_LOG_TRIVIAL(error) << "[install_plugin]Archive read exception:"<<e.what();
                    if (pro_fn) {
                        pro_fn(InstallStatusUnzipFailed, 0, cancel);
                    }
                    return InstallStatusUnzipFailed;
                }
            }
        }
        else {
            BOOST_LOG_TRIVIAL(error) << boost::format("[install_plugin]: %1%, mz_zip_reader_file_stat for file %2% failed")%__LINE__%i;
        }
    }

    close_zip_reader(&archive);

    if (pro_fn)
        pro_fn(InstallStatusInstallCompleted, 100, cancel);
    if (name == "plugins")
        app_config->set_str("app", "installed_networking", "1");
    BOOST_LOG_TRIVIAL(info) << "[install_plugin] success";
    return 0;
}

void GUI_App::restart_networking()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" enter, mainframe %1%")%mainframe;
    on_init_network(true);
    if(m_agent) {
        init_networking_callbacks();
        m_agent->set_on_ssdp_msg_fn(
            [this](std::string json_str) {
                if (m_is_closing) {
                    return;
                }
                GUI::wxGetApp().CallAfter([this, json_str] {
                    if (m_device_manager) {
                        m_device_manager->on_machine_alive(json_str);
                    }
                    });
            }
        );
        m_agent->set_on_http_error_fn([this](unsigned int status, std::string body) {
            this->handle_http_error(status, body);
        });
        m_agent->start_discovery(true, false);
        if (mainframe)
            mainframe->refresh_plugin_tips();
        if (plater_)
            plater_->get_notification_manager()->bbl_close_plugin_install_notification();
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(" exit, m_agent=%1%")%m_agent;
}

void GUI_App::remove_old_networking_plugins()
{
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    //auto plugin_folder = boost::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data()) / "plugins";
    if (boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info) << "[remove_old_networking_plugins] remove the directory "<<plugin_folder.string();
        try {
            fs::remove_all(plugin_folder);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing the plugins directory " << plugin_folder.string();
        }
    }
}

int GUI_App::updating_bambu_networking()
{
    DownloadProgressDialog dlg(_L("Downloading Bambu Network Plug-in"));
    dlg.ShowModal();
    return 0;
}

bool GUI_App::check_networking_version()
{
    std::string network_ver = Slic3r::NetworkAgent::get_version();
    if (!network_ver.empty()) {
        BOOST_LOG_TRIVIAL(info) << "get_network_agent_version=" << network_ver;
    }
    std::string studio_ver = SLIC3R_VERSION;
    if (network_ver.length() >= 8) {
        if (network_ver.substr(0,8) == studio_ver.substr(0,8)) {
            m_networking_compatible = true;
            return true;
        }
    }

    m_networking_compatible = false;
    return false;
}

bool GUI_App::is_compatibility_version()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": m_networking_compatible=%1%")%m_networking_compatible;
    return m_networking_compatible;
}

void GUI_App::cancel_networking_install()
{
    m_networking_cancel_update = true;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": plugin install cancelled!");
}

void GUI_App::init_networking_callbacks()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter, m_agent=%1%")%m_agent;
    if (m_agent) {
        //set callbacks
        m_agent->set_on_user_login_fn([this](int online_login, bool login) {
            GUI::wxGetApp().request_user_login(online_login);
            });

        m_agent->set_on_server_connected_fn([this]() {
            if (m_is_closing) {
                return;
            }
            GUI::wxGetApp().CallAfter([this] {
                if (m_is_closing)
                    return;
                BOOST_LOG_TRIVIAL(trace) << "static: server connected";
                m_agent->set_user_selected_machine(m_agent->get_user_selected_machine());
                });
            });

        m_agent->set_on_printer_connected_fn([this](std::string dev_id) {
            if (m_is_closing) {
                return;
            }
            GUI::wxGetApp().CallAfter([this, dev_id] {
                if (m_is_closing)
                    return;
                /* request_pushing */
                MachineObject* obj = m_device_manager->get_my_machine(dev_id);
                if (obj) {
                    obj->command_request_push_all();
                    obj->command_get_version();
                }
                });
            });

        m_agent->set_get_country_code_fn([this]() {
            if (app_config)
                return app_config->get_country_code();
            return std::string();
            }
        );

        m_agent->set_on_local_connect_fn(
            [this](int state, std::string dev_id, std::string msg) {
                if (m_is_closing) {
                    return;
                }
                CallAfter([this, state, dev_id, msg] {
                    if (m_is_closing) {
                        return;
                    }
                    /* request_pushing */
                    MachineObject* obj = m_device_manager->get_my_machine(dev_id);
                    wxCommandEvent event(EVT_CONNECT_LAN_MODE_PRINT);

                    if (obj) {

                        if (obj->is_lan_mode_printer()) {
                            if (state == ConnectStatus::ConnectStatusOk) {
                                obj->command_request_push_all();
                                obj->command_get_version();
                                event.SetInt(1);
                                event.SetString(obj->dev_id);
                            } else if (state == ConnectStatus::ConnectStatusFailed) {
                                obj->set_access_code("");
                                m_device_manager->set_selected_machine("");
                                wxString text;
                                if (msg == "5") {
                                    text = wxString::Format(_L("Incorrect password"));
                                    wxGetApp().show_dialog(text);
                                } else {
                                    text = wxString::Format(_L("Connect %s failed! [SN:%s, code=%s]"), from_u8(obj->dev_name), obj->dev_id, msg);
                                    wxGetApp().show_dialog(text);
                                }
                                event.SetInt(0);
                            } else if (state == ConnectStatus::ConnectStatusLost) {
                                m_device_manager->set_selected_machine("");
                                event.SetInt(0);
                                BOOST_LOG_TRIVIAL(info) << "set_on_local_connect_fn: state = lost";
                            } else {
                                event.SetInt(0);
                                BOOST_LOG_TRIVIAL(info) << "set_on_local_connect_fn: state = " << state;
                            }

                            obj->set_lan_mode_connection_state(false);
                        }
                    }
                    event.SetEventObject(this);
                    wxPostEvent(this, event);
                });
            }
        );

        auto message_arrive_fn = [this](std::string dev_id, std::string msg) {
            if (m_is_closing) {
                return;
            }
            CallAfter([this, dev_id, msg] {
                if (m_is_closing)
                    return;
                MachineObject* obj = this->m_device_manager->get_user_machine(dev_id);
                if (obj) {
                    obj->is_ams_need_update = false;
                    obj->parse_json(msg);

                    if (this->m_device_manager->get_selected_machine() == obj && obj->is_ams_need_update) {
                        GUI::wxGetApp().sidebar().load_ams_list(obj->amsList);
                    }
                }
            });
        };

        m_agent->set_on_message_fn(message_arrive_fn);

        auto lan_message_arrive_fn = [this](std::string dev_id, std::string msg) {
            if (m_is_closing) {
                return;
            }
            CallAfter([this, dev_id, msg] {
                if (m_is_closing)
                    return;

                MachineObject* obj = m_device_manager->get_my_machine(dev_id);
                if (!obj || !obj->is_lan_mode_printer()) {
                    obj = m_device_manager->get_local_machine(dev_id);
                }

                if (obj) {
                    obj->parse_json(msg);
                    if (this->m_device_manager->get_selected_machine() == obj && obj->is_ams_need_update) {
                        GUI::wxGetApp().sidebar().load_ams_list(obj->amsList);
                    }
                }
                });
        };
        m_agent->set_on_local_message_fn(lan_message_arrive_fn);
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit, m_agent=%1%")%m_agent;
}

GUI_App::~GUI_App()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter");
    if (app_config != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy app_config");
        delete app_config;
    }

    if (preset_bundle != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset_bundle");
        delete preset_bundle;
    }

    if (preset_updater != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset updater");
        delete preset_updater;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit");
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string GUI_App::get_gl_info(bool for_github)
{
    return OpenGLManager::get_gl_info().to_string(for_github);
}

wxGLContext* GUI_App::init_glcontext(wxGLCanvas& canvas)
{
    return m_opengl_mgr.init_glcontext(canvas);
}

bool GUI_App::init_opengl()
{
#ifdef __linux__
    bool status = m_opengl_mgr.init_gl();
    m_opengl_initialized = true;
    return status;
#else
    return m_opengl_mgr.init_gl();
#endif
}

// gets path to PrusaSlicer.ini, returns semver from first line comment
static boost::optional<Semver> parse_semver_from_ini(std::string path)
{
    std::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string body = buffer.str();
    size_t start = body.find("BambuStudio ");
    if (start == std::string::npos)
        return boost::none;
    body = body.substr(start + 12);
    size_t end = body.find_first_of(" \n");
    if (end < body.size())
        body.resize(end);
    return Semver::parse(body);
}

void GUI_App::init_download_path()
{
    std::string down_path = app_config->get("download_path");

    if (down_path.empty()) {
        std::string user_down_path = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
        app_config->set("download_path", user_down_path);
    }
    else {
        fs::path dp(down_path);
        if (!fs::exists(dp)) {

            if (!fs::create_directory(dp)) {
                std::string user_down_path = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
                app_config->set("download_path", user_down_path);
            }
        }
    }
}

void GUI_App::init_app_config()
{
	// Profiles for the alpha are stored into the PrusaSlicer-alpha directory to not mix with the current release.
    SetAppName(SLIC3R_APP_KEY);
//	SetAppName(SLIC3R_APP_KEY "-alpha");
//  SetAppName(SLIC3R_APP_KEY "-beta");
//	SetAppDisplayName(SLIC3R_APP_NAME);

	// Set the Slic3r data directory at the Slic3r XS module.
	// Unix: ~/ .Slic3r
	// Windows : "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
	// Mac : "~/Library/Application Support/Slic3r"

    if (data_dir().empty()) {
        #ifndef __linux__
            std::string data_dir = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
            //BBS create folder if not exists
            boost::filesystem::path data_dir_path(data_dir);
            if (!boost::filesystem::exists(data_dir_path))
                boost::filesystem::create_directory(data_dir_path);
            set_data_dir(data_dir);
        #else
            // Since version 2.3, config dir on Linux is in ${XDG_CONFIG_HOME}.
            // https://github.com/prusa3d/PrusaSlicer/issues/2911
            wxString dir;
            if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
                dir = wxFileName::GetHomeDir() + wxS("/.config");
            set_data_dir((dir + "/" + GetAppName()).ToUTF8().data());
            boost::filesystem::path data_dir_path(data_dir());
            if (!boost::filesystem::exists(data_dir_path))
                boost::filesystem::create_directory(data_dir_path);
        #endif
    } else {
        m_datadir_redefined = true;
    }

    //BBS: remove GCodeViewer as seperate APP logic
	if (!app_config)
        app_config = new AppConfig();
        //app_config = new AppConfig(is_editor() ? AppConfig::EAppMode::Editor : AppConfig::EAppMode::GCodeViewer);

	// load settings
	m_app_conf_exists = app_config->exists();
	if (m_app_conf_exists) {
        std::string error = app_config->load();
        if (!error.empty()) {
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            throw Slic3r::RuntimeError(
                _u8L("BambuStudio configuration file may be corrupted and is not abled to be parsed."
                     "Please delete the file and try again.") +
                "\n\n" + app_config->config_path() + "\n\n" + error);
        }
        // Save orig_version here, so its empty if no app_config existed before this run.
        m_last_config_version = app_config->orig_version();//parse_semver_from_ini(app_config->config_path());
    }
    else {
#ifdef _WIN32
        // update associate files from registry information
        if (is_associate_files(L"3mf")) {
            app_config->set("associate_3mf", "true");
        }
        if (is_associate_files(L"stl")) {
            app_config->set("associate_stl", "true");
        }
        if (is_associate_files(L"step") && is_associate_files(L"stp")) {
            app_config->set("associate_step", "true");
        }
#endif // _WIN32
    }
}

// returns true if found newer version and user agreed to use it
bool GUI_App::check_older_app_config(Semver current_version, bool backup)
{
    //BBS: current no need these logic
    return false;
}

void GUI_App::copy_older_config()
{
    preset_bundle->copy_files(m_older_data_dir_path);
}

std::map<std::string, std::string> GUI_App::get_extra_header()
{
    std::map<std::string, std::string> extra_headers;
    extra_headers.insert(std::make_pair("X-BBL-Client-Type", "slicer"));
    extra_headers.insert(std::make_pair("X-BBL-Client-Version", VersionInfo::convert_full_version(SLIC3R_VERSION)));
#if defined(__WINDOWS__)
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "windows"));
#elif defined(__APPLE__)
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "macos"));
#elif defined(__LINUX__)
    extra_headers.insert(std::make_pair("X-BBL-OS-Type", "linux"));
#endif
    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);
    std::string os_version = (boost::format("%1%.%2%.%3%") % major % minor % micro).str();
    extra_headers.insert(std::make_pair("X-BBL-OS-Version", os_version));
    if (app_config)
        extra_headers.insert(std::make_pair("X-BBL-Device-ID", app_config->get("slicer_uuid")));
    extra_headers.insert(std::make_pair("X-BBL-Language", convert_studio_language_to_api(app_config->get("language"))));
    return extra_headers;
}

//BBS
void GUI_App::init_http_extra_header()
{
    std::map<std::string, std::string> extra_headers = get_extra_header();

    if (m_agent)
        m_agent->set_extra_http_header(extra_headers);
}

std::string GUI_App::get_local_models_path()
{
    std::string local_path = "";
    if (data_dir().empty()) {
        return local_path;
    }

    auto models_folder = (boost::filesystem::path(data_dir()) / "models");
    local_path = models_folder.string();

    if (!fs::exists(models_folder)) {
        if (!fs::create_directory(models_folder)) {
            local_path = "";
        }
        BOOST_LOG_TRIVIAL(info) << "create models folder:" << models_folder.string();
    }
    return local_path;
}

/*void GUI_App::init_single_instance_checker(const std::string &name, const std::string &path)
{
    BOOST_LOG_TRIVIAL(debug) << "init wx instance checker " << name << " "<< path;
    m_single_instance_checker = std::make_unique<wxSingleInstanceChecker>(boost::nowide::widen(name), boost::nowide::widen(path));
}*/

bool GUI_App::OnInit()
{
    try {
        return on_init_inner();
    } catch (const std::exception&) {
        generic_exception_handle();
        return false;
    }
}

bool GUI_App::on_init_inner()
{
    //start log here
    std::time_t t = std::time(0);
    std::tm* now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "debug_%a_%b_%d_%H_%M_%S_");
    buf << get_current_pid() << ".log";
    std::string log_filename = buf.str();
#if !BBL_RELEASE_TO_PUBLIC
    set_log_path_and_level(log_filename, 5);
#else
    set_log_path_and_level(log_filename, 3);
#endif

    // Set initialization of image handlers before any UI actions - See GH issue #7469
    wxInitAllImageHandlers();
#ifdef NDEBUG
    wxImage::SetDefaultLoadFlags(0); // ignore waring in release build
#endif

#if defined(_WIN32) && ! defined(_WIN64)
    // BBS: remove 32bit build prompt
    // Win32 32bit build.
#endif // _WIN64

    // Forcing back menu icons under gtk2 and gtk3. Solution is based on:
    // https://docs.gtk.org/gtk3/class.Settings.html
    // see also https://docs.wxwidgets.org/3.0/classwx_menu_item.html#a2b5d6bcb820b992b1e4709facbf6d4fb
    // TODO: Find workaround for GTK4
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_object_set (gtk_settings_get_default (), "gtk-menu-images", TRUE, NULL);
#endif

#ifdef WIN32
    //BBS set crash log folder
    CBaseException::set_log_folder(data_dir());
#endif

    wxGetApp().Bind(wxEVT_QUERY_END_SESSION, [this](auto & e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "received wxEVT_QUERY_END_SESSION";
        if (mainframe) {
            wxCloseEvent e2(wxEVT_CLOSE_WINDOW);
            e2.SetCanVeto(true);
            mainframe->GetEventHandler()->ProcessEvent(e2);
            if (e2.GetVeto()) {
                e.Veto();
                return;
            }
        }
        for (auto d : dialogStack)
            d->EndModal(wxID_CANCEL);
    });

    std::map<std::string, std::string> extra_headers = get_extra_header();
    Slic3r::Http::set_extra_headers(extra_headers);

    // Verify resources path
    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format("Resources path does not exist or is not a directory: %s", resources_dir));

#ifdef __linux__
    if (! check_old_linux_datadir(GetAppName())) {
        std::cerr << "Quitting, user chose to move their data to new location." << std::endl;
        return false;
    }
#endif

    // Enable this to get the default Win32 COMCTRL32 behavior of static boxes.
//    wxSystemOptions::SetOption("msw.staticbox.optimized-paint", 0);
    // Enable this to disable Windows Vista themes for all wxNotebooks. The themes seem to lead to terrible
    // performance when working on high resolution multi-display setups.
//    wxSystemOptions::SetOption("msw.notebook.themed-background", 0);

//     Slic3r::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;
    if (is_editor()) {
        std::string msg = Slic3r::Http::tls_global_init();
        std::string ssl_cert_store = app_config->get("tls_accepted_cert_store_location");
        bool ssl_accept = app_config->get("tls_cert_store_accepted") == "yes" && ssl_cert_store == Slic3r::Http::tls_system_cert_store();

        if (!msg.empty() && !ssl_accept) {
            RichMessageDialog
                dlg(nullptr,
                    wxString::Format(_L("%s\nDo you want to continue?"), msg),
                    "BambuStudio", wxICON_QUESTION | wxYES_NO);
            dlg.ShowCheckBox(_L("Remember my choice"));
            if (dlg.ShowModal() != wxID_YES) return false;

            app_config->set("tls_cert_store_accepted",
                dlg.IsCheckBoxChecked() ? "yes" : "no");
            app_config->set("tls_accepted_cert_store_location",
                dlg.IsCheckBoxChecked() ? Slic3r::Http::tls_system_cert_store() : "");
        }
    }

    // !!! Initialization of UI settings as a language, application color mode, fonts... have to be done before first UI action.
    // Like here, before the show InfoDialog in check_older_app_config()

    // If load_language() fails, the application closes.
    load_language(wxString(), true);
#ifdef _MSW_DARK_MODE

#ifdef __APPLE__
    wxSystemAppearance app = wxSystemSettings::GetAppearance();
    GUI::wxGetApp().app_config->set("dark_color_mode", app.IsDark() ? "1" : "0");
    GUI::wxGetApp().app_config->save();
#endif // __APPLE__


    bool init_dark_color_mode = app_config->get("dark_color_mode") == "1";
    bool init_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
#ifdef __WINDOWS__
     NppDarkMode::InitDarkMode(init_dark_color_mode, init_sys_menu_enabled);
#endif // __WINDOWS__

#endif
    // initialize label colors and fonts
    init_label_colours();
    init_fonts();
    wxGetApp().Update_dark_mode_flag();


#ifdef _MSW_DARK_MODE
    // app_config can be updated in check_older_app_config(), so check if dark_color_mode and sys_menu_enabled was changed
    if (bool new_dark_color_mode = app_config->get("dark_color_mode") == "1";
        init_dark_color_mode != new_dark_color_mode) {

#ifdef __WINDOWS__
        NppDarkMode::SetDarkMode(new_dark_color_mode);
#endif // __WINDOWS__

        init_label_colours();
        //update_label_colours_from_appconfig();
    }
    if (bool new_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
        init_sys_menu_enabled != new_sys_menu_enabled)
#ifdef __WINDOWS__
        NppDarkMode::SetSystemMenuForApp(new_sys_menu_enabled);
#endif
#endif

    if (m_last_config_version) {
        int last_major = m_last_config_version->maj();
        int last_minor = m_last_config_version->min();
        int last_patch = m_last_config_version->patch()/100;
        std::string studio_ver = SLIC3R_VERSION;
        int cur_major = atoi(studio_ver.substr(0,2).c_str());
        int cur_minor = atoi(studio_ver.substr(3,2).c_str());
        int cur_patch = atoi(studio_ver.substr(6,2).c_str());
        BOOST_LOG_TRIVIAL(info) << boost::format("last app version {%1%.%2%.%3%}, current version {%4%.%5%.%6%}")
            %last_major%last_minor%last_patch%cur_major%cur_minor%cur_patch;
        if ((last_major != cur_major)
            ||(last_minor != cur_minor)
            ||(last_patch != cur_patch)) {
            remove_old_networking_plugins();
        }
    }

    app_config->set("version", SLIC3R_VERSION);
    app_config->save();

    BBLSplashScreen * scrn = nullptr;
    const bool show_splash_screen = true;
    if (show_splash_screen) {
        // make a bitmap with dark grey banner on the left side
        //BBS make BBL splash screen bitmap
        wxBitmap bmp = BBLSplashScreen::MakeBitmap();
        // Detect position (display) to show the splash screen
        // Now this position is equal to the mainframe position
        wxPoint splashscreen_pos = wxDefaultPosition;
        if (app_config->has("window_mainframe")) {
            auto metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            if (metrics)
                splashscreen_pos = metrics->get_rect().GetPosition();
        }

        BOOST_LOG_TRIVIAL(info) << "begin to show the splash screen...";
        //BBS use BBL splashScreen
        scrn = new BBLSplashScreen(bmp, wxSPLASH_CENTRE_ON_SCREEN | wxSPLASH_TIMEOUT, 10000, splashscreen_pos);
#ifndef __linux__
        wxYield();
#endif
        scrn->SetText(_L("Loading configuration")+ dots);
    }

    BOOST_LOG_TRIVIAL(info) << "loading systen presets...";
    preset_bundle = new PresetBundle();

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    preset_bundle->setup_directories();


    if (m_init_app_config_from_older)
        copy_older_config();

    if (is_editor()) {
#ifdef __WXMSW__
        if (app_config->get("associate_3mf") == "true")
            associate_files(L"3mf");
        if (app_config->get("associate_stl") == "true")
            associate_files(L"stl");
        if (app_config->get("associate_step") == "true") {
            associate_files(L"step");
            associate_files(L"stp");
        }
        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif // __WXMSW__

        preset_updater = new PresetUpdater();
        Bind(EVT_SLIC3R_VERSION_ONLINE, [this](const wxCommandEvent& evt) {
            if (this->plater_ != nullptr) {
                // this->plater_->get_notification_manager()->push_notification(NotificationType::NewAppAvailable);
                //BBS show msg box to download new version
               /* wxString tips = wxString::Format(_L("Click to download new version in default browser: %s"), version_info.version_str);
                DownloadDialog dialog(this->mainframe,
                    tips,
                    _L("New version of Bambu Studio"),
                    false,
                    wxCENTER | wxICON_INFORMATION);


                dialog.SetExtendedMessage(extmsg);*/

                std::string skip_version_str = this->app_config->get("app", "skip_version");
                bool skip_this_version = false;
                if (!skip_version_str.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "new version = " << version_info.version_str << ", skip version = " << skip_version_str;
                    if (version_info.version_str <= skip_version_str) {
                        skip_this_version = true;
                    } else {
                        app_config->set("skip_version", "");
                        skip_this_version = false;
                    }
                }
                if (!skip_this_version
                    || evt.GetInt() != 0) {
                    UpdateVersionDialog dialog(this->mainframe);
                    wxString            extmsg = wxString::FromUTF8(version_info.description);
                    dialog.update_version_info(extmsg, version_info.version_str);
                    //dialog.update_version_info(version_info.description);
                    if (evt.GetInt() != 0) {
                        dialog.m_remind_choice->Hide();
                    }
                    switch (dialog.ShowModal())
                    {
                    case wxID_YES:
                        wxLaunchDefaultBrowser(version_info.url);
                        break;
                    case wxID_NO:
                        break;
                    default:
                        ;
                    }
                }
            }
            });

        Bind(EVT_ENTER_FORCE_UPGRADE, [this](const wxCommandEvent& evt) {
                wxString      version_str = wxString::FromUTF8(this->app_config->get("upgrade", "version"));
                wxString      description_text = wxString::FromUTF8(this->app_config->get("upgrade", "description"));
                std::string   download_url = this->app_config->get("upgrade", "url");
                wxString tips = wxString::Format(_L("Click to download new version in default browser: %s"), version_str);
                DownloadDialog dialog(this->mainframe,
                    tips,
                    _L("The Bambu Studio needs an upgrade"),
                    false,
                    wxCENTER | wxICON_INFORMATION);
                dialog.SetExtendedMessage(description_text);

                int result = dialog.ShowModal();
                switch (result)
                {
                 case wxID_YES:
                     wxLaunchDefaultBrowser(download_url);
                     break;
                 case wxID_NO:
                     wxGetApp().mainframe->Close(true);
                     break;
                 default:
                     wxGetApp().mainframe->Close(true);
                }
            });

        Bind(EVT_SHOW_NO_NEW_VERSION, [this](const wxCommandEvent& evt) {
            wxString msg = _L("This is the newest version.");
            InfoDialog dlg(nullptr, _L("Info"), msg);
            dlg.ShowModal();
        });

        Bind(EVT_SHOW_DIALOG, [this](const wxCommandEvent& evt) {
            wxString msg = evt.GetString();
            InfoDialog dlg(this->mainframe, _L("Info"), msg);
            dlg.ShowModal();

            /*wxString text = evt.GetString();
            Slic3r::GUI::MessageDialog msg_dlg(this->mainframe, text, "", wxAPPLY | wxOK);
            msg_dlg.ShowModal();*/
        });
    }
    else {
#ifdef __WXMSW__
        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif // __WXMSW__
    }

    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(true);

    Bind(EVT_USER_LOGIN, &GUI_App::on_user_login, this);

    copy_network_if_available();
    on_init_network();

    if (app_config->get("sync_user_preset") == "true" && m_agent && m_agent->is_user_login()) {
        enable_user_preset_folder(true);
    }

    // BBS if load user preset failed
    //if (loaded_preset_result != 0) {
        try {
            // Enable all substitutions (in both user and system profiles), but log the substitutions in user profiles only.
            // If there are substitutions in system profiles, then a "reconfigure" event shall be triggered, which will force
            // installation of a compatible system preset, thus nullifying the system preset substitutions.
            init_params->preset_substitutions = preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        }
        catch (const std::exception& ex) {
            show_error(nullptr, ex.what());
        }
    //}




    if (app_config->get("sync_user_preset") == "true") {
        //BBS loading user preset
        BOOST_LOG_TRIVIAL(info) << "Loading user presets...";
        scrn->SetText(_L("Loading user presets..."));
        if (m_agent) {
            start_sync_user_preset();
        }
    }

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
    register_win32_dpi_event();
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN
    register_win32_device_notification_event();
#endif // WIN32

    // Let the libslic3r know the callback, which will translate messages on demand.
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);

    BOOST_LOG_TRIVIAL(info) << "create the main window";
    mainframe = new MainFrame();
    // hide settings tabs after first Layout
    if (is_editor()) {
        mainframe->select_tab(size_t(0));
    }

    sidebar().obj_list()->init();
    //sidebar().aux_list()->init_auxiliary();
    mainframe->m_auxiliary->init_auxiliary();

//     update_mode(); // !!! do that later
    SetTopWindow(mainframe);

    plater_->init_notification_manager();

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    if (is_gcode_viewer()) {
        mainframe->update_layout();
        if (plater_ != nullptr)
            // ensure the selected technology is ptFFF
            plater_->set_printer_technology(ptFFF);
    }
    else
        load_current_presets();

    if (plater_ != nullptr) {
        plater_->reset_project_dirty_initial_presets();
        plater_->update_project_dirty_from_presets();
    }

    // BBS:
#ifdef __WINDOWS__
    mainframe->topbar()->SaveNormalRect();
#endif
    mainframe->Show(true);
    BOOST_LOG_TRIVIAL(info) << "main frame firstly shown";

//#if BBL_HAS_FIRST_PAGE
    //BBS: set tp3DEditor firstly
    /*plater_->canvas3D()->enable_render(false);
    mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    scrn->SetText(_L("Loading Opengl resourses..."));
    plater_->select_view_3D("3D");
    //BBS init the opengl resource here
    Size canvas_size = plater_->canvas3D()->get_canvas_size();
    wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
    wxGetApp().init_opengl();
    plater_->canvas3D()->init();
    wxGetApp().imgui()->new_frame();
    plater_->canvas3D()->enable_render(true);
    plater_->canvas3D()->render();
    if (is_editor())
        mainframe->select_tab(size_t(0));*/
//#else
    //plater_->trigger_restore_project(1);
//#endif

    obj_list()->set_min_height();

    update_mode(); // update view mode after fix of the object_list size

//#ifdef __APPLE__
//    other_instance_message_handler()->bring_instance_forward();
//#endif //__APPLE__

    Bind(EVT_HTTP_ERROR, &GUI_App::on_http_error, this);


    Bind(wxEVT_IDLE, [this](wxIdleEvent& event)
    {
        bool curr_studio_active = this->is_studio_active();
        if (m_studio_active != curr_studio_active) {
            if (curr_studio_active) {
                BOOST_LOG_TRIVIAL(info) << "studio is active, start to subscribe";
                if (m_agent)
                    m_agent->start_subscribe("app");
            } else {
                BOOST_LOG_TRIVIAL(info) << "studio is inactive, stop to subscribe";
                if (m_agent)
                    m_agent->stop_subscribe("app");
            }
            m_studio_active = curr_studio_active;
        }


        if (! plater_)
            return;

        if (app_config->dirty())
            app_config->save();

        // BBS
        //this->obj_manipul()->update_if_dirty();

        //use m_post_initialized instead
        //static bool update_gui_after_init = true;

        // An ugly solution to GH #5537 in which GUI_App::init_opengl (normally called from events wxEVT_PAINT
        // and wxEVT_SET_FOCUS before GUI_App::post_init is called) wasn't called before GUI_App::post_init and OpenGL wasn't initialized.
//#ifdef __linux__
//        if (!m_post_initialized && m_opengl_initialized) {
//#else
        if (!m_post_initialized && !m_adding_script_handler) {
//#endif
            m_post_initialized = true;
#ifdef WIN32
            this->mainframe->register_win32_callbacks();
#endif
            this->post_init();
        }
    });

    m_initialized = true;

    flush_logs();

    BOOST_LOG_TRIVIAL(info) << "finished the gui app init";
    //BBS: delete splash screen
    delete scrn;
    return true;
}

void GUI_App::copy_network_if_available()
{
    std::string network_library, player_library, network_library_dst, player_library_dst;
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    auto cache_folder = data_dir_path / "ota";
#if defined(_MSC_VER) || defined(_WIN32)
    network_library = cache_folder.string() + "/bambu_networking.dll";
    player_library = cache_folder.string() + "/BambuSource.dll";
    network_library_dst = plugin_folder.string() + "/bambu_networking.dll";
    player_library_dst = plugin_folder.string() + "/BambuSource.dll";
#elif defined(__WXMAC__)
    network_library = cache_folder.string() + "/libbambu_networking.dylib";
    player_library = cache_folder.string() + "/libBambuSource.dylib";
    network_library_dst = plugin_folder.string() + "/libbambu_networking.dylib";
    player_library_dst = plugin_folder.string() + "/libBambuSource.dylib";
#else
    network_library = cache_folder.string() + "/libbambu_networking.so";
    player_library = cache_folder.string() + "/libBambuSource.so";
    network_library_dst = plugin_folder.string() + "/libbambu_networking.so";
    player_library_dst = plugin_folder.string() + "/libBambuSource.so";
#endif

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": checking network_library " << network_library << ", player_library " << player_library;
    if (!boost::filesystem::exists(plugin_folder)) {
        BOOST_LOG_TRIVIAL(info)<< __FUNCTION__ << ": create directory "<<plugin_folder.string();
        boost::filesystem::create_directory(plugin_folder);
    }
    std::string error_message;
    if (boost::filesystem::exists(network_library)) {
        CopyFileResult cfr = copy_file(network_library, network_library_dst, error_message, false);
        if (cfr != CopyFileResult::SUCCESS) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": Copying failed(" << cfr << "): " << error_message;
            return;
        }

        static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
        fs::permissions(network_library_dst, perms);
        fs::remove(network_library);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": Copying network library from" << network_library << " to " << network_library_dst<<" successfully.";
    }

    if (boost::filesystem::exists(player_library)) {
        CopyFileResult cfr = copy_file(player_library, player_library_dst, error_message, false);
        if (cfr != CopyFileResult::SUCCESS) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": Copying failed(" << cfr << "): " << error_message;
            return;
        }

        static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
        fs::permissions(player_library_dst, perms);
        fs::remove(player_library);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": Copying player library from" << player_library << " to " << player_library_dst<<" successfully.";
    }
}

bool GUI_App::on_init_network(bool try_backup)
{
    int load_agent_dll = Slic3r::NetworkAgent::initialize_network_module();
    bool create_network_agent = false;
__retry:
    if (!load_agent_dll) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, load dll ok";
        if (check_networking_version()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, compatibility version";
            auto bambu_source = Slic3r::NetworkAgent::get_bambu_source_entry();
            if (!bambu_source) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": can not get bambu source module!";
                m_networking_compatible = false;
                if (app_config->get("installed_networking") == "1") {
                    m_networking_need_update = true;
                }
            }
            else
                create_network_agent = true;
        } else {
            if (try_backup) {
                int result = Slic3r::NetworkAgent::unload_network_module();
                BOOST_LOG_TRIVIAL(info) << "on_init_network, version mismatch, unload_network_module, result = " << result;
                load_agent_dll = Slic3r::NetworkAgent::initialize_network_module(true);
                try_backup = false;
                goto __retry;
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, version dismatch, need upload network module";
            if (app_config->get("installed_networking") == "1") {
                m_networking_need_update = true;
            }
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, load dll failed";
        if (app_config->get("installed_networking") == "1") {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_init_network, need upload network module";
            m_networking_need_update = true;
        }
    }

    if (create_network_agent) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", create network agent...");
        m_agent = new Slic3r::NetworkAgent();

        if (!m_device_manager)
            m_device_manager = new Slic3r::DeviceManager(m_agent);
        else
            m_device_manager->set_agent(m_agent);
        //std::string data_dir = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
        std::string data_directory = data_dir();

        //BBS set config dir
        if (m_agent) {
            m_agent->set_config_dir(data_directory);
        }
        //BBS start http log
        if (m_agent) {
            m_agent->init_log();
        }

        //BBS set cert dir
        if (m_agent)
            m_agent->set_cert_file(resources_dir() + "/cert", "slicer_base64.cer");

        init_http_extra_header();

        if (m_agent) {
            init_networking_callbacks();
            std::string country_code = app_config->get_country_code();
            m_agent->set_country_code(country_code);
            m_agent->start();
        }
    }
    else {
        int result = Slic3r::NetworkAgent::unload_network_module();
        BOOST_LOG_TRIVIAL(info) << "on_init_network, unload_network_module, result = " << result;

        if (!m_device_manager)
            m_device_manager = new Slic3r::DeviceManager();
    }

    return true;
}

unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
        ));
}

bool GUI_App::dark_mode()
{
#ifdef SUPPORT_DARK_MODE
#if __APPLE__
    // The check for dark mode returns false positive on 10.12 and 10.13,
    // which allowed setting dark menu bar and dock area, which is
    // is detected as dark mode. We must run on at least 10.14 where the
    // proper dark mode was first introduced.
    return wxPlatformInfo::Get().CheckOSVersion(10, 14) && mac_dark_mode();
#else
    return wxGetApp().app_config->get("dark_color_mode") == "1" ? true : check_dark_mode();
    //const unsigned luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    //return luma < 128;
#endif
#else
    //BBS disable DarkUI mode
    return false;
#endif
}

const wxColour GUI_App::get_label_default_clr_system()
{
    return dark_mode() ? wxColour(115, 220, 103) : wxColour(26, 132, 57);
}

const wxColour GUI_App::get_label_default_clr_modified()
{
    return dark_mode() ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
}

void GUI_App::init_label_colours()
{
    bool is_dark_mode = dark_mode();
    m_color_label_modified = is_dark_mode ? wxColour("#F1754E") : wxColour("#F1754E");
    m_color_label_sys      = is_dark_mode ? wxColour("#B2B3B5") : wxColour("#363636");

#ifdef _WIN32
    m_color_label_default           = is_dark_mode ? wxColour(250, 250, 250) : m_color_label_sys; // wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_label_default = is_dark_mode ? wxColour(230, 230, 230): wxSystemSettings::GetColour(/*wxSYS_COLOUR_HIGHLIGHTTEXT*/wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_default       = is_dark_mode ? wxColour(78, 78, 78)   : wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
    m_color_hovered_btn_label       = is_dark_mode ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
    m_color_selected_btn_bg         = is_dark_mode ? wxColour(95, 73, 62)   : wxColour(228, 220, 216);
#else
    m_color_label_default = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
#endif
    m_color_window_default          = is_dark_mode ? wxColour(43, 43, 43)   : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    StateColor::SetDarkMode(is_dark_mode);
}

void GUI_App::update_label_colours_from_appconfig()
{
    if (app_config->has("label_clr_sys")) {
        auto str = app_config->get("label_clr_sys");
        if (str != "")
            m_color_label_sys = wxColour(str);
    }

    if (app_config->has("label_clr_modified")) {
        auto str = app_config->get("label_clr_modified");
        if (str != "")
            m_color_label_modified = wxColour(str);
    }
}

void GUI_App::update_label_colours()
{
    for (Tab* tab : tabs_list)
        tab->update_label_colours();
}

void GUI_App::UpdateDarkUI(wxWindow* window, bool highlited/* = false*/, bool just_font/* = false*/)
{
    if (wxButton *btn = dynamic_cast<wxButton *>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
    }

    if (Button* btn = dynamic_cast<Button*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
    }


    /*if (m_is_dark_mode != dark_mode() )
        m_is_dark_mode = dark_mode();*/


    if (m_is_dark_mode) {
        auto original_col = window->GetBackgroundColour();
        auto bg_col = StateColor::darkModeColorFor(original_col);

        if (bg_col != original_col) {
            window->SetBackgroundColour(bg_col);
        }

        original_col = window->GetForegroundColour();
        auto fg_col = StateColor::darkModeColorFor(original_col);

        if (fg_col != original_col) {
            window->SetForegroundColour(fg_col);
        }
    }
    else {
        auto original_col = window->GetBackgroundColour();
        auto bg_col = StateColor::lightModeColorFor(original_col);

        if (bg_col != original_col) {
            window->SetBackgroundColour(bg_col);
        }

        original_col = window->GetForegroundColour();
        auto fg_col = StateColor::lightModeColorFor(original_col);

        if (fg_col != original_col) {
            window->SetForegroundColour(fg_col);
        }
    }
}

// recursive function for scaling fonts for all controls in Window
static void update_dark_children_ui(wxWindow* window, bool just_buttons_update = false)
{
    /*bool is_btn = dynamic_cast<wxButton*>(window) != nullptr;
    is_btn = false;*/
    if (!window) return;

    wxGetApp().UpdateDarkUI(window);

    auto children = window->GetChildren();
    for (auto child : children) {
        update_dark_children_ui(child);
    }
}

// Note: Don't use this function for Dialog contains ScalableButtons
void GUI_App::UpdateDarkUIWin(wxWindow* win)
{
    update_dark_children_ui(win);
}

void GUI_App::Update_dark_mode_flag()
{
    m_is_dark_mode = dark_mode();
}

void GUI_App::UpdateDlgDarkUI(wxDialog* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateFrameDarkUI(wxFrame* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited/* = false*/)
{
#ifdef __WINDOWS__
    UpdateDarkUI(dvc, highlited ? dark_mode() : false);
#ifdef _MSW_DARK_MODE
    //dvc->RefreshHeaderDarkMode(&m_normal_font);
    HWND hwnd = (HWND)dvc->GenericGetHeader()->GetHandle();
    hwnd = GetWindow(hwnd, GW_CHILD);
    if (hwnd != NULL)
        NppDarkMode::SetDarkListViewHeader(hwnd);
    wxItemAttr attr;
    attr.SetTextColour(NppDarkMode::GetTextColor());
    attr.SetFont(m_normal_font);
    dvc->SetHeaderAttr(attr);
#endif //_MSW_DARK_MODE
    if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);
    if (dvc->GetBorder() != wxBORDER_SIMPLE)
        dvc->SetWindowStyle(dvc->GetWindowStyle() | wxBORDER_SIMPLE);
#endif
}

void GUI_App::UpdateAllStaticTextDarkUI(wxWindow* parent)
{
#ifdef __WINDOWS__
    wxGetApp().UpdateDarkUI(parent);

    auto children = parent->GetChildren();
    for (auto child : children) {
        if (dynamic_cast<wxStaticText*>(child))
            child->SetForegroundColour(m_color_label_default);
    }
#endif
}

void GUI_App::init_fonts()
{
    // BBS: modify font
    m_small_font = Label::Body_10;
    m_bold_font = Label::Body_10.Bold();
    m_normal_font = Label::Body_10;

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/

    // wxSYS_OEM_FIXED_FONT and wxSYS_ANSI_FIXED_FONT use the same as
    // DEFAULT in wxGtk. Use the TELETYPE family as a work-around
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
}

void GUI_App::update_fonts(const MainFrame *main_frame)
{
    /* Only normal and bold fonts are used for an application rescale,
     * because of under MSW small and normal fonts are the same.
     * To avoid same rescaling twice, just fill this values
     * from rescaled MainFrame
     */
	if (main_frame == nullptr)
		main_frame = this->mainframe;
    m_normal_font   = Label::Body_14; // BBS: larger font size
    m_small_font    = m_normal_font;
    m_bold_font     = m_normal_font.Bold();
    m_link_font     = m_bold_font.Underlined();
    m_em_unit       = main_frame->em_unit();
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
}

void GUI_App::set_label_clr_modified(const wxColour& clr)
{
    return;
    //BBS
    /*
    if (m_color_label_modified == clr)
        return;
    m_color_label_modified = clr;
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
    std::string str = clr_str.ToStdString();
    app_config->save();
    */
}

void GUI_App::set_label_clr_sys(const wxColour& clr)
{
    return;
    //BBS
    /*
    if (m_color_label_sys == clr)
        return;
    m_color_label_sys = clr;
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
    std::string str = clr_str.ToStdString();
    app_config->save();
    */
}

bool GUI_App::tabs_as_menu() const
{
    return false;
}

wxSize GUI_App::get_min_size() const
{
    return wxSize(76*m_em_unit, 49 * m_em_unit);
}

float GUI_App::toolbar_icon_scale(const bool is_limited/* = false*/) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    //return icon_sc;

    const std::string& auto_val = app_config->get("toolkit_size");

    if (auto_val.empty())
        return icon_sc;

    int int_val =  100;
    // correct value in respect to toolkit_size
    int_val = std::min(atoi(auto_val.c_str()), int_val);

    if (is_limited && int_val < 50)
        int_val = 50;

    return 0.01f * int_val * icon_sc;
}

void GUI_App::set_auto_toolbar_icon_scale(float scale) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    long int_val = std::min(int(std::lround(scale / icon_sc * 100)), 100);
    std::string val = std::to_string(int_val);

    app_config->set("toolkit_size", val);
}

// check user printer_presets for the containing information about "Print Host upload"
void GUI_App::check_printer_presets()
{
//BBS
#if 0
    std::vector<std::string> preset_names = PhysicalPrinter::presets_with_print_host_information(preset_bundle->printers);
    if (preset_names.empty())
        return;

    // BBS: remove "print host upload" message dialog
    preset_bundle->physical_printers.load_printers_from_presets(preset_bundle->printers);
#endif
}

void GUI_App::recreate_GUI(const wxString& msg_name)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "recreate_GUI enter";
    m_is_recreating_gui = true;

    mainframe->shutdown();

    ProgressDialog dlg(msg_name, msg_name, 100, nullptr, wxPD_AUTO_HIDE);
    dlg.Pulse();
    dlg.Update(10, _L("Rebuild") + dots);

    MainFrame *old_main_frame = mainframe;
    mainframe = new MainFrame();
    if (is_editor())
        // hide settings tabs after first Layout
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    // Propagate model objects to object list.
    sidebar().obj_list()->init();
    //sidebar().aux_list()->init_auxiliary();
    mainframe->m_auxiliary->init_auxiliary();
    SetTopWindow(mainframe);

    dlg.Update(30, _L("Rebuild") + dots);
    old_main_frame->Destroy();

    dlg.Update(80, _L("Loading current presets") + dots);
    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));
    load_current_presets();
    mainframe->Show(true);
    //mainframe->refresh_plugin_tips();

    dlg.Update(90, _L("Loading a mode view") + dots);

    obj_list()->set_min_height();
    update_mode();

    //check hms info for different language
    if (hms_query)
        hms_query->check_hms_info();

    //BBS: trigger restore project logic here, and skip confirm
    plater_->trigger_restore_project(1);

    // #ys_FIXME_delete_after_testing  Do we still need this  ?
//     CallAfter([]() {
//         // Run the config wizard, don't offer the "reset user profile" checkbox.
//         config_wizard_startup(true);
//     });

    m_is_recreating_gui = false;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "recreate_GUI exit";
}

void GUI_App::system_info()
{
    //SysInfoDialog dlg;
    //dlg.ShowModal();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
}


void GUI_App::ShowUserGuide() {
    // BBS:Show NewUser Guide
    try {
        bool res = false;
        GuideFrame GuideDlg(this);
        //if (GuideDlg.IsFirstUse())
        res = GuideDlg.run();
        if (res) {
            load_current_presets();

            // BBS: remove SLA related message
        }
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
    }
}

void GUI_App::ShowDownNetPluginDlg() {
    try {
        DownloadProgressDialog dlg(_L("Downloading Bambu Network Plug-in"));
        dlg.ShowModal();
    } catch (std::exception &e) {
        ;
    }
}

void GUI_App::ShowUserLogin()
{
    // BBS: User Login Dialog
    try {
        ZUserLogin LoginDlg;
        LoginDlg.ShowModal();
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
    }
}


void GUI_App::ShowOnlyFilament() {
    // BBS:Show NewUser Guide
    try {
        bool       res = false;
        GuideFrame GuideDlg(this);
        GuideDlg.SetStartPage(GuideFrame::GuidePage::BBL_FILAMENT_ONLY);
        res = GuideDlg.run();
        if (res) {
            load_current_presets();

            // BBS: remove SLA related message
        }
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
    }
}



// static method accepting a wxWindow object as first parameter
bool GUI_App::catch_error(std::function<void()> cb,
    //                       wxMessageDialog* message_dialog,
    const std::string& err /*= ""*/)
{
    if (!err.empty()) {
        if (cb)
            cb();
        //         if (message_dialog)
        //             message_dialog->(err, "Error", wxOK | wxICON_ERROR);
        show_error(/*this*/nullptr, err);
        return true;
    }
    return false;
}

// static method accepting a wxWindow object as first parameter
void fatal_error(wxWindow* parent)
{
    show_error(parent, "");
    //     exit 1; // #ys_FIXME
}

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
static void update_scrolls(wxWindow* window)
{
    wxWindowList::compatibility_iterator node = window->GetChildren().GetFirst();
    while (node)
    {
        wxWindow* win = node->GetData();
        if (dynamic_cast<wxScrollHelper*>(win) ||
            dynamic_cast<wxTreeCtrl*>(win) ||
            dynamic_cast<wxTextCtrl*>(win))
            NppDarkMode::SetDarkExplorerTheme(win->GetHWND());

        update_scrolls(win);
        node = node->GetNext();
    }
}
#endif //_MSW_DARK_MODE


#ifdef _MSW_DARK_MODE
void GUI_App::force_menu_update()
{
    NppDarkMode::SetSystemMenuForApp(app_config->get("sys_menu_enabled") == "1");
}
#endif //_MSW_DARK_MODE
#endif //__WINDOWS__

void GUI_App::force_colors_update()
{
#ifdef _MSW_DARK_MODE
#ifdef __WINDOWS__
    NppDarkMode::SetDarkMode(app_config->get("dark_color_mode") == "1");
    if (WXHWND wxHWND = wxToolTip::GetToolTipCtrl())
        NppDarkMode::SetDarkExplorerTheme((HWND)wxHWND);
    NppDarkMode::SetDarkTitleBar(mainframe->GetHWND());


    //NppDarkMode::SetDarkExplorerTheme((HWND)mainframe->m_settings_dialog.GetHWND());
    //NppDarkMode::SetDarkTitleBar(mainframe->m_settings_dialog.GetHWND());

#endif // __WINDOWS__
#endif //_MSW_DARK_MODE
    m_force_colors_update = true;
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings()
{
    update_label_colours();
    // Upadte UI colors before Update UI from settings
    if (m_force_colors_update) {
        m_force_colors_update = false;
        //UpdateDlgDarkUI(&mainframe->m_settings_dialog);
        //mainframe->m_settings_dialog.Refresh();
        //mainframe->m_settings_dialog.Update();

        if (mainframe) {
#ifdef __WINDOWS__
            mainframe->force_color_changed();
            update_scrolls(mainframe);
            update_scrolls(&mainframe->m_settings_dialog);
#endif //_MSW_DARK_MODE
            update_dark_children_ui(mainframe);
        }
    }

    if (mainframe) {mainframe->update_ui_from_settings();}
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": received wxEVT_CLOSE_WINDOW, trigger save for window_mainframe";
        window_pos_save(window, "mainframe");
        event.Skip();
    });

    if (window_pos_restore(window, "mainframe", default_maximized)) {
        on_window_geometry(window, [=]() {
            window_pos_sanitize(window);
        });
    } else {
        on_window_geometry(window, [=]() {
            window_pos_center(window);
        });
    }
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (3mf):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files) const
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one or more files (3mf/step/stl/svg/obj/amf):"),
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

void GUI_App::load_gcode(wxWindow* parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (gcode/.gco/.g/.ngc/ngc):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_GCODE), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

wxString GUI_App::transition_tridid(int trid_id)
{
    wxString maping_dict[8] = { "A", "B", "C", "D", "E", "F", "G" };
    int id_index = ceil(trid_id / 4);
    int id_suffix = (trid_id + 1) % 4 == 0 ? 4 : (trid_id + 1) % 4;
    return wxString::Format("%s%d", maping_dict[id_index], id_suffix);
}

//BBS
void GUI_App::request_login(bool show_user_info)
{
    ShowUserLogin();

    if (show_user_info) {
        get_login_info();
    }
}

void GUI_App::get_login_info()
{
    if (m_agent) {
        if (m_agent->is_user_login()) {
            std::string login_cmd = m_agent->build_login_cmd();
            wxString strJS = wxString::Format("window.postMessage(%s)", login_cmd);
            GUI::wxGetApp().run_script(strJS);
        }
        else {
            m_agent->user_logout();
            std::string logout_cmd = m_agent->build_logout_cmd();
            wxString strJS = wxString::Format("window.postMessage(%s)", logout_cmd);
            GUI::wxGetApp().run_script(strJS);
        }
    }
}

bool GUI_App::is_user_login()
{
    if (m_agent) {
        return m_agent->is_user_login();
    }
    return false;
}


bool GUI_App::check_login()
{
    bool result = false;
    if (m_agent) {
        result = m_agent->is_user_login();
    }

    if (!result) {
        ShowUserLogin();
    }
    return result;
}

void GUI_App::request_user_login(int online_login)
{
    auto evt = new wxCommandEvent(EVT_USER_LOGIN);
    evt->SetInt(online_login);
    wxQueueEvent(this, evt);
}

void GUI_App::request_user_logout()
{
    if (m_agent) {
        bool     transfer_preset_changes = false;
        wxString header = _L("Some presets are modified.") + "\n" +
            _L("You can keep the modifield presets to the new project, discard or save changes as new presets.");
        using ab        = UnsavedChangesDialog::ActionButtons;
        wxGetApp().check_and_keep_current_preset_changes(_L("User logged out"), header, ab::KEEP | ab::SAVE, &transfer_preset_changes);

        m_agent->user_logout();
        m_agent->set_user_selected_machine("");
        BOOST_LOG_TRIVIAL(info) << "preset_folder: set to empty, user_logout";
        enable_user_preset_folder(false);
        /* delete old user settings */
        m_device_manager->clean_user_info();
        GUI::wxGetApp().sidebar().load_ams_list({});
        GUI::wxGetApp().remove_user_presets();
        GUI::wxGetApp().stop_sync_user_preset();

#ifdef __WINDOWS__
        wxGetApp().mainframe->topbar()->show_publish_button(false);
#else
        wxGetApp().mainframe->show_publish_button(false);
#endif
    }
}

int GUI_App::request_user_unbind(std::string dev_id)
{
    int result = -1;
    if (m_agent) {
        result = m_agent->unbind(dev_id);
        BOOST_LOG_TRIVIAL(info) << "request_user_unbind, dev_id = " << dev_id << ", result = " << result;
        return result;
    }
    return result;
}

std::string GUI_App::handle_web_request(std::string cmd)
{
    try {
        //BBS use nlohmann json format
        json j = json::parse(cmd);

        std::string web_cmd = j["command"].get<std::string>();

        if (web_cmd == "request_model_download") {
           /* json j_data = j["data"];
            json import_j;*/
            /*  import_j["model_id"] = j["data"]["model_id"].get<std::string>();
              import_j["profile_id"] = j["data"]["profile_id"].get<std::string>();*/

            std::string download_url = "";
            if (j["data"].contains("download_url"))
                download_url = j["data"]["download_url"].get<std::string>();

            std::string filename = "";
            if (j["data"].contains("filename"))
                download_url = j["data"]["filename"].get<std::string>();

            this->request_model_download(download_url, filename);
        }

        std::stringstream ss(cmd), oss;
        pt::ptree root, response;
        pt::read_json(ss, root);
        if (root.empty())
            return "";

        boost::optional<std::string> sequence_id = root.get_optional<std::string>("sequence_id");
        boost::optional<std::string> command = root.get_optional<std::string>("command");
        if (command.has_value()) {
            std::string command_str = command.value();
            if (command_str.compare("request_project_download") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_project_download(project_id.value());
                    }
                }
            }
            else if (command_str.compare("open_project") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_open_project(project_id.value());
                    }
                }
            }
            else if (command_str.compare("get_login_info") == 0) {
                CallAfter([this] {
                        get_login_info();
                    });
            }
            else if (command_str.compare("homepage_login_or_register") == 0) {
                CallAfter([this] {
                    this->request_login(true);
                });
            }
            else if (command_str.compare("homepage_logout") == 0) {
                CallAfter([this] {
                    wxGetApp().request_user_logout();
                });
            }
            else if (command_str.compare("homepage_modeldepot") == 0) {
                CallAfter([this] {
                    wxGetApp().open_mall_page_dialog();
                });
            }
            else if (command_str.compare("homepage_newproject") == 0) {
                this->request_open_project("<new>");
            }
            else if (command_str.compare("homepage_openproject") == 0) {
                this->request_open_project({});
            }
            else if (command_str.compare("get_recent_projects") == 0) {
                if (mainframe) {
                    if (mainframe->m_webview) {
                        mainframe->m_webview->SendRecentList(from_u8(sequence_id.value()));
                    }
                }
            }
            else if (command_str.compare("homepage_open_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> path = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_open_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_remove_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_all_recentfile") == 0) {
                this->request_remove_project("");
            }
            else if (command_str.compare("homepage_explore_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value())
                    {
                        boost::filesystem::path NowFile(path.value());

                        std::string FilePath = NowFile.make_preferred().string();
                        desktop_open_any_folder(FilePath);
                    }
                }
            }
            else if (command_str.compare("homepage_open_hotspot") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> url = data_node.get_optional<std::string>("url");
                    if (url.has_value()) {
                        this->request_open_project(url.value());
                    }
                }
            }
            else if (command_str.compare("begin_network_plugin_download") == 0) {
                CallAfter([this] { wxGetApp().ShowDownNetPluginDlg(); });
            }
            else if (command_str.compare("get_web_shortcut") == 0) {
                if (root.get_child_optional("key_event") != boost::none) {
                    pt::ptree key_event_node = root.get_child("key_event");
                    auto keyCode = key_event_node.get<int>("key");
                    auto ctrlKey = key_event_node.get<bool>("ctrl");
                    auto shiftKey = key_event_node.get<bool>("shift");
                    auto cmdKey = key_event_node.get<bool>("cmd");

                    wxKeyEvent e(wxEVT_CHAR_HOOK);
#ifdef __APPLE__
                    e.SetControlDown(cmdKey);
                    e.SetRawControlDown(ctrlKey);
#else
                    e.SetControlDown(ctrlKey);
#endif
                    e.SetShiftDown(shiftKey);
                    keyCode     = keyCode == 188 ? ',' : keyCode;
                    e.m_keyCode = keyCode;
                    e.SetEventObject(mainframe);
                    wxPostEvent(mainframe, e);
                }
            }
            else if (command_str.compare("userguide_wiki_open") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("url");
                    if (path.has_value()) {
                        wxLaunchDefaultBrowser(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_open_ccabin") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("file");
                    if (path.has_value()) {
                        std::string Fullpath = resources_dir() + "/web/homepage/model/" + path.value();

                        this->request_open_project(Fullpath);
                    }
                }
            }
        }
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "parse json cmd failed " << cmd;
        return "";
    }
    return "";
}

void GUI_App::handle_script_message(std::string msg)
{
    try {
        json j = json::parse(msg);
        if (j.contains("command")) {
            wxString cmd = j["command"];
            if (cmd == "user_login") {
                if (m_agent) {
                    m_agent->change_user(j.dump());
                    if (m_agent->is_user_login()) {
                        request_user_login(1);
                    }
                }
            }
        }
    }
    catch (...) {
        ;
    }
}

void GUI_App::request_model_download(std::string url, std::string filename)
{
    if (!check_login()) return;

    if (plater_) {
        plater_->request_model_download(url, filename);
    }
}

//BBS download project by project id
void GUI_App::download_project(std::string project_id)
{
    if (plater_) {
        plater_->request_download_project(project_id);
    }
}

void GUI_App::request_project_download(std::string project_id)
{
    if (!check_login()) return;

    download_project(project_id);
}

void GUI_App::request_open_project(std::string project_id)
{
    if (plater()->is_background_process_slicing()) {
        Slic3r::GUI::show_info(nullptr, _L("new or open project file is not allowed during the slicing process!"), _L("Open Project"));
        return;
    }

    if (project_id == "<new>")
        plater()->new_project();
    else if (project_id.empty())
        plater()->load_project();
    else if (std::find_if_not(project_id.begin(), project_id.end(),
        [](char c) { return std::isdigit(c); }) == project_id.end())
        ;
    else if (boost::algorithm::starts_with(project_id, "http"))
        ;
    else
        CallAfter([this, project_id] { mainframe->open_recent_project(-1, wxString::FromUTF8(project_id)); });
}

void GUI_App::request_remove_project(std::string project_id)
{
    mainframe->remove_recent_project(-1, wxString::FromUTF8(project_id));
}

void GUI_App::handle_http_error(unsigned int status, std::string body)
{
    // tips body size must less than 1024
    auto evt = new wxCommandEvent(EVT_HTTP_ERROR);
    evt->SetInt(status);
    evt->SetString(wxString(body));
    wxQueueEvent(this, evt);
}

void GUI_App::on_http_error(wxCommandEvent &evt)
{
    int status = evt.GetInt();

    int code = 0;
    std::string error;
    wxString result;
    if (status >= 400 && status < 500) {
        try {
        json j = json::parse(evt.GetString());
        if (j.contains("code")) {
            if (!j["code"].is_null())
                code = j["code"].get<int>();
        }
        if (j.contains("error"))
            if (!j["error"].is_null())
                error = j["error"].get<std::string>();
        }
        catch (...) {}
    }

    // Version limit
    if (code == HttpErrorVersionLimited) {
        MessageDialog msg_dlg(nullptr, _L("The version of Bambu studio is too low and needs to be updated to the latest version before it can be used normally"), "", wxAPPLY | wxOK);
        if (msg_dlg.ShowModal() == wxOK) {
            return;
        }
    }

    // request login
    if (status == 401) {
        if (m_agent) {
            if (m_agent->is_user_login()) {
                this->request_user_logout();
                MessageDialog msg_dlg(nullptr, _L("Login information expired. Please login again."), "", wxAPPLY | wxOK);
                if (msg_dlg.ShowModal() == wxOK) {
                    return;
                }
            }
        }
        return;
    }
}

void GUI_App::enable_user_preset_folder(bool enable)
{
    if (enable) {
        std::string user_id = m_agent->get_user_id();
        app_config->set("preset_folder", user_id);
        GUI::wxGetApp().preset_bundle->update_user_presets_directory(user_id);
    } else {
        BOOST_LOG_TRIVIAL(info) << "preset_folder: set to empty";
        app_config->set("preset_folder", "");
        GUI::wxGetApp().preset_bundle->update_user_presets_directory(DEFAULT_USER_FOLDER_NAME);
    }
}

void GUI_App::on_user_login(wxCommandEvent &evt)
{
    if (!m_agent) { return; }

    int online_login = evt.GetInt();
    m_agent->connect_server();

    // get machine list
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    dev->update_user_machine_list_info();
    dev->set_selected_machine(m_agent->get_user_selected_machine());

    if (app_config->get("sync_user_preset") == "true") {
        enable_user_preset_folder(true);
    } else {
        enable_user_preset_folder(false);
    }

    if (online_login)
        GUI::wxGetApp().mainframe->show_sync_dialog();

    //show publish button
    if (m_agent->is_user_login() && mainframe) {
        int identifier;
        int result = m_agent->get_user_info(&identifier);
        auto publish_identifier = identifier & 1;

#ifdef __WINDOWS__
        if (result == 0 && publish_identifier >= 0) {
            mainframe->m_topbar->show_publish_button(publish_identifier == 0 ? false : true);
        }
#else
        mainframe->show_publish_button(publish_identifier == 0 ? false : true);
#endif
    }
}

bool GUI_App::is_studio_active()
{
    auto curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_active_point);
    if (diff.count() < STUDIO_INACTIVE_TIMEOUT) {
        return true;
    }
    return false;
}

void GUI_App::reset_to_active()
{
    last_active_point = std::chrono::system_clock::now();
}

void GUI_App::check_update(bool show_tips, int by_user)
{
    if (version_info.version_str.empty()) return;
    if (version_info.url.empty()) return;

    auto curr_version = Semver::parse(SLIC3R_VERSION);
    auto remote_version = Semver::parse(version_info.version_str);
    if (curr_version && remote_version && (*remote_version > *curr_version)) {
        if (version_info.force_upgrade) {
            wxGetApp().app_config->set_bool("force_upgrade", version_info.force_upgrade);
            wxGetApp().app_config->set("upgrade", "force_upgrade", true);
            wxGetApp().app_config->set("upgrade", "description", version_info.description);
            wxGetApp().app_config->set("upgrade", "version", version_info.version_str);
            wxGetApp().app_config->set("upgrade", "url", version_info.url);
            GUI::wxGetApp().enter_force_upgrade();
        }
        else {
            GUI::wxGetApp().request_new_version(by_user);
        }
    } else {
        wxGetApp().app_config->set("upgrade", "force_upgrade", false);
        if (show_tips)
            this->no_new_version();
    }
}

void GUI_App::check_new_version(bool show_tips, int by_user)
{
    std::string platform = "windows";

#ifdef __WINDOWS__
    platform = "windows";
#endif
#ifdef __APPLE__
    platform = "macos";
#endif
#ifdef __LINUX__
    platform = "linux";
#endif
    std::string query_params = (boost::format("?name=slicer&version=%1%&guide_version=%2%")
        % VersionInfo::convert_full_version(SLIC3R_VERSION)
        % VersionInfo::convert_full_version("0.0.0.1")
        ).str();

    std::string url = get_http_url(app_config->get_country_code()) + query_params;
    Slic3r::Http http = Slic3r::Http::get(url);

    http.header("accept", "application/json")
        .timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, show_tips, by_user](std::string body, unsigned) {
        try {
            json j = json::parse(body);
            if (j.contains("message")) {
                if (j["message"].get<std::string>() == "success") {
                    if (j.contains("software")) {
                        if (j["software"].empty() && show_tips) {
                            this->no_new_version();
                        }
                        else {
                            if (j["software"].contains("url")
                                && j["software"].contains("version")
                                && j["software"].contains("description")) {
                                version_info.url = j["software"]["url"].get<std::string>();
                                version_info.version_str = j["software"]["version"].get<std::string>();
                                version_info.description = j["software"]["description"].get<std::string>();
                            }
                            if (j["software"].contains("force_update")) {
                                version_info.force_upgrade = j["software"]["force_update"].get<bool>();
                            }
                            CallAfter([this, show_tips, by_user](){
                                this->check_update(show_tips, by_user);
                            });
                        }
                    }
                }
            }
        }
        catch (...) {
            ;
        }
            })
        .on_error([this](std::string body, std::string error, unsigned int status) {
            handle_http_error(status, body);
            BOOST_LOG_TRIVIAL(error) << "check new version error" << body;
    }).perform();
}


//BBS pop up a dialog and download files
void GUI_App::request_new_version(int by_user)
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
    evt->SetString(GUI::from_u8(version_info.version_str));
    evt->SetInt(by_user);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::enter_force_upgrade()
{
    wxCommandEvent *evt = new wxCommandEvent(EVT_ENTER_FORCE_UPGRADE);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::set_skip_version(bool skip)
{
    BOOST_LOG_TRIVIAL(info) << "set_skip_version, skip = " << skip << ", version = " <<version_info.version_str;
    if (skip) {
        app_config->set("skip_version", version_info.version_str);
    }else {
        app_config->set("skip_version", "");
    }
}

void GUI_App::no_new_version()
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_NO_NEW_VERSION);
    GUI::wxGetApp().QueueEvent(evt);
}

std::string GUI_App::version_display = "";
std::string GUI_App::format_display_version()
{
    if (!version_display.empty()) return version_display;

    auto version_text = std::string(SLIC3R_VERSION);
    int len = version_text.length();
    for (int i = 0, j = 0; i < len; ++i) {
        if (!(version_text[i] == '0' && j == 0))
            version_display += version_text[i];

        if (version_text[i] == '.')
            j = 0;
        else
            ++j;
    }
    return version_display;
}

void GUI_App::show_dialog(wxString msg)
{
    wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
    evt->SetString(msg);
    GUI::wxGetApp().QueueEvent(evt);
}

void GUI_App::reload_settings()
{
    if (preset_bundle && m_agent) {
        std::map<std::string, std::map<std::string, std::string>> user_presets;
        m_agent->get_user_presets(&user_presets);
        preset_bundle->load_user_presets(*app_config, user_presets, ForwardCompatibilitySubstitutionRule::Enable);
        preset_bundle->save_user_presets(*app_config, get_delete_cache_presets());
    }
}

//BBS reload when login
void GUI_App::remove_user_presets()
{
    if (preset_bundle && m_agent) {
        preset_bundle->remove_users_preset(*app_config);

        std::string user_id = m_agent->get_user_id();
        preset_bundle->remove_user_presets_directory(user_id);

        //update ui
        mainframe->update_side_preset_ui();
    }
}

void GUI_App::sync_preset(Preset* preset)
{
    int result = -1;
    unsigned int http_code = 200;
    std::string updated_info;
    // only sync user's preset
    if (!preset->is_user()) return;
    if (preset->is_custom_defined()) return;

    if (preset->setting_id.empty() && preset->sync_info.empty() && !preset->base_id.empty()) {
        std::map<std::string, std::string> values_map;
        int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
        if (!ret) {
            std::string new_setting_id = m_agent->request_setting_id(preset->name, &values_map, &http_code);
            if (!new_setting_id.empty()) {
                preset->setting_id = new_setting_id;
                result = 0;
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]init: request_setting_id failed, http code "<<http_code;
                // do not post new preset this time if http code >= 400
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                }
                else
                    result = -1;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "[sync_preset]init: can not generate differed key-values";
            result = 0;
            updated_info = "hold";
        }
    }
    else if ((preset->sync_info.compare("create") == 0) && !preset->base_id.empty()) {
        std::map<std::string, std::string> values_map;
        int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
        if (!ret) {
            std::string new_setting_id = m_agent->request_setting_id(preset->name, &values_map, &http_code);
            if (!new_setting_id.empty()) {
                preset->setting_id = new_setting_id;
                result = 0;
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]create: request_setting_id failed, http code "<<http_code;
                // do not post new preset this time if http code >= 400
                if (http_code >= 400) {
                    result = 0;
                    updated_info = "hold";
                }
                else
                    result = -1;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "[sync_preset]create: can not generate differed preset";
        }
    }
    else if ((preset->sync_info.compare("update") == 0) && !preset->base_id.empty()) {
        if (!preset->setting_id.empty()) {
            std::map<std::string, std::string> values_map;
            int ret = preset_bundle->get_differed_values_to_update(*preset, values_map);
            if (!ret) {
                if (values_map[BBL_JSON_KEY_BASE_ID] ==  preset->setting_id) {
                    //clear the setting_id in this case
                    preset->setting_id.clear();
                    result = 0;
                }
                else {
                    result = m_agent->put_setting(preset->setting_id, preset->name, &values_map, &http_code);
                    if (http_code >= 400) {
                        result = 0;
                        updated_info = "hold";
                        BOOST_LOG_TRIVIAL(error) << "[sync_preset] put setting_id = " << preset->setting_id << " failed, http_code = " << http_code;
                    }
                }

            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "[sync_preset]update: can not generate differed key-values, we need to skip this preset "<< preset->name;
                result = 0;
            }
        }
        else {
            //clear the sync_info
            result = 0;
        }
    }

    //update sync_info preset info in file

    if (result == 0) {
        //PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        if (!this->preset_bundle) return;

        BOOST_LOG_TRIVIAL(trace) << "sync_preset: sync operation: " << preset->sync_info << " success! preset = " << preset->name;
        if (preset->type == Preset::Type::TYPE_FILAMENT) {
            preset_bundle->filaments.set_sync_info_and_save(preset->name, preset->setting_id, updated_info);
        } else if (preset->type == Preset::Type::TYPE_PRINT) {
            preset_bundle->prints.set_sync_info_and_save(preset->name, preset->setting_id, updated_info);
        } else if (preset->type == Preset::Type::TYPE_PRINTER) {
            preset_bundle->printers.set_sync_info_and_save(preset->name, preset->setting_id, updated_info);
        }
    }
}

void GUI_App::start_sync_user_preset(bool with_progress_dlg)
{
    if (!m_agent) return;

    enable_user_preset_folder(true);

    // has already start sync
    if (enable_sync)
        return;

    if (m_agent->is_user_login()) {
        // get setting list, update setting list
        std::string version = preset_bundle->get_vendor_profile_version(PresetBundle::BBL_BUNDLE).to_string();
        if (with_progress_dlg) {
            ProgressDialog dlg(_L("Loading"), "", 100, this->mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
            dlg.Update(0, _L("Loading user preset"));
            m_agent->get_setting_list(version,
                            [this, &dlg](int percent){
                                dlg.Update(percent, _L("Loading user preset"));
                            },
                            [this, &dlg]() {
                                dlg.GetValue();
                                bool cont = dlg.Update(dlg.GetValue(), _L("Loading user preset"));
                                return !cont;
                            });
        } else {
            m_agent->get_setting_list(version);
        }
        GUI::wxGetApp().reload_settings();
    }

    BOOST_LOG_TRIVIAL(info) << "start_sync_service...";
    //BBS
    enable_sync = true;
    m_sync_update_thread = Slic3r::create_thread(
        [this] {
            int count = 0, sync_count = 0;
            std::vector<Preset> presets_to_sync;
            while (enable_sync) {
                count++;
                if (count % 20 == 0) {
                    if (m_agent) {
                        if (!m_agent->is_user_login()) {
                            continue;
                        }
                        //sync preset
                        if (!preset_bundle) continue;

                        sync_count = preset_bundle->prints.get_user_presets(presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_preset(&preset);
                            }
                        }

                        sync_count = preset_bundle->filaments.get_user_presets(presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_preset(&preset);
                            }
                        }

                        sync_count = preset_bundle->printers.get_user_presets(presets_to_sync);
                        if (sync_count > 0) {
                            for (Preset& preset : presets_to_sync) {
                                sync_preset(&preset);
                            }
                        }

                        unsigned int http_code = 200;

                        /* get list witch need to be deleted*/
                        std::vector<string>& delete_cache_presets = get_delete_cache_presets();
                        for (auto it = delete_cache_presets.begin(); it != delete_cache_presets.end();) {
                            if ((*it).empty()) continue;
                            std::string del_setting_id = *it;
                            int result = m_agent->delete_setting(del_setting_id);
                            if (result == 0) {
                                it = delete_cache_presets.erase(it);
                                BOOST_LOG_TRIVIAL(trace) << "sync_preset: sync operation: delete success! setting id = " << del_setting_id;
                            }
                            else {
                                BOOST_LOG_TRIVIAL(info) << "delete setting = " <<del_setting_id << " failed";
                                it++;
                            }
                        }
                    }
                } else {
                    boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                }
            }
        });
}

void GUI_App::stop_sync_user_preset()
{
    enable_user_preset_folder(false);

    if (!enable_sync)
        return;

    enable_sync = false;
    if (m_sync_update_thread.joinable())
        m_sync_update_thread.join();
}

bool GUI_App::switch_language()
{
    if (select_language()) {
        recreate_GUI(_L("Switching application language") + dots);
        return true;
    } else {
        return false;
    }
}

#ifdef __linux__
static const wxLanguageInfo* linux_get_existing_locale_language(const wxLanguageInfo* language,
                                                                const wxLanguageInfo* system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = into_u8(language->CanonicalName.BeforeFirst('_'));

    // Call locale -a so we can parse the output to get the list of available locales
    // We expect lines such as "en_US.utf8". Pick ones starting with the language code
    // we are switching to. Lines with different formatting will be removed later.
    FILE* fp = popen("locale -a", "r");
    if (fp != NULL) {
        while (fgets(path, max_len, fp) != NULL) {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    // locales now contain all candidates for this language.
    // Sort them so ones containing anything about UTF-8 are at the end.
    std::sort(locales.begin(), locales.end(), [](const std::string& a, const std::string& b)
    {
        auto has_utf8 = [](const std::string & s) {
            auto S = boost::to_upper_copy(s);
            return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
        };
        return ! has_utf8(a) && has_utf8(b);
    });

    // Remove the suffix behind a dot, if there is one.
    for (std::string& s : locales)
        s = s.substr(0, s.find("."));

    // We just hope that dear Linux "locale -a" returns country codes
    // in ISO 3166-1 alpha-2 code (two letter) format.
    // https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
    // To be sure, remove anything not looking as expected
    // (any number of lowercase letters, underscore, two uppercase letters).
    locales.erase(std::remove_if(locales.begin(),
                                 locales.end(),
                                 [](const std::string& s) {
                                     return ! std::regex_match(s,
                                         std::regex("^[a-z]+_[A-Z]{2}$"));
                                 }),
                   locales.end());

    // Is there a candidate matching a country code of a system language? Move it to the end,
    // while maintaining the order of matches, so that the best match ends up at the very end.
    std::string system_country = "_" + into_u8(system_language->CanonicalName.AfterFirst('_')).substr(0, 2);
    int cnt = locales.size();
    for (int i=0; i<cnt; ++i)
        if (locales[i].find(system_country) != std::string::npos) {
            locales.emplace_back(std::move(locales[i]));
            locales[i].clear();
        }

    // Now try them one by one.
    for (auto it = locales.rbegin(); it != locales.rend(); ++ it)
        if (! it->empty()) {
            const std::string &locale = *it;
            const wxLanguageInfo* lang = wxLocale::FindLanguageInfo(from_u8(locale));
            if (wxLocale::IsAvailable(lang->Language))
                return lang;
        }
    return language;
}
#endif

int GUI_App::GetSingleChoiceIndex(const wxString& message,
                                const wxString& caption,
                                const wxArrayString& choices,
                                int initialSelection)
{
#ifdef _WIN32
    wxSingleChoiceDialog dialog(nullptr, message, caption, choices);
    wxGetApp().UpdateDlgDarkUI(&dialog);

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
#else
    return wxGetSingleChoiceIndex(message, caption, choices, initialSelection);
#endif
}

// select language from the list of installed languages
bool GUI_App::select_language()
{
	wxArrayString translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo*> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++ i) {
	    const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);
        if (langinfo != nullptr)
            language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
	std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo* l, const wxLanguageInfo* r) { return l->Description < r->Description; });

    wxArrayString names;
    names.Alloc(language_infos.size());

    // Some valid language should be selected since the application start up.
    const wxLanguage current_language = wxLanguage(m_wxLocale->GetLanguage());
    int 		     init_selection   		= -1;
    int 			 init_selection_alt     = -1;
    int 			 init_selection_default = -1;
    for (size_t i = 0; i < language_infos.size(); ++ i) {
        if (wxLanguage(language_infos[i]->Language) == current_language)
        	// The dictionary matches the active language and country.
            init_selection = i;
        else if ((language_infos[i]->CanonicalName.BeforeFirst('_') == m_wxLocale->GetCanonicalName().BeforeFirst('_')) ||
        		 // if the active language is Slovak, mark the Czech language as active.
        	     (language_infos[i]->CanonicalName.BeforeFirst('_') == "cs" && m_wxLocale->GetCanonicalName().BeforeFirst('_') == "sk"))
        	// The dictionary matches the active language, it does not necessarily match the country.
        	init_selection_alt = i;
        if (language_infos[i]->CanonicalName.BeforeFirst('_') == "en")
        	// This will be the default selection if the active language does not match any dictionary.
        	init_selection_default = i;
        names.Add(language_infos[i]->Description);
    }
    if (init_selection == -1)
    	// This is the dictionary matching the active language.
    	init_selection = init_selection_alt;
    if (init_selection != -1)
    	// This is the language to highlight in the choice dialog initially.
    	init_selection_default = init_selection;

    const long index = GetSingleChoiceIndex(_L("Select the language"), _L("Language"), names, init_selection_default);
	// Try to load a new language.
    if (index != -1 && (init_selection == -1 || init_selection != index)) {
    	const wxLanguageInfo *new_language_info = language_infos[index];
    	if (this->load_language(new_language_info->CanonicalName, false)) {
			// Save language at application config.
            // Which language to save as the selected dictionary language?
            // 1) Hopefully the language set to wxTranslations by this->load_language(), but that API is weird and we don't want to rely on its
            //    stability in the future:
            //    wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
            // 2) Current locale language may not match the dictionary name, see GH issue #3901
            //    m_wxLocale->GetCanonicalName()
            // 3) new_language_info->CanonicalName is a safe bet. It points to a valid dictionary name.
			app_config->set("language", new_language_info->CanonicalName.ToUTF8().data());
			app_config->save();
    		return true;
    	}
    }

    return false;
}

// Load gettext translation files and activate them at the start of the application,
// based on the "language" key stored in the application config.
bool GUI_App::load_language(wxString language, bool initial)
{
    if (initial) {
    	// There is a static list of lookup path prefixes in wxWidgets. Add ours.
	    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(from_u8(localization_dir()));
    	// Get the active language from PrusaSlicer.ini, or empty string if the key does not exist.
        language = app_config->get("language");
        if (! language.empty())
        	BOOST_LOG_TRIVIAL(trace) << boost::format("language provided by PBambuStudio.conf: %1%") % language;
        else {
            // Get the system language.
            const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
            if (lang_system != wxLANGUAGE_UNKNOWN) {
                m_language_info_system = wxLocale::GetLanguageInfo(lang_system);
                BOOST_LOG_TRIVIAL(trace) << boost::format("System language detected (user locales and such): %1%") % m_language_info_system->CanonicalName.ToUTF8().data();
                // BBS set language to app config
                app_config->set("language", m_language_info_system->CanonicalName.ToUTF8().data());
            } else {
                {
                    std::map<wxString, wxString> language_descptions = {
                        {"zh_CN", wxString::FromUTF8("\xE4\xB8\xAD\xE6\x96\x87\x28\xE7\xAE\x80\xE4\xBD\x93\x29")},
                        {"zh_TW", wxString::FromUTF8("\xE4\xB8\xAD\xE6\x96\x87\x28\xE7\xB9\x81\xE9\xAB\x94\x29")},
                        {"de", wxString::FromUTF8("Deutsch")},
                        {"nl", wxString::FromUTF8("Nederlands")},
                        {"sv", wxString::FromUTF8("\x53\x76\x65\x6e\x73\x6b\x61")}, //Svenska
                        {"en", wxString::FromUTF8("English")},
                        {"es", wxString::FromUTF8("\x45\x73\x70\x61\xC3\xB1\x6F\x6C")},
                        {"fr", wxString::FromUTF8("\x46\x72\x61\x6E\xC3\xA7\x61\x69\x73")},
                        {"it", wxString::FromUTF8("\x49\x74\x61\x6C\x69\x61\x6E\x6F")},
                        {"ru", wxString::FromUTF8("\xD1\x80\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9")},
                        {"hu", wxString::FromUTF8("Magyar")}
                    };
                    for (auto l : language_descptions) {
                        const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(l.first);
                        if (langinfo) const_cast<wxLanguageInfo *>(langinfo)->Description = l.second;
                    }
                }
                {
                    // Allocating a temporary locale will switch the default wxTranslations to its internal wxTranslations instance.
                    wxLocale temp_locale;
                    // Set the current translation's language to default, otherwise GetBestTranslation() may not work (see the wxWidgets source code).
                    wxTranslations::Get()->SetLanguage(wxLANGUAGE_DEFAULT);
                    // Let the wxFileTranslationsLoader enumerate all translation dictionaries for PrusaSlicer
                    // and try to match them with the system specific "preferred languages".
                    // There seems to be a support for that on Windows and OSX, while on Linuxes the code just returns wxLocale::GetSystemLanguage().
                    // The last parameter gets added to the list of detected dictionaries. This is a workaround
                    // for not having the English dictionary. Let's hope wxWidgets of various versions process this call the same way.
                    wxString best_language = wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
                    if (!best_language.IsEmpty()) {
                        m_language_info_best = wxLocale::FindLanguageInfo(best_language);
                        BOOST_LOG_TRIVIAL(trace) << boost::format("Best translation language detected (may be different from user locales): %1%") %
                                                        m_language_info_best->CanonicalName.ToUTF8().data();
                        app_config->set("language", m_language_info_best->CanonicalName.ToUTF8().data());
                    }
#ifdef __linux__
                    wxString lc_all;
                    if (wxGetEnv("LC_ALL", &lc_all) && !lc_all.IsEmpty()) {
                        // Best language returned by wxWidgets on Linux apparently does not respect LC_ALL.
                        // Disregard the "best" suggestion in case LC_ALL is provided.
                        m_language_info_best = nullptr;
                    }
#endif
                }
            }
        }
    }

	const wxLanguageInfo *language_info = language.empty() ? nullptr : wxLocale::FindLanguageInfo(language);
	if (! language.empty() && (language_info == nullptr || language_info->CanonicalName.empty())) {
		// Fix for wxWidgets issue, where the FindLanguageInfo() returns locales with undefined ANSII code (wxLANGUAGE_KONKANI or wxLANGUAGE_MANIPURI).
		language_info = nullptr;
    	BOOST_LOG_TRIVIAL(error) << boost::format("Language code \"%1%\" is not supported") % language.ToUTF8().data();
	}

	if (language_info != nullptr && language_info->LayoutDirection == wxLayout_RightToLeft) {
    	BOOST_LOG_TRIVIAL(trace) << boost::format("The following language code requires right to left layout, which is not supported by BambuStudio: %1%") % language_info->CanonicalName.ToUTF8().data();
		language_info = nullptr;
	}

    if (language_info == nullptr) {
        // PrusaSlicer does not support the Right to Left languages yet.
        if (m_language_info_system != nullptr && m_language_info_system->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_system;
        if (m_language_info_best != nullptr && m_language_info_best->LayoutDirection != wxLayout_RightToLeft)
        	language_info = m_language_info_best;
	    if (language_info == nullptr)
			language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US);
    }

	BOOST_LOG_TRIVIAL(trace) << boost::format("Switching wxLocales to %1%") % language_info->CanonicalName.ToUTF8().data();

    // Select language for locales. This language may be different from the language of the dictionary.
    //if (language_info == m_language_info_best || language_info == m_language_info_system) {
    //    // The current language matches user's default profile exactly. That's great.
    //} else if (m_language_info_best != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_best->CanonicalName.BeforeFirst('_')) {
    //    // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
    //    // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
    //    language_info = m_language_info_best;
    //} else if (m_language_info_system != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_system->CanonicalName.BeforeFirst('_'))
    //    language_info = m_language_info_system;

    // Alternate language code.
    wxLanguage language_dict = wxLanguage(language_info->Language);
    if (language_info->CanonicalName.BeforeFirst('_') == "sk") {
    	// Slovaks understand Czech well. Give them the Czech translation.
    	language_dict = wxLANGUAGE_CZECH;
		BOOST_LOG_TRIVIAL(trace) << "Using Czech dictionaries for Slovak language";
    }

#ifdef __linux__
    // If we can't find this locale , try to use different one for the language
    // instead of just reporting that it is impossible to switch.
    if (! wxLocale::IsAvailable(language_info->Language)) {
        std::string original_lang = into_u8(language_info->CanonicalName);
        language_info = linux_get_existing_locale_language(language_info, m_language_info_system);
        BOOST_LOG_TRIVIAL(trace) << boost::format("Can't switch language to %1% (missing locales). Using %2% instead.")
                                    % original_lang % language_info->CanonicalName.ToUTF8().data();
    }
#endif

    if (! wxLocale::IsAvailable(language_info->Language)&&initial) {
        language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_UK);
        app_config->set("language", language_info->CanonicalName.ToUTF8().data());
    }
    else if (initial) {
        // bbs supported languages
        //TODO: use a global one with Preference
        wxLanguage supported_languages[]{
            wxLANGUAGE_ENGLISH,
            wxLANGUAGE_CHINESE_SIMPLIFIED,
            wxLANGUAGE_GERMAN,
            wxLANGUAGE_FRENCH,
            wxLANGUAGE_SPANISH,
            wxLANGUAGE_SWEDISH,
            wxLANGUAGE_DUTCH,
            wxLANGUAGE_HUNGARIAN};
        std::string cur_language = app_config->get("language");
        if (cur_language != "") {
            //cleanup the language wrongly set before
            const wxLanguageInfo *langinfo = nullptr;
            bool embedded_language = false;
            int language_num = sizeof(supported_languages) / sizeof(supported_languages[0]);
            for (auto index = 0; index < language_num; index++) {
                langinfo = wxLocale::GetLanguageInfo(supported_languages[index]);
                std::string temp_lan = langinfo->CanonicalName.ToUTF8().data();
                if (cur_language == temp_lan) {
                    embedded_language = true;
                    break;
                }
            }
            if (!embedded_language)
                app_config->erase("app", "language");
        }
    }

    if (! wxLocale::IsAvailable(language_info->Language)) {
    	// Loading the language dictionary failed.
    	wxString message = "Switching Bambu Studio to language " + language_info->CanonicalName + " failed.";
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message += "\nYou may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n";
#endif
        if (initial)
        	message + "\n\nApplication will close.";
        wxMessageBox(message, "Bambu Studio - Switching language failed", wxOK | wxICON_ERROR);
        if (initial)
			std::exit(EXIT_FAILURE);
		else
			return false;
    }

    // Release the old locales, create new locales.
    //FIXME wxWidgets cause havoc if the current locale is deleted. We just forget it causing memory leaks for now.
    m_wxLocale.release();
    m_wxLocale = Slic3r::make_unique<wxLocale>();
    m_wxLocale->Init(language_info->Language);
    // Override language at the active wxTranslations class (which is stored in the active m_wxLocale)
    // to load possibly different dictionary, for example, load Czech dictionary for Slovak language.
    wxTranslations::Get()->SetLanguage(language_dict);
    m_wxLocale->AddCatalog(SLIC3R_APP_KEY);
    m_imgui->set_language(into_u8(language_info->CanonicalName));

    //FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
    //wxSetlocale(LC_NUMERIC, "C");
    Preset::update_suffix_modified((_L("*") + " ").ToUTF8().data());
	return true;
}

Tab* GUI_App::get_tab(Preset::Type type)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab->completed() ? tab : nullptr; // To avoid actions with no-completed Tab
    return nullptr;
}

Tab* GUI_App::get_model_tab(bool part)
{
    return model_tabs_list[part ? 1 : 0];
}

ConfigOptionMode GUI_App::get_mode()
{
    if (!app_config->has("user_mode"))
        return comSimple;
    //BBS
    const auto mode = app_config->get("user_mode");
    return mode == "advanced" ? comAdvanced :
           mode == "simple" ? comSimple :
           mode == "develop" ? comDevelop : comSimple;
}

void GUI_App::save_mode(const /*ConfigOptionMode*/int mode)
{
    //BBS
    const std::string mode_str = mode == comAdvanced ? "advanced" :
                                 mode == comSimple ? "simple" :
                                 mode == comDevelop ? "develop" : "simple";
    app_config->set("user_mode", mode_str);
    app_config->save();
    update_mode();
}

// Update view mode according to selected menu
void GUI_App::update_mode()
{
    sidebar().update_mode();

    //BBS: GUI refactor
    if (mainframe->m_param_panel)
        mainframe->m_param_panel->update_mode();
    if (mainframe->m_param_dialog)
        mainframe->m_param_dialog->panel()->update_mode();
    mainframe->m_webview->update_mode();

#ifdef _MSW_DARK_MODE
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(mainframe->m_tabpanel)->UpdateMode();
#endif

    for (auto tab : tabs_list)
        tab->update_mode();
    for (auto tab : model_tabs_list)
        tab->update_mode();

    //BBS plater()->update_menus();

    plater()->canvas3D()->update_gizmos_on_off_state();
}

//void GUI_App::add_config_menu(wxMenuBar *menu)
//void GUI_App::add_config_menu(wxMenu *menu)
//{
//    auto local_menu = new wxMenu();
//    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt));
//
//    const auto config_wizard_name = _(ConfigWizard::name(true));
//    const auto config_wizard_tooltip = from_u8((boost::format(_utf8(L("Open %s"))) % config_wizard_name).str());
//    // Cmd+, is standard on OS X - what about other operating systems?
//    if (is_editor()) {
//        local_menu->Append(config_id_base + ConfigMenuWizard, config_wizard_name + dots, config_wizard_tooltip);
//        local_menu->Append(config_id_base + ConfigMenuUpdate, _L("Check for Configuration Updates"), _L("Check for configuration updates"));
//        local_menu->AppendSeparator();
//    }
//    local_menu->Append(config_id_base + ConfigMenuPreferences, _L("Preferences") + dots +
//#ifdef __APPLE__
//        "\tCtrl+,",
//#else
//        "\tCtrl+P",
//#endif
//        _L("Application preferences"));
//    wxMenu* mode_menu = nullptr;
//    if (is_editor()) {
//        local_menu->AppendSeparator();
//        mode_menu = new wxMenu();
//        mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeSimple, _L("Simple"), _L("Simple Mode"));
//        mode_menu->AppendRadioItem(config_id_base + ConfigMenuModeAdvanced, _L("Advanced"), _L("Advanced Mode"));
//        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if (get_mode() == comSimple) evt.Check(true); }, config_id_base + ConfigMenuModeSimple);
//        Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { if (get_mode() == comAdvanced) evt.Check(true); }, config_id_base + ConfigMenuModeAdvanced);
//
//        local_menu->AppendSubMenu(mode_menu, _L("Mode"), wxString::Format(_L("%s Mode"), SLIC3R_APP_NAME));
//    }
//    local_menu->AppendSeparator();
//    local_menu->Append(config_id_base + ConfigMenuLanguage, _L("Language"));
//    if (is_editor()) {
//        local_menu->AppendSeparator();
//    }
//
//    local_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent &event) {
//        switch (event.GetId() - config_id_base) {
//        case ConfigMenuWizard:
//            run_wizard(ConfigWizard::RR_USER);
//            break;
//		case ConfigMenuUpdate:
//			check_updates(true);
//			break;
//#ifdef __linux__
//        case ConfigMenuDesktopIntegration:
//            show_desktop_integration_dialog();
//            break;
//#endif
//        case ConfigMenuSnapshots:
//            //BBS do not support task snapshot
//            break;
//        case ConfigMenuPreferences:
//        {
//            //BBS GUI refactor: remove unuse layout logic
//            //bool app_layout_changed = false;
//            {
//                // the dialog needs to be destroyed before the call to recreate_GUI()
//                // or sometimes the application crashes into wxDialogBase() destructor
//                // so we put it into an inner scope
//                PreferencesDialog dlg(mainframe);
//                dlg.ShowModal();
//                //BBS GUI refactor: remove unuse layout logic
//                //app_layout_changed = dlg.settings_layout_changed();
//                if (dlg.seq_top_layer_only_changed())
//                    this->plater_->refresh_print();
//
//                if (dlg.recreate_GUI()) {
//                    recreate_GUI(_L("Restart application") + dots);
//                    return;
//                }
//#ifdef _WIN32
//                if (is_editor()) {
//                    if (app_config->get("associate_3mf") == "true")
//                        associate_3mf_files();
//                    if (app_config->get("associate_stl") == "true")
//                        associate_stl_files();
//                }
//                else {
//                    if (app_config->get("associate_gcode") == "true")
//                        associate_gcode_files();
//                }
//#endif // _WIN32
//            }
//            //BBS GUI refactor: remove unuse layout logic
//            /*if (app_layout_changed) {
//                // hide full main_sizer for mainFrame
//                mainframe->GetSizer()->Show(false);
//                mainframe->update_layout();
//                mainframe->select_tab(size_t(0));
//            }*/
//            break;
//        }
//        case ConfigMenuLanguage:
//        {
//            /* Before change application language, let's check unsaved changes on 3D-Scene
//             * and draw user's attention to the application restarting after a language change
//             */
//            {
//                // the dialog needs to be destroyed before the call to switch_language()
//                // or sometimes the application crashes into wxDialogBase() destructor
//                // so we put it into an inner scope
//                wxString title = is_editor() ? wxString(SLIC3R_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
//                title += " - " + _L("Choose language");
//                //wxMessageDialog dialog(nullptr,
//                MessageDialog dialog(nullptr,
//                    _L("Switching the language requires application restart.\n") + "\n\n" +
//                    _L("Do you want to continue?"),
//                    title,
//                    wxICON_QUESTION | wxOK | wxCANCEL);
//                if (dialog.ShowModal() == wxID_CANCEL)
//                    return;
//            }
//
//            switch_language();
//            break;
//        }
//        case ConfigMenuFlashFirmware:
//            //BBS FirmwareDialog::run(mainframe);
//            break;
//        default:
//            break;
//        }
//    });
//
//    using std::placeholders::_1;
//
//    if (mode_menu != nullptr) {
//        auto modfn = [this](int mode, wxCommandEvent&) { if (get_mode() != mode) save_mode(mode); };
//        mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comSimple, _1), config_id_base + ConfigMenuModeSimple);
//        mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comAdvanced, _1), config_id_base + ConfigMenuModeAdvanced);
//    }
//
//    // BBS
//    //menu->Append(local_menu, _L("Configuration"));
//    menu->AppendSubMenu(local_menu, _L("Configuration"));
//}

void GUI_App::open_preferences(size_t open_on_tab, const std::string& highlight_option)
{
    bool app_layout_changed = false;
    {
        // the dialog needs to be destroyed before the call to recreate_GUI()
        // or sometimes the application crashes into wxDialogBase() destructor
        // so we put it into an inner scope
        PreferencesDialog dlg(mainframe, open_on_tab, highlight_option);
        dlg.ShowModal();
        // BBS
        //app_layout_changed = dlg.settings_layout_changed();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
        if (dlg.seq_top_layer_only_changed())
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            this->plater_->refresh_print();
#ifdef _WIN32
        if (is_editor()) {
            if (app_config->get("associate_3mf") == "true")
                associate_files(L"3mf");
            if (app_config->get("associate_stl") == "true")
                associate_files(L"stl");
            if (app_config->get("associate_step") == "true") {
                associate_files(L"step");
                associate_files(L"stp");
            }
        }
        else {
            if (app_config->get("associate_gcode") == "true")
                associate_files(L"gcode");
        }
#endif // _WIN32
    }

    // BBS
    /*
    if (app_layout_changed) {
        // hide full main_sizer for mainFrame
        mainframe->GetSizer()->Show(false);
        mainframe->update_layout();
        mainframe->select_tab(size_t(0));
    }*/
}

bool GUI_App::has_unsaved_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->saved_preset_is_dirty())
            return true;
    }
    return false;
}

bool GUI_App::has_current_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            return true;
    }
    return false;
}

void GUI_App::update_saved_preset_from_current_preset()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->update_saved_preset_from_current_preset();
    }
}

std::vector<std::pair<unsigned int, std::string>> GUI_App::get_selected_presets() const
{
    std::vector<std::pair<unsigned int, std::string>> ret;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology)) {
            const PresetCollection* presets = tab->get_presets();
            ret.push_back({ static_cast<unsigned int>(presets->type()), presets->get_selected_preset_name() });
        }
    }
    return ret;
}

// To notify the user whether he is aware that some preset changes will be lost,
// UnsavedChangesDialog: "Discard / Save / Cancel"
// This is called when:
// - Close Application & Current project isn't saved
// - Load Project      & Current project isn't saved
// - Undo / Redo with change of print technologie
// - Loading snapshot
// - Loading config_file/bundle
// UnsavedChangesDialog: "Don't save / Save / Cancel"
// This is called when:
// - Exporting config_bundle
// - Taking snapshot
bool GUI_App::check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice/* = true*/, bool dont_save_insted_of_discard/* = false*/)
{
    if (has_current_preset_changes()) {
        int act_buttons = UnsavedChangesDialog::ActionButtons::SAVE;
        if (dont_save_insted_of_discard)
            act_buttons |= UnsavedChangesDialog::ActionButtons::DONT_SAVE;
        UnsavedChangesDialog dlg(caption, header, "", act_buttons);
        if (dlg.ShowModal() == wxID_CANCEL)
            return false;

        if (dlg.save_preset())  // save selected changes
        {
            //BBS: add project embedded preset relate logic
            for (const UnsavedChangesDialog::PresetData& nt : dlg.get_names_and_types())
                preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);
            //for (const std::pair<std::string, Preset::Type>& nt : dlg.get_names_and_types())
            //    preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

            load_current_presets(false);

            // if we saved changes to the new presets, we should to
            // synchronize config.ini with the current selections.
            preset_bundle->export_selections(*app_config);

            //MessageDialog(nullptr, _L_PLURAL("Modifications to the preset have been saved",
            //                                 "Modifications to the presets have been saved", dlg.get_names_and_types().size())).ShowModal();
        }
    }

    return true;
}

void GUI_App::apply_keeped_preset_modifications()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->apply_config_from_cache();
    }
    load_current_presets(false);
}

// This is called when creating new project or load another project
// OR close ConfigWizard
// to ask the user what should we do with unsaved changes for presets.
// New Project          => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Cancel"
//                      => Current project isn't saved => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Close ConfigWizard   => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Note: no_nullptr postponed_apply_of_keeped_changes indicates that thie function is called after ConfigWizard is closed
bool GUI_App::check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes/* = nullptr*/)
{
    if (has_current_preset_changes()) {
        bool is_called_from_configwizard = postponed_apply_of_keeped_changes != nullptr;

        UnsavedChangesDialog dlg(caption, header, "", action_buttons);
        if (dlg.ShowModal() == wxID_CANCEL)
            return false;

        auto reset_modifications = [this, is_called_from_configwizard]() {
            //if (is_called_from_configwizard)
            //    return; // no need to discared changes. It will be done fromConfigWizard closing

            PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
            for (const Tab* const tab : tabs_list) {
                if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        };

        if (dlg.discard())
            reset_modifications();
        else  // save selected changes
        {
            //BBS: add project embedded preset relate logic
            const auto& preset_names_and_types = dlg.get_names_and_types();
            if (dlg.save_preset()) {
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types)
                    preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);

                // if we saved changes to the new presets, we should to
                // synchronize config.ini with the current selections.
                preset_bundle->export_selections(*app_config);

                //wxString text = _L_PLURAL("Modifications to the preset have been saved",
                //    "Modifications to the presets have been saved", preset_names_and_types.size());
                //if (!is_called_from_configwizard)
                //    text += "\n\n" + _L("All modifications will be discarded for new project.");

                //MessageDialog(nullptr, text).ShowModal();
                reset_modifications();
            }
            else if (dlg.transfer_changes() && (dlg.has_unselected_options() || is_called_from_configwizard)) {
                // execute this part of code only if not all modifications are keeping to the new project
                // OR this function is called when ConfigWizard is closed and "Keep modifications" is selected
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types) {
                    Preset::Type type = nt.type;
                    Tab* tab = get_tab(type);
                    std::vector<std::string> selected_options = dlg.get_selected_options(type);
                    if (type == Preset::TYPE_PRINTER) {
                        auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                        if (it != selected_options.end()) {
                            // erase "extruders_count" option from the list
                            selected_options.erase(it);
                            // cache the extruders count
                            static_cast<TabPrinter*>(tab)->cache_extruder_cnt();
                        }
                    }
                    tab->cache_config_diff(selected_options);
                    if (!is_called_from_configwizard)
                        tab->m_presets->discard_current_changes();
                }
                if (is_called_from_configwizard)
                    *postponed_apply_of_keeped_changes = true;
                else
                    apply_keeped_preset_modifications();
            }
        }
    }

    return true;
}

bool GUI_App::can_load_project()
{
    return true;
}

bool GUI_App::check_print_host_queue()
{
    wxString dirty;
    std::vector<std::pair<std::string, std::string>> jobs;
    // Get ongoing jobs from dialog
    mainframe->m_printhost_queue_dlg->get_active_jobs(jobs);
    if (jobs.empty())
        return true;
    // Show dialog
    wxString job_string = wxString();
    for (const auto& job : jobs) {
        job_string += format_wxstr("   %1% : %2% \n", job.first, job.second);
    }
    wxString message;
    message += _(L("The uploads are still ongoing")) + ":\n\n" + job_string +"\n" + _(L("Stop them and continue anyway?"));
    //wxMessageDialog dialog(mainframe,
    MessageDialog dialog(mainframe,
        message,
        wxString(SLIC3R_APP_NAME) + " - " + _(L("Ongoing uploads")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    if (dialog.ShowModal() == wxID_YES)
        return true;

    // TODO: If already shown, bring forward
    mainframe->m_printhost_queue_dlg->Show();
    return false;
}

bool GUI_App::checked_tab(Tab* tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end() &&
        find(model_tabs_list.begin(), model_tabs_list.end(), tab) == model_tabs_list.end())
        ret = false;
    return ret;
}

// Update UI / Tabs to reflect changes in the currently loaded presets
//BBS: add preset combo box re-activate logic
void GUI_App::load_current_presets(bool active_preset_combox/*= false*/, bool check_printer_presets_ /*= true*/)
{
    // check printer_presets for the containing information about "Print Host upload"
    // and create physical printer from it, if any exists
    if (check_printer_presets_)
        check_printer_presets();

    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
	this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Preset::TYPE_PRINTER) {
				static_cast<TabPrinter*>(tab)->update_pages();
				// Mark the plater to update print bed by tab->load_current_preset() from Plater::on_config_change().
				this->plater()->force_print_bed_update();
			}
			tab->load_current_preset();
			//BBS: add preset combox re-active logic
			if (active_preset_combox)
				tab->reactive_preset_combo_box();
		}
    // BBS: model config
    for (Tab *tab : model_tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
            tab->rebuild_page_tree();
        }
}

std::vector<std::string>& GUI_App::get_delete_cache_presets()
{
    return need_delete_presets;
}

void GUI_App::delete_preset_from_cloud(std::string setting_id)
{
    need_delete_presets.push_back(setting_id);
}

bool GUI_App::OnExceptionInMainLoop()
{
    generic_exception_handle();
    return false;
}

#ifdef __APPLE__
// This callback is called from wxEntry()->wxApp::CallOnInit()->NSApplication run
// that is, before GUI_App::OnInit(), so we have a chance to switch GUI_App
// to a G-code viewer.
void GUI_App::OSXStoreOpenFiles(const wxArrayString &fileNames)
{
    //BBS: remove GCodeViewer as seperate APP logic
    /*size_t num_gcodes = 0;
    for (const wxString &filename : fileNames)
        if (is_gcode_file(into_u8(filename)))
            ++ num_gcodes;
    if (fileNames.size() == num_gcodes) {
        // Opening PrusaSlicer by drag & dropping a G-Code onto BambuStudio icon in Finder,
        // just G-codes were passed. Switch to G-code viewer mode.
        m_app_mode = EAppMode::GCodeViewer;
        unlock_lockfile(get_instance_hash_string() + ".lock", data_dir() + "/cache/");
        if(app_config != nullptr)
            delete app_config;
        app_config = nullptr;
        init_app_config();
    }*/
    wxApp::OSXStoreOpenFiles(fileNames);
}
// wxWidgets override to get an event on open files.
void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    if (m_post_initialized) {
        bool has3mf = false;
        std::vector<wxString> names;
        for (auto & n : fileNames) {
            has3mf |= n.EndsWith(".3mf");
            names.push_back(n);
        }
        if (has3mf) {
            start_new_slicer(names);
            return;
        }
    }
    std::vector<std::string> files;
    std::vector<wxString>    gcode_files;
    std::vector<wxString>    non_gcode_files;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", open files, size " << fileNames.size();
    for (const auto& filename : fileNames) {
        if (is_gcode_file(into_u8(filename)))
            gcode_files.emplace_back(filename);
        else {
            files.emplace_back(into_u8(filename));
            non_gcode_files.emplace_back(filename);
        }
    }
    //BBS: remove GCodeViewer as seperate APP logic
    /*if (m_app_mode == EAppMode::GCodeViewer) {
        // Running in G-code viewer.
        // Load the first G-code into the G-code viewer.
        // Or if no G-codes, send other files to slicer.
        if (! gcode_files.empty())
            this->plater()->load_gcode(gcode_files.front());
        if (!non_gcode_files.empty())
            start_new_slicer(non_gcode_files, true);
    } else*/
    {
        if (! files.empty()) {
            if (m_post_initialized) {
                wxArrayString input_files;
                for (size_t i = 0; i < non_gcode_files.size(); ++i) {
                    input_files.push_back(non_gcode_files[i]);
                }
                this->plater()->load_files(input_files);
            }
            else {
                for (size_t i = 0; i < files.size(); ++i) {
                    this->init_params->input_files.emplace_back(files[i]);
                }
            }
        }
        else {
            if (m_post_initialized) {
                this->plater()->load_gcode(gcode_files.front());
            }
            else {
                this->init_params->input_gcode = true;
                this->init_params->input_files = { into_u8(gcode_files.front()) };
            }
        }
        /*for (const wxString &filename : gcode_files)
            start_new_gcodeviewer(&filename);*/
    }
}
#endif /* __APPLE */

Sidebar& GUI_App::sidebar()
{
    return plater_->sidebar();
}

ObjectSettings* GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList* GUI_App::obj_list()
{
    return sidebar().obj_list();
}

Plater* GUI_App::plater()
{
    return plater_;
}

const Plater* GUI_App::plater() const
{
    return plater_;
}

ParamsPanel* GUI_App::params_panel()
{
    if (mainframe)
        return mainframe->m_param_panel;
    return nullptr;
}

ParamsDialog* GUI_App::params_dialog()
{
    if (mainframe)
        return mainframe->m_param_dialog;
    return nullptr;
}

Model& GUI_App::model()
{
    return plater_->model();
}

void GUI_App::load_url(wxString url)
{
    if (mainframe)
        return mainframe->load_url(url);
}

void GUI_App::open_mall_page_dialog()
{
    std::string url;

    if (getAgent() && mainframe) {
        getAgent()->get_model_mall_home_url(&url);

        if (!m_mall_home_dialog) {
            m_mall_home_dialog = new ModelMallDialog();
            m_mall_home_dialog->go_to_mall(url);
        }
        else {
            if (m_mall_home_dialog->IsIconized())
                m_mall_home_dialog->Iconize(false);

            //m_mall_home_dialog->go_to_mall(url);
        }
        m_mall_home_dialog->Raise();
        m_mall_home_dialog->Show();
    }
}

void GUI_App::open_publish_page_dialog()
{
    std::string url;
    if (getAgent() && mainframe) {
        getAgent()->get_model_publish_url(&url);

        if (!m_mall_publish_dialog) {
            m_mall_publish_dialog = new ModelMallDialog();
            m_mall_publish_dialog->go_to_mall(url);
        }
        else {
            if (m_mall_publish_dialog->IsIconized())
                m_mall_publish_dialog->Iconize(false);

            //m_mall_publish_dialog->go_to_publish(url);
        }
        m_mall_publish_dialog->Raise();
        m_mall_publish_dialog->Show();
    }
}

void GUI_App::remove_mall_system_dialog()
{
    if (m_mall_publish_dialog != nullptr) {
        m_mall_publish_dialog->Destroy();
        delete m_mall_publish_dialog;
    }


    if (m_mall_home_dialog != nullptr) {
        m_mall_home_dialog->Destroy();
        delete m_mall_home_dialog;
    }
}

void GUI_App::run_script(wxString js)
{
    if (mainframe)
        return mainframe->RunScript(js);
}

Notebook* GUI_App::tab_panel() const
{
    if (mainframe)
        return mainframe->m_tabpanel;
    return nullptr;
}

NotificationManager * GUI_App::notification_manager()
{
    if (plater_)
        return plater_->get_notification_manager();
    return nullptr;
}

// extruders count from selected printer preset
int GUI_App::extruders_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_selected_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// extruders count from edited printer preset
int GUI_App::extruders_edited_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_edited_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// BBS
int GUI_App::filaments_cnt() const
{
    return preset_bundle->filament_presets.size();
}

wxString GUI_App::current_language_code_safe() const
{
	// Translate the language code to a code, for which Prusa Research maintains translations.
	const std::map<wxString, wxString> mapping {
		{ "cs", 	"cs_CZ", },
		{ "sk", 	"cs_CZ", },
		{ "de", 	"de_DE", },
		{ "nl", 	"nl_NL", },
		{ "sv", 	"sv_SE", },
		{ "es", 	"es_ES", },
		{ "fr", 	"fr_FR", },
		{ "it", 	"it_IT", },
		{ "ja", 	"ja_JP", },
		{ "ko", 	"ko_KR", },
		{ "pl", 	"pl_PL", },
		{ "uk", 	"uk_UA", },
		{ "zh", 	"zh_CN", },
		{ "ru", 	"ru_RU", },
	};
	wxString language_code = this->current_language_code().BeforeFirst('_');
	auto it = mapping.find(language_code);
	if (it != mapping.end())
		language_code = it->second;
	else
		language_code = "en_US";
	return language_code;
}

void GUI_App::open_web_page_localized(const std::string &http_address)
{
    open_browser_with_warning_dialog(http_address + "&lng=" + this->current_language_code_safe());
}

// If we are switching from the FFF-preset to the SLA, we should to control the printed objects if they have a part(s).
// Because of we can't to print the multi-part objects with SLA technology.
bool GUI_App::may_switch_to_SLA_preset(const wxString& caption)
{
    if (model_has_multi_part_objects(model())) {
        // BBS: remove SLA related message
        return false;
    }
    return true;
}

bool GUI_App::run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page)
{
    wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");

    //if (reason == ConfigWizard::RR_USER) {
    //    //TODO: turn off it currently, maybe need to turn on in the future
    //    if (preset_updater->config_update(app_config->orig_version(), PresetUpdater::UpdateParams::FORCED_BEFORE_WIZARD) == PresetUpdater::R_ALL_CANCELED)
    //        return false;
    //}

    //auto wizard_t = new ConfigWizard(mainframe);
    //const bool res = wizard_t->run(reason, start_page);

    std::string strFinish = wxGetApp().app_config->get("firstguide", "finish");
    long        pStyle    = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU;
    if (strFinish == "false" || strFinish.empty())
        pStyle = wxCAPTION | wxTAB_TRAVERSAL;

    GuideFrame wizard(this, pStyle);
    auto page = start_page == ConfigWizard::SP_WELCOME ? GuideFrame::BBL_WELCOME :
                start_page == ConfigWizard::SP_FILAMENTS ? GuideFrame::BBL_FILAMENT_ONLY :
                start_page == ConfigWizard::SP_PRINTERS ? GuideFrame::BBL_MODELS_ONLY :
                GuideFrame::BBL_MODELS;
    wizard.SetStartPage(page);
    bool       res = wizard.run();

    if (res) {
        load_current_presets();

        // BBS: remove SLA related message
    }

    return res;
}

void GUI_App::show_desktop_integration_dialog()
{
#ifdef __linux__
    //wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");
    DesktopIntegrationDialog dialog(mainframe);
    dialog.ShowModal();
#endif //__linux__
}

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
void GUI_App::gcode_thumbnails_debug()
{
    const std::string BEGIN_MASK = "; thumbnail begin";
    const std::string END_MASK = "; thumbnail end";
    std::string gcode_line;
    bool reading_image = false;
    unsigned int width = 0;
    unsigned int height = 0;

    wxFileDialog dialog(GetTopWindow(), _L("Select a G-code file:"), "", "", "G-code files (*.gcode)|*.gcode;*.GCODE;", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::string in_filename = into_u8(dialog.GetPath());
    std::string out_path = boost::filesystem::path(in_filename).remove_filename().append(L"thumbnail").string();

    boost::nowide::ifstream in_file(in_filename.c_str());
    std::vector<std::string> rows;
    std::string row;
    if (in_file.good())
    {
        while (std::getline(in_file, gcode_line))
        {
            if (in_file.good())
            {
                if (boost::starts_with(gcode_line, BEGIN_MASK))
                {
                    reading_image = true;
                    gcode_line = gcode_line.substr(BEGIN_MASK.length() + 1);
                    std::string::size_type x_pos = gcode_line.find('x');
                    std::string width_str = gcode_line.substr(0, x_pos);
                    width = (unsigned int)::atoi(width_str.c_str());
                    std::string height_str = gcode_line.substr(x_pos + 1);
                    height = (unsigned int)::atoi(height_str.c_str());
                    row.clear();
                }
                else if (reading_image && boost::starts_with(gcode_line, END_MASK))
                {
                    std::string out_filename = out_path + std::to_string(width) + "x" + std::to_string(height) + ".png";
                    boost::nowide::ofstream out_file(out_filename.c_str(), std::ios::binary);
                    if (out_file.good())
                    {
                        std::string decoded;
                        decoded.resize(boost::beast::detail::base64::decoded_size(row.size()));
                        decoded.resize(boost::beast::detail::base64::decode((void*)&decoded[0], row.data(), row.size()).first);

                        out_file.write(decoded.c_str(), decoded.size());
                        out_file.close();
                    }

                    reading_image = false;
                    width = 0;
                    height = 0;
                    rows.clear();
                }
                else if (reading_image)
                    row += gcode_line.substr(2);
            }
        }

        in_file.close();
    }
}
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    app_config->save();
}

bool GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized)
{
    if (name.empty()) { return false; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) {
        //window->Maximize(default_maximized);
        return false;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) {
        window->Maximize(default_maximized);
        return true;
    }

    const wxRect& rect = metrics->get_rect();
    window->SetPosition(rect.GetPosition());
    window->SetSize(rect.GetSize());
    window->Maximize(metrics->get_maximized());
    return true;
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow* window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

void GUI_App::window_pos_center(wxTopLevelWindow *window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.center_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

bool GUI_App::config_wizard_startup()
{
    if (!m_app_conf_exists || preset_bundle->printers.only_default_printers()) {
        BOOST_LOG_TRIVIAL(info) << "run wizard...";
        run_wizard(ConfigWizard::RR_DATA_EMPTY);
        BOOST_LOG_TRIVIAL(info) << "finished run wizard";
        return true;
    } /*else if (get_app_config()->legacy_datadir()) {
        // Looks like user has legacy pre-vendorbundle data directory,
        // explain what this is and run the wizard

        MsgDataLegacy dlg;
        dlg.ShowModal();

        run_wizard(ConfigWizard::RR_DATA_LEGACY);
        return true;
    }*/
    return false;
}

void GUI_App::check_updates(const bool verbose)
{
	PresetUpdater::UpdateResult updater_result;
	try {
		updater_result = preset_updater->config_update(app_config->orig_version(), verbose ? PresetUpdater::UpdateParams::SHOW_TEXT_BOX : PresetUpdater::UpdateParams::SHOW_NOTIFICATION);
		if (updater_result == PresetUpdater::R_INCOMPAT_EXIT) {
			mainframe->Close();
		}
		else if (updater_result == PresetUpdater::R_INCOMPAT_CONFIGURED) {
            m_app_conf_exists = true;
		}
		else if (verbose && updater_result == PresetUpdater::R_NOOP) {
			MsgNoUpdates dlg;
			dlg.ShowModal();
		}
	}
	catch (const std::exception & ex) {
		show_error(nullptr, ex.what());
	}
}

bool GUI_App::open_browser_with_warning_dialog(const wxString& url, int flags/* = 0*/)
{
    return wxLaunchDefaultBrowser(url, flags);
}

// static method accepting a wxWindow object as first parameter
// void warning_catcher{
//     my($self, $message_dialog) = @_;
//     return sub{
//         my $message = shift;
//         return if $message = ~/ GLUquadricObjPtr | Attempt to free unreferenced scalar / ;
//         my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
//         $message_dialog
//             ? $message_dialog->(@params)
//             : Wx::MessageDialog->new($self, @params)->ShowModal;
//     };
// }

// Do we need this function???
// void GUI_App::notify(message) {
//     auto frame = GetTopWindow();
//     // try harder to attract user attention on OS X
//     if (!frame->IsActive())
//         frame->RequestUserAttention(defined(__WXOSX__/*&Wx::wxMAC */)? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO);
//
//     // There used to be notifier using a Growl application for OSX, but Growl is dead.
//     // The notifier also supported the Linux X D - bus notifications, but that support was broken.
//     //TODO use wxNotificationMessage ?
// }


#ifdef __WXMSW__
static bool set_into_win_registry(HKEY hkeyHive, const wchar_t* pszVar, const wchar_t* pszValue)
{
    // see as reference: https://stackoverflow.com/questions/20245262/c-program-needs-an-file-association
    wchar_t szValueCurrent[1000];
    DWORD dwType;
    DWORD dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        // an error occurred
        return false;

    if (!bDidntExist) {
        if (dwType != REG_SZ)
            // invalid type
            return false;

        if (::wcscmp(szValueCurrent, pszValue) == 0)
            // value already set
            return false;
    }

    DWORD dwDisposition;
    HKEY hkey;
    iRC = ::RegCreateKeyExW(hkeyHive, pszVar, 0, 0, 0, KEY_ALL_ACCESS, nullptr, &hkey, &dwDisposition);
    bool ret = false;
    if (iRC == ERROR_SUCCESS) {
        iRC = ::RegSetValueExW(hkey, L"", 0, REG_SZ, (BYTE*)pszValue, (::wcslen(pszValue) + 1) * sizeof(wchar_t));
        if (iRC == ERROR_SUCCESS)
            ret = true;
    }

    RegCloseKey(hkey);
    return ret;
}

static bool del_win_registry(HKEY hkeyHive, const wchar_t *pszVar, const wchar_t *pszValue)
{
    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        return false;

    if (!bDidntExist) {
        DWORD dwDisposition;
        HKEY  hkey;
        iRC      = ::RegDeleteKeyExW(hkeyHive, pszVar, KEY_ALL_ACCESS, 0);
        if (iRC == ERROR_SUCCESS) {
            return true;
        }
    }

    return false;
}


void GUI_App::associate_files(std::wstring extend)
{
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Bambu.Studio.1";
    std::wstring prog_desc = L"BambuStudio";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    if (is_new)
        // notify Windows only when any of the values gets changed
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void GUI_App::disassociate_files(std::wstring extend)
{
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Bambu.Studio.1";
    std::wstring prog_desc = L"BambuStudio";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= del_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());

    bool is_associate_3mf  = app_config->get("associate_3mf") == "true";
    bool is_associate_stl  = app_config->get("associate_stl") == "true";
    bool is_associate_step = app_config->get("associate_step") == "true";
    if (!is_associate_3mf && !is_associate_stl && !is_associate_step)
    {
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    }

    if (is_new)
       ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}


#endif // __WXMSW__

} // GUI
} //Slic3r
