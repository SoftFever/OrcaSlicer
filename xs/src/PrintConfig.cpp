#include "PrintConfig.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif

namespace Slic3r {

t_optiondef_map PrintConfigDef::def = PrintConfigDef::build_def();

#ifdef SLIC3RXS
REGISTER_CLASS(DynamicPrintConfig, "Config");
REGISTER_CLASS(PrintObjectConfig, "Config::PrintObject");
REGISTER_CLASS(PrintRegionConfig, "Config::PrintRegion");
REGISTER_CLASS(PrintConfig, "Config::Print");
REGISTER_CLASS(FullPrintConfig, "Config::Full");
#endif

}
