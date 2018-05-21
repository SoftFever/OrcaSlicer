#ifndef slic3r_GUI_Utils_Serial_hpp_
#define slic3r_GUI_Utils_Serial_hpp_

#include <memory>
#include <vector>
#include <string>
#include <vector>

namespace Slic3r {
namespace Utils {

struct SerialPortInfo {
	std::string port;
	std::string hardware_id;
	std::string friendly_name;
	bool 		is_printer = false;
};

inline bool operator==(const SerialPortInfo &sp1, const SerialPortInfo &sp2) 
{
	return sp1.port 	   == sp2.port 	      &&
		   sp1.hardware_id == sp2.hardware_id &&
		   sp1.is_printer  == sp2.is_printer;
}

extern std::vector<std::string> 	scan_serial_ports();
extern std::vector<SerialPortInfo> 	scan_serial_ports_extended();

} // Utils
} // Slic3r

#endif /* slic3r_GUI_Utils_Serial_hpp_ */
