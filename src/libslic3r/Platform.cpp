#include "Platform.hpp"

#include <boost/log/trivial.hpp>
#include <boost/filesystem/operations.hpp>

namespace Slic3r {

static auto s_platform 		  = Platform::Uninitialized;
static auto s_platform_flavor = PlatformFlavor::Uninitialized;

void detect_platform()
{
#if defined(_WIN32)
    BOOST_LOG_TRIVIAL(info) << "Platform: Windows";
	s_platform 		  = Platform::Windows;
	s_platform_flavor = PlatformFlavor::Generic;
#elif defined(__APPLE__)
    BOOST_LOG_TRIVIAL(info) << "Platform: OSX";
	s_platform 		  = Platform::OSX;
	s_platform_flavor = PlatformFlavor::Generic;
#elif defined(__linux__)
    BOOST_LOG_TRIVIAL(info) << "Platform: Linux";
	s_platform 		  = Platform::Linux;
	s_platform_flavor = PlatformFlavor::GenericLinux;
	// Test for Chromium.
	{
		FILE *f = ::fopen("/proc/version", "rt");
		if (f) {
			char buf[4096];
			// Read the 1st line.
			if (::fgets(buf, 4096, f)) {
				if (strstr(buf, "Chromium OS") != nullptr) {
					s_platform_flavor = PlatformFlavor::LinuxOnChromium;
				    BOOST_LOG_TRIVIAL(info) << "Platform flavor: LinuxOnChromium";
				} else if (strstr(buf, "microsoft") != nullptr || strstr(buf, "Microsoft") != nullptr) {
					if (boost::filesystem::exists("/run/WSL") && getenv("WSL_INTEROP") != nullptr) {
						BOOST_LOG_TRIVIAL(info) << "Platform flavor: WSL2";
						s_platform_flavor = PlatformFlavor::WSL2;
					} else {
						BOOST_LOG_TRIVIAL(info) << "Platform flavor: WSL";
						s_platform_flavor = PlatformFlavor::WSL;
					}
				}
			}
			::fclose(f);
		}
	}
#elif defined(__OpenBSD__)
    BOOST_LOG_TRIVIAL(info) << "Platform: OpenBSD";
	s_platform 		  = Platform::BSDUnix;
	s_platform_flavor = PlatformFlavor::OpenBSD;
#else
	// This should not happen.
    BOOST_LOG_TRIVIAL(info) << "Platform: Unknown";
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

} // namespace Slic3r
