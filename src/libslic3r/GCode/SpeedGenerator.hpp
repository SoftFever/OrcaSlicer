#ifndef libslic3r_SpeedGenerator_hpp_
#define libslic3r_SpeedGenerator_hpp_

#include <string>

namespace Slic3r {

class ExtrusionPath;

class SpeedGenerator {
public:
	SpeedGenerator();
	double calculate_speed(const ExtrusionPath& path, double max_speed, double min_speed);

private:
	boost::property_tree::ptree root;
	int speed_table[6];
};

}

#endif // libslic3r_SpeedGenerator_hpp_
