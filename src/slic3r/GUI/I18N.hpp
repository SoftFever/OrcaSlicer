#ifndef _
#define _(s)    Slic3r::GUI::I18N::translate((s))
#define _utf8(s)    Slic3r::GUI::I18N::translate_utf8((s))
#endif /* _ */

#ifndef _CTX
#define _CTX(s, ctx) Slic3r::GUI::I18N::translate((s), (ctx))
#define _CTX_utf8(s, ctx) Slic3r::GUI::I18N::translate_utf8((s), (ctx))
#endif /* _ */

#ifndef L
// !!! If you needed to translate some wxString,
// !!! please use _(L(string))
// !!! _() - is a standard wxWidgets macro to translate
// !!! L() is used only for marking localizable string 
// !!! It will be used in "xgettext" to create a Locating Message Catalog.
#define L(s) s
#endif /* L */

#ifndef L_CONTEXT
#define L_CONTEXT(s, context) s
#endif /* L */

#ifndef _CHB
//! macro used to localization, return wxScopedCharBuffer
//! With wxConvUTF8 explicitly specify that the source string is already in UTF-8 encoding
#define _CHB(s) wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str()
#endif /* _CHB */

#ifndef slic3r_GUI_I18N_hpp_
#define slic3r_GUI_I18N_hpp_

#include <wx/intl.h>
#include <wx/version.h>

namespace Slic3r { namespace GUI { 

namespace I18N {
	inline wxString translate(const char *s)    	 { return wxGetTranslation(wxString(s, wxConvUTF8)); }
	inline wxString translate(const wchar_t *s) 	 { return wxGetTranslation(s); }
	inline wxString translate(const std::string &s)  { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)); }
	inline wxString translate(const std::wstring &s) { return wxGetTranslation(s.c_str()); }

	inline std::string translate_utf8(const char *s)    	 { return wxGetTranslation(wxString(s, wxConvUTF8)).ToUTF8().data(); }
	inline std::string translate_utf8(const wchar_t *s) 	 { return wxGetTranslation(s).ToUTF8().data(); }
	inline std::string translate_utf8(const std::string &s)  { return wxGetTranslation(wxString(s.c_str(), wxConvUTF8)).ToUTF8().data(); }
	inline std::string translate_utf8(const std::wstring &s) { return wxGetTranslation(s.c_str()).ToUTF8().data(); }

#if wxCHECK_VERSION(3, 1, 1)
	#define _wxGetTranslation_ctx(S, CTX) wxGetTranslation((S), wxEmptyString, (CTX))
#else
	#define _wxGetTranslation_ctx(S, CTX) ((void)(CTX), wxGetTranslation((S)))
#endif

	inline wxString translate(const char *s, const char* ctx)         { return _wxGetTranslation_ctx(wxString(s, wxConvUTF8), ctx); }
	inline wxString translate(const wchar_t *s, const char* ctx)      { return _wxGetTranslation_ctx(s, ctx); }
	inline wxString translate(const std::string &s, const char* ctx)  { return _wxGetTranslation_ctx(wxString(s.c_str(), wxConvUTF8), ctx); }
	inline wxString translate(const std::wstring &s, const char* ctx) { return _wxGetTranslation_ctx(s.c_str(), ctx); }

	inline wxString translate_utf8(const char *s, const char* ctx)         { return _wxGetTranslation_ctx(wxString(s, wxConvUTF8), ctx).ToUTF8().data(); }
	inline wxString translate_utf8(const wchar_t *s, const char* ctx)      { return _wxGetTranslation_ctx(s, ctx).ToUTF8().data(); }
	inline wxString translate_utf8(const std::string &s, const char* ctx)  { return _wxGetTranslation_ctx(wxString(s.c_str(), wxConvUTF8), ctx).ToUTF8().data(); }
	inline wxString translate_utf8(const std::wstring &s, const char* ctx) { return _wxGetTranslation_ctx(s.c_str(), ctx).ToUTF8().data(); }

#undef _wxGetTranslation_ctx
}

// Return translated std::string as a wxString
wxString	L_str(const std::string &str);

} }

#endif /* slic3r_GUI_I18N_hpp_ */
