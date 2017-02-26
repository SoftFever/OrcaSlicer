#ifdef SLIC3R_PRUS

#include <string.h>

#include <wx/string.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include "../libslic3r.h"
#include "../Model.hpp"

#include "PRUS.hpp"

#if 0
// Enable debugging and assert in this file.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r
{

struct StlHeader
{
    char        comment[80];
    uint32_t    nTriangles;
};

// Buffered line reader for the wxInputStream.
class LineReader
{
public:
    LineReader(wxInputStream &input_stream, const char *initial_data, int initial_len) : 
        m_input_stream(input_stream),
        m_pos(0),
        m_len(initial_len)
    {
        assert(initial_len >= 0 && initial_len < m_bufsize);
        memcpy(m_buffer, initial_data, initial_len);
    }

    const char* next_line() {
        for (;;) {
            // Skip empty lines.
            while (m_pos < m_len && (m_buffer[m_pos] == '\r' || m_buffer[m_pos] == '\n'))
                ++ m_pos;
            if (m_pos == m_len) {
                // Empty buffer, fill it from the input stream.
                m_pos = 0;
                m_input_stream.Read(m_buffer, m_bufsize - 1);
                m_len = m_input_stream.LastRead();
				assert(m_len >= 0 && m_len < m_bufsize);
                if (m_len == 0)
                    // End of file.
                    return nullptr;
                // Skip empty lines etc.
                continue;
            }
            // The buffer is nonempty and it does not start with end of lines. Find the first end of line.
            int end = m_pos + 1;
            while (end < m_len && m_buffer[end] != '\r' && m_buffer[end] != '\n')
                ++ end;
            if (end == m_len && ! m_input_stream.Eof() && m_len < m_bufsize) {
                // Move the buffer content to the buffer start and fill the rest of the buffer.
                assert(m_pos > 0);
                memmove(m_buffer, m_buffer + m_pos, m_len - m_pos);
				m_len -= m_pos;
				assert(m_len >= 0 && m_len < m_bufsize);
				m_pos = 0;
                m_input_stream.Read(m_buffer + m_len, m_bufsize - 1 - m_len);
                int new_data = m_input_stream.LastRead();
                if (new_data > 0) {
                    m_len += new_data;
					assert(m_len >= 0 && m_len < m_bufsize);
					continue;
                }
            }
            char *ptr_out = m_buffer + m_pos;
            m_pos = end + 1;
            m_buffer[end] = 0;
            if (m_pos >= m_len) {
                m_pos = 0;
                m_len = 0;
            }
            return ptr_out;
        }
    }

    int next_line_scanf(char *format, ...)
    {
        const char *line = next_line();
        if (line == nullptr)
            return -1;
        int result;
        va_list arglist;
        va_start(arglist, format);
        result = vsscanf(line, format, arglist);
        va_end(arglist);
        return result;
    }

private:
    wxInputStream &m_input_stream;
    static const int m_bufsize = 4096;
    char m_buffer[m_bufsize];
    int  m_pos = 0;
    int  m_len = 0;
};

// Load a PrusaControl project file into a provided model.
bool load_prus(const char *path, Model *model)
{
    // To receive the content of the zipped 'scene.xml' file.
    std::vector<char>           scene_xml_data;
    wxFFileInputStream          in(path);
    wxZipInputStream            zip(in);
    std::unique_ptr<wxZipEntry> entry;
    size_t                      num_models = 0;
    while (entry.reset(zip.GetNextEntry()), entry.get() != NULL) {
        wxString name = entry->GetName();
        if (name == "scene.xml") {
            if (! scene_xml_data.empty()) {
                // scene.xml has been found more than once in the archive.
                return false;
            }
            size_t size_last = 0;
            size_t size_incr = 4096;
            scene_xml_data.resize(size_incr);
            while (! zip.Read(scene_xml_data.data() + size_last, size_incr).Eof()) {
                size_last += zip.LastRead();
                if (scene_xml_data.size() < size_last + size_incr)
                    scene_xml_data.resize(size_last + size_incr);
            }
            size_last += size_last + zip.LastRead();
            if (scene_xml_data.size() == size_last)
                scene_xml_data.resize(size_last + 1);
            else if (scene_xml_data.size() > size_last + 1)
                scene_xml_data.erase(scene_xml_data.begin() + size_last + 1, scene_xml_data.end());
            scene_xml_data[size_last] = 0;
        }
        else if (name.EndsWith(".stl") || name.EndsWith(".STL")) {
            // Find the model entry in the XML data.
            const wxScopedCharBuffer name_utf8 = name.ToUTF8();
            char model_name_tag[1024];
            sprintf(model_name_tag, "<model name=\"%s\">", name_utf8.data());
            const char *model_xml = strstr(scene_xml_data.data(), model_name_tag);
            float trafo[3][4] = { 0 };
            bool  trafo_set = false;
            if (model_xml != nullptr) {
                model_xml += strlen(model_name_tag);
                const char *position_tag = "<position>";
                const char *position_xml = strstr(model_xml, position_tag);
                const char *rotation_tag = "<rotation>";
                const char *rotation_xml = strstr(model_xml, rotation_tag);
                const char *scale_tag    = "<scale>";
                const char *scale_xml    = strstr(model_xml, scale_tag);
                float position[3], rotation[3][3], scale[3][3];
                if (position_xml != nullptr && rotation_xml != nullptr && scale_xml != nullptr &&
                    sscanf(position_xml+strlen(position_tag), 
                        "[%f, %f, %f]", position, position+1, position+2) == 3 &&
                    sscanf(rotation_xml+strlen(rotation_tag), 
                        "[[%f, %f, %f], [%f, %f, %f], [%f, %f, %f]]", 
                        rotation[0], rotation[0]+1, rotation[0]+2, 
                        rotation[1], rotation[1]+1, rotation[1]+2, 
                        rotation[2], rotation[2]+1, rotation[2]+2) == 9 &&
                    sscanf(scale_xml+strlen(scale_tag), 
                        "[[%f, %f, %f], [%f, %f, %f], [%f, %f, %f]]", 
                        scale[0], scale[0]+1, scale[0]+2, 
                        scale[1], scale[1]+1, scale[1]+2, 
                        scale[2], scale[2]+1, scale[2]+2) == 9) {
                    for (size_t r = 0; r < 3; ++ r) {
                        for (size_t c = 0; c < 3; ++ c) {
                            for (size_t i = 0; i < 3; ++ i)
                                trafo[r][c] += rotation[r][i]*scale[i][c];
                        }
                        trafo[r][3] = position[r];
                    }
                    trafo_set = true;
                }
            }
            if (trafo_set) {
				// Extract the STL.
				StlHeader header;
				bool stl_ascii = false;
				if (!zip.Read((void*)&header, sizeof(StlHeader)).Eof()) {
					if (strncmp(header.comment, "solid ", 6) == 0)
						stl_ascii = true;
					else {
						// Header has been extracted. Now read the faces.
						TriangleMesh mesh;
						stl_file &stl = mesh.stl;
						stl.error = 0;
						stl.stats.type = inmemory;
						stl.stats.number_of_facets = header.nTriangles;
						stl.stats.original_num_facets = header.nTriangles;
						stl_allocate(&stl);
						if (zip.ReadAll((void*)stl.facet_start, 50 * header.nTriangles)) {
							// All the faces have been read.
							stl_get_size(&stl);
							mesh.repair();
							// Transform the model.
							stl_transform(&stl, &trafo[0][0]);
							// Add a mesh to a model.
							if (mesh.facets_count() > 0) {
                                model->add_object(name_utf8.data(), path, std::move(mesh));
								++ num_models;
							}
						}
					}
				} else
					stl_ascii = true;
				if (stl_ascii) {
					// Try to parse ASCII STL.
                    char                    normal_buf[3][32];
                    stl_facet               facet;
                    std::vector<stl_facet>  facets;
                    LineReader              line_reader(zip, (char*)&header, zip.LastRead());
                    std::string             solid_name;
                    facet.extra[0] = facet.extra[1] = 0;
                    for (;;) {
                        const char *line = line_reader.next_line();
                        if (line == nullptr)
                            // End of file.
                            break;
                        if (strncmp(line, "solid", 5) == 0) {
                            // Opening the "solid" block.
                            if (! solid_name.empty()) {
                                // Error, solid block is already open.
                                facets.clear();
                                break;
                            }
                            solid_name = line + 5;
                            if (solid_name.empty())
                                solid_name = "unknown";
                            continue;
                        }
                        if (strncmp(line, "endsolid", 8) == 0) {
                            // Closing the "solid" block.
                            if (solid_name.empty()) {
                                // Error, no solid block is open.
                                facets.clear();
                                break;
                            }
							solid_name.clear();
                            continue;
                        }
                        // Line has to start with the word solid.
						int res_normal		= sscanf(line, " facet normal %31s %31s %31s", normal_buf[0], normal_buf[1], normal_buf[2]);
						assert(res_normal == 3);
                        int res_outer_loop	= line_reader.next_line_scanf(" outer loop");
						assert(res_outer_loop == 0);
						int res_vertex1 = line_reader.next_line_scanf(" vertex %f %f %f", &facet.vertex[0].x, &facet.vertex[0].y, &facet.vertex[0].z);
						assert(res_vertex1 == 3);
						int res_vertex2 = line_reader.next_line_scanf(" vertex %f %f %f", &facet.vertex[1].x, &facet.vertex[1].y, &facet.vertex[1].z);
						assert(res_vertex2 == 3);
						int res_vertex3 = line_reader.next_line_scanf(" vertex %f %f %f", &facet.vertex[2].x, &facet.vertex[2].y, &facet.vertex[2].z);
						assert(res_vertex3 == 3);
						int res_endloop = line_reader.next_line_scanf(" endloop");
						assert(res_endloop == 0);
						int res_endfacet = line_reader.next_line_scanf(" endfacet");
						if (res_normal != 3 || res_outer_loop != 0 || res_vertex1 != 3 || res_vertex2 != 3 || res_vertex3 != 3 || res_endloop != 0 || res_endfacet != 0) {
                            // perror("Something is syntactically very wrong with this ASCII STL!");
                            facets.clear();
                            break;
                        }
                        // The facet normal has been parsed as a single string as to workaround for not a numbers in the normal definition.
                        if (sscanf(normal_buf[0], "%f", &facet.normal.x) != 1 ||
                            sscanf(normal_buf[1], "%f", &facet.normal.y) != 1 ||
                            sscanf(normal_buf[2], "%f", &facet.normal.z) != 1) {
                            // Normal was mangled. Maybe denormals or "not a number" were stored?
                            // Just reset the normal and silently ignore it.
                            memset(&facet.normal, 0, sizeof(facet.normal));
                        }
                        facets.emplace_back(facet);
                    }
                    if (! facets.empty() && solid_name.empty()) {
                        TriangleMesh mesh;
                        stl_file &stl = mesh.stl;
                        stl.stats.type = inmemory;
                        stl.stats.number_of_facets = facets.size();
                        stl.stats.original_num_facets = facets.size();
                        stl_allocate(&stl);
                        memcpy((void*)stl.facet_start, facets.data(), facets.size() * 50);
                        stl_get_size(&stl);
                        mesh.repair();
                        // Transform the model.
                        stl_transform(&stl, &trafo[0][0]);
                        // Add a mesh to a model.
                        if (mesh.facets_count() > 0) {
                            model->add_object(name_utf8.data(), path, std::move(mesh));
                            ++ num_models;
                        }
                    }
				}
            }
        }
    }
    return num_models > 0;
}

}; // namespace Slic3r

#endif /* SLIC3R_PRUS */
