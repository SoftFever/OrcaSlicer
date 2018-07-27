#include "avrdude.h"


static const char* SYS_CONFIG = "/etc/avrdude-slic3r.conf";

int main(int argc, char *argv[])
{
	return avrdude_main(argc, argv, SYS_CONFIG);
}
