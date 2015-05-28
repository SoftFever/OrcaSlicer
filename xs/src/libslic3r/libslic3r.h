#ifndef _libslic3r_h_
#define _libslic3r_h_

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <ostream>
#include <iostream>
#include <sstream>

#define SLIC3R_VERSION "1.2.8-dev"

#define EPSILON 1e-4
#define SCALING_FACTOR 0.000001
#define PI 3.141592653589793238
#define scale_(val) (val / SCALING_FACTOR)
#define unscale(val) (val * SCALING_FACTOR)
#define SCALED_EPSILON scale_(EPSILON)
typedef long coord_t;
typedef double coordf_t;

namespace Slic3r {

// TODO: make sure X = 0
enum Axis { X, Y, Z };

}
using namespace Slic3r;

/* Implementation of CONFESS("foo"): */
#define CONFESS(...) confess_at(__FILE__, __LINE__, __func__, __VA_ARGS__)
void confess_at(const char *file, int line, const char *func, const char *pat, ...);
/* End implementation of CONFESS("foo"): */

#endif
