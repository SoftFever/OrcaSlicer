#include "PchipInterpolator.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cmath>

int PchipInterpolator::parseAndSetData(const std::string& data) {
    flow_interpolators_.clear();
    accelerations_.clear();

    try {
        std::istringstream ss(data);
        std::string line;
        std::map<double, std::vector<std::pair<double, double>>> acc_to_flow_pa;

        while (std::getline(ss, line)) {
            std::istringstream lineStream(line);
            std::string value;
            double paValue, flowRate, acceleration;

            if (std::getline(lineStream, value, ',')) {
                paValue = std::stod(value);
            }
            if (std::getline(lineStream, value, ',')) {
                flowRate = std::stod(value);
            }
            if (std::getline(lineStream, value, ',')) {
                acceleration = std::stod(value);
            }

            acc_to_flow_pa[acceleration].emplace_back(flowRate, paValue);
        }

        for (const auto& kv : acc_to_flow_pa) {
            double acceleration = kv.first;
            const auto& data = kv.second;

            std::vector<double> flowRates;
            std::vector<double> paValues;

            for (const auto& pair : data) {
                flowRates.push_back(pair.first);
                paValues.push_back(pair.second);
            }

            if (flowRates.size() > 1) {
                PchipInterpolatorHelper interpolator(flowRates, paValues);
                flow_interpolators_[acceleration] = interpolator;
                accelerations_.push_back(acceleration);
            }
        }
    } catch (const std::exception&) {
        return -1; // Error: Exception during parsing
    }

    return 0; // Success
}

double PchipInterpolator::operator()(double flow_rate, double acceleration) {
    std::vector<double> pa_values;
    std::vector<double> acc_values;

    for (const auto& kv : flow_interpolators_) {
        double pa_value = kv.second.interpolate(flow_rate);
        if (pa_value != -1) {
            pa_values.push_back(pa_value);
            acc_values.push_back(kv.first);
        }
    }

    if (acc_values.size() < 2) {
        if (acc_values.size() == 1) {
            return std::round(pa_values[0] * 1000.0) / 1000.0; // Special case: Only one acceleration value, rounded to 3 decimal places
        }
        return -1; // Error: Not enough data points for interpolation
    }

    // Create a new PchipInterpolatorHelper for PA-acceleration interpolation
    PchipInterpolatorHelper pa_accel_interpolator(acc_values, pa_values);
    return std::round(pa_accel_interpolator.interpolate(acceleration) * 1000.0) / 1000.0; // Rounded to 3 decimal places
}
