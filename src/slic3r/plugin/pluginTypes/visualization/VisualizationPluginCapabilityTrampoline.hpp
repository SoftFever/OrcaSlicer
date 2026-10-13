#pragma once

#include "VisualizationPluginCapability.hpp"
#include "../../PyPluginTrampoline.hpp"
#include "slic3r/plugin/PythonPluginInterface.hpp"
#include <pybind11/pybind11.h>

#include <vector>

namespace Slic3r {

// No audit setup is needed: the negotiated snapshot files live in the plugin's storage folder under
// data_dir(), a global allowed root. Anything else a visualizer does, such as launching a packaged
// helper, goes through the same permission prompt as every other capability.
class PyVisualizationPluginCapabilityTrampoline : public PyPluginCommonTrampoline<VisualizationPluginCapability>
{
public:
    using PyPluginCommonTrampoline<VisualizationPluginCapability>::PyPluginCommonTrampoline;

    std::vector<VisualizationInputSpec> get_supported_inputs() override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE_PURE,
            std::vector<VisualizationInputSpec>,
            VisualizationPluginCapability,
            get_supported_inputs);
    }

    std::vector<VisualizationResourceRequest> get_requested_resources() override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE,
            std::vector<VisualizationResourceRequest>,
            VisualizationPluginCapability,
            get_requested_resources);
    }

    ExecutionResult open(const VisualizationContext& ctx) override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE_PURE,
            ExecutionResult,
            VisualizationPluginCapability,
            open,
            ctx);
    }

    ExecutionResult update(const VisualizationContext& ctx) override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE_PURE,
            ExecutionResult,
            VisualizationPluginCapability,
            update,
            ctx);
    }

    void close() override
    {
        ORCA_PY_OVERRIDE_AUDITED(
            [] {},
            PYBIND11_OVERRIDE,
            void,
            VisualizationPluginCapability,
            close);
    }
};

} // namespace Slic3r
