#include "SpeedGenerator.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Utils.hpp"

#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/date_time.hpp>
#include <boost/foreach.hpp>

namespace Slic3r {

SpeedGenerator::SpeedGenerator() {
	// default is 100 speed
	for (int i = 0; i < 6; i++) {
		speed_table[i] = 100;
	}
	std::string config_file = resources_dir() + "/PerimeterSpeedConfig.json";
	std::string encoded_path = encode_path(config_file.c_str());
	boost::property_tree::read_json<boost::property_tree::ptree>(encoded_path, root);
	if (root.count("speed_table")) {
		int i = 0;
		auto array6 = root.get_child("speed_table");
		for (auto pos = array6.begin(); pos != array6.end() && i < 6; pos++, i++)
			speed_table[i] = pos->second.get_value<int>();
	}
}

double SpeedGenerator::calculate_speed(const ExtrusionPath& path, double max_speed, double min_speed) {
	// limit the speed in case of F0 generated in gcode when user set 0 speed in UI
	// which cause printer stopped. 1mm/s is slow enough and can make printer not really stopped.
	max_speed = max_speed < 1 ? 1 : max_speed;
	min_speed = min_speed < 1 ? 1 : min_speed;
	// switch min and max speed if user set the max speed to be slower than min_speed
	if (max_speed < min_speed) {
		double temp = max_speed;
		max_speed = min_speed;
		min_speed = temp;
	}
	speed_table[0] = max_speed;

	int overhang_degree = path.get_overhang_degree();
	assert(overhang_degree >= 0 && overhang_degree <= 6);
	double speed = (double)speed_table[overhang_degree];
	speed = std::max(speed, min_speed);
	speed = std::min(speed, max_speed);
	return speed;
}

}
