#ifndef slic3r_FillLightning_hpp_
#define slic3r_FillLightning_hpp_

#include "FillBase.hpp"

/*
* A few modifications based on dba1179(2022.06.10) from Prusa, mainly in Generator.hpp and .cpp: 
* 1. delete the second parameter(a throw back function) of Generator(), since I didnt find corresponding throw back function in BBS code
* 2. those codes that call the functions above
* 3. add codes of generating lightning in TreeSupport.cpp
*/

namespace Slic3r {

class PrintObject;

namespace FillLightning {

class Generator;
// To keep the definition of Octree opaque, we have to define a custom deleter.
struct GeneratorDeleter { void operator()(Generator *p); };
using  GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;

GeneratorPtr build_generator(const PrintObject &print_object);

class Filler : public Slic3r::Fill
{
public:
    ~Filler() override = default;

    Generator   *generator { nullptr };
protected:
    Fill* clone() const override { return new Filler(*this); }

    void _fill_surface_single(const FillParams              &params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point> &direction,
                              ExPolygon                      expolygon,
                              Polylines &polylines_out) override;

    // Let the G-code export reoder the infill lines.
	bool no_sort() const override { return false; }
};

} // namespace FillAdaptive
} // namespace Slic3r

#endif // slic3r_FillLightning_hpp_
