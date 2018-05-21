#include "Serial.hpp"

#include <algorithm>
#include <string>
#include <vector>
#include <fstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#if _WIN32
	#include <Windows.h>
	#include <Setupapi.h>
	#include <initguid.h>
	#include <devguid.h>
	// Undefine min/max macros incompatible with the standard library
	// For example, std::numeric_limits<std::streamsize>::max()
	// produces some weird errors
	#ifdef min
	#undef min
	#endif
	#ifdef max
	#undef max
	#endif
	#include "boost/nowide/convert.hpp"
	#pragma comment(lib, "user32.lib")
#elif __APPLE__
	#include <CoreFoundation/CoreFoundation.h>
	#include <CoreFoundation/CFString.h>
	#include <IOKit/IOKitLib.h>
	#include <IOKit/serial/IOSerialKeys.h>
	#include <IOKit/serial/ioss.h>
	#include <sys/syslimits.h>
#endif

namespace Slic3r {
namespace Utils {

static bool looks_like_printer(const std::string &friendly_name)
{
	return friendly_name.find("Original Prusa") != std::string::npos;
}

#ifdef __linux__
static std::string get_tty_friendly_name(const std::string &path, const std::string &name)
{
	const auto sysfs_product = (boost::format("/sys/class/tty/%1%/device/../product") % name).str();
	std::ifstream file(sysfs_product);
	std::string product;

	std::getline(file, product);
	return file.good() ? (boost::format("%1% (%2%)") % product % path).str() : path;
}
#endif

std::vector<SerialPortInfo> scan_serial_ports_extended()
{
	std::vector<SerialPortInfo> output;

#ifdef _WIN32
	SP_DEVINFO_DATA devInfoData = { 0 };
	devInfoData.cbSize = sizeof(devInfoData);
	// Get the tree containing the info for the ports.
	HDEVINFO hDeviceInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, nullptr, DIGCF_PRESENT);
	if (hDeviceInfo != INVALID_HANDLE_VALUE) {
		// Iterate over all the devices in the tree.
		for (int nDevice = 0; SetupDiEnumDeviceInfo(hDeviceInfo, nDevice, &devInfoData); ++ nDevice) {
			SerialPortInfo port_info;
			// Get the registry key which stores the ports settings.
			HKEY hDeviceKey = SetupDiOpenDevRegKey(hDeviceInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
			if (hDeviceKey) {
				// Read in the name of the port.
				wchar_t pszPortName[4096];
				DWORD dwSize = sizeof(pszPortName);
				DWORD dwType = 0;
				if (RegQueryValueEx(hDeviceKey, L"PortName", NULL, &dwType, (LPBYTE)pszPortName, &dwSize) == ERROR_SUCCESS)
					port_info.port = boost::nowide::narrow(pszPortName);
				RegCloseKey(hDeviceKey);
				if (port_info.port.empty())
					continue;
			}
			// Find the size required to hold the device info.
			DWORD regDataType;
			DWORD reqSize = 0;
			SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, nullptr, 0, &reqSize);
			std::vector<wchar_t> hardware_id(reqSize > 1 ? reqSize : 1);
			// Now store it in a buffer.
			if (! SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_HARDWAREID, &regDataType, (BYTE*)hardware_id.data(), reqSize, nullptr))
				continue;
			port_info.hardware_id = boost::nowide::narrow(hardware_id.data());
			// Find the size required to hold the friendly name.
			reqSize = 0;
			SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_FRIENDLYNAME, nullptr, nullptr, 0, &reqSize);
			std::vector<wchar_t> friendly_name;
			friendly_name.reserve(reqSize > 1 ? reqSize : 1);
			// Now store it in a buffer.
			if (! SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_FRIENDLYNAME, nullptr, (BYTE*)friendly_name.data(), reqSize, nullptr)) {
				port_info.friendly_name = port_info.port;
			} else {
				port_info.friendly_name = boost::nowide::narrow(friendly_name.data());
				port_info.is_printer = looks_like_printer(port_info.friendly_name);
			}
			output.emplace_back(std::move(port_info));
		}
	}
#elif __APPLE__
	// inspired by https://sigrok.org/wiki/Libserialport
	CFMutableDictionaryRef classes = IOServiceMatching(kIOSerialBSDServiceValue);
	if (classes != 0) {
		io_iterator_t iter;
		if (IOServiceGetMatchingServices(kIOMasterPortDefault, classes, &iter) == KERN_SUCCESS) {
			io_object_t port;
			while ((port = IOIteratorNext(iter)) != 0) {
				CFTypeRef cf_property = IORegistryEntryCreateCFProperty(port, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
				if (cf_property) {
					char path[PATH_MAX];
					Boolean result = CFStringGetCString((CFStringRef)cf_property, path, sizeof(path), kCFStringEncodingUTF8);
					CFRelease(cf_property);
					if (result) {
						SerialPortInfo port_info;
						port_info.port = path;
						if ((cf_property = IORegistryEntrySearchCFProperty(port, kIOServicePlane,
						         CFSTR("USB Interface Name"), kCFAllocatorDefault,
						         kIORegistryIterateRecursively | kIORegistryIterateParents)) ||
						    (cf_property = IORegistryEntrySearchCFProperty(port, kIOServicePlane,
						         CFSTR("USB Product Name"), kCFAllocatorDefault,
						         kIORegistryIterateRecursively | kIORegistryIterateParents)) ||
						    (cf_property = IORegistryEntrySearchCFProperty(port, kIOServicePlane,
						         CFSTR("Product Name"), kCFAllocatorDefault,
						         kIORegistryIterateRecursively | kIORegistryIterateParents)) ||
						    (cf_property = IORegistryEntryCreateCFProperty(port, 
						         CFSTR(kIOTTYDeviceKey), kCFAllocatorDefault, 0))) {
							// Description limited to 127 char, anything longer would not be user friendly anyway.
							char description[128];
							if (CFStringGetCString((CFStringRef)cf_property, description, sizeof(description), kCFStringEncodingUTF8)) {
								port_info.friendly_name = std::string(description) + " (" + port_info.port + ")";
								port_info.is_printer = looks_like_printer(port_info.friendly_name);
							}
							CFRelease(cf_property);
						}
						if (port_info.friendly_name.empty())
							port_info.friendly_name = port_info.port;
						output.emplace_back(std::move(port_info));
					}
				}
				IOObjectRelease(port);
			}
		}
	}
#else
    // UNIX / Linux
    std::initializer_list<const char*> prefixes { "ttyUSB" , "ttyACM", "tty.", "cu.", "rfcomm" };
    for (auto &dir_entry : boost::filesystem::directory_iterator(boost::filesystem::path("/dev"))) {
        std::string name = dir_entry.path().filename().string();
        for (const char *prefix : prefixes) {
            if (boost::starts_with(name, prefix)) {
                const auto path = dir_entry.path().string();
                SerialPortInfo spi;
                spi.port = path;
                spi.hardware_id = path;
#ifdef __linux__
                spi.friendly_name = get_tty_friendly_name(path, name);
#else
                spi.friendly_name = path;
#endif
                output.emplace_back(std::move(spi));
                break;
            }
        }
    }
#endif

    output.erase(std::remove_if(output.begin(), output.end(), 
        [](const SerialPortInfo &info) {
            return boost::starts_with(info.port, "Bluetooth") || boost::starts_with(info.port, "FireFly"); 
        }),
        output.end());
    return output;
}

std::vector<std::string> scan_serial_ports()
{
	std::vector<SerialPortInfo> ports = scan_serial_ports_extended();
	std::vector<std::string> output;
	output.reserve(ports.size());
	for (const SerialPortInfo &spi : ports)
		output.emplace_back(std::move(spi.port));
	return output;
}

} // namespace Utils
} // namespace Slic3r
