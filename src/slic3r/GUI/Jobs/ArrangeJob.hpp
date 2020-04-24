#ifndef ARRANGEJOB_HPP
#define ARRANGEJOB_HPP

#include "Job.hpp"
#include "libslic3r/Arrange.hpp"

namespace Slic3r { namespace GUI {

class Plater;

class ArrangeJob : public Job
{
    Plater *m_plater;
    
    using ArrangePolygon = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;
    
    // The gap between logical beds in the x axis expressed in ratio of
    // the current bed width.
    static const constexpr double LOGICAL_BED_GAP = 1. / 5.;
    
    ArrangePolygons m_selected, m_unselected, m_unprintable;
    
    // clear m_selected and m_unselected, reserve space for next usage
    void clear_input();
    
    // Stride between logical beds
    double bed_stride() const;
    
    // Set up arrange polygon for a ModelInstance and Wipe tower
    template<class T> ArrangePolygon get_arrange_poly(T *obj) const
    {
        ArrangePolygon ap = obj->get_arrange_polygon();
        ap.priority       = 0;
        ap.bed_idx        = ap.translation.x() / bed_stride();
        ap.setter         = [obj, this](const ArrangePolygon &p) {
            if (p.is_arranged()) {
                Vec2d t = p.translation.cast<double>();
                t.x() += p.bed_idx * bed_stride();
                obj->apply_arrange_result(t, p.rotation);
            }
        };
        return ap;
    }
    
    // Prepare all objects on the bed regardless of the selection
    void prepare_all();
    
    // Prepare the selected and unselected items separately. If nothing is
    // selected, behaves as if everything would be selected.
    void prepare_selected();
    
protected:
    
    void prepare() override;
    
public:
    ArrangeJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : Job{std::move(pri)}, m_plater{plater}
    {}
    
    int status_range() const override
    {
        return int(m_selected.size() + m_unprintable.size());
    }
    
    void process() override;
    
    void finalize() override;
};

arrangement::ArrangePolygon get_wipe_tower_arrangepoly(Plater &);
void apply_wipe_tower_arrangepoly(Plater &plater, const arrangement::ArrangePolygon &ap);

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
