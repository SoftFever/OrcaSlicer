#include <admesh/stl.h>

class TriangleMesh
{
    public:
    TriangleMesh();
    ~TriangleMesh();
    void ReadSTLFile(char* input_file);
    void Repair();
    void WriteOBJFile(char* output_file);
    private:
    stl_file stl;
};


