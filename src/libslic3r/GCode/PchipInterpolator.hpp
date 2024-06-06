//
//  PchipInterpolator.hpp
//  OrcaSlicer
//
//

#ifndef PCHIP_INTERPOLATOR_H
#define PCHIP_INTERPOLATOR_H

#include <vector>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <sstream>

/**
 * @class PchipInterpolator
 * @brief Implements Piecewise Cubic Hermite Interpolating Polynomial (PCHIP) interpolation.
 */
class PchipInterpolator {
public:
    /**
     * @brief Default constructor for PchipInterpolator.
     */
    PchipInterpolator() = default;

    /**
     * @brief Helper function to parse a string and set the data points.
     * @param data String containing PA and speed values in the format: "pa,speed\npa2,speed2\n..."
     * @return -1 in case of an error, 0 otherwise.
     */
    int parseAndSetData(const std::string& data);

    /**
     * @brief Interpolate a value.
     * @param xi The x-coordinate to interpolate.
     * @return The interpolated y-coordinate or -1 in case of an error.
     */
    double operator()(double xi) const;

private:
    std::vector<double> x_; ///< x-coordinates of the data points
    std::vector<double> y_; ///< y-coordinates of the data points
    std::vector<double> d_; ///< Computed derivatives at the data points

    /**
     * @brief Compute the derivatives for PCHIP interpolation.
     * @return A vector of derivatives.
     */
    std::vector<double> computeDerivatives() const;

    /**
     * @brief Sort the data points based on x-coordinates if they are not already sorted.
     */
    void sortData();
};

#endif // PCHIP_INTERPOLATOR_H
