#include "Platform.hpp"


// For starting another PrusaSlicer instance on OSX.
// Fails to compile on Windows on the build server.

#include <wx/stdpaths.h>

namespace Slic3r {
namespace GUI {

static auto s_platform 		  = Platform::Uninitialized;
static auto s_platform_flavor = PlatformFlavor::Uninitialized;

void detect_platform()
{
#if defined(_WIN32)
	s_platform 		  = Platform::Windows;
	s_platform_flavor = PlatformFlavor::Generic;
#elif defined(__APPLE__)
	s_platform 		  = Platform::OSX;
	s_platform_flavor = PlatformFlavor::Generic;
#elif defined(__linux__)
	s_platform 		  = Platform::Linux;
	s_platform_flavor = PlatformFlavor::GenericLinux;
	// Test for Chromium.
	{
		FILE *f = ::fopen("/proc/version", "rt");
		if (f) {
			char buf[4096];
			// Read the 1st line.
			if (::fgets(buf, 4096, f) && strstr(buf, "Chromium OS") != nullptr)
				s_platform_flavor = PlatformFlavor::LinuxOnChromium;
			::fclose(f);
		}
	}
#elif defined(__OpenBSD__)
	s_platform 		  = Platform::BSD;
	s_platform_flavor = PlatformFlavor::OpenBSD;
#else
	// This should not happen.
	static_assert(false, "Unknown platform detected");
	s_platform 		  = Platform::Unknown;
	s_platform_flavor = PlatformFlavor::Unknown;
#endif
}

Platform platform()
{
	return s_platform;
}

PlatformFlavor platform_flavor()
{
	return s_platform_flavor;
}

} // namespace GUI
} // namespace Slic3r
