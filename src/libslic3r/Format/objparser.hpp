#ifndef slic3r_Format_objparser_hpp_
#define slic3r_Format_objparser_hpp_

#include <string>
#include <vector>
#include <istream>

namespace ObjParser {

struct ObjVertex
{
	int coordIdx;
	int textureCoordIdx;
	int normalIdx;
};

inline bool operator==(const ObjVertex &v1, const ObjVertex &v2)
{
	return 
		v1.coordIdx			== v2.coordIdx			&& 
		v1.textureCoordIdx	== v2.textureCoordIdx	&& 
		v1.normalIdx		== v2.normalIdx;
}

struct ObjUseMtl
{
	int			vertexIdxFirst;
	std::string name;
};

inline bool operator==(const ObjUseMtl &v1, const ObjUseMtl &v2)
{
	return 
		v1.vertexIdxFirst	== v2.vertexIdxFirst	&& 
		v1.name.compare(v2.name) == 0;
}

struct ObjObject
{
	int			vertexIdxFirst;
	std::string name;
};

inline bool operator==(const ObjObject &v1, const ObjObject &v2)
{
	return 
		v1.vertexIdxFirst	== v2.vertexIdxFirst	&& 
		v1.name.compare(v2.name) == 0;
}

struct ObjGroup
{
	int			vertexIdxFirst;
	std::string name;
};

inline bool operator==(const ObjGroup &v1, const ObjGroup &v2)
{
	return 
		v1.vertexIdxFirst	== v2.vertexIdxFirst	&& 
		v1.name.compare(v2.name) == 0;
}

struct ObjSmoothingGroup
{
	int			vertexIdxFirst;
	int			smoothingGroupID;
};

inline bool operator==(const ObjSmoothingGroup &v1, const ObjSmoothingGroup &v2)
{
	return 
		v1.vertexIdxFirst	== v2.vertexIdxFirst	&& 
		v1.smoothingGroupID == v2.smoothingGroupID;
}

struct ObjData {
	// Version of the data structure for load / store in the private binary format.
	int								version;

	// x, y, z, w
	std::vector<float>				coordinates;
	// u, v, w
	std::vector<float>				textureCoordinates;
	// x, y, z
	std::vector<float>				normals;
	// u, v, w
	std::vector<float>				parameters;

	std::vector<std::string>		mtllibs;
	std::vector<ObjUseMtl>			usemtls;
	std::vector<ObjObject>			objects;
	std::vector<ObjGroup>			groups;
	std::vector<ObjSmoothingGroup>	smoothingGroups;

	// List of faces, delimited by an ObjVertex with all members set to -1.
	std::vector<ObjVertex>			vertices;
};

extern bool objparse(const char *path, ObjData &data);
extern bool objparse(std::istream &stream, ObjData &data);

extern bool objbinsave(const char *path, const ObjData &data);

extern bool objbinload(const char *path, ObjData &data);

extern bool objequal(const ObjData &data1, const ObjData &data2);

} // namespace ObjParser

#endif /* slic3r_Format_objparser_hpp_ */
