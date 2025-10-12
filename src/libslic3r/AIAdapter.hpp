#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Slic3r { namespace AI {

struct GeometryFeatures {
    double width = 0.0;
    double height = 0.0;
    double depth = 0.0;
    double surface_area = 0.0;
    double volume = 0.0;
    bool has_overhangs = false;
    bool has_bridges = false;
    bool has_thin_walls = false;
    bool has_small_details = false;
    double overhang_percentage = 0.0;
    int layer_count_estimate = 0;
};

struct AIOptimizationResult {
    nlohmann::json parameters;
    std::string reasoning;
    double confidence = 0.0;
    std::string source;
};

class AIAdapter {
public:
    virtual ~AIAdapter() = default;
    virtual bool is_available() const = 0;
    virtual std::string name() const = 0;
    virtual AIOptimizationResult optimize(const GeometryFeatures& features,
                                          const nlohmann::json& current_parameters,
                                          const std::string& context = "") = 0;
};

}} // namespace Slic3r::AI
