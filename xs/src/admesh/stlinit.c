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
stl_open(stl_file *stl, char *file) {
  stl_initialize(stl);
  stl_count_facets(stl, file);
  stl_allocate(stl);
  stl_read(stl, 0, 1);
  if (!stl->error) fclose(stl->fp);
}


void
stl_initialize(stl_file *stl) {
  stl->error = 0;
  stl->stats.degenerate_facets = 0;
  stl->stats.edges_fixed  = 0;
  stl->stats.facets_added = 0;
  stl->stats.facets_removed = 0;
  stl->stats.facets_reversed = 0;
  stl->stats.normals_fixed = 0;
  stl->stats.number_of_parts = 0;
  stl->stats.original_num_facets = 0;
  stl->stats.number_of_facets = 0;
  stl->stats.facets_malloced = 0;
  stl->stats.volume = -1.0;

  stl->neighbors_start = NULL;
  stl->facet_start = NULL;
  stl->v_indices = NULL;
  stl->v_shared = NULL;
}

void
stl_count_facets(stl_file *stl, char *file) {
  long           file_size;
  int            header_num_facets;
  int            num_facets;
  int            i, j;
  size_t         s;
  unsigned char  chtest[128];
  int            num_lines = 1;
  char           *error_msg;

  if (stl->error) return;

  /* Open the file in binary mode first */
  stl->fp = fopen(file, "rb");
  if(stl->fp == NULL) {
    error_msg = (char*)
                malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
    sprintf(error_msg, "stl_initialize: Couldn't open %s for reading",
            file);
    perror(error_msg);
    free(error_msg);
    stl->error = 1;
    return;
  }
  /* Find size of file */
  fseek(stl->fp, 0, SEEK_END);
  file_size = ftell(stl->fp);

  /* Check for binary or ASCII file */
  fseek(stl->fp, HEADER_SIZE, SEEK_SET);
  if (!fread(chtest, sizeof(chtest), 1, stl->fp)) {
    perror("The input is an empty file");
    stl->error = 1;
    return;
  }
  stl->stats.type = ascii;
  for(s = 0; s < sizeof(chtest); s++) {
    if(chtest[s] > 127) {
      stl->stats.type = binary;
      break;
    }
  }
  rewind(stl->fp);

  /* Get the header and the number of facets in the .STL file */
  /* If the .STL file is binary, then do the following */
  if(stl->stats.type == binary) {
    /* Test if the STL file has the right size  */
    if(((file_size - HEADER_SIZE) % SIZEOF_STL_FACET != 0)
        || (file_size < STL_MIN_FILE_SIZE)) {
      fprintf(stderr, "The file %s has the wrong size.\n", file);
      stl->error = 1;
      return;
    }
    num_facets = (file_size - HEADER_SIZE) / SIZEOF_STL_FACET;

    /* Read the header */
    if (fread(stl->stats.header, LABEL_SIZE, 1, stl->fp) > 79) {
      stl->stats.header[80] = '\0';
    }

    /* Read the int following the header.  This should contain # of facets */
    if((!fread(&header_num_facets, sizeof(int), 1, stl->fp)) || (num_facets != header_num_facets)) {
      fprintf(stderr,
              "Warning: File size doesn't match number of facets in the header\n");
    }
  }
  /* Otherwise, if the .STL file is ASCII, then do the following */
  else {
    /* Reopen the file in text mode (for getting correct newlines on Windows) */
    // fix to silence a warning about unused return value.
    // obviously if it fails we have problems....
    stl->fp = freopen(file, "r", stl->fp);

    // do another null check to be safe
    if(stl->fp == NULL) {
      error_msg = (char*)
        malloc(81 + strlen(file)); /* Allow 80 chars+file size for message */
      sprintf(error_msg, "stl_initialize: Couldn't open %s for reading",
          file);
      perror(error_msg);
      free(error_msg);
      stl->error = 1;
      return;
    }
    
    /* Find the number of facets */
    j = 0;
    for(i = 0; i < file_size ; i++) {
      j++;
      if(getc(stl->fp) == '\n') {
        if(j > 4) { /* don't count short lines */
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
stl_allocate(stl_file *stl) {
  if (stl->error) return;

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
stl_open_merge(stl_file *stl, char *file_to_merge) {
  int num_facets_so_far;
  stl_type origStlType;
  FILE *origFp;
  stl_file stl_to_merge;

  if (stl->error) return;

  /* Record how many facets we have so far from the first file.  We will start putting
     facets in the next position.  Since we're 0-indexed, it'l be the same position. */
  num_facets_so_far = stl->stats.number_of_facets;

  /* Record the file type we started with: */
  origStlType=stl->stats.type;
  /* Record the file pointer too: */
  origFp=stl->fp;

  /* Initialize the sturucture with zero stats, header info and sizes: */
  stl_initialize(&stl_to_merge);
  stl_count_facets(&stl_to_merge, file_to_merge);

  /* Copy what we need to into stl so that we can read the file_to_merge directly into it
     using stl_read:  Save the rest of the valuable info: */
  stl->stats.type=stl_to_merge.stats.type;
  stl->fp=stl_to_merge.fp;

  /* Add the number of facets we already have in stl with what we we found in stl_to_merge but
     haven't read yet. */
  stl->stats.number_of_facets=num_facets_so_far+stl_to_merge.stats.number_of_facets;

  /* Allocate enough room for stl->stats.number_of_facets facets and neighbors: */
  stl_reallocate(stl);

  /* Read the file to merge directly into stl, adding it to what we have already.
     Start at num_facets_so_far, the index to the first unused facet.  Also say
     that this isn't our first time so we should augment stats like min and max
     instead of erasing them. */
  stl_read(stl, num_facets_so_far, 0);

  /* Restore the stl information we overwrote (for stl_read) so that it still accurately
     reflects the subject part: */
  stl->stats.type=origStlType;
  stl->fp=origFp;
}

extern void
stl_reallocate(stl_file *stl) {
  if (stl->error) return;
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


/* Reads the contents of the file pointed to by stl->fp into the stl structure,
   starting at facet first_facet.  The second argument says if it's our first
   time running this for the stl and therefore we should reset our max and min stats. */
void
stl_read(stl_file *stl, int first_facet, int first) {
  stl_facet facet;
  int   i;

  if (stl->error) return;

  if(stl->stats.type == binary) {
    fseek(stl->fp, HEADER_SIZE, SEEK_SET);
  } else {
    rewind(stl->fp);
    /* Skip the first line of the file */
    while(getc(stl->fp) != '\n');
  }

  for(i = first_facet; i < stl->stats.number_of_facets; i++) {
    if(stl->stats.type == binary)
      /* Read a single facet from a binary .STL file */
    {
      /* we assume little-endian architecture! */
      if (fread(&facet.normal, sizeof(stl_normal), 1, stl->fp) \
          + fread(&facet.vertex, sizeof(stl_vertex), 3, stl->fp) \
          + fread(&facet.extra, sizeof(char), 2, stl->fp) != 6) {
        perror("Cannot read facet");
        stl->error = 1;
        return;
      }
    } else
      /* Read a single facet from an ASCII .STL file */
    {
      if((fscanf(stl->fp, "%*s %*s %f %f %f\n", &facet.normal.x, &facet.normal.y, &facet.normal.z) + \
          fscanf(stl->fp, "%*s %*s") + \
          fscanf(stl->fp, "%*s %f %f %f\n", &facet.vertex[0].x, &facet.vertex[0].y,  &facet.vertex[0].z) + \
          fscanf(stl->fp, "%*s %f %f %f\n", &facet.vertex[1].x, &facet.vertex[1].y,  &facet.vertex[1].z) + \
          fscanf(stl->fp, "%*s %f %f %f\n", &facet.vertex[2].x, &facet.vertex[2].y,  &facet.vertex[2].z) + \
          fscanf(stl->fp, "%*s") + \
          fscanf(stl->fp, "%*s")) != 12) {
        perror("Something is syntactically very wrong with this ASCII STL!");
        stl->error = 1;
        return;
      }
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
stl_facet_stats(stl_file *stl, stl_facet facet, int first) {
  float diff_x;
  float diff_y;
  float diff_z;
  float max_diff;

  if (stl->error) return;

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
stl_close(stl_file *stl) {
  if (stl->error) return;

  if(stl->neighbors_start != NULL)
    free(stl->neighbors_start);
  if(stl->facet_start != NULL)
    free(stl->facet_start);
  if(stl->v_indices != NULL)
    free(stl->v_indices);
  if(stl->v_shared != NULL)
    free(stl->v_shared);
}

