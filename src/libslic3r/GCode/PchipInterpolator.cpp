//
//  PchipInterpolator.cpp
//  OrcaSlicer
//
//

#include "PchipInterpolator.hpp"

/**
 * @brief Helper function to parse a string and set the data points.
 * @param data String containing PA and speed values in the format: "pa,speed\npa2,speed2\n..."
 * @return -1 in case of an error, 0 otherwise.
 */
int PchipInterpolator::parseAndSetData(const std::string& data) {
    std::vector<double> pas;
    std::vector<double> speeds;

    std::istringstream stream(data);
    std::string line;

    // Split the input string into lines
    while (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string paStr, speedStr;

        // Split each line into PA and speed values
        if (std::getline(lineStream, paStr, ',') && std::getline(lineStream, speedStr)) {
            try {
                double pa = std::stod(paStr);
                double speed = std::stod(speedStr);
                pas.push_back(pa);
                speeds.push_back(speed);
            } catch (const std::invalid_argument&) {
                return -1; // Invalid format in input data
            } catch (const std::out_of_range&) {
                return -1; // Out of range error in input data
            }
        }
    }

    // Set the parsed data
    if (speeds.size() != pas.size() || speeds.size() < 2) {
        return -1; // Parsed vectors must have the same size and contain at least two elements
    }
    x_ = speeds;
    y_ = pas;
    sortData(); // Sort the data points if needed
    d_ = computeDerivatives(); // Compute the derivatives needed for PCHIP interpolation
    return 0; // Successfully parsed and set data
}

/**
 * @brief Sort the data points based on x-coordinates if they are not already sorted.
 */
void PchipInterpolator::sortData() {
    // Check if the data is already sorted
    if (std::is_sorted(x_.begin(), x_.end())) {
        return;
    }

    // Sort the data points
    std::vector<std::pair<double, double>> xy_pairs(x_.size());
    for (size_t i = 0; i < x_.size(); ++i) {
        xy_pairs[i] = std::make_pair(x_[i], y_[i]);
    }
    std::sort(xy_pairs.begin(), xy_pairs.end());

    for (size_t i = 0; i < x_.size(); ++i) {
        x_[i] = xy_pairs[i].first;
        y_[i] = xy_pairs[i].second;
    }
}

/**
 * @brief Compute the derivatives for PCHIP interpolation.
 * @return A vector of derivatives.
 */
std::vector<double> PchipInterpolator::computeDerivatives() const {
    int n = x_.size();
    std::vector<double> h(n - 1), delta(n - 1), d(n);

    // Calculate the spacing between points (h) and the slopes (delta) between consecutive points
    for (int i = 0; i < n - 1; ++i) {
        h[i] = x_[i + 1] - x_[i];
        delta[i] = (y_[i + 1] - y_[i]) / h[i];
    }

    // Compute the derivative values at each point
    for (int i = 1; i < n - 1; ++i) {
        if (delta[i - 1] * delta[i] > 0) {
            // Use a weighted harmonic mean for the derivatives if the slopes are of the same sign
            d[i] = (std::abs(delta[i - 1]) * h[i] + std::abs(delta[i]) * h[i - 1]) / (h[i - 1] + h[i]);
        } else {
            // Set derivative to zero if the slopes have different signs
            d[i] = 0.0;
        }
    }

    // Set the first and last derivatives to match the slopes at the boundaries
    d[0] = delta[0];
    d[n - 1] = delta[n - 2];

    return d;
}

/**
 * @brief Interpolate a value.
 * @param xi The x-coordinate to interpolate.
 * @return The interpolated y-coordinate or -1 in case of an error.
 */
double PchipInterpolator::operator()(double xi) const {
    // Handle the case where xi is outside the range of the provided data points
    if (x_.empty() || y_.empty() || d_.empty()) {
        return -1; // Data has not been set
    }
    if (xi <= x_.front()) return y_.front();
    if (xi >= x_.back()) return y_.back();

    // Find the interval [x_i, x_(i+1)] that contains xi
    auto it = std::upper_bound(x_.begin(), x_.end(), xi);
    int i = std::distance(x_.begin(), it) - 1;

    // Calculate the normalized distance t within the interval
    double h = x_[i + 1] - x_[i];
    double t = (xi - x_[i]) / h;
    double h00 = (1 + 2 * t) * (1 - t) * (1 - t);
    double h10 = t * (1 - t) * (1 - t);
    double h01 = t * t * (3 - 2 * t);
    double h11 = t * t * (t - 1);
    
    // Interpolate the value using the Hermite cubic polynomial
    double interpolated_value = h00 * y_[i] + h10 * h * d_[i] + h01 * y_[i + 1] + h11 * h * d_[i + 1];

    // Round the interpolated value to three decimal places
    return std::round(interpolated_value * 1000.0) / 1000.0;
}
