//
//  PchipInterpolator.hpp
//  OrcaSlicer
//
//  Created by Ioannis Giannakas for Orca Slicer
//

#ifndef PCHIP_INTERPOLATOR_H
#define PCHIP_INTERPOLATOR_H

#include <vector>
#include <stdexcept>
#include <algorithm>

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
     * @brief Set the data points and calculate the model.
     * @param x Vector of x-coordinates (must be sorted).
     * @param y Vector of y-coordinates.
     * @throws std::invalid_argument if x and y have different sizes or fewer than 2 points.
     */
    void setData(const std::vector<double>& x, const std::vector<double>& y);

    /**
     * @brief Interpolate a value.
     * @param xi The x-coordinate to interpolate.
     * @return The interpolated y-coordinate.
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
