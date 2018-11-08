#include "SLAPrint.hpp"

namespace Slic3r {

void SLAPrint::clear()
{
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model, const DynamicPrintConfig &config)
{
	return APPLY_STATUS_INVALIDATED;
}

void SLAPrint::process()
{
}

} // namespace Slic3r