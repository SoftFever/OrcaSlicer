#ifndef FILLBEDJOB_HPP
#define FILLBEDJOB_HPP

#include "ArrangeJob.hpp"

namespace Slic3r { namespace GUI {

class Plater;

class FillBedJob : public PlaterJob
{
    int     m_object_idx = -1;

    using ArrangePolygon  = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    ArrangePolygons m_selected;
    ArrangePolygons m_unselected;

    Points m_bedpts;

    int m_status_range = 0;

protected:

    void prepare() override;

public:
    FillBedJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}

    int status_range() const override
    {
        return m_status_range;
    }

    void process() override;

    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // FILLBEDJOB_HPP
