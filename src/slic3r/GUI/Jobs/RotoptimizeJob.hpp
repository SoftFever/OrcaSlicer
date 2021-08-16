#ifndef ROTOPTIMIZEJOB_HPP
#define ROTOPTIMIZEJOB_HPP

#include "PlaterJob.hpp"

#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

namespace GUI {

class RotoptimizeJob : public PlaterJob
{
    using FindFn = std::function<Vec2d(const ModelObject &           mo,
                                       const sla::RotOptimizeParams &params)>;

    struct FindMethod { std::string name; FindFn findfn; std::string descr; };

    static inline const FindMethod Methods[] = {
        { L("Best surface quality"), sla::find_best_misalignment_rotation, L("Optimize object rotation for best surface quality.") },
        { L("Least supports"), sla::find_least_supports_rotation, L("Optimize object rotation to have minimum amount of overhangs needing support structures.") },
        // Just a min area bounding box that is done for all methods anyway.
        { L("Z axis only"), nullptr, L("Rotate the object only in Z axis to have the smallest bounding box.") }
    };

    size_t m_method_id = 0;
    float  m_accuracy  = 0.75;

    DynamicPrintConfig m_default_print_cfg;

    struct ObjRot
    {
        size_t               idx;
        std::optional<Vec2d> rot;
        ObjRot(size_t id): idx{id}, rot{} {}
    };

    std::vector<ObjRot> m_selected_object_ids;

protected:

    void prepare() override;

public:

    RotoptimizeJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}
    
    void process() override;
    void finalize() override;

    static constexpr size_t get_methods_count() { return std::size(Methods); }

    static std::string get_method_name(size_t i)
    {
        return _utf8(Methods[i].name);
    }

    static std::string get_method_description(size_t i)
    {
        return _utf8(Methods[i].descr);
    }
};

}} // namespace Slic3r::GUI

#endif // ROTOPTIMIZEJOB_HPP
