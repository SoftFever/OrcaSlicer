#ifndef slic3r_I18N_hpp_
#define slic3r_I18N_hpp_

#include <string>

namespace Slic3r {

namespace I18N {
	typedef std::string (*translate_fn_type)(const char*);
	extern translate_fn_type translate_fn;
	inline void set_translate_callback(translate_fn_type fn) { translate_fn = fn; }
	inline std::string translate(const std::string &s) { return (translate_fn == nullptr) ? s : (*translate_fn)(s.c_str()); }
	inline std::string translate(const char *ptr) { return (translate_fn == nullptr) ? std::string(ptr) : (*translate_fn)(ptr); }
} // namespace I18N

} // namespace Slic3r

#endif /* slic3r_I18N_hpp_ */
