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
const std::string PRINT_CONFIG_FILE = "Metadata/Slic3r_PE.config";
const std::string MODEL_CONFIG_FILE = "Metadata/Slic3r_PE_model.config";

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

const char* CONFIG_TAG = "config";
const char* METADATA_TAG = "metadata";
const char* VOLUME_TAG = "volume";

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

const char* KEY_ATTR = "key";
const char* VALUE_ATTR = "value";
const char* FIRST_TRIANGLE_ID_ATTR = "firstid";
const char* LAST_TRIANGLE_ID_ATTR = "lastid";

const char* OBJECT_TYPE = "object";
const char* VOLUME_TYPE = "volume";

const char* NAME_KEY = "name";
const char* MODIFIER_KEY = "modifier";

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

            explicit Component(int object_id)
                : object_id(object_id)
                , matrix(Matrix4x4::Identity())
            {
            }

            Component(int object_id, const Matrix4x4& matrix)
                : object_id(object_id)
                , matrix(matrix)
            {
            }
        };

        typedef std::vector<Component> ComponentsList;

        struct Geometry
        {
            std::vector<float> vertices;
            std::vector<unsigned int> triangles;

            bool empty()
            {
                return vertices.empty() || triangles.empty();
            }

            void reset()
            {
                vertices.clear();
                triangles.clear();
            }
        };

        struct CurrentObject
        {
            int id;
            Geometry geometry;
            ModelObject* object;
            ComponentsList components;

            CurrentObject()
            {
                reset();
            }

            void reset()
            {
                id = -1;
                geometry.reset();
                object = nullptr;
                components.clear();
            }
        };

        struct CurrentConfig
        {
            int object_id;
            int volume_id;
        };

        struct Instance
        {
            ModelInstance* instance;
            Matrix4x4 matrix;

            Instance(ModelInstance* instance, const Matrix4x4& matrix)
                : instance(instance)
                , matrix(matrix)
            {
            }
        };

        struct Metadata
        {
            std::string key;
            std::string value;

            Metadata(const std::string& key, const std::string& value)
                : key(key)
                , value(value)
            {
            }
        };

        typedef std::vector<Metadata> MetadataList;

        struct ObjectMetadata
        {
            struct VolumeMetadata
            {
                unsigned int first_triangle_id;
                unsigned int last_triangle_id;
                MetadataList metadata;

                VolumeMetadata(unsigned int first_triangle_id, unsigned int last_triangle_id)
                    : first_triangle_id(first_triangle_id)
                    , last_triangle_id(last_triangle_id)
                {
                }
            };

            typedef std::vector<VolumeMetadata> VolumeMetadataList;

            MetadataList metadata;
            VolumeMetadataList volumes;
        };

        typedef std::map<int, ModelObject*> IdToModelObjectMap;
        typedef std::map<int, ComponentsList> IdToAliasesMap;
        typedef std::vector<Instance> InstancesList;
        typedef std::map<int, ObjectMetadata> IdToMetadataMap;
        typedef std::map<int, Geometry> IdToGeometryMap;

        XML_Parser m_xml_parser;
        Model* m_model;
        float m_unit_factor;
        CurrentObject m_curr_object;
        IdToModelObjectMap m_objects;
        IdToAliasesMap m_objects_aliases;
        InstancesList m_instances;
        IdToGeometryMap m_geometries;
        CurrentConfig m_curr_config;
        IdToMetadataMap m_objects_metadata;

    public:
        _3MF_Importer();
        ~_3MF_Importer();

        bool load_model_from_file(const std::string& filename, Model& model, PresetBundle& bundle);

    private:
        void _destroy_xml_parser();
        void _stop_xml_parser();

        bool _load_model_from_file(const std::string& filename, Model& model, PresetBundle& bundle);
        bool _extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        bool _extract_print_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, PresetBundle& bundle, const std::string& archive_filename);
        bool _extract_model_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model);

        // handlers to parse the .model file
        void _handle_start_model_xml_element(const char* name, const char** attributes);
        void _handle_end_model_xml_element(const char* name);

        // handlers to parse the MODEL_CONFIG_FILE file
        void _handle_start_config_xml_element(const char* name, const char** attributes);
        void _handle_end_config_xml_element(const char* name);

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

        void _apply_transform(ModelInstance& instance, const Matrix4x4& matrix);

        bool _handle_start_config(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config();

        bool _handle_start_config_object(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_object();

        bool _handle_start_config_volume(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_volume();

        bool _handle_start_config_metadata(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_metadata();

        bool _generate_volumes(ModelObject& object, const Geometry& geometry, const ObjectMetadata::VolumeMetadataList& volumes);

        // callbacks to parse the .model file
        static void XMLCALL _handle_start_model_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_model_xml_element(void* userData, const char* name);

        // callbacks to parse the MODEL_CONFIG_FILE file
        static void XMLCALL _handle_start_config_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_config_xml_element(void* userData, const char* name);
    };

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
        m_geometries.clear();
        m_curr_config.object_id = -1;
        m_curr_config.volume_id = -1;
        m_objects_metadata.clear();
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
                else if (boost::algorithm::iequals(name, PRINT_CONFIG_FILE))
                {
                    // extract slic3r print config file
                    if (!_extract_print_config_from_archive(archive, stat, bundle, filename))
                    {
                        mz_zip_reader_end(&archive);
                        add_error("Archive does not contain a valid print config");
                        return false;
                    }
                }
                else if (boost::algorithm::iequals(name, MODEL_CONFIG_FILE))
                {
                    // extract slic3r model config file
                    if (!_extract_model_config_from_archive(archive, stat, model))
                    {
                        mz_zip_reader_end(&archive);
                        add_error("Archive does not contain a valid model config");
                        return false;
                    }
                }
            }
        }

        mz_zip_reader_end(&archive);

        for (const IdToModelObjectMap::value_type& object : m_objects)
        {
            ObjectMetadata::VolumeMetadataList volumes;
            ObjectMetadata::VolumeMetadataList* volumes_ptr = nullptr;

            IdToGeometryMap::const_iterator obj_geometry = m_geometries.find(object.first);
            if (obj_geometry == m_geometries.end())
            {
                add_error("Unable to find object geometry");
                return false;
            }

            IdToMetadataMap::iterator obj_metadata = m_objects_metadata.find(object.first);
            if (obj_metadata != m_objects_metadata.end())
            {
                // config data has been found, this model was saved using slic3r pe

                // apply object's name and config data
                for (const Metadata& metadata : obj_metadata->second.metadata)
                {
                    if (metadata.key == "name")
                        object.second->name = metadata.value;
                    else
                        object.second->config.set_deserialize(metadata.key, metadata.value);
                }

                // select object's detected volumes
                volumes_ptr = &obj_metadata->second.volumes;
            }
            else
            {
                // config data not found, this model was not saved using slic3r pe

                // add the entire geometry as the single volume to generate
                volumes.emplace_back(0, (int)obj_geometry->second.triangles.size() / 3 - 1);

                // select as volumes
                volumes_ptr = &volumes;
            }

            if (!_generate_volumes(*object.second, obj_geometry->second, *volumes_ptr))
                return false;
        }

        // fixes the min z of the model if negative
        model.adjust_min_z();

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
        XML_SetElementHandler(m_xml_parser, _3MF_Importer::_handle_start_model_xml_element, _3MF_Importer::_handle_end_model_xml_element);

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

    bool _3MF_Importer::_extract_print_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, PresetBundle& bundle, const std::string& archive_filename)
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

    bool _3MF_Importer::_extract_model_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model)
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
        XML_SetElementHandler(m_xml_parser, _3MF_Importer::_handle_start_config_xml_element, _3MF_Importer::_handle_end_config_xml_element);

        void* parser_buffer = XML_GetBuffer(m_xml_parser, (int)stat.m_uncomp_size);
        if (parser_buffer == nullptr)
        {
            add_error("Unable to create buffer");
            return false;
        }

        mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, parser_buffer, (size_t)stat.m_uncomp_size, 0);
        if (res == 0)
        {
            add_error("Error while reading config data to buffer");
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

    void _3MF_Importer::_handle_start_model_xml_element(const char* name, const char** attributes)
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

    void _3MF_Importer::_handle_end_model_xml_element(const char* name)
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

    void _3MF_Importer::_handle_start_config_xml_element(const char* name, const char** attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;
        unsigned int num_attributes = (unsigned int)XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(CONFIG_TAG, name) == 0)
            res = _handle_start_config(attributes, num_attributes);
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_start_config_object(attributes, num_attributes);
        else if (::strcmp(VOLUME_TAG, name) == 0)
            res = _handle_start_config_volume(attributes, num_attributes);
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_start_config_metadata(attributes, num_attributes);

        if (!res)
            _stop_xml_parser();
    }

    void _3MF_Importer::_handle_end_config_xml_element(const char* name)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;

        if (::strcmp(CONFIG_TAG, name) == 0)
            res = _handle_end_config();
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_end_config_object();
        else if (::strcmp(VOLUME_TAG, name) == 0)
            res = _handle_end_config_volume();
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_end_config_metadata();

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
                    // apply the matrix to the instance
                    _apply_transform(*instance.instance, instance.matrix);
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
                // geometry defined, store it for later use
                m_geometries.insert(IdToGeometryMap::value_type(m_curr_object.id, std::move(m_curr_object.geometry)));

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

    bool _3MF_Importer::_handle_start_config(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_end_config()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_config_object(const char** attributes, unsigned int num_attributes)
    {
        int object_id = get_attribute_value_int(attributes, num_attributes, ID_ATTR);
        IdToMetadataMap::iterator object_item = m_objects_metadata.find(object_id);
        if (object_item != m_objects_metadata.end())
        {
            add_error("Found duplicated object id");
            return false;
        }

        m_objects_metadata.insert(IdToMetadataMap::value_type(object_id, ObjectMetadata()));
        m_curr_config.object_id = object_id;
        return true;
    }

    bool _3MF_Importer::_handle_end_config_object()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_config_volume(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end())
        {
            add_error("Cannot assign volume to a valid object");
            return false;
        }

        m_curr_config.volume_id = object->second.volumes.size();

        unsigned int first_triangle_id = (unsigned int)get_attribute_value_int(attributes, num_attributes, FIRST_TRIANGLE_ID_ATTR);
        unsigned int last_triangle_id = (unsigned int)get_attribute_value_int(attributes, num_attributes, LAST_TRIANGLE_ID_ATTR);

        object->second.volumes.emplace_back(first_triangle_id, last_triangle_id);
        return true;
    }

    bool _3MF_Importer::_handle_end_config_volume()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_handle_start_config_metadata(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end())
        {
            add_error("Cannot assign metadata to valid object id");
            return false;
        }

        std::string type = get_attribute_value_string(attributes, num_attributes, TYPE_ATTR);
        std::string key = get_attribute_value_string(attributes, num_attributes, KEY_ATTR);
        std::string value = get_attribute_value_string(attributes, num_attributes, VALUE_ATTR);

        if (type == OBJECT_TYPE)
            object->second.metadata.emplace_back(key, value);
        else if (type == VOLUME_TYPE)
        {
            if (m_curr_config.volume_id < object->second.volumes.size())
                object->second.volumes[m_curr_config.volume_id].metadata.emplace_back(key, value);
        }
        else
        {
            add_error("Found invalid metadata type");
            return false;
        }

        return true;
    }

    bool _3MF_Importer::_handle_end_config_metadata()
    {
        // do nothing
        return true;
    }

    bool _3MF_Importer::_generate_volumes(ModelObject& object, const Geometry& geometry, const ObjectMetadata::VolumeMetadataList& volumes)
    {
        if (!object.volumes.empty())
        {
            add_error("Found invalid volumes count");
            return false;
        }

        unsigned int geo_tri_count = geometry.triangles.size() / 3;

        for (const ObjectMetadata::VolumeMetadata& volume_data : volumes)
        {
            if ((geo_tri_count <= volume_data.first_triangle_id) || (geo_tri_count <= volume_data.last_triangle_id) || (volume_data.last_triangle_id < volume_data.first_triangle_id))
            {
                add_error("Found invalid triangle id");
                return false;
            }

            // splits volume out of imported geometry
            unsigned int triangles_count = volume_data.last_triangle_id - volume_data.first_triangle_id + 1;
            ModelVolume* volume = object.add_volume(TriangleMesh());
            stl_file& stl = volume->mesh.stl;
            stl.stats.type = inmemory;
            stl.stats.number_of_facets = (uint32_t)triangles_count;
            stl.stats.original_num_facets = (int)stl.stats.number_of_facets;
            stl_allocate(&stl);

            unsigned int src_start_id = volume_data.first_triangle_id * 3;

            for (size_t i = 0; i < triangles_count; ++i)
            {
                unsigned int ii = i * 3;
                stl_facet& facet = stl.facet_start[i];
                for (unsigned int v = 0; v < 3; ++v)
                {
                    ::memcpy((void*)&facet.vertex[v].x, (const void*)&geometry.vertices[geometry.triangles[src_start_id + ii + v] * 3], 3 * sizeof(float));
                }
            }

            stl_get_size(&stl);
            volume->mesh.repair();

            // apply volume's name and config data
            for (const Metadata& metadata : volume_data.metadata)
            {
                if (metadata.key == NAME_KEY)
                    volume->name = metadata.value;
                else if ((metadata.key == MODIFIER_KEY) && (metadata.value == "1"))
                    volume->modifier = true;
                else
                    volume->config.set_deserialize(metadata.key, metadata.value);
            }
        }

        return true;
    }

    void XMLCALL _3MF_Importer::_handle_start_model_xml_element(void* userData, const char* name, const char** attributes)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_model_xml_element(name, attributes);
    }

    void XMLCALL _3MF_Importer::_handle_end_model_xml_element(void* userData, const char* name)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_model_xml_element(name);
    }

    void XMLCALL _3MF_Importer::_handle_start_config_xml_element(void* userData, const char* name, const char** attributes)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_config_xml_element(name, attributes);
    }
    
    void XMLCALL _3MF_Importer::_handle_end_config_xml_element(void* userData, const char* name)
    {
        _3MF_Importer* importer = (_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_config_xml_element(name);
    }

    class _3MF_Exporter : public _3MF_Base
    {
        struct BuildItem
        {
            unsigned int id;
            Matrix4x4 matrix;

            BuildItem(unsigned int id, const Matrix4x4& matrix)
                : id(id)
                , matrix(matrix)
            {
            }
        };

        struct Offsets
        {
            unsigned int first_vertex_id;
            unsigned int first_triangle_id;
            unsigned int last_triangle_id;

            Offsets(unsigned int first_vertex_id)
                : first_vertex_id(first_vertex_id)
                , first_triangle_id(-1)
                , last_triangle_id(-1)
            {
            }
        };

        typedef std::map<const ModelVolume*, Offsets> VolumeToOffsetsMap;

        struct ObjectData
        {
            ModelObject* object;
            VolumeToOffsetsMap volumes_offsets;

            explicit ObjectData(ModelObject* object)
                : object(object)
            {
            }
        };

        typedef std::vector<BuildItem> BuildItemsList;
        typedef std::map<int, ObjectData> IdToObjectDataMap;

        IdToObjectDataMap m_objects_data;

    public:
        bool save_model_to_file(const std::string& filename, Model& model, const Print& print, bool export_print_config);

    private:
        bool _save_model_to_file(const std::string& filename, Model& model, const Print& print, bool export_print_config);
        bool _add_content_types_file_to_archive(mz_zip_archive& archive);
        bool _add_relationships_file_to_archive(mz_zip_archive& archive);
        bool _add_model_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_object_to_model_stream(std::stringstream& stream, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items, VolumeToOffsetsMap& volumes_offsets);
        bool _add_mesh_to_object_stream(std::stringstream& stream, ModelObject& object, VolumeToOffsetsMap& volumes_offsets);
        bool _add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items);
        bool _add_print_config_file_to_archive(mz_zip_archive& archive, const Print& print);
        bool _add_model_config_file_to_archive(mz_zip_archive& archive, const Model& model);
    };

    bool _3MF_Exporter::save_model_to_file(const std::string& filename, Model& model, const Print& print, bool export_print_config)
    {
        clear_errors();
        return _save_model_to_file(filename, model, print, export_print_config);
    }

    bool _3MF_Exporter::_save_model_to_file(const std::string& filename, Model& model, const Print& print, bool export_print_config)
    {
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);

        m_objects_data.clear();

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

        // adds slic3r print config file
        if (export_print_config)
        {
            if (!_add_print_config_file_to_archive(archive, print))
            {
                mz_zip_writer_end(&archive);
                boost::filesystem::remove(filename);
                return false;
            }
        }

        // adds slic3r model config file
        if (!_add_model_config_file_to_archive(archive, model))
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
        stream << "<" << MODEL_TAG << " unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
        stream << " <" << RESOURCES_TAG << ">\n";

        BuildItemsList build_items;

        unsigned int object_id = 1;
        for (ModelObject* obj : model.objects)
        {
            if (obj == nullptr)
                continue;

            unsigned int curr_id = object_id;
            IdToObjectDataMap::iterator object_it = m_objects_data.insert(IdToObjectDataMap::value_type(curr_id, ObjectData(obj))).first;

            if (!_add_object_to_model_stream(stream, object_id, *obj, build_items, object_it->second.volumes_offsets))
            {
                add_error("Unable to add object to archive");
                return false;
            }
        }

        stream << " </" << RESOURCES_TAG << ">\n";

        if (!_add_build_to_model_stream(stream, build_items))
        {
            add_error("Unable to add build to archive");
            return false;
        }

        stream << "</" << MODEL_TAG << ">\n";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, MODEL_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add model file to archive");
            return false;
        }

        return true;
    }

    bool _3MF_Exporter::_add_object_to_model_stream(std::stringstream& stream, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items, VolumeToOffsetsMap& volumes_offsets)
    {
        unsigned int id = 0;
        for (const ModelInstance* instance : object.instances)
        {
            if (instance == nullptr)
                continue;

            unsigned int instance_id = object_id + id;
            stream << "  <" << OBJECT_TAG << " id=\"" << instance_id << "\" type=\"model\">\n";

            if (id == 0)
            {
                if (!_add_mesh_to_object_stream(stream, object, volumes_offsets))
                {
                    add_error("Unable to add mesh to archive");
                    return false;
                }
            }
            else
            {
                stream << "   <" << COMPONENTS_TAG << ">\n";
                stream << "    <" << COMPONENT_TAG << " objectid=\"" << object_id << "\" />\n";
                stream << "   </" << COMPONENTS_TAG << ">\n";
            }

            Eigen::Affine3f transform;
            transform = Eigen::Translation3f((float)instance->offset.x, (float)instance->offset.y, 0.0f) * Eigen::AngleAxisf((float)instance->rotation, Eigen::Vector3f::UnitZ()) * Eigen::Scaling((float)instance->scaling_factor);
            build_items.emplace_back(instance_id, transform.matrix());

            stream << "  </" << OBJECT_TAG << ">\n";

            ++id;
        }

        object_id += id;
        return true;
    }

    bool _3MF_Exporter::_add_mesh_to_object_stream(std::stringstream& stream, ModelObject& object, VolumeToOffsetsMap& volumes_offsets)
    {
        stream << "   <" << MESH_TAG << ">\n";
        stream << "    <" << VERTICES_TAG << ">\n";

        unsigned int vertices_count = 0;
        for (ModelVolume* volume : object.volumes)
        {
            if (volume == nullptr)
                continue;

            VolumeToOffsetsMap::iterator volume_it = volumes_offsets.insert(VolumeToOffsetsMap::value_type(volume, Offsets(vertices_count))).first;

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
                stream << "     <" << VERTEX_TAG << " ";
                stream << "x=\"" << stl.v_shared[i].x << "\" ";
                stream << "y=\"" << stl.v_shared[i].y << "\" ";
                stream << "z=\"" << stl.v_shared[i].z << "\" />\n";
            }
        }

        stream << "    </" << VERTICES_TAG << ">\n";
        stream << "    <" << TRIANGLES_TAG << ">\n";

        unsigned int triangles_count = 0;
        for (ModelVolume* volume : object.volumes)
        {
            if (volume == nullptr)
                continue;

            VolumeToOffsetsMap::iterator volume_it = volumes_offsets.find(volume);
            assert(volume_it != volumes_offsets.end());

            stl_file& stl = volume->mesh.stl;

            // updates triangle offsets
            volume_it->second.first_triangle_id = triangles_count;
            triangles_count += stl.stats.number_of_facets;
            volume_it->second.last_triangle_id = triangles_count - 1;

            for (uint32_t i = 0; i < stl.stats.number_of_facets; ++i)
            {
                stream << "     <" << TRIANGLE_TAG << " ";
                for (int j = 0; j < 3; ++j)
                {
                    stream << "v" << j + 1 << "=\"" << stl.v_indices[i].vertex[j] + volume_it->second.first_vertex_id << "\" ";
                }
                stream << "/>\n";
            }
        }

        stream << "    </" << TRIANGLES_TAG << ">\n";
        stream << "   </" << MESH_TAG << ">\n";

        return true;
    }

    bool _3MF_Exporter::_add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items)
    {
        if (build_items.size() == 0)
        {
            add_error("No build item found");
            return false;
        }

        stream << " <" << BUILD_TAG << ">\n";

        for (const BuildItem& item : build_items)
        {
            stream << "  <" << ITEM_TAG << " objectid=\"" << item.id << "\" transform =\"";
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

        stream << " </" << BUILD_TAG << ">\n";

        return true;
    }

    bool _3MF_Exporter::_add_print_config_file_to_archive(mz_zip_archive& archive, const Print& print)
    {
        char buffer[1024];
        sprintf(buffer, "; %s\n\n", header_slic3r_generated().c_str());
        std::string out = buffer;

        GCode::append_full_config(print, out);

        if (!mz_zip_writer_add_mem(&archive, PRINT_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add print config file to archive");
            return false;
        }

        return true;
    }

    bool _3MF_Exporter::_add_model_config_file_to_archive(mz_zip_archive& archive, const Model& model)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<" << CONFIG_TAG << ">\n";

        for (const IdToObjectDataMap::value_type& obj_metadata : m_objects_data)
        {
            const ModelObject* obj = obj_metadata.second.object;
            if (obj != nullptr)
            {
                stream << " <" << OBJECT_TAG << " id=\"" << obj_metadata.first << "\">\n";

                // stores object's name
                if (!obj->name.empty())
                    stream << "  <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << OBJECT_TYPE << "\" " << KEY_ATTR << "=\"name\" " << VALUE_ATTR << "=\"" << obj->name << "\"/>\n";

                // stores object's config data
                for (const std::string& key : obj->config.keys())
                {
                    stream << "  <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << OBJECT_TYPE << "\" " << KEY_ATTR << "=\"" << key << "\" " << VALUE_ATTR << "=\"" << obj->config.serialize(key) << "\"/>\n";
                }

                for (const ModelVolume* volume : obj_metadata.second.object->volumes)
                {
                    if (volume != nullptr)
                    {
                        const VolumeToOffsetsMap& offsets = obj_metadata.second.volumes_offsets;
                        VolumeToOffsetsMap::const_iterator it = offsets.find(volume);
                        if (it != offsets.end())
                        {
                            // stores volume's offsets
                            stream << "  <" << VOLUME_TAG << " ";
                            stream << FIRST_TRIANGLE_ID_ATTR << "=\"" << it->second.first_triangle_id << "\" ";
                            stream << LAST_TRIANGLE_ID_ATTR << "=\"" << it->second.last_triangle_id << "\">\n";

                            // stores volume's name
                            if (!volume->name.empty())
                                stream << "   <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << VOLUME_TYPE << "\" " << KEY_ATTR << "=\"" << NAME_KEY << "\" " << VALUE_ATTR << "=\"" << volume->name << "\"/>\n";

                            // stores volume's modifier field
                            if (volume->modifier)
                                stream << "   <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << VOLUME_TYPE << "\" " << KEY_ATTR << "=\"" << MODIFIER_KEY << "\" " << VALUE_ATTR << "=\"1\"/>\n";

                            // stores volume's config data
                            for (const std::string& key : volume->config.keys())
                            {
                                stream << "   <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << VOLUME_TYPE << "\" " << KEY_ATTR << "=\"" << key << "\" " << VALUE_ATTR << "=\"" << volume->config.serialize(key) << "\"/>\n";
                            }

                            stream << "  </" << VOLUME_TAG << ">\n";
                        }
                    }
                }

                stream << " </" << OBJECT_TAG << ">\n";
            }
        }

        stream << "</" << CONFIG_TAG << ">\n";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, MODEL_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        {
            add_error("Unable to add model config file to archive");
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

    bool store_3mf(const char* path, Model* model, Print* print, bool export_print_config)
    {
        if ((path == nullptr) || (model == nullptr) || (print == nullptr))
            return false;

        _3MF_Exporter exporter;
        bool res = exporter.save_model_to_file(path, *model, *print, export_print_config);

        if (!res)
            exporter.log_errors();

        return res;
    }
} // namespace Slic3r
