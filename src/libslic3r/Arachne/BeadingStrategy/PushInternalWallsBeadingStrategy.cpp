#include "PushInternalWallsBeadingStrategy.hpp"

namespace Slic3r::Arachne
{

PushInternalWallsBeadingStrategy::PushInternalWallsBeadingStrategy(BeadingStrategyPtr parent, unsigned int interlock_percent_)
    : BeadingStrategy(*parent)
    , parent(std::move(parent))
    , interlock_percent(interlock_percent_)
{
}

std::string PushInternalWallsBeadingStrategy::toString() const
{
    return std::string("PushInternalWalls+") + parent->toString();
}

PushInternalWallsBeadingStrategy::Beading PushInternalWallsBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    auto ret = parent->compute(thickness, bead_count);

    if (bead_count > 1) {
        coord_t push_width = ret.bead_widths[1] *  interlock_percent / 100;

        ret.bead_widths[1] += push_width;
        ret.toolpath_locations[1] += push_width / 2;

        for (unsigned i = 2; i < bead_count; i++) {
            ret.toolpath_locations[i] += push_width;
        }

    }

    return ret;
}

coord_t PushInternalWallsBeadingStrategy::getOptimalThickness(coord_t bead_count) const
{
    return parent->getOptimalThickness(bead_count);
}

coord_t PushInternalWallsBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    return parent->getTransitionThickness(lower_bead_count);
}

coord_t PushInternalWallsBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    return parent->getOptimalBeadCount(thickness);
}

coord_t PushInternalWallsBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float PushInternalWallsBeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::vector<coord_t> PushInternalWallsBeadingStrategy::getNonlinearThicknesses(coord_t lower_bead_count) const
{
    return parent->getNonlinearThicknesses(lower_bead_count);
}

} // namespace Slic3r::Arachne
