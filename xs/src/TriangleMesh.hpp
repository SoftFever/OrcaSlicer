#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

#include <admesh/stl.h>

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

namespace Slic3r {

class TriangleMesh
{
    public:
    TriangleMesh();
    ~TriangleMesh();
    void ReadSTLFile(char* input_file);
    void ReadFromPerl(SV* vertices, SV* facets);
    void Repair();
    void WriteOBJFile(char* output_file);
    AV* ToPerl();
    stl_file stl;
};

}

#endif
