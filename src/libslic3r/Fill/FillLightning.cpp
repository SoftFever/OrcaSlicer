#include "../Print.hpp"

#include "FillLightning.hpp"
#include "Lightning/Generator.hpp"
#include "../Surface.hpp"

#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Slic3r::FillLightning {

Polylines Filler::fill_surface(const Surface *surface, const FillParams &params)
{
    const Layer &layer = generator->getTreesForLayer(this->layer_id);
    return layer.convertToLines(to_polygons(surface->expolygon), generator->infilll_extrusion_width());
}

void GeneratorDeleter::operator()(Generator *p) {
    delete p;
}

GeneratorPtr build_generator(const PrintObject &print_object)
{
    return GeneratorPtr(new Generator(print_object));
}

} // namespace Slic3r::FillAdaptive
