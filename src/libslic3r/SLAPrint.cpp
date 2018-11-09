#include "SLAPrint.hpp"

namespace Slic3r {

void SLAPrint::clear()
{
	tbb::mutex::scoped_lock lock(this->cancel_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
	for (SLAPrintObject *object : m_objects)
		delete object;
	m_objects.clear();
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model, const DynamicPrintConfig &config)
{
	if (m_objects.empty())
		return APPLY_STATUS_UNCHANGED;

    // Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->cancel_mutex());

	// Temporary quick fix, just invalidate everything.
	{
		for (SLAPrintObject *print_object : m_objects) {
			print_object->invalidate_all_steps();
			delete print_object;
		}
		m_objects.clear();
		this->invalidate_all_steps();
		// Copy the model by value (deep copy), keep the Model / ModelObject / ModelInstance / ModelVolume IDs.
        m_model.assign_copy(model);
        // Generate new SLAPrintObjects.
        for (const ModelObject *model_object : m_model.objects) {
       		//TODO
        }
	}

	return APPLY_STATUS_INVALIDATED;
}

void SLAPrint::process()
{
}

} // namespace Slic3r
