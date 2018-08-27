#include "OctoPrint.hpp"
#include "Duet.hpp"

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {


PrintHost::~PrintHost() {}

PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
	PrintHostType kind = config->option<ConfigOptionEnum<PrintHostType>>("host_type")->value;
	if (kind == htOctoPrint) {
		return new OctoPrint(config);
	} else if (kind == htDuet) {
		return new Duet(config);
	}
	return nullptr;
}


}
