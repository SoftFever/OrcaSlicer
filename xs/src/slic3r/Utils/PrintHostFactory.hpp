#ifndef slic3r_PrintHostFactory_hpp_
#define slic3r_PrintHostFactory_hpp_

#include <string>
#include <wx/string.h>


namespace Slic3r {

class DynamicPrintConfig;
class PrintHost;

class PrintHostFactory
{
public:
	PrintHostFactory() {};
	~PrintHostFactory() {};
	static PrintHost * get_print_host(DynamicPrintConfig *config);
};


}

#endif
