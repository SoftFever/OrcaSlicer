#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

	HelioQuery::PresignedURLResult create_presigned_url() { 
		std::string                    a = "";
        HelioQuery::PresignedURLResult ret_item = {a, a, a};
        return ret_item;
	};
}
