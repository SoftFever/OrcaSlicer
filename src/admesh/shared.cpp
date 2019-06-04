/*  ADMesh -- process triangulated solid meshes
 *  Copyright (C) 1995, 1996  Anthony D. Martin <amartin@engr.csulb.edu>
 *  Copyright (C) 2013, 2014  several contributors, see AUTHORS
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  Questions, comments, suggestions, etc to
 *           https://github.com/admesh/admesh/issues
 */

#include <stdlib.h>
#include <string.h>

#include <vector>

#include <boost/nowide/cstdio.hpp>

#include "stl.h"

void stl_invalidate_shared_vertices(stl_file *stl)
{
  	stl->v_indices.clear();
  	stl->v_shared.clear();
}

void stl_generate_shared_vertices(stl_file *stl)
{
	if (stl->error)
		return;

	/* make sure this function is idempotent and does not leak memory */
	stl_invalidate_shared_vertices(stl);

	// 3 indices to vertex per face
	stl->v_indices.assign(stl->stats.number_of_facets, v_indices_struct());
	// Shared vertices (3D coordinates)
	stl->v_shared.assign(stl->stats.number_of_facets / 2, stl_vertex());
	stl->stats.shared_vertices = 0;

	// A degenerate mesh may contain loops: Traversing a fan will end up in an endless loop
	// while never reaching the starting face. To avoid these endless loops, traversed faces at each fan traversal
	// are marked with a unique fan_traversal_stamp.
	unsigned int			  fan_traversal_stamp = 0;
	std::vector<unsigned int> fan_traversal_facet_visited(stl->stats.number_of_facets, 0);

	for (uint32_t facet_idx = 0; facet_idx < stl->stats.number_of_facets; ++ facet_idx) {
		for (int j = 0; j < 3; ++ j) {
			if (stl->v_indices[facet_idx].vertex[j] != -1)
				// Shared vertex was already assigned.
				continue;
			// Create a new shared vertex.
			stl->v_shared.emplace_back(stl->facet_start[facet_idx].vertex[j]);
			// Traverse the fan around the j-th vertex of the i-th face, assign the newly created shared vertex index to all the neighboring triangles in the triangle fan.
			int  facet_in_fan_idx 	= facet_idx;
			bool edge_direction 	= false;
			bool traversal_reversed = false;
			int  vnot      			= (j + 2) % 3;
			// Increase the 
			++ fan_traversal_stamp;
			for (;;) {
				// Next edge on facet_in_fan_idx to be traversed. The edge is indexed by its starting vertex index.
				int next_edge    = 0;
				// Vertex index in facet_in_fan_idx, which is being pivoted around, and which is being assigned a new shared vertex.
				int pivot_vertex = 0;
				if (vnot > 2) {
					// The edge of facet_in_fan_idx opposite to vnot is equally oriented, therefore
					// the neighboring facet is flipped.
			  		if (! edge_direction) {
			    		pivot_vertex = (vnot + 2) % 3;
			    		next_edge    = pivot_vertex;			    		
			  		} else {
			    		pivot_vertex = (vnot + 1) % 3;
			    		next_edge    = vnot % 3;
			  		}
			  		edge_direction = ! edge_direction;
				} else {
					// The neighboring facet is correctly oriented.
			  		if (! edge_direction) {
			    		pivot_vertex = (vnot + 1) % 3;
			    		next_edge    = vnot;
			  		} else {
			    		pivot_vertex = (vnot + 2) % 3;
			    		next_edge    = pivot_vertex;
			  		}
				}
				stl->v_indices[facet_in_fan_idx].vertex[pivot_vertex] = stl->stats.shared_vertices;
				fan_traversal_facet_visited[facet_in_fan_idx] = fan_traversal_stamp;

				// next_edge is an index of the starting vertex of the edge, not an index of the opposite vertex to the edge!
				int next_facet = stl->neighbors_start[facet_in_fan_idx].neighbor[next_edge];
				if (next_facet == -1) {
					// No neighbor going in the current direction.
					if (traversal_reversed) {
						// Went to one limit, then turned back and reached the other limit. Quit the fan traversal.
					    break;
					} else {
						// Reached the first limit. Now try to reverse and traverse up to the other limit.
					    edge_direction        = true;
					    vnot 	         	  = (j + 1) % 3;
					    traversal_reversed    = true;
				    	facet_in_fan_idx      = facet_idx;
					}
				} else if (next_facet == facet_idx) {
					// Traversed a closed fan all around.
//					assert(! traversal_reversed);
					break;
				} else if (next_facet >= (int)stl->stats.number_of_facets) {
					// The mesh is not valid!
					// assert(false);
					break;
				} else if (fan_traversal_facet_visited[next_facet] == fan_traversal_stamp) {
					// Traversed a closed fan all around, but did not reach the starting face.
					// This indicates an invalid geometry (non-manifold).
					//assert(false);
					break;
				} else {
					// Continue traversal.
					// next_edge is an index of the starting vertex of the edge, not an index of the opposite vertex to the edge!
					vnot = stl->neighbors_start[facet_in_fan_idx].which_vertex_not[next_edge];
					facet_in_fan_idx = next_facet;
				}
			}

			++ stl->stats.shared_vertices;
		}
	}
}

void stl_write_off(stl_file *stl, const char *file)
{
	if (stl->error)
		return;

	/* Open the file */
	FILE *fp = boost::nowide::fopen(file, "w");
	if (fp == nullptr) {
		char *error_msg = (char*)malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
		sprintf(error_msg, "stl_write_ascii: Couldn't open %s for writing", file);
		perror(error_msg);
		free(error_msg);
		stl->error = 1;
		return;
	}

	fprintf(fp, "OFF\n");
	fprintf(fp, "%d %d 0\n", stl->stats.shared_vertices, stl->stats.number_of_facets);
	for (int i = 0; i < stl->stats.shared_vertices; ++ i)
		fprintf(fp, "\t%f %f %f\n", stl->v_shared[i](0), stl->v_shared[i](1), stl->v_shared[i](2));
	for (uint32_t i = 0; i < stl->stats.number_of_facets; ++ i)
		fprintf(fp, "\t3 %d %d %d\n", stl->v_indices[i].vertex[0], stl->v_indices[i].vertex[1], stl->v_indices[i].vertex[2]);
	fclose(fp);
}

void stl_write_vrml(stl_file *stl, const char *file)
{
  	if (stl->error) 
  		return;

	/* Open the file */
  	FILE *fp = boost::nowide::fopen(file, "w");
	if (fp == nullptr) {
  		char *error_msg = (char*)malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
		sprintf(error_msg, "stl_write_ascii: Couldn't open %s for writing", file);
		perror(error_msg);
		free(error_msg);
		stl->error = 1;
		return;
	}

	fprintf(fp, "#VRML V1.0 ascii\n\n");
	fprintf(fp, "Separator {\n");
	fprintf(fp, "\tDEF STLShape ShapeHints {\n");
	fprintf(fp, "\t\tvertexOrdering COUNTERCLOCKWISE\n");
	fprintf(fp, "\t\tfaceType CONVEX\n");
	fprintf(fp, "\t\tshapeType SOLID\n");
	fprintf(fp, "\t\tcreaseAngle 0.0\n");
	fprintf(fp, "\t}\n");
	fprintf(fp, "\tDEF STLModel Separator {\n");
	fprintf(fp, "\t\tDEF STLColor Material {\n");
	fprintf(fp, "\t\t\temissiveColor 0.700000 0.700000 0.000000\n");
	fprintf(fp, "\t\t}\n");
	fprintf(fp, "\t\tDEF STLVertices Coordinate3 {\n");
	fprintf(fp, "\t\t\tpoint [\n");

	int i = 0;
	for (; i < (stl->stats.shared_vertices - 1); i++)
		fprintf(fp, "\t\t\t\t%f %f %f,\n", stl->v_shared[i](0), stl->v_shared[i](1), stl->v_shared[i](2));
	fprintf(fp, "\t\t\t\t%f %f %f]\n", stl->v_shared[i](0), stl->v_shared[i](1), stl->v_shared[i](2));
	fprintf(fp, "\t\t}\n");
	fprintf(fp, "\t\tDEF STLTriangles IndexedFaceSet {\n");
	fprintf(fp, "\t\t\tcoordIndex [\n");

	for (int i = 0; i + 1 < (int)stl->stats.number_of_facets; ++ i)
		fprintf(fp, "\t\t\t\t%d, %d, %d, -1,\n", stl->v_indices[i].vertex[0], stl->v_indices[i].vertex[1], stl->v_indices[i].vertex[2]);
	fprintf(fp, "\t\t\t\t%d, %d, %d, -1]\n", stl->v_indices[i].vertex[0], stl->v_indices[i].vertex[1], stl->v_indices[i].vertex[2]);
	fprintf(fp, "\t\t}\n");
	fprintf(fp, "\t}\n");
	fprintf(fp, "}\n");
	fclose(fp);
}

void stl_write_obj (stl_file *stl, const char *file)
{
	if (stl->error)
		return;

  	FILE *fp = boost::nowide::fopen(file, "w");
  	if (fp == nullptr) {
    	char* error_msg = (char*)malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
    	sprintf(error_msg, "stl_write_ascii: Couldn't open %s for writing", file);
    	perror(error_msg);
    	free(error_msg);
    	stl->error = 1;
    	return;
  	}

	for (int i = 0; i < stl->stats.shared_vertices; ++ i)
    	fprintf(fp, "v %f %f %f\n", stl->v_shared[i](0), stl->v_shared[i](1), stl->v_shared[i](2));
  	for (uint32_t i = 0; i < stl->stats.number_of_facets; ++ i)
    	fprintf(fp, "f %d %d %d\n", stl->v_indices[i].vertex[0]+1, stl->v_indices[i].vertex[1]+1, stl->v_indices[i].vertex[2]+1);
  	fclose(fp);
}
