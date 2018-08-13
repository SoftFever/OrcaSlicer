#ifndef slic3r_Hex_hpp_
#define slic3r_Hex_hpp_

#include <string>
#include <boost/filesystem/path.hpp>


namespace Slic3r {
namespace Utils {


struct HexFile
{
	enum DeviceKind {
		DEV_GENERIC,
		DEV_MK2,
		DEV_MK3,
		DEV_MM_CONTROL,
	};

	boost::filesystem::path path;
	DeviceKind device = DEV_GENERIC;
	std::string model_id;

	HexFile() {}
	HexFile(boost::filesystem::path path);
};


}
}

#endif
