#ifndef SLIC3R_Platform_HPP
#define SLIC3R_Platform_HPP

namespace Slic3r {

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
	// Microsoft's Windows on Linux (Linux kernel simulated on NTFS kernel)
	WSL,
	// Microsoft's Windows on Linux, version 2 (virtual machine)
	WSL2,
	// For Platform::BSDUnix
	OpenBSD,
	// For Platform::OSX
	GenericOSX,
	// For Apple's on Intel X86 CPU
	OSXOnX86,
	// For Apple's on Arm CPU
	OSXOnArm,
};

// To be called on program start-up.
void 			detect_platform();

Platform 		platform();
PlatformFlavor 	platform_flavor();

} // namespace Slic3r

#endif // SLIC3R_Platform_HPP
