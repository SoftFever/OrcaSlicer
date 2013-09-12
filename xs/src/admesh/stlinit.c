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

#if !defined(SEEK_SET)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

void
stl_open(stl_file *stl, char *file)
{
  stl_initialize(stl);
  stl_count_facets(stl, file);
  stl_allocate(stl);
  stl_read(stl, 0, 1);
  fclose(stl->fp);
}

static int
stl_get_little_int(FILE *fp)
{
  int value;
  value  =  fgetc(fp) & 0xFF;
  value |= (fgetc(fp) & 0xFF) << 0x08;
  value |= (fgetc(fp) & 0xFF) << 0x10;
  value |= (fgetc(fp) & 0xFF) << 0x18;
  return(value);
}

static float
stl_get_little_float(FILE *fp)
{
  union 
    {
      int   int_value;
      float float_value;
    } value;
  
  value.int_value  =  fgetc(fp) & 0xFF;
  value.int_value |= (fgetc(fp) & 0xFF) << 0x08;
  value.int_value |= (fgetc(fp) & 0xFF) << 0x10;
  value.int_value |= (fgetc(fp) & 0xFF) << 0x18;
  return(value.float_value);
}


void
stl_initialize(stl_file *stl)
{
  stl->stats.degenerate_facets = 0;
  stl->stats.edges_fixed  = 0;
  stl->stats.facets_added = 0;
  stl->stats.facets_removed = 0;
  stl->stats.facets_reversed = 0;
  stl->stats.normals_fixed = 0;
  stl->stats.number_of_parts = 0;
  stl->stats.original_num_facets = 0;
  stl->stats.number_of_facets = 0;
  stl->stats.volume = -1.0;
  
  stl->neighbors_start = NULL;
  stl->facet_start = NULL;
  stl->v_indices = NULL;
  stl->v_shared = NULL;
}

static void
stl_count_facets(stl_file *stl, char *file)
{
  long           file_size;
  int            header_num_facets;
  int            num_facets;
  int            i, j;
  unsigned char  chtest[128];
  int            num_lines = 1;
  char           *error_msg;
  
  /* Open the file */
  stl->fp = fopen(file, "r");
  if(stl->fp == NULL)
    {
      error_msg = (char*)
	malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
      sprintf(error_msg, "stl_initialize: Couldn't open %s for reading",
	      file);
      perror(error_msg);
      free(error_msg);
      exit(1);
    }
  /* Find size of file */
  fseek(stl->fp, 0, SEEK_END);
  file_size = ftell(stl->fp);
  
  /* Check for binary or ASCII file */
  fseek(stl->fp, HEADER_SIZE, SEEK_SET);
  fread(chtest, sizeof(chtest), 1, stl->fp);
  stl->stats.type = ascii;
  for(i = 0; i < sizeof(chtest); i++)
    {
      if(chtest[i] > 127)
	{
	  stl->stats.type = binary;
	  break;
	}
    }
  rewind(stl->fp);

  /* Get the header and the number of facets in the .STL file */
  /* If the .STL file is binary, then do the following */
  if(stl->stats.type == binary)
    {
      /* Test if the STL file has the right size  */
      if(((file_size - HEADER_SIZE) % SIZEOF_STL_FACET != 0)
	 || (file_size < STL_MIN_FILE_SIZE))
	{
	  fprintf(stderr, "The file %s has the wrong size.\n", file);
	  exit(1);
	}
      num_facets = (file_size - HEADER_SIZE) / SIZEOF_STL_FACET;

      /* Read the header */
      fread(stl->stats.header, LABEL_SIZE, 1, stl->fp);
      stl->stats.header[80] = '\0';

      /* Read the int following the header.  This should contain # of facets */
      header_num_facets = stl_get_little_int(stl->fp);
      if(num_facets != header_num_facets)
	{
	  fprintf(stderr, 
	  "Warning: File size doesn't match number of facets in the header\n");
	}
    }
  /* Otherwise, if the .STL file is ASCII, then do the following */
  else
    {
      /* Find the number of facets */
      j = 0;
      for(i = 0; i < file_size ; i++)
	{
	  j++;
	  if(getc(stl->fp) == '\n')
	    {
	      if(j > 4) /* don't count short lines */
		{
		  num_lines++;
		}
	      j = 0;
	    }
	}
      rewind(stl->fp);

      /* Get the header */
      for(i = 0; 
	  (i < 80) && (stl->stats.header[i] = getc(stl->fp)) != '\n'; i++);
      stl->stats.header[i] = '\0'; /* Lose the '\n' */
      stl->stats.header[80] = '\0';
      
      num_facets = num_lines / ASCII_LINES_PER_FACET;
    }
  stl->stats.number_of_facets += num_facets;
  stl->stats.original_num_facets = stl->stats.number_of_facets;
}

void
stl_allocate(stl_file *stl)
{
  /*  Allocate memory for the entire .STL file */
  stl->facet_start = (stl_facet*)calloc(stl->stats.number_of_facets, 
			    sizeof(stl_facet));
  if(stl->facet_start == NULL) perror("stl_initialize");
  stl->stats.facets_malloced = stl->stats.number_of_facets;

  /* Allocate memory for the neighbors list */
  stl->neighbors_start = (stl_neighbors*)
    calloc(stl->stats.number_of_facets, sizeof(stl_neighbors));
  if(stl->facet_start == NULL) perror("stl_initialize");
}

void
stl_open_merge(stl_file *stl, char *file)
{
  int first_facet;
  
  first_facet = stl->stats.number_of_facets;
  stl_initialize(stl);
  stl_count_facets(stl, file);
  stl_reallocate(stl);
  stl_read(stl, first_facet, 0);
}

extern void
stl_reallocate(stl_file *stl)
{
  /*  Reallocate more memory for the .STL file(s) */
  stl->facet_start = (stl_facet*)realloc(stl->facet_start, stl->stats.number_of_facets *
			     sizeof(stl_facet));
  if(stl->facet_start == NULL) perror("stl_initialize");
  stl->stats.facets_malloced = stl->stats.number_of_facets;

  /* Reallocate more memory for the neighbors list */
  stl->neighbors_start = (stl_neighbors*)
    realloc(stl->neighbors_start, stl->stats.number_of_facets *
	    sizeof(stl_neighbors));
  if(stl->facet_start == NULL) perror("stl_initialize");
}

static void
stl_read(stl_file *stl, int first_facet, int first)
{
  stl_facet facet;
  int   i;

  if(stl->stats.type == binary)
    {
      fseek(stl->fp, HEADER_SIZE, SEEK_SET);
    }
  else
    {
      rewind(stl->fp);
      /* Skip the first line of the file */
      while(getc(stl->fp) != '\n');
    }

  for(i = first_facet; i < stl->stats.number_of_facets; i++)
    {
      if(stl->stats.type == binary)
	/* Read a single facet from a binary .STL file */
	{
	  facet.normal.x = stl_get_little_float(stl->fp);
	  facet.normal.y = stl_get_little_float(stl->fp);
	  facet.normal.z = stl_get_little_float(stl->fp);
	  facet.vertex[0].x = stl_get_little_float(stl->fp);
	  facet.vertex[0].y = stl_get_little_float(stl->fp);
	  facet.vertex[0].z = stl_get_little_float(stl->fp);
	  facet.vertex[1].x = stl_get_little_float(stl->fp);
	  facet.vertex[1].y = stl_get_little_float(stl->fp);
	  facet.vertex[1].z = stl_get_little_float(stl->fp);
	  facet.vertex[2].x = stl_get_little_float(stl->fp);
	  facet.vertex[2].y = stl_get_little_float(stl->fp);
	  facet.vertex[2].z = stl_get_little_float(stl->fp);
	  facet.extra[0] = fgetc(stl->fp);
	  facet.extra[1] = fgetc(stl->fp);
	}
      else
	/* Read a single facet from an ASCII .STL file */
	{
      	  fscanf(stl->fp, "%*s %*s %f %f %f\n", &facet.normal.x,
		 &facet.normal.y, &facet.normal.z);
	  fscanf(stl->fp, "%*s %*s");
	  fscanf(stl->fp, "%*s %f %f %f\n", &facet.vertex[0].x,
		 &facet.vertex[0].y,  &facet.vertex[0].z);
	  fscanf(stl->fp, "%*s %f %f %f\n", &facet.vertex[1].x,
		 &facet.vertex[1].y,  &facet.vertex[1].z);
	  fscanf(stl->fp, "%*s %f %f %f\n", &facet.vertex[2].x,
		 &facet.vertex[2].y,  &facet.vertex[2].z);
	  fscanf(stl->fp, "%*s");
	  fscanf(stl->fp, "%*s");
	}
      /* Write the facet into memory. */
      stl->facet_start[i] = facet;
      
      stl_facet_stats(stl, facet, first);
      first = 0;
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
stl_facet_stats(stl_file *stl, stl_facet facet, int first)
{
    float diff_x;
    float diff_y;
    float diff_z;
    float max_diff;
    /* while we are going through all of the facets, let's find the  */
    /* maximum and minimum values for x, y, and z  */
    
    /* Initialize the max and min values the first time through*/
    if (first) {
        stl->stats.max.x = facet.vertex[0].x;
        stl->stats.min.x = facet.vertex[0].x;
        stl->stats.max.y = facet.vertex[0].y;
        stl->stats.min.y = facet.vertex[0].y;
        stl->stats.max.z = facet.vertex[0].z;
        stl->stats.min.z = facet.vertex[0].z;
        
        diff_x = ABS(facet.vertex[0].x - facet.vertex[1].x);
        diff_y = ABS(facet.vertex[0].y - facet.vertex[1].y);
        diff_z = ABS(facet.vertex[0].z - facet.vertex[1].z);
        max_diff = STL_MAX(diff_x, diff_y);
        max_diff = STL_MAX(diff_z, max_diff);
        stl->stats.shortest_edge = max_diff;
        
        first = 0;
    }
    
    /* now find the max and min values */
    stl->stats.max.x = STL_MAX(stl->stats.max.x, facet.vertex[0].x);
    stl->stats.min.x = STL_MIN(stl->stats.min.x, facet.vertex[0].x);
    stl->stats.max.y = STL_MAX(stl->stats.max.y, facet.vertex[0].y);
    stl->stats.min.y = STL_MIN(stl->stats.min.y, facet.vertex[0].y);
    stl->stats.max.z = STL_MAX(stl->stats.max.z, facet.vertex[0].z);
    stl->stats.min.z = STL_MIN(stl->stats.min.z, facet.vertex[0].z);
    
    stl->stats.max.x = STL_MAX(stl->stats.max.x, facet.vertex[1].x);
    stl->stats.min.x = STL_MIN(stl->stats.min.x, facet.vertex[1].x);
    stl->stats.max.y = STL_MAX(stl->stats.max.y, facet.vertex[1].y);
    stl->stats.min.y = STL_MIN(stl->stats.min.y, facet.vertex[1].y);
    stl->stats.max.z = STL_MAX(stl->stats.max.z, facet.vertex[1].z);
    stl->stats.min.z = STL_MIN(stl->stats.min.z, facet.vertex[1].z);
    
    stl->stats.max.x = STL_MAX(stl->stats.max.x, facet.vertex[2].x);
    stl->stats.min.x = STL_MIN(stl->stats.min.x, facet.vertex[2].x);
    stl->stats.max.y = STL_MAX(stl->stats.max.y, facet.vertex[2].y);
    stl->stats.min.y = STL_MIN(stl->stats.min.y, facet.vertex[2].y);
    stl->stats.max.z = STL_MAX(stl->stats.max.z, facet.vertex[2].z);
    stl->stats.min.z = STL_MIN(stl->stats.min.z, facet.vertex[2].z);
}

void
stl_close(stl_file *stl)
{
    if(stl->neighbors_start != NULL)
	free(stl->neighbors_start);
    if(stl->facet_start != NULL)
	free(stl->facet_start);
    if(stl->v_indices != NULL)
	free(stl->v_indices);
    if(stl->v_shared != NULL)
	free(stl->v_shared);
}

