#ifndef slic3r_HelioDragon_hpp_
#define slic3r_HelioDragon_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"


namespace Slic3r {

class DynamicPrintConfig;
class Http;
class AppConfig;

class HelioQuery
{

	public:
    struct PresignedURLResult
    {
		std::string key;
		std::string mimeType;
		std::string url;
	};

	struct CreateGCodeQuery
	{
        std::string name;
        std::string materialID;
        std::string printerID;
        std::string gcodeKey;
	};

	struct CreateSimulationQuery
	{
        std::string name;
        std::string gcodeID;
        float initialRoomAirtemp;
		float layerThreshold;
		float objectProximityAirtemp;
	};

	PresignedURLResult create_presigned_url();
};
}

#endif
