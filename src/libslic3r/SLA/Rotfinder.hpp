#ifndef SLA_ROTFINDER_HPP
#define SLA_ROTFINDER_HPP

#include <functional>
#include <array>

namespace Slic3r {

class ModelObject;

namespace sla {

/**
  * The function should find the best rotation for SLA upside down printing.
  *
  * @param modelobj The model object representing the 3d mesh.
  * @param accuracy The optimization accuracy from 0.0f to 1.0f. Currently,
  * the nlopt genetic optimizer is used and the number of iterations is
  * accuracy * 100000. This can change in the future.
  * @param statuscb A status indicator callback called with the unsigned
  * argument spanning from 0 to 100. May not reach 100 if the optimization finds
  * an optimum before max iterations are reached.
  * @param stopcond A function that if returns true, the search process will be
  * terminated and the best solution found will be returned.
  *
  * @return Returns the rotations around each axis (x, y, z)
  */
std::array<double, 3> find_best_rotation(
        const ModelObject& modelobj,
        float accuracy = 1.0f,
        std::function<void(unsigned)> statuscb = [] (unsigned) {},
        std::function<bool()> stopcond = [] () { return false; }
        );

}
}

#endif // SLAROTFINDER_HPP
