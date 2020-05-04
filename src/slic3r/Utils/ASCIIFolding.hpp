#ifndef slic3r_ASCIIFolding_hpp_
#define slic3r_ASCIIFolding_hpp_

#include <string>

namespace Slic3r {

// If possible, remove accents from accented latin characters.
// This function is useful for generating file names to be processed by legacy firmwares.
extern std::string 	fold_utf8_to_ascii(const char *src);
extern std::string 	fold_utf8_to_ascii(const std::string &src);

// Convert the input UNICODE character to a string of maximum 4 output ASCII characters.
// Return the end of the string written to the output.
// The output buffer must be at least 4 characters long.
extern char* 		fold_to_ascii(wchar_t c, char *out);

template<typename OUTPUT_ITERATOR> 
void fold_to_ascii(wchar_t c, OUTPUT_ITERATOR out)
{
	char tmp[4];
	char *end = fold_to_ascii(c, tmp);
	for (char *it = tmp; it != end; ++ it)
		*out = *it;
}

}; // namespace Slic3r

#endif /* slic3r_ASCIIFolding_hpp_ */
