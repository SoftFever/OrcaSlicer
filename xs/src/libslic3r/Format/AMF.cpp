#include <string.h>
#include <map>
#include <string>
#include <expat/expat.h>

#include <boost/nowide/cstdio.hpp>

#include "../libslic3r.h"
#include "../Model.hpp"
#include "AMF.hpp"

#if 0
// Enable debugging and assert in this file.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r
{

struct AMFParserContext
{
    AMFParserContext(XML_Parser parser, Model *model) :
        m_parser(parser),
        m_model(*model), 
        m_object(nullptr), 
        m_volume(nullptr),
        m_material(nullptr),
        m_instance(nullptr)
    {
        m_path.reserve(12);
    }

    void stop() 
    {
        XML_StopParser(m_parser, 0);
    }

    void startElement(const char *name, const char **atts);
    void endElement(const char *name);
    void endDocument();
    void characters(const XML_Char *s, int len);

    static void XMLCALL startElement(void *userData, const char *name, const char **atts)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->startElement(name, atts);
    }

    static void XMLCALL endElement(void *userData, const char *name)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->endElement(name);
    }

    /* s is not 0 terminated. */
    static void XMLCALL characters(void *userData, const XML_Char *s, int len)
    {
        AMFParserContext *ctx = (AMFParserContext*)userData;
        ctx->characters(s, len);    
    }

    static const char* get_attribute(const char **atts, const char *id) {
        if (atts == nullptr)
            return nullptr;
        while (*atts != nullptr) {
            if (strcmp(*(atts ++), id) == 0)
                return *atts;
            ++ atts;
        }
        return nullptr;
    }

    enum AMFNodeType {
        NODE_TYPE_INVALID = 0,
        NODE_TYPE_UNKNOWN,
        NODE_TYPE_AMF,                  // amf
                                        // amf/metadata
        NODE_TYPE_MATERIAL,             // amf/material
                                        // amf/material/metadata
        NODE_TYPE_OBJECT,               // amf/object
                                        // amf/object/metadata
        NODE_TYPE_MESH,                 // amf/object/mesh
        NODE_TYPE_VERTICES,             // amf/object/mesh/vertices
        NODE_TYPE_VERTEX,               // amf/object/mesh/vertices/vertex
        NODE_TYPE_COORDINATES,          // amf/object/mesh/vertices/vertex/coordinates
        NODE_TYPE_COORDINATE_X,         // amf/object/mesh/vertices/vertex/coordinates/x
        NODE_TYPE_COORDINATE_Y,         // amf/object/mesh/vertices/vertex/coordinates/y
        NODE_TYPE_COORDINATE_Z,         // amf/object/mesh/vertices/vertex/coordinates/z
        NODE_TYPE_VOLUME,               // amf/object/mesh/volume
                                        // amf/object/mesh/volume/metadata
        NODE_TYPE_TRIANGLE,             // amf/object/mesh/volume/triangle
        NODE_TYPE_VERTEX1,              // amf/object/mesh/volume/triangle/v1
        NODE_TYPE_VERTEX2,              // amf/object/mesh/volume/triangle/v2
        NODE_TYPE_VERTEX3,              // amf/object/mesh/volume/triangle/v3
        NODE_TYPE_CONSTELLATION,        // amf/constellation
        NODE_TYPE_INSTANCE,             // amf/constellation/instance
        NODE_TYPE_DELTAX,               // amf/constellation/instance/deltax
        NODE_TYPE_DELTAY,               // amf/constellation/instance/deltay
        NODE_TYPE_RZ,                   // amf/constellation/instance/rz
        NODE_TYPE_SCALE,                // amf/constellation/instance/scale
        NODE_TYPE_METADATA,             // anywhere under amf/*/metadata
    };

    struct Instance {
        Instance() : deltax_set(false), deltay_set(false), rz_set(false), scale_set(false) {}
        // Shift in the X axis.
        float deltax;
        bool  deltax_set;
        // Shift in the Y axis.
        float deltay;
        bool  deltay_set;
        // Rotation around the Z axis.
        float rz;
        bool  rz_set;
        // Scaling factor
        float scale;
        bool  scale_set;
    };

    struct Object {
        Object() : idx(-1) {}
        int                     idx;
        std::vector<Instance>   instances;
    };

    // Current Expat XML parser instance.
    XML_Parser               m_parser;
    // Model to receive objects extracted from an AMF file.
    Model                   &m_model;
    // Current parsing path in the XML file.
    std::vector<AMFNodeType> m_path;
    // Current object allocated for an amf/object XML subtree.
    ModelObject             *m_object;
    // Map from obect name to object idx & instances.
    std::map<std::string, Object> m_object_instances_map;
    // Vertices parsed for the current m_object.
    std::vector<float>       m_object_vertices;
    // Current volume allocated for an amf/object/mesh/volume subtree.
    ModelVolume             *m_volume;
    // Faces collected for the current m_volume.
    std::vector<int>         m_volume_facets;
    // Current material allocated for an amf/metadata subtree.
    ModelMaterial           *m_material;
    // Current instance allocated for an amf/constellation/instance subtree.
    Instance                *m_instance;
    // Generic string buffer for vertices, face indices, metadata etc.
    std::string              m_value[3];

private:
    AMFParserContext& operator=(AMFParserContext&);
};

void AMFParserContext::startElement(const char *name, const char **atts)
{
    AMFNodeType node_type_new = NODE_TYPE_UNKNOWN;
    switch (m_path.size()) {
    case 0:
        // An AMF file must start with an <amf> tag.
        node_type_new = NODE_TYPE_AMF;
        if (strcmp(name, "amf") != 0)
            this->stop();
        break;
    case 1:
        if (strcmp(name, "metadata") == 0) {
            const char *type = get_attribute(atts, "type");
            if (type != nullptr) {
                m_value[0] = type;
                node_type_new = NODE_TYPE_METADATA;
            }
        } else if (strcmp(name, "material") == 0) {
            const char *material_id = get_attribute(atts, "id");
            m_material = m_model.add_material((material_id == nullptr) ? "_" : material_id);
            node_type_new = NODE_TYPE_MATERIAL;
        } else if (strcmp(name, "object") == 0) {
            const char *object_id = get_attribute(atts, "id");
            if (object_id == nullptr)
                this->stop();
            else {
				assert(m_object_vertices.empty());
                m_object = m_model.add_object();
                m_object_instances_map[object_id].idx = int(m_model.objects.size())-1;
                node_type_new = NODE_TYPE_OBJECT;
            }
        } else if (strcmp(name, "constellation") == 0) {
            node_type_new = NODE_TYPE_CONSTELLATION;
        }
        break;
    case 2:
        if (strcmp(name, "metadata") == 0) {
            if (m_path[1] == NODE_TYPE_MATERIAL || m_path[1] == NODE_TYPE_OBJECT) {
                m_value[0] = get_attribute(atts, "type");
                node_type_new = NODE_TYPE_METADATA;
            }
        } else if (strcmp(name, "mesh") == 0) {
            if (m_path[1] == NODE_TYPE_OBJECT)
                node_type_new = NODE_TYPE_MESH;
        } else if (strcmp(name, "instance") == 0) {
            if (m_path[1] == NODE_TYPE_CONSTELLATION) {
                const char *object_id = get_attribute(atts, "objectid");
                if (object_id == nullptr)
                    this->stop();
                else {
                    m_object_instances_map[object_id].instances.push_back(AMFParserContext::Instance());
                    m_instance = &m_object_instances_map[object_id].instances.back(); 
                    node_type_new = NODE_TYPE_INSTANCE;
                }
            }
            else
                this->stop();
        }
        break;
    case 3:
        if (m_path[2] == NODE_TYPE_MESH) {
			assert(m_object);
            if (strcmp(name, "vertices") == 0)
                node_type_new = NODE_TYPE_VERTICES;
			else if (strcmp(name, "volume") == 0) {
				assert(! m_volume);
				m_volume = m_object->add_volume(TriangleMesh());
				node_type_new = NODE_TYPE_VOLUME;
			}
        } else if (m_path[2] == NODE_TYPE_INSTANCE) {
            assert(m_instance);
            if (strcmp(name, "deltax") == 0)
                node_type_new = NODE_TYPE_DELTAX; 
            else if (strcmp(name, "deltay") == 0)
                node_type_new = NODE_TYPE_DELTAY;
            else if (strcmp(name, "rz") == 0)
                node_type_new = NODE_TYPE_RZ;
            else if (strcmp(name, "scale") == 0)
                node_type_new = NODE_TYPE_SCALE;
        }
        break;
    case 4:
        if (m_path[3] == NODE_TYPE_VERTICES) {
            if (strcmp(name, "vertex") == 0)
                node_type_new = NODE_TYPE_VERTEX; 
        } else if (m_path[3] == NODE_TYPE_VOLUME) {
            if (strcmp(name, "metadata") == 0) {
                const char *type = get_attribute(atts, "type");
                if (type == nullptr)
                    this->stop();
                else {
                    m_value[0] = type;
                    node_type_new = NODE_TYPE_METADATA;
                }
            } else if (strcmp(name, "triangle") == 0)
                node_type_new = NODE_TYPE_TRIANGLE;
        }
        break;
    case 5:
        if (strcmp(name, "coordinates") == 0) {
            if (m_path[4] == NODE_TYPE_VERTEX) {
                node_type_new = NODE_TYPE_COORDINATES; 
            } else
                this->stop();
        } else if (name[0] == 'v' && name[1] >= '1' && name[1] <= '3' && name[2] == 0) {
            if (m_path[4] == NODE_TYPE_TRIANGLE) {
                node_type_new = AMFNodeType(NODE_TYPE_VERTEX1 + name[1] - '1');
            } else
                this->stop();
        }
        break;
    case 6:
        if ((name[0] == 'x' || name[0] == 'y' || name[0] == 'z') && name[1] == 0) {
            if (m_path[5] == NODE_TYPE_COORDINATES)
                node_type_new = AMFNodeType(NODE_TYPE_COORDINATE_X + name[0] - 'x');
            else
                this->stop();
        }
        break;
    default:
        break;
    }

    m_path.push_back(node_type_new);
}

void AMFParserContext::characters(const XML_Char *s, int len)
{
    if (m_path.back() == NODE_TYPE_METADATA) {
        m_value[1].append(s, len);
    }
    else
    {
        switch (m_path.size()) {
        case 4:
            if (m_path.back() == NODE_TYPE_DELTAX || m_path.back() == NODE_TYPE_DELTAY || m_path.back() == NODE_TYPE_RZ || m_path.back() == NODE_TYPE_SCALE)
                m_value[0].append(s, len);
            break;
        case 6:
            switch (m_path.back()) {
                case NODE_TYPE_VERTEX1: m_value[0].append(s, len); break;
                case NODE_TYPE_VERTEX2: m_value[1].append(s, len); break;
                case NODE_TYPE_VERTEX3: m_value[2].append(s, len); break;
                default: break;
            }
        case 7:
            switch (m_path.back()) {
                case NODE_TYPE_COORDINATE_X: m_value[0].append(s, len); break;
                case NODE_TYPE_COORDINATE_Y: m_value[1].append(s, len); break;
                case NODE_TYPE_COORDINATE_Z: m_value[2].append(s, len); break;
                default: break;
            }
        default:
            break;
        }
    }
}

void AMFParserContext::endElement(const char * /* name */)
{
    switch (m_path.back()) {

    // Constellation transformation:
    case NODE_TYPE_DELTAX:
        assert(m_instance);
        m_instance->deltax = float(atof(m_value[0].c_str()));
        m_instance->deltax_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_DELTAY:
        assert(m_instance);
        m_instance->deltay = float(atof(m_value[0].c_str()));
        m_instance->deltay_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_RZ:
        assert(m_instance);
        m_instance->rz = float(atof(m_value[0].c_str()));
        m_instance->rz_set = true;
        m_value[0].clear();
        break;
    case NODE_TYPE_SCALE:
        assert(m_instance);
        m_instance->scale = float(atof(m_value[0].c_str()));
        m_instance->scale_set = true;
        m_value[0].clear();
        break;

    // Object vertices:
    case NODE_TYPE_VERTEX:
        assert(m_object);
        // Parse the vertex data
        m_object_vertices.emplace_back(atof(m_value[0].c_str()));
        m_object_vertices.emplace_back(atof(m_value[1].c_str()));
        m_object_vertices.emplace_back(atof(m_value[2].c_str()));
        m_value[0].clear();
        m_value[1].clear();
        m_value[2].clear();
        break;

    // Faces of the current volume:
    case NODE_TYPE_TRIANGLE:
        assert(m_object && m_volume);
        m_volume_facets.push_back(atoi(m_value[0].c_str()));
        m_volume_facets.push_back(atoi(m_value[1].c_str()));
        m_volume_facets.push_back(atoi(m_value[2].c_str()));
        m_value[0].clear();
        m_value[1].clear();
        m_value[2].clear();
        break;

    // Closing the current volume. Create an STL from m_volume_facets pointing to m_object_vertices.
    case NODE_TYPE_VOLUME:
    {
		assert(m_object && m_volume);
        stl_file &stl = m_volume->mesh.stl;
        stl.stats.type = inmemory;
        stl.stats.number_of_facets = int(m_volume_facets.size() / 3);
        stl.stats.original_num_facets = stl.stats.number_of_facets;
        stl_allocate(&stl);
        for (size_t i = 0; i < m_volume_facets.size();) {
            stl_facet &facet = stl.facet_start[i/3];
            for (unsigned int v = 0; v < 3; ++ v)
                memcpy(&facet.vertex[v].x, &m_object_vertices[m_volume_facets[i ++] * 3], 3 * sizeof(float));
        }
        stl_get_size(&stl);
        m_volume->mesh.repair();
        m_volume_facets.clear();
        m_volume = nullptr;
        break;
    }

    case NODE_TYPE_OBJECT:
        assert(m_object);
        m_object_vertices.clear();
        m_object = nullptr;
        break;

    case NODE_TYPE_MATERIAL:
        assert(m_material);
        m_material = nullptr;
        break;

    case NODE_TYPE_INSTANCE:
        assert(m_instance);
        m_instance = nullptr;
        break;

    case NODE_TYPE_METADATA:
        if (strncmp(m_value[0].c_str(), "slic3r.", 7) == 0) {
            const char *opt_key = m_value[0].c_str() + 7;
            if (print_config_def.options.find(opt_key) != print_config_def.options.end()) {
                DynamicPrintConfig *config = nullptr;
                if (m_path.size() == 3) {
                    if (m_path[1] == NODE_TYPE_MATERIAL && m_material)
                        config = &m_material->config;
                    else if (m_path[1] == NODE_TYPE_OBJECT && m_object)
                        config = &m_object->config;
                } else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME && m_volume)
                    config = &m_volume->config;
                if (config)
                    config->set_deserialize(opt_key, m_value[1]);
            } else if (m_path.size() == 3 && m_path[1] == NODE_TYPE_OBJECT && m_object && strcmp(opt_key, "layer_height_profile") == 0) {
                // Parse object's layer height profile, a semicolon separated list of floats.
                char *p = const_cast<char*>(m_value[1].c_str());
                for (;;) {
                    char *end = strchr(p, ';');
                    if (end != nullptr)
	                    *end = 0;
                    m_object->layer_height_profile.push_back(float(atof(p)));
					if (end == nullptr)
						break;
					p = end + 1;
                }
                m_object->layer_height_profile_valid = true;
            } else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME && m_volume && strcmp(opt_key, "modifier") == 0) {
                // Is this volume a modifier volume?
                m_volume->modifier = atoi(m_value[1].c_str()) == 1;
            }
        } else if (m_path.size() == 3) {
            if (m_path[1] == NODE_TYPE_MATERIAL) {
                if (m_material)
                    m_material->attributes[m_value[0]] = m_value[1];
            } else if (m_path[1] == NODE_TYPE_OBJECT) {
                if (m_object && m_value[0] == "name")
                    m_object->name = std::move(m_value[1]);
            }
        } else if (m_path.size() == 5 && m_path[3] == NODE_TYPE_VOLUME) {
            if (m_volume && m_value[0] == "name")
                m_volume->name = std::move(m_value[1]);
        }
        m_value[0].clear();
        m_value[1].clear();
        break;
    default:
        break;
    }

    m_path.pop_back();
}

void AMFParserContext::endDocument()
{
    for (const auto &object : m_object_instances_map) {
        if (object.second.idx == -1) {
            printf("Undefined object %s referenced in constellation\n", object.first.c_str());
            continue;
        }
        for (const Instance &instance : object.second.instances)
            if (instance.deltax_set && instance.deltay_set) {
                ModelInstance *mi = m_model.objects[object.second.idx]->add_instance();
                mi->offset.x = instance.deltax;
                mi->offset.y = instance.deltay;
                mi->rotation = instance.rz_set ? instance.rz : 0.f;
                mi->scaling_factor = instance.scale_set ? instance.scale : 1.f;
            }
    }
}

// Load an AMF file into a provided model.
bool load_amf(const char *path, Model *model)
{
    XML_Parser parser = XML_ParserCreate(nullptr); // encoding
    if (! parser) {
        printf("Couldn't allocate memory for parser\n");
        return false;
    }

    FILE *pFile = boost::nowide::fopen(path, "rt");
    if (pFile == nullptr) {
        printf("Cannot open file %s\n", path);
        return false;
    }

    AMFParserContext ctx(parser, model);
    XML_SetUserData(parser, (void*)&ctx);
    XML_SetElementHandler(parser, AMFParserContext::startElement, AMFParserContext::endElement);
    XML_SetCharacterDataHandler(parser, AMFParserContext::characters);

    char buff[8192];
    bool result = false;
    for (;;) {
        int len = (int)fread(buff, 1, 8192, pFile);
        if (ferror(pFile)) {
            printf("AMF parser: Read error\n");
            break;
        }
        int done = feof(pFile);
        if (XML_Parse(parser, buff, len, done) == XML_STATUS_ERROR) {
            printf("AMF parser: Parse error at line %ul:\n%s\n",
                  XML_GetCurrentLineNumber(parser),
                  XML_ErrorString(XML_GetErrorCode(parser)));
            break;
        }
        if (done) {
            result = true;
            break;
        }
    }

    XML_ParserFree(parser);
    ::fclose(pFile);

    if (result)
        ctx.endDocument();
    return result;
}

bool store_amf(const char *path, Model *model)
{
    FILE *file = boost::nowide::fopen(path, "wb");
    if (file == nullptr)
        return false;

    fprintf(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(file, "<amf unit=\"millimeter\">\n");
    fprintf(file, "<metadata type=\"cad\">Slic3r %s</metadata>\n", SLIC3R_VERSION);
    for (const auto &material : model->materials) {
        if (material.first.empty())
            continue;
        // note that material-id must never be 0 since it's reserved by the AMF spec
        fprintf(file, "  <material id=\"%s\">\n", material.first.c_str());
        for (const auto &attr : material.second->attributes)
             fprintf(file, "    <metadata type=\"%s\">%s</metadata>\n", attr.first.c_str(), attr.second.c_str());
        for (const std::string &key : material.second->config.keys())
             fprintf(file, "    <metadata type=\"slic3r.%s\">%s</metadata>\n", key.c_str(), material.second->config.serialize(key).c_str());
        fprintf(file, "  </material>\n");
    }
    std::string instances;
    for (size_t object_id = 0; object_id < model->objects.size(); ++ object_id) {
        ModelObject *object = model->objects[object_id];
        fprintf(file, "  <object id=\"" PRINTF_ZU "\">\n", object_id);
        for (const std::string &key : object->config.keys())
             fprintf(file, "    <metadata type=\"slic3r.%s\">%s</metadata>\n", key.c_str(), object->config.serialize(key).c_str());
        if (! object->name.empty())
            fprintf(file, "    <metadata type=\"name\">%s</metadata>\n", object->name.c_str());
        std::vector<double> layer_height_profile = object->layer_height_profile_valid ? object->layer_height_profile : std::vector<double>();
        if (layer_height_profile.size() >= 4 && (layer_height_profile.size() % 2) == 0) {
            // Store the layer height profile as a single semicolon separated list.
            fprintf(file, "    <metadata type=\"slic3r.layer_height_profile\">");
            fprintf(file, "%f", layer_height_profile.front());
            for (size_t i = 1; i < layer_height_profile.size(); ++ i)
                fprintf(file, ";%f", layer_height_profile[i]);
            fprintf(file, "\n    </metadata>\n");
        }
        //FIXME Store the layer height ranges (ModelObject::layer_height_ranges)
        fprintf(file, "    <mesh>\n");
        fprintf(file, "      <vertices>\n");
        std::vector<int> vertices_offsets;
        int              num_vertices = 0;
        for (ModelVolume *volume : object->volumes) {
            vertices_offsets.push_back(num_vertices);
            if (! volume->mesh.repaired) 
                CONFESS("store_amf() requires repair()");
            auto &stl = volume->mesh.stl;
            if (stl.v_shared == nullptr)
                stl_generate_shared_vertices(&stl);
            for (size_t i = 0; i < stl.stats.shared_vertices; ++ i) {
                fprintf(file, "         <vertex>\n");
                fprintf(file, "           <coordinates>\n");
                fprintf(file, "             <x>%f</x>\n", stl.v_shared[i].x);
                fprintf(file, "             <y>%f</y>\n", stl.v_shared[i].y);
                fprintf(file, "             <z>%f</z>\n", stl.v_shared[i].z);
                fprintf(file, "           </coordinates>\n");
                fprintf(file, "         </vertex>\n");
            }
            num_vertices += stl.stats.shared_vertices;
        }
        fprintf(file, "      </vertices>\n");
        for (size_t i_volume = 0; i_volume < object->volumes.size(); ++ i_volume) {
            ModelVolume *volume = object->volumes[i_volume];
            int vertices_offset = vertices_offsets[i_volume];
            if (volume->material_id().empty())
                fprintf(file, "      <volume>\n");
            else
                fprintf(file, "      <volume materialid=\"%s\">\n", volume->material_id().c_str());
            for (const std::string &key : volume->config.keys())
                fprintf(file, "        <metadata type=\"slic3r.%s\">%s</metadata>\n", key.c_str(), volume->config.serialize(key).c_str());
            if (! volume->name.empty())
                fprintf(file, "        <metadata type=\"name\">%s</metadata>\n", volume->name.c_str());
            if (volume->modifier)
                fprintf(file, "        <metadata type=\"slic3r.modifier\">1</metadata>\n");
            for (int i = 0; i < volume->mesh.stl.stats.number_of_facets; ++ i) {
                fprintf(file, "        <triangle>\n");
                for (int j = 0; j < 3; ++ j)
                    fprintf(file, "          <v%d>%d</v%d>\n", j+1, volume->mesh.stl.v_indices[i].vertex[j] + vertices_offset, j+1);
                fprintf(file, "        </triangle>\n");
            }
            fprintf(file, "      </volume>\n");
        }
        fprintf(file, "    </mesh>\n");
        fprintf(file, "  </object>\n");
        if (! object->instances.empty()) {
            for (ModelInstance *instance : object->instances) {
                char buf[512];
                sprintf(buf,
                    "    <instance objectid=\"" PRINTF_ZU "\">\n"
                    "      <deltax>%lf</deltax>\n"
                    "      <deltay>%lf</deltay>\n"
                    "      <rz>%lf</rz>\n"
                    "      <scale>%lf</scale>\n"
                    "    </instance>\n",
                    object_id,
                    instance->offset.x,
                    instance->offset.y,
                    instance->rotation,
                    instance->scaling_factor);
                //FIXME missing instance->scaling_factor
                instances.append(buf);
            }
        }
    }
    if (! instances.empty()) {
        fprintf(file, "  <constellation id=\"1\">\n");
        fwrite(instances.data(), instances.size(), 1, file);
        fprintf(file, "  </constellation>\n");
    }
    fprintf(file, "</amf>\n");
    fclose(file);
    return true;
}

}; // namespace Slic3r
