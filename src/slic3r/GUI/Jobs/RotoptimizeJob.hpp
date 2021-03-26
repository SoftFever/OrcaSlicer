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

    struct FindMethod { std::string name; FindFn findfn; };

    static inline const FindMethod Methods[] = {
        { L("Best misalignment"), sla::find_best_misalignment_rotation },
        { L("Least supports"), sla::find_least_supports_rotation }
    };

    size_t m_method_id = 0;
    float  m_accuracy  = 0.75;

    DynamicPrintConfig m_default_print_cfg;

protected:

    void prepare() override;

public:

    RotoptimizeJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}
    
    void process() override;
    void finalize() override;

    static constexpr size_t get_methods_count() { return std::size(Methods); }
    static const auto & get_method_names()
    {
        static bool m_method_names_valid = false;
        static std::array<std::string, std::size(Methods)> m_method_names;

        if (!m_method_names_valid) {

            for (size_t i = 0; i < std::size(Methods); ++i)
                m_method_names[i] = _utf8(Methods[i].name);

            m_method_names_valid = true;
        }

        return m_method_names;
    }
};

}} // namespace Slic3r::GUI

#endif // ROTOPTIMIZEJOB_HPP
