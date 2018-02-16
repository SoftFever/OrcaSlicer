#include "../libslic3r.h"
#include "../Model.hpp"
#include "../Utils.hpp"
#include "../GCode.hpp"
#include "../slic3r/GUI/PresetBundle.hpp"

#include "3mf.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>

#include <expat.h>
#include <Eigen/Dense>
#include <miniz/miniz_zip.h>

const std::string MODEL_FOLDER = "3D/";
const std::string MODEL_EXTENSION = ".model";
const std::string MODEL_FILE = "3D/3dmodel.model"; // << this is the only format of the string which works with CURA
const std::string CONTENT_TYPES_FILE = "[Content_Types].xml";
const std::string RELATIONSHIPS_FILE = "_rels/.rels";
const std::string CONFIG_FILE = "Metadata/Slic3r_PE.config";

const char* MODEL_TAG = "model";
const char* RESOURCES_TAG = "resources";
const char* OBJECT_TAG = "object";
const char* MESH_TAG = "mesh";
const char* VERTICES_TAG = "vertices";
const char* VERTEX_TAG = "vertex";
const char* TRIANGLES_TAG = "triangles";
const char* TRIANGLE_TAG = "triangle";
const char* COMPONENTS_TAG = "components";
const char* COMPONENT_TAG = "component";
const char* BUILD_TAG = "build";
const char* ITEM_TAG = "item";

const char* UNIT_ATTR = "unit";
const char* NAME_ATTR = "name";
const char* TYPE_ATTR = "type";
const char* ID_ATTR = "id";
const char* X_ATTR = "x";
const char* Y_ATTR = "y";
const char* Z_ATTR = "z";
const char* V1_ATTR = "v1";
const char* V2_ATTR = "v2";
const char* V3_ATTR = "v3";
const char* OBJECTID_ATTR = "objectid";
const char* TRANSFORM_ATTR = "transform";

const unsigned int VALID_OBJECT_TYPES_COUNT = 1;
const char* VALID_OBJECT_TYPES[] =
{
    "model"
};

const unsigned int INVALID_OBJECT_TYPES_COUNT = 4;
const char* INVALID_OBJECT_TYPES[] =
{
    "solidsupport",
    "support",
    "surface",
    "other"
};

typedef Eigen::Matrix<float, 4, 4, Eigen::RowMajor> Matrix4x4;

const char* get_attribute_value_charptr(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    if ((attributes == nullptr) || (attributes_size == 0) || (attributes_size % 2 != 0) || (attribute_key == nullptr))
        return nullptr;

    for (unsigned int a = 0; a < attributes_size; a += 2)
    {
        if (::strcmp(attributes[a], attribute_key) == 0)
            return attributes[a + 1];
    }

    return nullptr;
}

std::string get_attribute_value_string(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? text : "";
}

float get_attribute_value_float(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? (float)::atof(text) : 0.0f;
}

int get_attribute_value_int(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? ::atoi(text) : 0;
}

Matrix4x4 get_matrix_from_string(const std::string& mat_str)
{
    if (mat_str.empty())
        // empty string means default identity matrix
        return Matrix4x4::Identity();

    std::vector<std::string> mat_elements_str;
    boost::split(mat_elements_str, mat_str, boost::is_any_of(" "), boost::token_compress_on);

    unsigned int size = (unsigned int)mat_elements_str.size();
    if (size != 12)
        // invalid data, return identity matrix
        return Matrix4x4::Identity();

    Matrix4x4 ret = Matrix4x4::Identity();
    unsigned int i = 0;
    // matrices are stored into 3mf files as 4x3
    // we need to transpose them
    for (unsigned int c = 0; c < 4; ++c)
    {
        for (unsigned int r = 0; r < 3; ++r)
        {
            ret(r, c) = (float)::atof(mat_elements_str[i++].c_str());
        }
    }
    return ret;
}

float get_unit_factor(const std::string& unit)
{
    const char* text = unit.c_str();

    if (::strcmp(text, "micron") == 0)
        return 0.001f;
    else if (::strcmp(text, "centimeter") == 0)
        return 10.0f;
    else if (::strcmp(text, "inch") == 0)
        return 25.4f;
    else if (::strcmp(text, "foot") == 0)
        return 304.8f;
    else if (::strcmp(text, "meter") == 0)
        return 1000.0f;
    else
        // default "millimeters" (see specification)
        return 1.0f;
}

bool is_valid_object_type(const std::string& type)
{
    // if the type is empty defaults to "model" (see specification)
    if (type.empty())
        return true;

    for (unsigned int i = 0; i < VALID_OBJECT_TYPES_COUNT; ++i)
    {
        if (::strcmp(type.c_str(), VALID_OBJECT_TYPES[i]) == 0)
            return true;
    }

    return false;
}

namespace Slic3r {

    // Base class with error messages management
    class _3MF_Base
    {
        std::vector<std::string> m_errors;

    protected:
        void add_error(const std::string& error) { m_errors.push_back(error); }
        void clear_errors() { m_errors.clear(); }

    public:
        void log_errors()
        {
            for (const std::string& error : m_errors)
            {
                printf("%s\n", error.c_str());
            }
        }
    };

    class _3MF_Importer : public _3MF_Base
    {
        struct Component
        {
            int object_id;
            Matrix4x4 matrix;

            explicit Component(int object_id);
            Component(int object_id, const Matrix4x4& matrix);
        };

        typedef std::vector<Component> ComponentsList;

        struct CurrentObject
        {
            struct Geometry
            {
                std::vector<float> vertices;
                std::vector<unsigned int> triangles;

                bool empty();
                void reset();
            };

            int id;
            Geometry geometry;
            ModelObject* object;
            TriangleMesh mesh;
            ComponentsList components;

            CurrentObject();

            void reset();
        };

        struct Instance
        {
            ModelInstance* instance;
            Matrix4x4 matrix;

            Instance(ModelInstance* instance, const Matrix4x4& matrix);
        };

        typedef std::map<int, ModelObject*> IdToModelObjectMap;
        typedef std::map<int, ComponentsList> IdToAliasesMap;
        typedef std::vector<Instance> InstancesList;

        XML_Parser m_xml_parser;
        Model* m_model;
        float m_unit_factor;
        CurrentObject m_curr_object;
        IdToModelObjectMap m_objects;
        IdToAliasesMap m_objects_aliases;
        InstancesList m_instances;

    public:
        _3MF_Importer();
        ~_3MF_Importer();

        bool load_model_from_file(const std::string& filename, Model& model, PresetBundle& bundle);

    private:
        void _destroy_xml_parser();
        void _stop_xml_parser();

        bool _load_model_from_file(const std::string& filename, Model& model, PresetBundle& bundle);
        bool _extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        bool _extract_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, PresetBundle& bundle, const std::string& archive_filename);

        void _handle_start_xml_element(const char* name, const char** attributes);
        void _handle_end_xml_element(const char* name);

        bool _handle_start_model(const char** attributes, unsigned int num_attributes);
        bool _handle_end_model();

        bool _handle_start_resources(const char** attributes, unsigned int num_attributes);
        bool _handle_end_resources();

        bool _handle_start_object(const char** attributes, unsigned int num_attributes);
        bool _handle_end_object();

        bool _handle_start_mesh(const char** attributes, unsigned int num_attributes);
        bool _handle_end_mesh();

        bool _handle_start_vertices(const char** attributes, unsigned int num_attributes);
        bool _handle_end_vertices();

        bool _handle_start_vertex(const char** attributes, unsigned int num_attributes);
        bool _handle_end_vertex();

        bool _handle_start_triangles(const char** attributes, unsigned int num_attributes);
        bool _handle_end_triangles();

        bool _handle_start_triangle(const char** attributes, unsigned int num_attributes);
        bool _handle_end_triangle();

        bool _handle_start_components(const char** attributes, unsigned int num_attributes);
        bool _handle_end_components();

        bool _handle_start_component(const char** attributes, unsigned int num_attributes);
        bool _handle_end_component();

        bool _handle_start_build(const char** attributes, unsigned int num_attributes);
        bool _handle_end_build();

        bool _handle_start_item(const char** attributes, unsigned int num_attributes);
        bool _handle_end_item();

        bool _create_object_instance(int object_id, const Matrix4x4& matrix, unsigned int recur_counter);

        void _apply_transform(ModelObject& object, const Matrix4x4& matrix);
        void _apply_transform(ModelInstance& instance, const Matrix4x4& matrix);

        static void XMLCALL _handle_start_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_xml_element(void* userData, const char* name);
    };

    _3MF_Importer::Component::Component(int object_id)
        : object_id(object_id)
        , matrix(Matrix4x4::Identity())
    {
    }

    _3MF_Importer::Component::Component(int object_id, const Matrix4x4& matrix)
        : object_id(object_id)
        , matrix(matrix)
    {
    }

    bool _3MF_Importer::CurrentObject::Geometry::empty()
    {
        return vertices.empty() || triangles.empty();
    }

    void _3MF_Importer::CurrentObject::Geometry::reset()
    {
        vertices.clear();
        triangles.clear();
    }

    _3MF_Importer::CurrentObject::CurrentObject()
    {
        reset();
    }

    void _3MF_Importer::CurrentObject::reset()
    {
        id = -1;
        geometry.reset();
        object = nullptr;
        mesh = TriangleMesh();
        components.clear();
    }

    _3MF_Importer::Instance::Instance(ModelInstance* instance, const Matrix4x4& matrix)
        : instance(instance)
        , matrix(matrix)
    {
    }

    _3MF_Importer::_3MF_Importer()
        : m_xml_parser(nullptr)
        , m_model(nullptr)   
        , m_unit_factor(1.0f)
    {
    }

    _3MF_Importer::~_3MF_Importer()
    {
        _destroy_xml_parser();
    }

    bool _3MF_Importer::load_model_from_file(const std::string& filename, Model& model, PresetBundle& bundle)
    {
        m_model = &model;
        m_unit_factor = 1.0f;
        m_curr_object.reset();
        m_objects.clear();
        m_objects_aliases.clear();
        m_instances.clear();
        clear_errors();

        return _load_model_from_file(filename, model, bundle);
    }

    void _3MF_Importer::_destroy_xml_parser()
    {
        if (m_xml_parser != nullptr)
        {
            XML_ParserFree(m_xml_parser);
            m_xml_parser = nullptr;
        }
    }

    void _3MF_Importer::_stop_xml_parser()
    {
        if (m_xml_parser != nullptr)
            XML_StopParser(m_xml_parser, false);
    }

    bool _3MF_Importer::_load_model_from_file(const std::string& filename, Model& model, PresetBundle& bundle)
    {
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);
       
        mz_bool res = mz_zip_reader_init_file(&archive, filename.c_str(), 0);
        if (res == 0)
        {
            add_error("Unable to open the file");
            return false;
        }

        mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

        mz_zip_archive_file_stat stat;
        for (mz_uint i = 0; i < num_entries; ++i)
        {
            if (mz_zip_reader_file_stat(&archive, i, &stat))
            {
                std::string name(stat.m_filename);
                std::replace(name.begin(), name.end(), '\\', '/');

                if (boost::algorithm::istarts_with(name, MODEL_FOLDER) && boost::algorithm::iends_with(name, MODEL_EXTENSION))
                {
                    // valid model name -> extract model
                    if (!_extract_model_from_archive(archive, stat))
                    {
                        mz_zip_reader_end(&archive);
                        add_error("Archive does not contain a valid model");
                        return false;
                    }
                }
                else if (boost::algorithm::iequals(name, CONFIG_FILE))
                {
                    // extract slic3r config file
                    if (!_extract_config_from_archive(archive, stat, bundle, filename))
                    {
                        mz_zip_reader_end(&archive);
                        add_error("Archive does not contain a valid config");
                        return false;
                    }
                }
            }
        }

        mz_zip_reader_end(&archive);
        return true;
    }

    bool _3MF_Importer::_extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size == 0)
        {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr)
        {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _3MF_Importer::_handle_start_xml_element, _3MF_Importer::_handle_end_xml_element);

        void* parser_buffer = XML_GetBuffer(m_xml_parser, (int)stat.m_uncomp_size);
        if (parser_buffer == nullptr)
        {
            add_error("Unable to create buffer");
            return false;
        }

        mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, parser_buffer, (size_t)stat.m_uncomp_size, 0);
        if (res == 0)
        {
            add_error("Error while reading model data to buffer");
            return false;
        }

        if (!XML_ParseBuffer(m_xml_parser, (int)stat.m_uncomp_size, 1))
        {
            char error_buf[1024];
            ::sprintf(error_buf, "Error (%s) while parsing xml file at line %d", XML_ErrorString(XML_GetErrorCode(m_xml_parser)), XML_GetCurrentLineNumber(m_xml_parser));
            add_error(error_buf);
            return false;
        }

        return true;
    }

    bool _3MF_Importer::_extract_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, PresetBundle& bundle, const std::string& archive_filename)
    {
        if (stat.m_uncomp_size > 0)
        {
            std::vector<char> buffer((size_t)stat.m_uncomp_size + 1, 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0)
            {
                add_error("Error while reading config data to buffer");
                return false;
            }

            buffer.back() = '\0';
            bundle.load_config_string(buffer.data(), archive_filename.c_str());
        }

        return true;
    }

    void _3MF_Importer::_handle_start_xml_element(const char* name, const char** attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;
        unsigned int num_attributes = (unsigned int)XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(MODEL_TAG, name) == 0)
            res = _handle_start_model(attributes, num_attributes);
        else if (::strcmp(RESOURCES_TAG, name) == 0)
            res = _handle_start_resources(attributes, num_attributes);
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_start_object(attributes, num_attributes);
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_start_mesh(attributes, num_attributes);
        else if (::strcmp(VERTICES_TAG, name) == 0)
            res = _handle_start_vertices(attributes, num_attributes);
        else if (::strcmp(VERTEX_TAG, name) == 0)
            res = _handle_start_vertex(attributes, num_attributes);
        else if (::strcmp(TRIANGLES_TAG, name) == 0)
            res = _handle_start_triangles(attributes, num_attributes);
        else if (::strcmp(TRIANGLE_TAG, name) == 0)
            res = _handle_start_triangle(attributes, num_attributes);
        else if (::strcmp(COMPONENTS_TAG, name) == 0)
            res = _handle_start_components(attributes, num_attributes);
        else if (::strcmp(COMPONENT_TAG, name) == 0)
            res = _handle_start_component(attributes, num_attributes);
        else if (::strcmp(BUILD_TAG, name) == 0)
            res = _handle_start_build(attributes, num_attributes);
        else if (::strcmp(ITEM_TAG, name) == 0)
            res = _handle_start_item(attributes, num_attributes);

        if (!res)
            _stop_xml_parser();
    }

    void _3MF_Importer::_handle_end_xml_element(const char* name)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;

        if (::strcmp(MODEL_TAG, name) == 0)
            res = _handle_end_model();
        else if (::strcmp(RESOURCES_TAG, name) == 0)
            res = _handle_end_resources();
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_end_object();
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_end_mesh();
        else if (::strcmp(VERTICES_TAG, name) == 0)
            res = _handle_end_vertices();
        else if (::strcmp(VERTEX_TAG, name) == 0)
            res = _handle_end_vertex();
        else if (::strcmp(TRIANGLES_TAG, name) == 0)
            res = _handle_end_triangles();
        else if (::strcmp(TRIANGLE_TAG, name) == 0)
            res = _handle_end_triangle();
        else if (::strcmp(COMPONENTS_TAG, name) == 0)
            res = _handle_end_components();
        else if (::strcmp(COMPONENT_TAG, name) == 0)
            res = _handle_end_component();
        else if (::strcmp(BUILD_TAG, name) == 0)
            res = _handle_end_build();
        else if (::strcmp(ITEM_TAG, name) == 0)
            res = _handle_end_item();

        if (!res)
            _stop_xml_parser();
    }

    bool _3MF_Importer::_handle_start_model(const char** attributes, unsigned int num_attributes)
    {
        m_unit_factor = get_unit_factor(get_attribute_value_string(attributes, num_attributes, UNIT_ATTR));
        return true;
    }

    bool _3MF_Importer::_handle_end_model()
    {
        // deletes all non-built or non-instanced objects
        for (const IdToModelObjectMap::value_type& object : m_objects)
        {
            if ((object.second != nullptr) && (object.second->instances.size() == 0))
                m_model->delete_object(object.second);
        }

        // applies instances' matrices
        for (Instance& instance : m_instances)
        {
            if (instance.instance != nullptr)
            {
                ModelObject* object = instance.instance->get_object();
                if (object != nullptr)
                {
                    if (object->instances.size() == 1)
                    {
                        // single instance -> apply the matrix to object geometry
                        _apply_transform(*object, instance.matrix);
                    }
                    else
                    {
                        // multiple instances -> apply the matrix to the instance
                        _apply_transform(*instance.instance, instance.matrix);
                    }
                }
            }
        }

        return true;
    }

    bool _3MF_Importer::_handle_start_resources(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_resources()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_object(const char** attributes, unsigned int num_attributes)
    {
        // reset current data
        m_curr_object.reset();

        if (is_valid_object_type(get_attribute_value_string(attributes, num_attributes, TYPE_ATTR)))
        {
            // create new object (it may be removed later if no instances are generated from it)
            m_curr_object.object = m_model->add_object();
            if (m_curr_object.object == nullptr)
            {
                add_error("Unable to create object");
                return false;
            }

            // set object data
            m_curr_object.object->name = get_attribute_value_string(attributes, num_attributes, NAME_ATTR);
            m_curr_object.id = get_attribute_value_int(attributes, num_attributes, ID_ATTR);
        }

        return true;
    }

    bool _3MF_Importer::_handle_end_object()
    {
        if (m_curr_object.object != nullptr)
        {
            if (m_curr_object.geometry.empty())
            {
                // no geometry defined
                // remove the object from the model
                m_model->delete_object(m_curr_object.object);

                if (m_curr_object.components.empty())
                {
                    // no components defined -> invalid object, delete it
                    IdToModelObjectMap::iterator object_item = m_objects.find(m_curr_object.id);
                    if (object_item != m_objects.end())
                        m_objects.erase(object_item);

                    IdToAliasesMap::iterator alias_item = m_objects_aliases.find(m_curr_object.id);
                    if (alias_item != m_objects_aliases.end())
                        m_objects_aliases.erase(alias_item);
                }
                else
                    // adds components to aliases
                    m_objects_aliases.insert(IdToAliasesMap::value_type(m_curr_object.id, m_curr_object.components));
            }
            else
            {
                // geometry defined, add it to the object

                ModelVolume* volume = m_curr_object.object->add_volume(m_curr_object.mesh);
                if (volume == nullptr)
                {
                    add_error("Unable to add volume");
                    return false;
                }

                stl_file& stl = volume->mesh.stl;
                stl.stats.type = inmemory;
                stl.stats.number_of_facets = (uint32_t)m_curr_object.geometry.triangles.size() / 3;
                stl.stats.original_num_facets = (int)stl.stats.number_of_facets;
                stl_allocate(&stl);
                for (size_t i = 0; i < m_curr_object.geometry.triangles.size(); /*nothing*/)
                {
                    stl_facet& facet = stl.facet_start[i / 3];
                    for (unsigned int v = 0; v < 3; ++v)
                    {
                        ::memcpy((void*)&facet.vertex[v].x, (const void*)&m_curr_object.geometry.vertices[m_curr_object.geometry.triangles[i++] * 3], 3 * sizeof(float));
                    }
                }
                stl_get_size(&stl);
                volume->mesh.repair();

                // stores the object for later use
                if (m_objects.find(m_curr_object.id) == m_objects.end())
                {
                    m_objects.insert(IdToModelObjectMap::value_type(m_curr_object.id, m_curr_object.object));
                    m_objects_aliases.insert(IdToAliasesMap::value_type(m_curr_object.id, ComponentsList(1, Component(m_curr_object.id)))); // aliases itself
                }
                else
                {
                    add_error("Found object with duplicate id");
                    return false;
                }
            }
        }

        return true;
    }

    bool _3MF_Importer::_handle_start_mesh(const char** attributes, unsigned int num_attributes)
    {
        // reset current geometry
        m_curr_object.geometry.reset();
        return true;
    }

    bool _3MF_Importer::_handle_end_mesh()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_vertices(const char** attributes, unsigned int num_attributes)
    {
        // reset current vertices
        m_curr_object.geometry.vertices.clear();
        return true;
    }

    bool _3MF_Importer::_handle_end_vertices()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_vertex(const char** attributes, unsigned int num_attributes)
    {
        // appends the vertex coordinates
        // missing values are set equal to ZERO
        m_curr_object.geometry.vertices.push_back(m_unit_factor * get_attribute_value_float(attributes, num_attributes, X_ATTR));
        m_curr_object.geometry.vertices.push_back(m_unit_factor * get_attribute_value_float(attributes, num_attributes, Y_ATTR));
        m_curr_object.geometry.vertices.push_back(m_unit_factor * get_attribute_value_float(attributes, num_attributes, Z_ATTR));
        return true;
    }

    bool _3MF_Importer::_handle_end_vertex()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_triangles(const char** attributes, unsigned int num_attributes)
    {
        // reset current triangles
        m_curr_object.geometry.triangles.clear();
        return true;
    }

    bool _3MF_Importer::_handle_end_triangles()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_triangle(const char** attributes, unsigned int num_attributes)
    {
        // we are ignoring the following attributes:
        // p1
        // p2
        // p3
        // pid
        // see specifications

        // appends the triangle's vertices indices
        // missing values are set equal to ZERO
        m_curr_object.geometry.triangles.push_back((unsigned int)get_attribute_value_int(attributes, num_attributes, V1_ATTR));
        m_curr_object.geometry.triangles.push_back((unsigned int)get_attribute_value_int(attributes, num_attributes, V2_ATTR));
        m_curr_object.geometry.triangles.push_back((unsigned int)get_attribute_value_int(attributes, num_attributes, V3_ATTR));
        return true;
    }

    bool _3MF_Importer::_handle_end_triangle()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_components(const char** attributes, unsigned int num_attributes)
    {
        // reset current components
        m_curr_object.components.clear();
        return true;
    }

    bool _3MF_Importer::_handle_end_components()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_component(const char** attributes, unsigned int num_attributes)
    {
        int object_id = get_attribute_value_int(attributes, num_attributes, OBJECTID_ATTR);
        Matrix4x4 matrix = get_matrix_from_string(get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));

        IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
        if (object_item == m_objects.end())
        {
            IdToAliasesMap::iterator alias_item = m_objects_aliases.find(object_id);
            if (alias_item == m_objects_aliases.end())
            {
                add_error("Found component with invalid object id");
                return false;
            }
        }

        m_curr_object.components.emplace_back(object_id, matrix);

        return true;
    }

    bool _3MF_Importer::_handle_end_component()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_build(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_build()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_item(const char** attributes, unsigned int num_attributes)
    {
        // we are ignoring the following attributes
        // thumbnail
        // partnumber
        // pid
        // pindex
        // see specifications

        int object_id = get_attribute_value_int(attributes, num_attributes, OBJECTID_ATTR);
        Matrix4x4 matrix = get_matrix_from_string(get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));

        return _create_object_instance(object_id, matrix, 1);
    }

    bool _3MF_Importer::_handle_end_item()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_create_object_instance(int object_id, const Matrix4x4& matrix, unsigned int recur_counter)
    {
        static const unsigned int MAX_RECURSIONS = 10;

        // escape from circular aliasing
        if (recur_counter > MAX_RECURSIONS)
        {
            add_error("Too many recursions");
            return false;
        }

        IdToAliasesMap::iterator it = m_objects_aliases.find(object_id);
        if (it == m_objects_aliases.end())
        {
            add_error("Found item with invalid object id");
            return false;
        }

        if ((it->second.size() == 1) && (it->second[0].object_id == object_id))
        {
            // aliasing to itself

            IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
            if ((object_item == m_objects.end()) || (object_item->second == nullptr))
            {
                add_error("Found invalid object");
                return false;
            }
            else
            {
                ModelInstance* instance = object_item->second->add_instance();
                if (instance == nullptr)
                {
                    add_error("Unable to add object instance");
                    return false;
                }

                m_instances.emplace_back(instance, matrix);
            }
        }
        else
        {
            // recursively process nested components
            for (const Component& component : it->second)
            {
                if (!_create_object_instance(component.object_id, matrix * component.matrix, recur_counter + 1))
                    return false;
            }
        }

        return true;
    }

    void _3MF_Importer::_apply_transform(ModelObject& object, const Matrix4x4& matrix)
    {
        float matrix3x4[12] = { matrix(0, 0), matrix(0, 1), matrix(0, 2), matrix(0, 3),
                                matrix(1, 0), matrix(1, 1), matrix(1, 2), matrix(1, 3),
                                matrix(2, 0), matrix(2, 1), matrix(2, 2), matrix(2, 3) };

        object.transform(matrix3x4);
    }

    void _3MF_Importer::_apply_transform(ModelInstance& instance, const Matrix4x4& matrix)
    {
        // slic3r ModelInstance cannot be transformed using a matrix
        // we extract from the given matrix only the values currently used

        // translation
        double offset_x = (double)matrix(0, 3);
        double offset_y = (double)matrix(1, 3);
        double offset_z = (double)matrix(2, 3);

        // scale
        double sx = ::sqrt(sqr((double)matrix(0, 0)) + sqr((double)matrix(1, 0)) + sqr((double)matrix(2, 0)));
        double sy = ::sqrt(sqr((double)matrix(0, 1)) + sqr((double)matrix(1, 1)) + sqr((double)matrix(2, 1)));
        double sz = ::sqrt(sqr((double)matrix(0, 2)) + sqr((double)matrix(1, 2)) + sqr((double)matrix(2, 2)));

        // invalid scale value, return
        if ((sx == 0.0) || (sy == 0.0) || (sz == 0.0))
            return;

        // non-uniform scale value, return
        if ((std::abs(sx - sy) > 0.00001) || (std::abs(sx - sz) > 0.00001))
            return;

        // rotations (extracted using quaternion)
        double inv_sx = 1.0 / sx;
        double inv_sy = 1.0 / sy;
        double inv_sz = 1.0 / sz;
        
        Eigen::Matrix<double, 3, 3, Eigen::RowMajor> m3x3;
        m3x3 << (double)matrix(0, 0) * inv_sx, (double)matrix(0, 1) * inv_sy, (double)matrix(0, 2) * inv_sz,
                (double)matrix(1, 0) * inv_sx, (double)matrix(1, 1) * inv_sy, (double)matrix(1, 2) * inv_sz,
                (double)matrix(2, 0) * inv_sx, (double)matrix(2, 1) * inv_sy, (double)matrix(2, 2) * inv_sz;

        double qw = 0.5 * ::sqrt(std::max(0.0, 1.0 + m3x3(0, 0) + m3x3(1, 1) + m3x3(2, 2)));
        double qx = 0.5 * ::sqrt(std::max(0.0, 1.0 + m3x3(0, 0) - m3x3(1, 1) - m3x3(2, 2)));
        double qy = 0.5 * ::sqrt(std::max(0.0, 1.0 - m3x3(0, 0) + m3x3(1, 1) - m3x3(2, 2)));
        double qz = 0.5 * ::sqrt(std::max(0.0, 1.0 - m3x3(0, 0) - m3x3(1, 1) + m3x3(2, 2)));

        double q_magnitude = ::sqrt(sqr(qw) + sqr(qx) + sqr(qy) + sqr(qz));

        // invalid length, return
        if (q_magnitude == 0.0)
            return;

        double inv_q_magnitude = 1.0 / q_magnitude;

        qw *= inv_q_magnitude;
        qx *= inv_q_magnitude;
        qy *= inv_q_magnitude;
        qz *= inv_q_magnitude;

        double test = qx * qy + qz * qw;
        double angle_x, angle_y, angle_z;

        if (test > 0.499)
        {
            // singularity at north pole
            angle_x = 0.0;
            angle_y = 2.0 * ::atan2(qx, qw);
            angle_z = 0.5 * PI;
        }
        else if (test < -0.499)
        {
            // singularity at south pole
            angle_x = 0.0;
            angle_y = -2.0 * ::atan2(qx, qw);
            angle_z = -0.5 * PI;
        }
        else
        {
            angle_x = ::atan2(2.0 * qx * qw - 2.0 * qy * qz, 1.0 - 2.0 * sqr(qx) - 2.0 * sqr(qz));
            angle_y = ::atan2(2.0 * qy * qw - 2.0 * qx * qz, 1.0 - 2.0 * sqr(qy) - 2.0 * sqr(qz));
            angle_z = ::asin(2.0 * qx * qy + 2.0 * qz * qw);

            if (angle_x < 0.0)
                angle_x += 2.0 * PI;

            if (angle_y < 0.0)
                angle_y += 2.0 * PI;

            if (angle_z < 0.0)
                angle_z += 2.0 * PI;
        }

        instance.offset.x = offset_x;
        instance.offset.y = offset_y;
        instance.scaling_factor = sx;
        instance.rotation = angle_z;
    }

    void XMLCALL _3MF_Importer::_handle_start_xml_element(void* userData, const char* name, const char** attributes)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_xml_element(name, attributes);
    }

    void XMLCALL _3MF_Importer::_handle_end_xml_element(void* userData, const char* name)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_xml_element(name);
    }

    class _3MF_Exporter : public _3MF_Base
    {
        struct BuildItem
        {
            unsigned int id;
            Matrix4x4 matrix;

            BuildItem(unsigned int id, const Matrix4x4& matrix);
        };

        typedef std::vector<BuildItem> BuildItemsList;

    public:
        bool save_model_to_file(const std::string& filename, Model& model, const Print& print);

    private:
        bool _save_model_to_file(const std::string& filename, Model& model, const Print& print);
        bool _add_content_types_file_to_archive(mz_zip_archive& archive);
        bool _add_relationships_file_to_archive(mz_zip_archive& archive);
        bool _add_model_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_object_to_model_stream(std::stringstream& stream, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items);
        bool _add_mesh_to_object_stream(std::stringstream& stream, ModelObject& object);
        bool _add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items);
        bool _add_config_file_to_archive(mz_zip_archive& archive, const Print& print);
    };

    _3MF_Exporter::BuildItem::BuildItem(unsigned int id, const Matrix4x4& matrix)
        : id(id)
        , matrix(matrix)
    {
    }

    bool _3MF_Exporter::save_model_to_file(const std::string& filename, Model& model, const Print& print)
    {
        clear_errors();
        return _save_model_to_file(filename, model, print);
    }

    bool _3MF_Exporter::_save_model_to_file(const std::string& filename, Model& model, const Print& print)
    {
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);

        mz_bool res = mz_zip_writer_init_file(&archive, filename.c_str(), 0);
        if (res == 0)
        {
            add_error("Unable to open the file");
            return false;
        }

        // adds content types file
        if (!_add_content_types_file_to_archive(archive))
        {
            mz_zip_writer_end(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // adds relationships file
        if (!_add_relationships_file_to_archive(archive))
        {
            mz_zip_writer_end(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // adds model file
        if (!_add_model_file_to_archive(archive, model))
        {
            mz_zip_writer_end(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // adds slic3r config file
        if (!_add_config_file_to_archive(archive, print))
        {
            mz_zip_writer_end(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        if (!mz_zip_writer_finalize_archive(&archive))
        {
            mz_zip_writer_end(&archive);
            boost::filesystem::remove(filename);
            add_error("Unable to finalize the archive");
            return false;
        }

        mz_zip_writer_end(&archive);

        return true;
    }

    bool _3MF_Exporter::_add_content_types_file_to_archive(mz_zip_archive& archive)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
        stream << " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\" />\n";
        stream << " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\" />\n";
        stream << "</Types>";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, CONTENT_TYPES_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add content types file to archive");
            return false;
        }

        return true;
    }

    bool _3MF_Exporter::_add_relationships_file_to_archive(mz_zip_archive& archive)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
        stream << " <Relationship Target=\"/" << MODEL_FILE << "\" Id=\"rel-1\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\" />\n";
        stream << "</Relationships>";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, RELATIONSHIPS_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add relationships file to archive");
            return false;
        }

        return true;
    }

    bool _3MF_Exporter::_add_model_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
        stream << " <resources>\n";

        BuildItemsList build_items;

        unsigned int object_id = 1;
        for (ModelObject* obj : model.objects)
        {
            if (obj == nullptr)
                continue;

            if (!_add_object_to_model_stream(stream, object_id, *obj, build_items))
            {
                add_error("Unable to add object to archive");
                return false;
            }
        }


        stream << " </resources>\n";

        if (!_add_build_to_model_stream(stream, build_items))
        {
            add_error("Unable to add build to archive");
            return false;
        }

        stream << "</model>\n";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, MODEL_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add model file to archive");
            return false;
        }

        return true;
    }

    bool _3MF_Exporter::_add_object_to_model_stream(std::stringstream& stream, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items)
    {
        unsigned int id = 0;
        for (const ModelInstance* instance : object.instances)
        {
            if (instance == nullptr)
                continue;

            unsigned int instance_id = object_id + id;
            stream << "  <object id=\"" << instance_id << "\" type=\"model\">\n";

            if (id == 0)
            {
                if (!_add_mesh_to_object_stream(stream, object))
                {
                    add_error("Unable to add mesh to archive");
                    return false;
                }
            }
            else
            {
                stream << "   <components>\n";
                stream << "    <component objectid=\"" << object_id << "\" />\n";
                stream << "   </components>\n";
            }

            Eigen::Affine3f transform;
            transform = Eigen::Translation3f((float)(instance->offset.x + object.origin_translation.x), (float)(instance->offset.y + object.origin_translation.y), (float)object.origin_translation.z)
                        * Eigen::AngleAxisf((float)instance->rotation, Eigen::Vector3f::UnitZ())
                        * Eigen::Scaling((float)instance->scaling_factor);
            build_items.emplace_back(instance_id, transform.matrix());

            stream << "  </object>\n";

            ++id;
        }

        object_id += id;
        return true;
    }

    bool _3MF_Exporter::_add_mesh_to_object_stream(std::stringstream& stream, ModelObject& object)
    {
        stream << "   <mesh>\n";
        stream << "    <vertices>\n";

        typedef std::map<ModelVolume*, unsigned int> VolumeToOffsetMap; 
        VolumeToOffsetMap volumes_offset;
        unsigned int vertices_count = 0;
        for (ModelVolume* volume : object.volumes)
        {
            if (volume == nullptr)
                continue;

            volumes_offset.insert(VolumeToOffsetMap::value_type(volume, vertices_count));

            if (!volume->mesh.repaired)
                volume->mesh.repair();

            stl_file& stl = volume->mesh.stl;
            if (stl.v_shared == nullptr)
                stl_generate_shared_vertices(&stl);

            if (stl.stats.shared_vertices == 0)
            {
                add_error("Found invalid mesh");
                return false;
            }

            vertices_count += stl.stats.shared_vertices;

            for (int i = 0; i < stl.stats.shared_vertices; ++i)
            {
                stream << "     <vertex ";
                // Subtract origin_translation in order to restore the original local coordinates
                stream << "x=\"" << (stl.v_shared[i].x - object.origin_translation.x) << "\" ";
                stream << "y=\"" << (stl.v_shared[i].y - object.origin_translation.y) << "\" ";
                stream << "z=\"" << (stl.v_shared[i].z - object.origin_translation.z) << "\" />\n";
            }
        }

        stream << "    </vertices>\n";
        stream << "    <triangles>\n";

        for (ModelVolume* volume : object.volumes)
        {
            if (volume == nullptr)
                continue;

            VolumeToOffsetMap::const_iterator offset_it = volumes_offset.find(volume);
            assert(offset_it != volumes_offset.end());

            stl_file& stl = volume->mesh.stl;

            for (uint32_t i = 0; i < stl.stats.number_of_facets; ++i)
            {
                stream << "     <triangle ";
                for (int j = 0; j < 3; ++j)
                {
                    stream << "v" << j + 1 << "=\"" << stl.v_indices[i].vertex[j] + offset_it->second << "\" ";
                }
                stream << "/>\n";
            }

        }

        stream << "    </triangles>\n";
        stream << "   </mesh>\n";

        return true;
    }

    bool _3MF_Exporter::_add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items)
    {
        if (build_items.size() == 0)
        {
            add_error("No build item found");
            return false;
        }

        stream << " <build>\n";

        for (const BuildItem& item : build_items)
        {
            stream << "  <item objectid=\"" << item.id << "\" transform =\"";
            for (unsigned c = 0; c < 4; ++c)
            {
                for (unsigned r = 0; r < 3; ++r)
                {
                    stream << item.matrix(r, c);
                    if ((r != 2) || (c != 3))
                        stream << " ";
                }
            }
            stream << "\" />\n";
        }

        stream << " </build>\n";

        return true;
    }

    bool _3MF_Exporter::_add_config_file_to_archive(mz_zip_archive& archive, const Print& print)
    {
        char buffer[1024];
        sprintf(buffer, "; %s\n\n", header_slic3r_generated().c_str());
        std::string out = buffer;

        GCode::append_full_config(print, out);

        if (!mz_zip_writer_add_mem(&archive, CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add config file to archive");
            return false;
        }

        return true;
    }

    bool load_3mf(const char* path, PresetBundle* bundle, Model* model)
    {
        if ((path == nullptr) || (bundle == nullptr) || (model == nullptr))
            return false;

        _3MF_Importer importer;
        bool res = importer.load_model_from_file(path, *model, *bundle);

        if (!res)
            importer.log_errors();

        return res;
    }

    bool store_3mf(const char* path, Model* model, Print* print)
    {
        if ((path == nullptr) || (model == nullptr) || (print == nullptr))
            return false;

        _3MF_Exporter exporter;
        bool res = exporter.save_model_to_file(path, *model, *print);

        if (!res)
            exporter.log_errors();

        return res;
    }

} // namespace Slic3r
