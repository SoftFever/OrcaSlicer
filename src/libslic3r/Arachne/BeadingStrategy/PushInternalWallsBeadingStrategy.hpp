#ifndef PUSH_INTERNAL_WALLS_BEADING_STRATEGY_H
#define PUSH_INTERNAL_WALLS_BEADING_STRATEGY_H

#include "BeadingStrategy.hpp"

namespace Slic3r::Arachne
{

/*!
 * This strategy intended to incrrease outer shell strength by interlocking layers.
 *
 * Idea is to make cross section to look like bricklayer. To do this second perimeter width
 * increased by given percentage effectively pushing rest of internal perimetern inwards
 */
class PushInternalWallsBeadingStrategy : public BeadingStrategy
{
public:
    /*!
     * Takes responsibility for deleting \param parent
     */
    PushInternalWallsBeadingStrategy(BeadingStrategyPtr parent, unsigned int interlock_percent_);

    ~PushInternalWallsBeadingStrategy() override = default;

    Beading compute(coord_t thickness, coord_t bead_count) const override;
    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;
    std::vector<coord_t> getNonlinearThicknesses(coord_t lower_bead_count) const override;
    std::string toString() const override;

protected:
    BeadingStrategyPtr parent;
    unsigned int interlock_percent = 0;
};

} // namespace Slic3r::Arachne
#endif // PUSH_INTERNAL_WALLS_BEADING_STRATEGY_H
