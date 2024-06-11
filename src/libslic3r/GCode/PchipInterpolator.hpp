#ifndef PCHIPINTERPOLATOR_HPP
#define PCHIPINTERPOLATOR_HPP

#include <vector>
#include <string>
#include <map>
#include "PchipInterpolatorHelper.hpp"

class PchipInterpolator {
public:
    PchipInterpolator() = default;
    int parseAndSetData(const std::string& data);
    double operator()(double flow_rate, double acceleration);

private:
    std::map<double, PchipInterpolatorHelper> flow_interpolators_; // Map each acceleration to a flow-rate-to-PA interpolator
    std::vector<double> accelerations_; // Store unique accelerations
};

#endif // PCHIPINTERPOLATOR_HPP
