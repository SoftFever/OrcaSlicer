#include "PlaceholderParser.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif


namespace Slic3r {


PlaceholderParser::PlaceholderParser()
{
    // TODO: port these methods to C++, then call them here
    // this->apply_env_variables();
    // this->update_timestamp();
}

PlaceholderParser::~PlaceholderParser()
{
}

#ifdef SLIC3RXS
REGISTER_CLASS(PlaceholderParser, "GCode::PlaceholderParser");
#endif

}
