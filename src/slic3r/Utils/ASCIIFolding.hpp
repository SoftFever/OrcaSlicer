#ifndef slic3r_ASCIIFolding_hpp_
#define slic3r_ASCIIFolding_hpp_

#include <string>

namespace Slic3r {

// If possible, remove accents from accented latin characters.
// This function is useful for generating file names to be processed by legacy firmwares.
extern std::string fold_utf8_to_ascii(const char *src);
extern std::string fold_utf8_to_ascii(const std::string &src);

}; // namespace Slic3r

#endif /* slic3r_ASCIIFolding_hpp_ */
