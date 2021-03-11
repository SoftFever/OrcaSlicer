#ifndef SLIC3R_GUI_Utils_Platform_HPP
#define SLIC3R_GUI_Utils_Platform_HPP

namespace Slic3r {
namespace GUI {

enum class Platform
{
	Uninitialized,
	Unknown,
	Windows,
	OSX,
	Linux,
	BSDUnix,
};

enum class PlatformFlavor
{
	Uninitialized,
	Unknown,
	// For Windows and OSX, until we need to be more specific.
	Generic,
	// For Platform::Linux
	GenericLinux,
	LinuxOnChromium,
	// For Platform::BSDUnix
	OpenBSD,
};

// To be called on program start-up.
void 			detect_platform();

Platform 		platform();
PlatformFlavor 	platform_flavor();


} // namespace GUI
} // namespace Slic3r

#endif // SLIC3R_GUI_Utils_Platform_HPP
