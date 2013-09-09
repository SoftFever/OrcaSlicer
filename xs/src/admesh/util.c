/*  ADMesh -- process triangulated solid meshes
 *  Copyright (C) 1995, 1996  Anthony D. Martin
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *  
 *  Questions, comments, suggestions, etc to <amartin@engr.csulb.edu>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stl.h"

static void stl_rotate(float *x, float *y, float angle);
static float get_area(stl_facet *facet);
static float get_volume(stl_file *stl);


void
stl_verify_neighbors(stl_file *stl)
{
  int i;
  int j;
  stl_edge edge_a;
  stl_edge edge_b;
  int neighbor;
  int vnot;

  stl->stats.backwards_edges = 0;

  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  edge_a.p1 = stl->facet_start[i].vertex[j];
	  edge_a.p2 = stl->facet_start[i].vertex[(j + 1) % 3];
	  neighbor = stl->neighbors_start[i].neighbor[j];
	  vnot = stl->neighbors_start[i].which_vertex_not[j];
	  
	  if(neighbor == -1)
	    continue;		/* this edge has no neighbor... Continue. */
	  if(vnot < 3)
	    {
	      edge_b.p1 = stl->facet_start[neighbor].vertex[(vnot + 2) % 3];
	      edge_b.p2 = stl->facet_start[neighbor].vertex[(vnot + 1) % 3];
	    }
	  else
	    {
	      stl->stats.backwards_edges += 1;
	      edge_b.p1 = stl->facet_start[neighbor].vertex[(vnot + 1) % 3];
	      edge_b.p2 = stl->facet_start[neighbor].vertex[(vnot + 2) % 3];
	    }
	  if(memcmp(&edge_a, &edge_b, SIZEOF_EDGE_SORT) != 0)
	    {
	      /* These edges should match but they don't.  Print results. */
	      printf("edge %d of facet %d doesn't match edge %d of facet %d\n",
		     j, i, vnot + 1, neighbor);
	      stl_write_facet(stl, (char*)"first facet", i);
	      stl_write_facet(stl, (char*)"second facet", neighbor);
	    }
	}
    }
}

void
stl_translate(stl_file *stl, float x, float y, float z)
{
  int i;
  int j;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl->facet_start[i].vertex[j].x += x;
	  stl->facet_start[i].vertex[j].y += y;
	  stl->facet_start[i].vertex[j].z += z;
	}
    }
  stl->stats.min.x += x;
  stl->stats.min.y += y;
  stl->stats.min.z += z;
  stl->stats.max.x += x;
  stl->stats.max.y += y;
  stl->stats.max.z += z;
  
  stl_invalidate_shared_vertices(stl);
}

void
stl_scale(stl_file *stl, float versor[3])
{
  int i;
  int j;
  
  // scale extents
  stl->stats.min.x *= versor[0];
  stl->stats.min.y *= versor[1];
  stl->stats.min.z *= versor[2];
  stl->stats.max.x *= versor[0];
  stl->stats.max.y *= versor[1];
  stl->stats.max.z *= versor[2];
  
  // scale size
  stl->stats.size.x *= versor[0];
  stl->stats.size.y *= versor[1];
  stl->stats.size.z *= versor[2];
  
  // scale volume
  if (stl->stats.volume > 0.0) {
    stl->stats.volume *= (versor[0] * versor[1] * versor[2]);
  }
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl->facet_start[i].vertex[j].x *= versor[0];
	  stl->facet_start[i].vertex[j].y *= versor[1];
	  stl->facet_start[i].vertex[j].z *= versor[2];
	}
    }
   
   stl_invalidate_shared_vertices(stl);
}

void
stl_scale(stl_file *stl, float factor)
{
    float versor[3];
    versor[0] = factor;
    versor[1] = factor;
    versor[2] = factor;
    stl_scale(stl, versor);
}

static void calculate_normals(stl_file *stl)
{
	long i;
	float normal[3];
	
	for(i = 0; i < stl->stats.number_of_facets; i++){
		stl_calculate_normal(normal, &stl->facet_start[i]);
		stl_normalize_vector(normal);
		stl->facet_start[i].normal.x = normal[0];
		stl->facet_start[i].normal.y = normal[1];
		stl->facet_start[i].normal.z = normal[2];
	}
}

void
stl_rotate_x(stl_file *stl, float angle)
{
  int i;
  int j;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl_rotate(&stl->facet_start[i].vertex[j].y,
		     &stl->facet_start[i].vertex[j].z, angle);
	}
    }
  stl_get_size(stl);
	calculate_normals(stl);
}

void
stl_rotate_y(stl_file *stl, float angle)
{
  int i;
  int j;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl_rotate(&stl->facet_start[i].vertex[j].z,
		     &stl->facet_start[i].vertex[j].x, angle);
	}
    }
  stl_get_size(stl);
	calculate_normals(stl);
}

void
stl_rotate_z(stl_file *stl, float angle)
{
  int i;
  int j;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl_rotate(&stl->facet_start[i].vertex[j].x,
		     &stl->facet_start[i].vertex[j].y, angle);
	}
    }
  stl_get_size(stl);
	calculate_normals(stl);
}

		

static void
stl_rotate(float *x, float *y, float angle)
{
  double r;
  double theta;
  double radian_angle;
  
  radian_angle = (angle / 180.0) * M_PI;
  
  r = sqrt((*x * *x) + (*y * *y));
  theta = atan2(*y, *x);
  *x = r * cos(theta + radian_angle);
  *y = r * sin(theta + radian_angle);
}

extern void
stl_get_size(stl_file *stl)
{
  int i;
  int j;

  stl->stats.min.x = stl->facet_start[0].vertex[0].x;
  stl->stats.min.y = stl->facet_start[0].vertex[0].y;
  stl->stats.min.z = stl->facet_start[0].vertex[0].z;
  stl->stats.max.x = stl->facet_start[0].vertex[0].x;
  stl->stats.max.y = stl->facet_start[0].vertex[0].y;
  stl->stats.max.z = stl->facet_start[0].vertex[0].z;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl->stats.min.x = STL_MIN(stl->stats.min.x,
				     stl->facet_start[i].vertex[j].x);
	  stl->stats.min.y = STL_MIN(stl->stats.min.y,
				     stl->facet_start[i].vertex[j].y);
	  stl->stats.min.z = STL_MIN(stl->stats.min.z,
				     stl->facet_start[i].vertex[j].z);
	  stl->stats.max.x = STL_MAX(stl->stats.max.x,
				     stl->facet_start[i].vertex[j].x);
	  stl->stats.max.y = STL_MAX(stl->stats.max.y,
				     stl->facet_start[i].vertex[j].y);
	  stl->stats.max.z = STL_MAX(stl->stats.max.z,
				     stl->facet_start[i].vertex[j].z);
	}
    }
    stl->stats.size.x = stl->stats.max.x - stl->stats.min.x;
    stl->stats.size.y = stl->stats.max.y - stl->stats.min.y;
    stl->stats.size.z = stl->stats.max.z - stl->stats.min.z;
    stl->stats.bounding_diameter = sqrt(
        stl->stats.size.x * stl->stats.size.x +
        stl->stats.size.y * stl->stats.size.y +
        stl->stats.size.z * stl->stats.size.z
        );
}

void
stl_mirror_xy(stl_file *stl)
{
  int i;
  int j;
  float temp_size;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl->facet_start[i].vertex[j].z *= -1.0;
    	}
    }
  temp_size = stl->stats.min.z;
  stl->stats.min.z = stl->stats.max.z;
  stl->stats.max.z = temp_size;
  stl->stats.min.z *= -1.0;
  stl->stats.max.z *= -1.0;
}

void
stl_mirror_yz(stl_file *stl)
{
  int i;
  int j;
  float temp_size;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl->facet_start[i].vertex[j].x *= -1.0;
    	}
    }
  temp_size = stl->stats.min.x;
  stl->stats.min.x = stl->stats.max.x;
  stl->stats.max.x = temp_size;
  stl->stats.min.x *= -1.0;
  stl->stats.max.x *= -1.0;
}

void
stl_mirror_xz(stl_file *stl)
{
  int i;
  int j;
  float temp_size;
  
  for(i = 0; i < stl->stats.number_of_facets; i++)
    {
      for(j = 0; j < 3; j++)
	{
	  stl->facet_start[i].vertex[j].y *= -1.0;
    	}
    }
  temp_size = stl->stats.min.y;
  stl->stats.min.y = stl->stats.max.y;
  stl->stats.max.y = temp_size;
  stl->stats.min.y *= -1.0;
  stl->stats.max.y *= -1.0;
}

static float get_volume(stl_file *stl)
{
	long i;
	stl_vertex p0;
	stl_vertex p;
	stl_normal n;
	float height;
	float area;
	float volume = 0.0;
	
	/* Choose a point, any point as the reference */
	p0.x = stl->facet_start[0].vertex[0].x;
	p0.y = stl->facet_start[0].vertex[0].y;
	p0.z = stl->facet_start[0].vertex[0].z;

	for(i = 0; i < stl->stats.number_of_facets; i++){
		p.x = stl->facet_start[i].vertex[0].x - p0.x;
		p.y = stl->facet_start[i].vertex[0].y - p0.y;
		p.z = stl->facet_start[i].vertex[0].z - p0.z;
		/* Do dot product to get distance from point to plane */
		n = stl->facet_start[i].normal;
		height = (n.x * p.x) + (n.y * p.y) + (n.z * p.z);
		area = get_area(&stl->facet_start[i]);
		volume += (area * height) / 3.0;
	}
	return volume;
}

void stl_calculate_volume(stl_file *stl)
{
	stl->stats.volume = get_volume(stl);
	if(stl->stats.volume < 0.0){
		stl_reverse_all_facets(stl);
		stl->stats.volume = -stl->stats.volume;
	}
}

static float get_area(stl_facet *facet)
{
	float cross[3][3];
	float sum[3];
	float n[3];
	float area;
	int i;
	
	for(i = 0; i < 3; i++){
	    cross[i][0]=((facet->vertex[i].y * facet->vertex[(i + 1) % 3].z) -
			 (facet->vertex[i].z * facet->vertex[(i + 1) % 3].y));
	    cross[i][1]=((facet->vertex[i].z * facet->vertex[(i + 1) % 3].x) -
			 (facet->vertex[i].x * facet->vertex[(i + 1) % 3].z));
	    cross[i][2]=((facet->vertex[i].x * facet->vertex[(i + 1) % 3].y) -
			 (facet->vertex[i].y * facet->vertex[(i + 1) % 3].x));
	}
	
	sum[0] = cross[0][0] + cross[1][0] + cross[2][0];
	sum[1] = cross[0][1] + cross[1][1] + cross[2][1];
	sum[2] = cross[0][2] + cross[1][2] + cross[2][2];

	/* This should already be done.  But just in case, let's do it again */
	stl_calculate_normal(n, facet);
	stl_normalize_vector(n);

	area = 0.5 * (n[0] * sum[0] + n[1] * sum[1] + n[2] * sum[2]);
	return area;
}
