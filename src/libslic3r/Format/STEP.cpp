#include "../libslic3r.h"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include "STEP.hpp"

#include <string>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/fstream.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "Interface_Static.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "TopoDS_Solid.hxx"
#include "TopoDS_Compound.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"

const double STEP_TRANS_CHORD_ERROR = 0.003;
const double STEP_TRANS_ANGLE_RES = 0.5;


namespace Slic3r {

bool StepPreProcessor::preprocess(const char* path, std::string &output_path)
{
    boost::nowide::ifstream infile(path);
    if (!infile.good()) {
        throw Slic3r::RuntimeError(std::string("Load step file failed.\nCannot open file for reading.\n"));
        return false;
    }

    boost::filesystem::path temp_path(temporary_dir());
    std::string temp_step_path = temp_path.string() + "/temp.step";
    boost::nowide::remove(temp_step_path.c_str());
    boost::nowide::ofstream temp_file(temp_step_path, std::ios::app);
    std::string temp_line;
    while (std::getline(infile, temp_line)) {
        if (m_encode_type == EncodedType::UTF8) {
            //BBS: continue to judge whether is other type
            if (isUtf8(temp_line)) {
                //BBS: do nothing, but must be checked before checking whether is GBK
            }
            //BBS: not utf8, then maybe GBK
            else if (isGBK(temp_line)) {
                m_encode_type = EncodedType::GBK;
            }
            //BBS: not UTF8 and not GBK, then maybe some kind of special encoded type which we can't handle
            // Load the step as UTF and user will see garbage characters in slicer but we have no solution at the moment
            else {
                m_encode_type = EncodedType::OTHER;
            }
        }
        if (m_encode_type == EncodedType::GBK)
            //BBS: transform to UTF8 format if is GBK
            //todo: use gbkToUtf8 function to replace
            temp_file << decode_path(temp_line.c_str()) << std::endl;
        else
            temp_file << temp_line.c_str() << std::endl;
    }
    temp_file.close();
    infile.close();
    if (m_encode_type == EncodedType::GBK) {
        output_path = temp_step_path;
    } else {
        boost::nowide::remove(temp_step_path.c_str());
        output_path = std::string(path);
    }

    return true;
}

bool StepPreProcessor::isUtf8File(const char* path)
{
    boost::nowide::ifstream infile(path);
    if (!infile.good()) {
        throw Slic3r::RuntimeError(std::string("Load step file failed.\nCannot open file for reading.\n"));
        return false;
    }

    std::string temp_line;
    while (std::getline(infile, temp_line)) {
        if (!isUtf8(temp_line)) {
            infile.close();
            return false;
        }
    }

    infile.close();
    return true;
}

bool StepPreProcessor::isUtf8(const std::string str)
{
    size_t num = 0;
    int i = 0;
    while (i < str.length()) {
        if ((str[i] & 0x80) == 0x00) {
            i++;
        } else if ((num = preNum(str[i])) > 2) {
            i++;
            for (int j = 0; j < num - 1; j++) {
                if ((str[i] & 0xc0) != 0x80)
                    return false;
                i++;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool StepPreProcessor::isGBK(const std::string str) {
    size_t i = 0;
    while (i < str.length()) {
        if (str[i] <= 0x7f) {
            i++;
            continue;
        } else {
            if (str[i] >= 0x81 &&
                str[i] <= 0xfe &&
                str[i + 1] >= 0x40 &&
                str[i + 1] <= 0xfe &&
                str[i + 1] != 0xf7) {
                i += 2;
                continue;
            }
            else {
                return false;
            }
        }
    }
    return true;
}

int StepPreProcessor::preNum(const unsigned char byte) {
    unsigned char mask = 0x80;
    int num = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte & mask) == mask) {
            mask = mask >> 1;
            num++;
        } else {
            break;
        }
    }
    return num;
}

struct NamedSolid {
    NamedSolid(const TopoDS_Shape& s,
               const std::string& n) : solid{s}, name{n} {}
    const TopoDS_Shape solid;
    const std::string  name;
};

static void getNamedSolids(const TopLoc_Location& location, const std::string& prefix,
                           unsigned int& id, const Handle(XCAFDoc_ShapeTool) shapeTool,
                           const TDF_Label label, std::vector<NamedSolid>& namedSolids) {
    TDF_Label referredLabel{label};
    if (shapeTool->IsReference(label))
        shapeTool->GetReferredShape(label, referredLabel);

    std::string name;
    Handle(TDataStd_Name) shapeName;
    if (referredLabel.FindAttribute(TDataStd_Name::GetID(), shapeName))
        name = TCollection_AsciiString(shapeName->Get()).ToCString();

    if (name == "")
        name = std::to_string(id++);
    std::string fullName{name};

    TopLoc_Location localLocation = location * shapeTool->GetLocation(label);
    TDF_LabelSequence components;
    if (shapeTool->GetComponents(referredLabel, components)) {
        for (Standard_Integer compIndex = 1; compIndex <= components.Length(); ++compIndex) {
            getNamedSolids(localLocation, fullName, id, shapeTool, components.Value(compIndex), namedSolids);
        }
    } else {
        TopoDS_Shape shape;
        shapeTool->GetShape(referredLabel, shape);
        TopAbs_ShapeEnum shape_type = shape.ShapeType();
        BRepBuilderAPI_Transform transform(shape, localLocation, Standard_True);
        switch (shape_type) {
        case TopAbs_COMPOUND:
            namedSolids.emplace_back(TopoDS::Compound(transform.Shape()), fullName);
            break;
        case TopAbs_COMPSOLID:
            namedSolids.emplace_back(TopoDS::CompSolid(transform.Shape()), fullName);
            break;
        case TopAbs_SOLID:
            namedSolids.emplace_back(TopoDS::Solid(transform.Shape()), fullName);
            break;
        default:
            break;
        }
    }
}

bool load_step(const char *path, Model *model, bool& is_cancel, ImportStepProgressFn stepFn, StepIsUtf8Fn isUtf8Fn)
{
    bool cb_cancel = false;
    if (stepFn) {
        stepFn(LOAD_STEP_STAGE_READ_FILE, 0, 1, cb_cancel);
        is_cancel = cb_cancel;
        if (cb_cancel) {
            return false;
        }
    }

    if (!StepPreProcessor::isUtf8File(path) && isUtf8Fn)
        isUtf8Fn(false);
    std::string file_after_preprocess = std::string(path);

    std::vector<NamedSolid> namedSolids;
    Handle(TDocStd_Document) document;
    Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    application->NewDocument(file_after_preprocess.c_str(), document);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    //BBS: Todo, read file is slow which cause the progress_bar no update and gui no response
    IFSelect_ReturnStatus stat = reader.ReadFile(file_after_preprocess.c_str());
    if (stat != IFSelect_RetDone || !reader.Transfer(document)) {
        application->Close(document);
        throw std::logic_error{ std::string{"Could not read '"} + path + "'" };
        return false;
    }
    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence topLevelShapes;
    shapeTool->GetFreeShapes(topLevelShapes);

    unsigned int id{1};
    Standard_Integer topShapeLength = topLevelShapes.Length() + 1;
    auto stage_unit2 = topShapeLength / LOAD_STEP_STAGE_UNIT_NUM + 1;

    for (Standard_Integer iLabel = 1; iLabel < topShapeLength; ++iLabel) {
        if (stepFn) {
            if ((iLabel % stage_unit2) == 0) {
                stepFn(LOAD_STEP_STAGE_GET_SOLID, iLabel, topShapeLength, cb_cancel);
                is_cancel = cb_cancel;
            }
            if (cb_cancel) {
                shapeTool.reset(nullptr);
                application->Close(document);
                return false;
            }
        }
        getNamedSolids(TopLoc_Location{}, "", id, shapeTool, topLevelShapes.Value(iLabel), namedSolids);
    }

    std::vector<stl_file> stl;
    stl.resize(namedSolids.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, namedSolids.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t i = range.begin(); i < range.end(); i++) {
            BRepMesh_IncrementalMesh mesh(namedSolids[i].solid, STEP_TRANS_CHORD_ERROR, false, STEP_TRANS_ANGLE_RES, true);
            // BBS: calculate total number of the nodes and triangles
            int aNbNodes     = 0;
            int aNbTriangles = 0;
            for (TopExp_Explorer anExpSF(namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
                TopLoc_Location aLoc;
                Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(anExpSF.Current()), aLoc);
                if (!aTriangulation.IsNull()) {
                    aNbNodes += aTriangulation->NbNodes();
                    aNbTriangles += aTriangulation->NbTriangles();
                }
            }

            if (aNbTriangles == 0 || aNbNodes == 0)
                // BBS: No triangulation on the shape.
                continue;

            stl[i].stats.type                = inmemory;
            stl[i].stats.number_of_facets    = (uint32_t) aNbTriangles;
            stl[i].stats.original_num_facets = stl[i].stats.number_of_facets;
            stl_allocate(&stl[i]);

            std::vector<Vec3f> points;
            points.reserve(aNbNodes);
            // BBS: count faces missing triangulation
            Standard_Integer aNbFacesNoTri = 0;
            // BBS: fill temporary triangulation
            Standard_Integer aNodeOffset    = 0;
            Standard_Integer aTriangleOffet = 0;
            for (TopExp_Explorer anExpSF(namedSolids[i].solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
                const TopoDS_Shape &aFace = anExpSF.Current();
                TopLoc_Location     aLoc;
                Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);
                if (aTriangulation.IsNull()) {
                    ++aNbFacesNoTri;
                    continue;
                }
                // BBS: copy nodes
                gp_Trsf aTrsf = aLoc.Transformation();
                for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
                    gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
                    aPnt.Transform(aTrsf);
                    points.emplace_back(std::move(Vec3f(aPnt.X(), aPnt.Y(), aPnt.Z())));
                }
                // BBS: copy triangles
                const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
                Standard_Integer anId[3];
                for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
                    Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);

                    aTri.Get(anId[0], anId[1], anId[2]);
                    if (anOrientation == TopAbs_REVERSED)
                        std::swap(anId[1], anId[2]);
                    // BBS: save triangles facets
                    stl_facet facet;
                    facet.vertex[0] = points[anId[0] + aNodeOffset - 1].cast<float>();
                    facet.vertex[1] = points[anId[1] + aNodeOffset - 1].cast<float>();
                    facet.vertex[2] = points[anId[2] + aNodeOffset - 1].cast<float>();
                    facet.extra[0]  = 0;
                    facet.extra[1]  = 0;
                    stl_normal normal;
                    stl_calculate_normal(normal, &facet);
                    stl_normalize_vector(normal);
                    facet.normal                                      = normal;
                    stl[i].facet_start[aTriangleOffet + aTriIter - 1] = facet;
                }

                aNodeOffset += aTriangulation->NbNodes();
                aTriangleOffet += aTriangulation->NbTriangles();
            }
        }
    });

    ModelObject *new_object = model->add_object();
    const char * last_slash = strrchr(path, DIR_SEPARATOR);
    new_object->name.assign((last_slash == nullptr) ? path : last_slash + 1);
    new_object->input_file = path;

    auto stage_unit3 = stl.size() / LOAD_STEP_STAGE_UNIT_NUM + 1;
    for (size_t i = 0; i < stl.size(); i++) {
        if (stepFn) {
            if ((i % stage_unit3) == 0) {
                stepFn(LOAD_STEP_STAGE_GET_MESH, i, stl.size(), cb_cancel);
                is_cancel = cb_cancel;
            }
            if (cb_cancel) {
                model->delete_object(new_object);
                shapeTool.reset(nullptr);
                application->Close(document);
                return false;
            }
        }

        //BBS: maybe mesh is empty from step file. Don't add
        if (stl[i].stats.number_of_facets > 0) {
            TriangleMesh triangle_mesh;
            triangle_mesh.from_stl(stl[i]);
            ModelVolume* new_volume = new_object->add_volume(std::move(triangle_mesh));
            new_volume->name = namedSolids[i].name;
            new_volume->source.input_file = path;
            new_volume->source.object_idx = (int)model->objects.size() - 1;
            new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
        }
    }

    shapeTool.reset(nullptr);
    application->Close(document);

    //BBS: no valid shape from the step, delete the new object as well
    if (new_object->volumes.size() == 0) {
        model->delete_object(new_object);
        return false;
    }

    return true;
}

}; // namespace Slic3r
