#include <iostream>
#include <vector>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/SLAPrintSteps.hpp>
#include <libslic3r/MeshBoolean.hpp>

#include <libnest2d/tools/benchmark.h>

#include <boost/log/trivial.hpp>

namespace Slic3r {

} // namespace Slic3r

int main(const int argc, const char * argv[])
{
    using namespace Slic3r;
    
    if (argc < 1) return EXIT_FAILURE;
    
    DynamicPrintConfig cfg;
    auto model = Model::read_from_file(argv[1], &cfg);
    
    if (model.objects.empty()) return EXIT_SUCCESS;
    
    SLAPrint print;
    print.apply(model, cfg);
    PrintBase::TaskParams task;
    task.to_object_step = slaposHollowing;
    
    print.set_task(task);
    print.process();
    
    Benchmark bench;
    
    for (SLAPrintObject *po : print.objects()) {
        TriangleMesh holes;
        sla::DrainHoles holepts = po->transformed_drainhole_points();
        
        for (auto &hole: holepts)
            holes.merge(sla::to_triangle_mesh(hole.to_mesh()));
        
        TriangleMesh hollowed_mesh = po->transformed_mesh();
        hollowed_mesh.merge(po->hollowed_interior_mesh());
        
        hollowed_mesh.require_shared_vertices();
        holes.require_shared_vertices();
        
        TriangleMesh drilled_mesh_igl = hollowed_mesh;
        bench.start();
        MeshBoolean::minus(drilled_mesh_igl, holes);
        bench.stop();
        
        std::cout << "Mesh boolean duration with IGL: " << bench.getElapsedSec() << std::endl;
        
        TriangleMesh drilled_mesh_cgal = hollowed_mesh;
        bench.start();
        MeshBoolean::cgal::self_union(drilled_mesh_cgal);
        MeshBoolean::cgal::minus(drilled_mesh_cgal, holes);
        bench.stop();
        
        std::cout << "Mesh boolean duration with CGAL: " << bench.getElapsedSec() << std::endl;
        
        std::string name("obj"), outf;
        outf = name + "igl" + std::to_string(po->model_object()->id().id) + ".obj";
        drilled_mesh_igl.WriteOBJFile(outf.c_str());
        
        outf = name + "cgal" + std::to_string(po->model_object()->id().id) + ".obj";
        drilled_mesh_cgal.WriteOBJFile(outf.c_str());
    }
    
    return 0;
}
