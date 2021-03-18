#ifndef SLA_ROTFINDER_HPP
#define SLA_ROTFINDER_HPP

#include <functional>
#include <array>

#include <libslic3r/Point.hpp>

namespace Slic3r {

class SLAPrintObject;

namespace sla {

/**
  * The function should find the best rotation for SLA upside down printing.
  *
  * @param modelobj The model object representing the 3d mesh.
  * @param accuracy The optimization accuracy from 0.0f to 1.0f. Currently,
  * the nlopt genetic optimizer is used and the number of iterations is
  * accuracy * 100000. This can change in the future.
  * @param statuscb A status indicator callback called with the int
  * argument spanning from 0 to 100. May not reach 100 if the optimization finds
  * an optimum before max iterations are reached. It should return a boolean
  * signaling if the operation may continue (true) or not (false). A status
  * value lower than 0 shall not update the status but still return a valid
  * continuation indicator.
  *
  * @return Returns the rotations around each axis (x, y, z)
  */
Vec2d find_best_rotation(
        const SLAPrintObject& modelobj,
        float accuracy = 1.0f,
        std::function<bool(int)> statuscb = [] (int) { return true; }
        );

double get_model_supportedness(const SLAPrintObject &mesh,
                               const Transform3f & tr);

} // namespace sla
} // namespace Slic3r

#endif // SLAROTFINDER_HPP
