#pragma once

// User-facing plugin messages are translated in the GUI build. The headless probe links neither
// libslic3r_gui nor wx, so it formats the same English text without wxGetTranslation. The GUI path
// expands to exactly the calls the plugin sources used before this header existed.

#if defined(ORCA_PLUGIN_PROBE_BUILD)

#include "libslic3r/format.hpp"

#include <string>
#include <utility>

namespace Slic3r::plugin_text {

inline std::string translate_utf8(const char* text) { return text == nullptr ? std::string{} : std::string(text); }
inline std::string translate_utf8(const std::string& text) { return text; }

template<typename... TArgs>
inline std::string format(const char* fmt, TArgs&&... args)
{
    return Slic3r::format(fmt, std::forward<TArgs>(args)...);
}

template<typename... TArgs>
inline std::string format(const std::string& fmt, TArgs&&... args)
{
    return Slic3r::format(fmt, std::forward<TArgs>(args)...);
}

} // namespace Slic3r::plugin_text

#define PLUGIN_U8L(text) ::Slic3r::plugin_text::translate_utf8(text)
#define PLUGIN_FORMAT(...) ::Slic3r::plugin_text::format(__VA_ARGS__)

#else

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"

#define PLUGIN_U8L(text) _u8L(text)
#define PLUGIN_FORMAT(...) GUI::format(__VA_ARGS__)

#endif
