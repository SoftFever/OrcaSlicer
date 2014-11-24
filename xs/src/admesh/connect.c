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


static void stl_match_neighbors_exact(stl_file *stl,
                                      stl_hash_edge *edge_a, stl_hash_edge *edge_b);
static void stl_match_neighbors_nearby(stl_file *stl,
                                       stl_hash_edge *edge_a, stl_hash_edge *edge_b);
static void stl_record_neighbors(stl_file *stl,
                                 stl_hash_edge *edge_a, stl_hash_edge *edge_b);
static void stl_initialize_facet_check_exact(stl_file *stl);
static void stl_initialize_facet_check_nearby(stl_file *stl);
static void stl_load_edge_exact(stl_file *stl, stl_hash_edge *edge,
                                stl_vertex *a, stl_vertex *b);
static int stl_load_edge_nearby(stl_file *stl, stl_hash_edge *edge,
                                stl_vertex *a, stl_vertex *b, float tolerance);
static void insert_hash_edge(stl_file *stl, stl_hash_edge edge,
                             void (*match_neighbors)(stl_file *stl,
                                 stl_hash_edge *edge_a, stl_hash_edge *edge_b));
static int stl_get_hash_for_edge(int M, stl_hash_edge *edge);
static int stl_compare_function(stl_hash_edge *edge_a, stl_hash_edge *edge_b);
static void stl_free_edges(stl_file *stl);
static void stl_remove_facet(stl_file *stl, int facet_number);
static void stl_change_vertices(stl_file *stl, int facet_num, int vnot,
                                stl_vertex new_vertex);
static void stl_which_vertices_to_change(stl_file *stl, stl_hash_edge *edge_a,
    stl_hash_edge *edge_b, int *facet1, int *vertex1,
    int *facet2, int *vertex2,
    stl_vertex *new_vertex1, stl_vertex *new_vertex2);
static void stl_remove_degenerate(stl_file *stl, int facet);
extern int stl_check_normal_vector(stl_file *stl,
                                   int facet_num, int normal_fix_flag);
static void stl_update_connects_remove_1(stl_file *stl, int facet_num);


void
stl_check_facets_exact(stl_file *stl) {
  /* This function builds the neighbors list.  No modifications are made
   *  to any of the facets.  The edges are said to match only if all six
   *  floats of the first edge matches all six floats of the second edge.
   */

  stl_hash_edge  edge;
  stl_facet      facet;
  int            i;
  int            j;

  if (stl->error) return;

  stl->stats.connected_edges = 0;
  stl->stats.connected_facets_1_edge = 0;
  stl->stats.connected_facets_2_edge = 0;
  stl->stats.connected_facets_3_edge = 0;

  stl_initialize_facet_check_exact(stl);

  for(i = 0; i < stl->stats.number_of_facets; i++) {
    facet = stl->facet_start[i];

    /* If any two of the three vertices are found to be exactally the same, call them degenerate and remove the facet. */
    if(   !memcmp(&facet.vertex[0], &facet.vertex[1],
                  sizeof(stl_vertex))
          || !memcmp(&facet.vertex[1], &facet.vertex[2],
                     sizeof(stl_vertex))
          || !memcmp(&facet.vertex[0], &facet.vertex[2],
                     sizeof(stl_vertex))) {
      stl->stats.degenerate_facets += 1;
      stl_remove_facet(stl, i);
      i--;
      continue;

    }
    for(j = 0; j < 3; j++) {
      edge.facet_number = i;
      edge.which_edge = j;
      stl_load_edge_exact(stl, &edge, &facet.vertex[j],
                          &facet.vertex[(j + 1) % 3]);

      insert_hash_edge(stl, edge, stl_match_neighbors_exact);
    }
  }
  stl_free_edges(stl);
}

static void
stl_load_edge_exact(stl_file *stl, stl_hash_edge *edge,
                    stl_vertex *a, stl_vertex *b) {

  float diff_x;
  float diff_y;
  float diff_z;
  float max_diff;

  if (stl->error) return;

  diff_x = ABS(a->x - b->x);
  diff_y = ABS(a->y - b->y);
  diff_z = ABS(a->z - b->z);
  max_diff = STL_MAX(diff_x, diff_y);
  max_diff = STL_MAX(diff_z, max_diff);
  stl->stats.shortest_edge = STL_MIN(max_diff, stl->stats.shortest_edge);

  if(diff_x == max_diff) {
    if(a->x > b->x) {
      memcpy(&edge->key[0], a, sizeof(stl_vertex));
      memcpy(&edge->key[3], b, sizeof(stl_vertex));
    } else {
      memcpy(&edge->key[0], b, sizeof(stl_vertex));
      memcpy(&edge->key[3], a, sizeof(stl_vertex));
      edge->which_edge += 3; /* this edge is loaded backwards */
    }
  } else if(diff_y == max_diff) {
    if(a->y > b->y) {
      memcpy(&edge->key[0], a, sizeof(stl_vertex));
      memcpy(&edge->key[3], b, sizeof(stl_vertex));
    } else {
      memcpy(&edge->key[0], b, sizeof(stl_vertex));
      memcpy(&edge->key[3], a, sizeof(stl_vertex));
      edge->which_edge += 3; /* this edge is loaded backwards */
    }
  } else {
    if(a->z > b->z) {
      memcpy(&edge->key[0], a, sizeof(stl_vertex));
      memcpy(&edge->key[3], b, sizeof(stl_vertex));
    } else {
      memcpy(&edge->key[0], b, sizeof(stl_vertex));
      memcpy(&edge->key[3], a, sizeof(stl_vertex));
      edge->which_edge += 3; /* this edge is loaded backwards */
    }
  }
}

static void
stl_initialize_facet_check_exact(stl_file *stl) {
  int i;

  if (stl->error) return;

  stl->stats.malloced = 0;
  stl->stats.freed = 0;
  stl->stats.collisions = 0;


  stl->M = 81397;

  for(i = 0; i < stl->stats.number_of_facets ; i++) {
    /* initialize neighbors list to -1 to mark unconnected edges */
    stl->neighbors_start[i].neighbor[0] = -1;
    stl->neighbors_start[i].neighbor[1] = -1;
    stl->neighbors_start[i].neighbor[2] = -1;
  }

  stl->heads = (stl_hash_edge**)calloc(stl->M, sizeof(*stl->heads));
  if(stl->heads == NULL) perror("stl_initialize_facet_check_exact");

  stl->tail = (stl_hash_edge*)malloc(sizeof(stl_hash_edge));
  if(stl->tail == NULL) perror("stl_initialize_facet_check_exact");

  stl->tail->next = stl->tail;

  for(i = 0; i < stl->M; i++) {
    stl->heads[i] = stl->tail;
  }
}

static void
insert_hash_edge(stl_file *stl, stl_hash_edge edge,
                 void (*match_neighbors)(stl_file *stl,
                     stl_hash_edge *edge_a, stl_hash_edge *edge_b)) {
  stl_hash_edge *link;
  stl_hash_edge *new_edge;
  stl_hash_edge *temp;
  int            chain_number;

  if (stl->error) return;

  chain_number = stl_get_hash_for_edge(stl->M, &edge);

  link = stl->heads[chain_number];

  if(link == stl->tail) {
    /* This list doesn't have any edges currently in it.  Add this one. */
    new_edge = (stl_hash_edge*)malloc(sizeof(stl_hash_edge));
    if(new_edge == NULL) perror("insert_hash_edge");
    stl->stats.malloced++;
    *new_edge = edge;
    new_edge->next = stl->tail;
    stl->heads[chain_number] = new_edge;
    return;
  } else  if(!stl_compare_function(&edge, link)) {
    /* This is a match.  Record result in neighbors list. */
    match_neighbors(stl, &edge, link);
    /* Delete the matched edge from the list. */
    stl->heads[chain_number] = link->next;
    free(link);
    stl->stats.freed++;
    return;
  } else {
    /* Continue through the rest of the list */
    for(;;) {
      if(link->next == stl->tail) {
        /* This is the last item in the list. Insert a new edge. */
        new_edge = (stl_hash_edge*)malloc(sizeof(stl_hash_edge));
        if(new_edge == NULL) perror("insert_hash_edge");
        stl->stats.malloced++;
        *new_edge = edge;
        new_edge->next = stl->tail;
        link->next = new_edge;
        stl->stats.collisions++;
        return;
      } else  if(!stl_compare_function(&edge, link->next)) {
        /* This is a match.  Record result in neighbors list. */
        match_neighbors(stl, &edge, link->next);

        /* Delete the matched edge from the list. */
        temp = link->next;
        link->next = link->next->next;
        free(temp);
        stl->stats.freed++;
        return;
      } else {
        /* This is not a match.  Go to the next link */
        link = link->next;
        stl->stats.collisions++;
      }
    }
  }
}


static int
stl_get_hash_for_edge(int M, stl_hash_edge *edge) {
  return ((edge->key[0] / 23 + edge->key[1] / 19 + edge->key[2] / 17
           + edge->key[3] /13  + edge->key[4] / 11 + edge->key[5] / 7 ) % M);
}

static int
stl_compare_function(stl_hash_edge *edge_a, stl_hash_edge *edge_b) {
  if(edge_a->facet_number == edge_b->facet_number) {
    return 1;			/* Don't match edges of the same facet */
  } else {
    return memcmp(edge_a, edge_b, SIZEOF_EDGE_SORT);
  }
}

void
stl_check_facets_nearby(stl_file *stl, float tolerance) {
  stl_hash_edge  edge[3];
  stl_facet      facet;
  int            i;
  int            j;

  if (stl->error) return;

  if(   (stl->stats.connected_facets_1_edge == stl->stats.number_of_facets)
        && (stl->stats.connected_facets_2_edge == stl->stats.number_of_facets)
        && (stl->stats.connected_facets_3_edge == stl->stats.number_of_facets)) {
    /* No need to check any further.  All facets are connected */
    return;
  }

  stl_initialize_facet_check_nearby(stl);

  for(i = 0; i < stl->stats.number_of_facets; i++) {
    facet = stl->facet_start[i];
    for(j = 0; j < 3; j++) {
      if(stl->neighbors_start[i].neighbor[j] == -1) {
        edge[j].facet_number = i;
        edge[j].which_edge = j;
        if(stl_load_edge_nearby(stl, &edge[j], &facet.vertex[j],
                                &facet.vertex[(j + 1) % 3],
                                tolerance)) {
          /* only insert edges that have different keys */
          insert_hash_edge(stl, edge[j], stl_match_neighbors_nearby);
        }
      }
    }
  }

  stl_free_edges(stl);
}

static int
stl_load_edge_nearby(stl_file *stl, stl_hash_edge *edge,
                     stl_vertex *a, stl_vertex *b, float tolerance) {
  float diff_x;
  float diff_y;
  float diff_z;
  float max_diff;
  unsigned vertex1[3];
  unsigned vertex2[3];


  diff_x = ABS(a->x - b->x);
  diff_y = ABS(a->y - b->y);
  diff_z = ABS(a->z - b->z);
  max_diff = STL_MAX(diff_x, diff_y);
  max_diff = STL_MAX(diff_z, max_diff);

  vertex1[0] = (unsigned)((a->x - stl->stats.min.x) / tolerance);
  vertex1[1] = (unsigned)((a->y - stl->stats.min.y) / tolerance);
  vertex1[2] = (unsigned)((a->z - stl->stats.min.z) / tolerance);
  vertex2[0] = (unsigned)((b->x - stl->stats.min.x) / tolerance);
  vertex2[1] = (unsigned)((b->y - stl->stats.min.y) / tolerance);
  vertex2[2] = (unsigned)((b->z - stl->stats.min.z) / tolerance);

  if(   (vertex1[0] == vertex2[0])
        && (vertex1[1] == vertex2[1])
        && (vertex1[2] == vertex2[2])) {
    /* Both vertices hash to the same value */
    return 0;
  }

  if(diff_x == max_diff) {
    if(a->x > b->x) {
      memcpy(&edge->key[0], vertex1, sizeof(stl_vertex));
      memcpy(&edge->key[3], vertex2, sizeof(stl_vertex));
    } else {
      memcpy(&edge->key[0], vertex2, sizeof(stl_vertex));
      memcpy(&edge->key[3], vertex1, sizeof(stl_vertex));
      edge->which_edge += 3; /* this edge is loaded backwards */
    }
  } else if(diff_y == max_diff) {
    if(a->y > b->y) {
      memcpy(&edge->key[0], vertex1, sizeof(stl_vertex));
      memcpy(&edge->key[3], vertex2, sizeof(stl_vertex));
    } else {
      memcpy(&edge->key[0], vertex2, sizeof(stl_vertex));
      memcpy(&edge->key[3], vertex1, sizeof(stl_vertex));
      edge->which_edge += 3; /* this edge is loaded backwards */
    }
  } else {
    if(a->z > b->z) {
      memcpy(&edge->key[0], vertex1, sizeof(stl_vertex));
      memcpy(&edge->key[3], vertex2, sizeof(stl_vertex));
    } else {
      memcpy(&edge->key[0], vertex2, sizeof(stl_vertex));
      memcpy(&edge->key[3], vertex1, sizeof(stl_vertex));
      edge->which_edge += 3; /* this edge is loaded backwards */
    }
  }
  return 1;
}

static void
stl_free_edges(stl_file *stl) {
  int i;
  stl_hash_edge *temp;

  if (stl->error) return;

  if(stl->stats.malloced != stl->stats.freed) {
    for(i = 0; i < stl->M; i++) {
      for(temp = stl->heads[i]; stl->heads[i] != stl->tail;
          temp = stl->heads[i]) {
        stl->heads[i] = stl->heads[i]->next;
        free(temp);
        stl->stats.freed++;
      }
    }
  }
  free(stl->heads);
  free(stl->tail);
}

static void
stl_initialize_facet_check_nearby(stl_file *stl) {
  int i;

  if (stl->error) return;

  stl->stats.malloced = 0;
  stl->stats.freed = 0;
  stl->stats.collisions = 0;

  /*  tolerance = STL_MAX(stl->stats.shortest_edge, tolerance);*/
  /*  tolerance = STL_MAX((stl->stats.bounding_diameter / 500000.0), tolerance);*/
  /*  tolerance *= 0.5;*/

  stl->M = 81397;

  stl->heads = (stl_hash_edge**)calloc(stl->M, sizeof(*stl->heads));
  if(stl->heads == NULL) perror("stl_initialize_facet_check_nearby");

  stl->tail = (stl_hash_edge*)malloc(sizeof(stl_hash_edge));
  if(stl->tail == NULL) perror("stl_initialize_facet_check_nearby");

  stl->tail->next = stl->tail;

  for(i = 0; i < stl->M; i++) {
    stl->heads[i] = stl->tail;
  }
}



static void
stl_record_neighbors(stl_file *stl,
                     stl_hash_edge *edge_a, stl_hash_edge *edge_b) {
  int i;
  int j;

  if (stl->error) return;

  /* Facet a's neighbor is facet b */
  stl->neighbors_start[edge_a->facet_number].neighbor[edge_a->which_edge % 3] =
    edge_b->facet_number;	/* sets the .neighbor part */

  stl->neighbors_start[edge_a->facet_number].
  which_vertex_not[edge_a->which_edge % 3] =
    (edge_b->which_edge + 2) % 3; /* sets the .which_vertex_not part */

  /* Facet b's neighbor is facet a */
  stl->neighbors_start[edge_b->facet_number].neighbor[edge_b->which_edge % 3] =
    edge_a->facet_number;	/* sets the .neighbor part */

  stl->neighbors_start[edge_b->facet_number].
  which_vertex_not[edge_b->which_edge % 3] =
    (edge_a->which_edge + 2) % 3; /* sets the .which_vertex_not part */

  if(   ((edge_a->which_edge < 3) && (edge_b->which_edge < 3))
        || ((edge_a->which_edge > 2) && (edge_b->which_edge > 2))) {
    /* these facets are oriented in opposite directions.  */
    /*  their normals are probably messed up. */
    stl->neighbors_start[edge_a->facet_number].
    which_vertex_not[edge_a->which_edge % 3] += 3;
    stl->neighbors_start[edge_b->facet_number].
    which_vertex_not[edge_b->which_edge % 3] += 3;
  }


  /* Count successful connects */
  /* Total connects */
  stl->stats.connected_edges += 2;
  /* Count individual connects */
  i = ((stl->neighbors_start[edge_a->facet_number].neighbor[0] == -1) +
       (stl->neighbors_start[edge_a->facet_number].neighbor[1] == -1) +
       (stl->neighbors_start[edge_a->facet_number].neighbor[2] == -1));
  j = ((stl->neighbors_start[edge_b->facet_number].neighbor[0] == -1) +
       (stl->neighbors_start[edge_b->facet_number].neighbor[1] == -1) +
       (stl->neighbors_start[edge_b->facet_number].neighbor[2] == -1));
  if(i == 2) {
    stl->stats.connected_facets_1_edge +=1;
  } else if(i == 1) {
    stl->stats.connected_facets_2_edge +=1;
  } else {
    stl->stats.connected_facets_3_edge +=1;
  }
  if(j == 2) {
    stl->stats.connected_facets_1_edge +=1;
  } else if(j == 1) {
    stl->stats.connected_facets_2_edge +=1;
  } else {
    stl->stats.connected_facets_3_edge +=1;
  }
}

static void
stl_match_neighbors_exact(stl_file *stl,
                          stl_hash_edge *edge_a, stl_hash_edge *edge_b) {
  if (stl->error) return;
  stl_record_neighbors(stl, edge_a, edge_b);
}

static void
stl_match_neighbors_nearby(stl_file *stl,
                           stl_hash_edge *edge_a, stl_hash_edge *edge_b) {
  int facet1;
  int facet2;
  int vertex1;
  int vertex2;
  int vnot1;
  int vnot2;
  stl_vertex new_vertex1;
  stl_vertex new_vertex2;

  if (stl->error) return;

  stl_record_neighbors(stl, edge_a, edge_b);
  stl_which_vertices_to_change(stl, edge_a, edge_b, &facet1, &vertex1,
                               &facet2, &vertex2, &new_vertex1, &new_vertex2);
  if(facet1 != -1) {
    if(facet1 == edge_a->facet_number) {
      vnot1 = (edge_a->which_edge + 2) % 3;
    } else {
      vnot1 = (edge_b->which_edge + 2) % 3;
    }
    if(((vnot1 + 2) % 3) == vertex1) {
      vnot1 += 3;
    }
    stl_change_vertices(stl, facet1, vnot1, new_vertex1);
  }
  if(facet2 != -1) {
    if(facet2 == edge_a->facet_number) {
      vnot2 = (edge_a->which_edge + 2) % 3;
    } else {
      vnot2 = (edge_b->which_edge + 2) % 3;
    }
    if(((vnot2 + 2) % 3) == vertex2) {
      vnot2 += 3;
    }
    stl_change_vertices(stl, facet2, vnot2, new_vertex2);
  }
  stl->stats.edges_fixed += 2;
}


static void
stl_change_vertices(stl_file *stl, int facet_num, int vnot,
                    stl_vertex new_vertex) {
  int first_facet;
  int direction;
  int next_edge;
  int pivot_vertex;

  if (stl->error) return;

  first_facet = facet_num;
  direction = 0;

  for(;;) {
    if(vnot > 2) {
      if(direction == 0) {
        pivot_vertex = (vnot + 2) % 3;
        next_edge = pivot_vertex;
        direction = 1;
      } else {
        pivot_vertex = (vnot + 1) % 3;
        next_edge = vnot % 3;
        direction = 0;
      }
    } else {
      if(direction == 0) {
        pivot_vertex = (vnot + 1) % 3;
        next_edge = vnot;
      } else {
        pivot_vertex = (vnot + 2) % 3;
        next_edge = pivot_vertex;
      }
    }
    stl->facet_start[facet_num].vertex[pivot_vertex] = new_vertex;
    vnot = stl->neighbors_start[facet_num].which_vertex_not[next_edge];
    facet_num = stl->neighbors_start[facet_num].neighbor[next_edge];

    if(facet_num == -1) {
      break;
    }

    if(facet_num == first_facet) {
      /* back to the beginning */
      printf("\
Back to the first facet changing vertices: probably a mobius part.\n\
Try using a smaller tolerance or don't do a nearby check\n");
      return;
    }
  }
}


static void
stl_which_vertices_to_change(stl_file *stl, stl_hash_edge *edge_a,
                             stl_hash_edge *edge_b, int *facet1, int *vertex1,
                             int *facet2, int *vertex2,
                             stl_vertex *new_vertex1, stl_vertex *new_vertex2) {
  int v1a;			/* pair 1, facet a */
  int v1b;			/* pair 1, facet b */
  int v2a;			/* pair 2, facet a */
  int v2b;			/* pair 2, facet b */

  /* Find first pair */
  if(edge_a->which_edge < 3) {
    v1a = edge_a->which_edge;
    v2a = (edge_a->which_edge + 1) % 3;
  } else {
    v2a = edge_a->which_edge % 3;
    v1a = (edge_a->which_edge + 1) % 3;
  }
  if(edge_b->which_edge < 3) {
    v1b = edge_b->which_edge;
    v2b = (edge_b->which_edge + 1) % 3;
  } else {
    v2b = edge_b->which_edge % 3;
    v1b = (edge_b->which_edge + 1) % 3;
  }

  /* Of the first pair, which vertex, if any, should be changed */
  if(!memcmp(&stl->facet_start[edge_a->facet_number].vertex[v1a],
             &stl->facet_start[edge_b->facet_number].vertex[v1b],
             sizeof(stl_vertex))) {
    /* These facets are already equal.  No need to change. */
    *facet1 = -1;
  } else {
    if(   (stl->neighbors_start[edge_a->facet_number].neighbor[v1a] == -1)
          && (stl->neighbors_start[edge_a->facet_number].
              neighbor[(v1a + 2) % 3] == -1)) {
      /* This vertex has no neighbors.  This is a good one to change */
      *facet1 = edge_a->facet_number;
      *vertex1 = v1a;
      *new_vertex1 = stl->facet_start[edge_b->facet_number].vertex[v1b];
    } else {
      *facet1 = edge_b->facet_number;
      *vertex1 = v1b;
      *new_vertex1 = stl->facet_start[edge_a->facet_number].vertex[v1a];
    }
  }

  /* Of the second pair, which vertex, if any, should be changed */
  if(!memcmp(&stl->facet_start[edge_a->facet_number].vertex[v2a],
             &stl->facet_start[edge_b->facet_number].vertex[v2b],
             sizeof(stl_vertex))) {
    /* These facets are already equal.  No need to change. */
    *facet2 = -1;
  } else {
    if(   (stl->neighbors_start[edge_a->facet_number].neighbor[v2a] == -1)
          && (stl->neighbors_start[edge_a->facet_number].
              neighbor[(v2a + 2) % 3] == -1)) {
      /* This vertex has no neighbors.  This is a good one to change */
      *facet2 = edge_a->facet_number;
      *vertex2 = v2a;
      *new_vertex2 = stl->facet_start[edge_b->facet_number].vertex[v2b];
    } else {
      *facet2 = edge_b->facet_number;
      *vertex2 = v2b;
      *new_vertex2 = stl->facet_start[edge_a->facet_number].vertex[v2a];
    }
  }
}

static void
stl_remove_facet(stl_file *stl, int facet_number) {
  int neighbor[3];
  int vnot[3];
  int i;
  int j;

  if (stl->error) return;

  stl->stats.facets_removed += 1;
  /* Update list of connected edges */
  j = ((stl->neighbors_start[facet_number].neighbor[0] == -1) +
       (stl->neighbors_start[facet_number].neighbor[1] == -1) +
       (stl->neighbors_start[facet_number].neighbor[2] == -1));
  if(j == 2) {
    stl->stats.connected_facets_1_edge -= 1;
  } else if(j == 1) {
    stl->stats.connected_facets_2_edge -= 1;
    stl->stats.connected_facets_1_edge -= 1;
  } else if(j == 0) {
    stl->stats.connected_facets_3_edge -= 1;
    stl->stats.connected_facets_2_edge -= 1;
    stl->stats.connected_facets_1_edge -= 1;
  }

  stl->facet_start[facet_number] =
    stl->facet_start[stl->stats.number_of_facets - 1];
  /* I could reallocate at this point, but it is not really necessary. */
  stl->neighbors_start[facet_number] =
    stl->neighbors_start[stl->stats.number_of_facets - 1];
  stl->stats.number_of_facets -= 1;

  for(i = 0; i < 3; i++) {
    neighbor[i] = stl->neighbors_start[facet_number].neighbor[i];
    vnot[i] = stl->neighbors_start[facet_number].which_vertex_not[i];
  }

  for(i = 0; i < 3; i++) {
    if(neighbor[i] != -1) {
      if(stl->neighbors_start[neighbor[i]].neighbor[(vnot[i] + 1)% 3] !=
          stl->stats.number_of_facets) {
        printf("\
in stl_remove_facet: neighbor = %d numfacets = %d this is wrong\n",
               stl->neighbors_start[neighbor[i]].neighbor[(vnot[i] + 1)% 3],
               stl->stats.number_of_facets);
        return;
      }
      stl->neighbors_start[neighbor[i]].neighbor[(vnot[i] + 1)% 3]
        = facet_number;
    }
  }
}

void
stl_remove_unconnected_facets(stl_file *stl) {
  /* A couple of things need to be done here.  One is to remove any */
  /* completely unconnected facets (0 edges connected) since these are */
  /* useless and could be completely wrong.   The second thing that needs to */
  /* be done is to remove any degenerate facets that were created during */
  /* stl_check_facets_nearby(). */

  int i;

  if (stl->error) return;

  /* remove degenerate facets */
  for(i = 0; i < stl->stats.number_of_facets; i++) {
    if(   !memcmp(&stl->facet_start[i].vertex[0],
                  &stl->facet_start[i].vertex[1], sizeof(stl_vertex))
          || !memcmp(&stl->facet_start[i].vertex[1],
                     &stl->facet_start[i].vertex[2], sizeof(stl_vertex))
          || !memcmp(&stl->facet_start[i].vertex[0],
                     &stl->facet_start[i].vertex[2], sizeof(stl_vertex))) {
      stl_remove_degenerate(stl, i);
      i--;
    }
  }

  if(stl->stats.connected_facets_1_edge < stl->stats.number_of_facets) {
    /* remove completely unconnected facets */
    for(i = 0; i < stl->stats.number_of_facets; i++) {
      if(   (stl->neighbors_start[i].neighbor[0] == -1)
            && (stl->neighbors_start[i].neighbor[1] == -1)
            && (stl->neighbors_start[i].neighbor[2] == -1)) {
        /* This facet is completely unconnected.  Remove it. */
        stl_remove_facet(stl, i);
        i--;
      }
    }
  }
}

static void
stl_remove_degenerate(stl_file *stl, int facet) {
  int edge1;
  int edge2;
  int edge3;
  int neighbor1;
  int neighbor2;
  int neighbor3;
  int vnot1;
  int vnot2;
  int vnot3;

  if (stl->error) return;

  if(   !memcmp(&stl->facet_start[facet].vertex[0],
                &stl->facet_start[facet].vertex[1], sizeof(stl_vertex))
        && !memcmp(&stl->facet_start[facet].vertex[1],
                   &stl->facet_start[facet].vertex[2], sizeof(stl_vertex))) {
    /* all 3 vertices are equal.  Just remove the facet.  I don't think*/
    /* this is really possible, but just in case... */
    printf("removing a facet in stl_remove_degenerate\n");

    stl_remove_facet(stl, facet);
    return;
  }

  if(!memcmp(&stl->facet_start[facet].vertex[0],
             &stl->facet_start[facet].vertex[1], sizeof(stl_vertex))) {
    edge1 = 1;
    edge2 = 2;
    edge3 = 0;
  } else if(!memcmp(&stl->facet_start[facet].vertex[1],
                    &stl->facet_start[facet].vertex[2], sizeof(stl_vertex))) {
    edge1 = 0;
    edge2 = 2;
    edge3 = 1;
  } else if(!memcmp(&stl->facet_start[facet].vertex[2],
                    &stl->facet_start[facet].vertex[0], sizeof(stl_vertex))) {
    edge1 = 0;
    edge2 = 1;
    edge3 = 2;
  } else {
    /* No degenerate. Function shouldn't have been called. */
    return;
  }
  neighbor1 = stl->neighbors_start[facet].neighbor[edge1];
  neighbor2 = stl->neighbors_start[facet].neighbor[edge2];

  if(neighbor1 == -1) {
    stl_update_connects_remove_1(stl, neighbor2);
  }
  if(neighbor2 == -1) {
    stl_update_connects_remove_1(stl, neighbor1);
  }


  neighbor3 = stl->neighbors_start[facet].neighbor[edge3];
  vnot1 = stl->neighbors_start[facet].which_vertex_not[edge1];
  vnot2 = stl->neighbors_start[facet].which_vertex_not[edge2];
  vnot3 = stl->neighbors_start[facet].which_vertex_not[edge3];

  stl->neighbors_start[neighbor1].neighbor[(vnot1 + 1) % 3] = neighbor2;
  stl->neighbors_start[neighbor2].neighbor[(vnot2 + 1) % 3] = neighbor1;
  stl->neighbors_start[neighbor1].which_vertex_not[(vnot1 + 1) % 3] = vnot2;
  stl->neighbors_start[neighbor2].which_vertex_not[(vnot2 + 1) % 3] = vnot1;

  stl_remove_facet(stl, facet);

  if(neighbor3 != -1) {
    stl_update_connects_remove_1(stl, neighbor3);
    stl->neighbors_start[neighbor3].neighbor[(vnot3 + 1) % 3] = -1;
  }
}

void
stl_update_connects_remove_1(stl_file *stl, int facet_num) {
  int j;

  if (stl->error) return;
  /* Update list of connected edges */
  j = ((stl->neighbors_start[facet_num].neighbor[0] == -1) +
       (stl->neighbors_start[facet_num].neighbor[1] == -1) +
       (stl->neighbors_start[facet_num].neighbor[2] == -1));
  if(j == 0) {		       /* Facet has 3 neighbors */
    stl->stats.connected_facets_3_edge -= 1;
  } else if(j == 1) {	     /* Facet has 2 neighbors */
    stl->stats.connected_facets_2_edge -= 1;
  } else if(j == 2) {	     /* Facet has 1 neighbor  */
    stl->stats.connected_facets_1_edge -= 1;
  }
}

void
stl_fill_holes(stl_file *stl) {
  stl_facet facet;
  stl_facet new_facet;
  int neighbors_initial[3];
  stl_hash_edge edge;
  int first_facet;
  int direction;
  int facet_num;
  int vnot;
  int next_edge;
  int pivot_vertex;
  int next_facet;
  int i;
  int j;
  int k;

  if (stl->error) return;

  /* Insert all unconnected edges into hash list */
  stl_initialize_facet_check_nearby(stl);
  for(i = 0; i < stl->stats.number_of_facets; i++) {
    facet = stl->facet_start[i];
    for(j = 0; j < 3; j++) {
      if(stl->neighbors_start[i].neighbor[j] != -1) continue;
      edge.facet_number = i;
      edge.which_edge = j;
      stl_load_edge_exact(stl, &edge, &facet.vertex[j],
                          &facet.vertex[(j + 1) % 3]);

      insert_hash_edge(stl, edge, stl_match_neighbors_exact);
    }
  }

  for(i = 0; i < stl->stats.number_of_facets; i++) {
    facet = stl->facet_start[i];
    neighbors_initial[0] = stl->neighbors_start[i].neighbor[0];
    neighbors_initial[1] = stl->neighbors_start[i].neighbor[1];
    neighbors_initial[2] = stl->neighbors_start[i].neighbor[2];
    first_facet = i;
    for(j = 0; j < 3; j++) {
      if(stl->neighbors_start[i].neighbor[j] != -1) continue;

      new_facet.vertex[0] = facet.vertex[j];
      new_facet.vertex[1] = facet.vertex[(j + 1) % 3];
      if(neighbors_initial[(j + 2) % 3] == -1) {
        direction = 1;
      } else {
        direction = 0;
      }

      facet_num = i;
      vnot = (j + 2) % 3;

      for(;;) {
        if(vnot > 2) {
          if(direction == 0) {
            pivot_vertex = (vnot + 2) % 3;
            next_edge = pivot_vertex;
            direction = 1;
          } else {
            pivot_vertex = (vnot + 1) % 3;
            next_edge = vnot % 3;
            direction = 0;
          }
        } else {
          if(direction == 0) {
            pivot_vertex = (vnot + 1) % 3;
            next_edge = vnot;
          } else {
            pivot_vertex = (vnot + 2) % 3;
            next_edge = pivot_vertex;
          }
        }
        next_facet = stl->neighbors_start[facet_num].neighbor[next_edge];

        if(next_facet == -1) {
          new_facet.vertex[2] = stl->facet_start[facet_num].
                                vertex[vnot % 3];
          stl_add_facet(stl, &new_facet);
          for(k = 0; k < 3; k++) {
            edge.facet_number = stl->stats.number_of_facets - 1;
            edge.which_edge = k;
            stl_load_edge_exact(stl, &edge, &new_facet.vertex[k],
                                &new_facet.vertex[(k + 1) % 3]);

            insert_hash_edge(stl, edge, stl_match_neighbors_exact);
          }
          break;
        } else {
          vnot = stl->neighbors_start[facet_num].
                 which_vertex_not[next_edge];
          facet_num = next_facet;
        }

        if(facet_num == first_facet) {
          /* back to the beginning */
          printf("\
Back to the first facet filling holes: probably a mobius part.\n\
Try using a smaller tolerance or don't do a nearby check\n");
          return;
        }
      }
    }
  }
}

void
stl_add_facet(stl_file *stl, stl_facet *new_facet) {
  if (stl->error) return;

  stl->stats.facets_added += 1;
  if(stl->stats.facets_malloced < stl->stats.number_of_facets + 1) {
    stl->facet_start = (stl_facet*)realloc(stl->facet_start,
                                           (sizeof(stl_facet) * (stl->stats.facets_malloced + 256)));
    if(stl->facet_start == NULL) perror("stl_add_facet");
    stl->neighbors_start = (stl_neighbors*)realloc(stl->neighbors_start,
                           (sizeof(stl_neighbors) * (stl->stats.facets_malloced + 256)));
    if(stl->neighbors_start == NULL) perror("stl_add_facet");
    stl->stats.facets_malloced += 256;
  }
  stl->facet_start[stl->stats.number_of_facets] = *new_facet;

  /* note that the normal vector is not set here, just initialized to 0 */
  stl->facet_start[stl->stats.number_of_facets].normal.x = 0.0;
  stl->facet_start[stl->stats.number_of_facets].normal.y = 0.0;
  stl->facet_start[stl->stats.number_of_facets].normal.z = 0.0;

  stl->neighbors_start[stl->stats.number_of_facets].neighbor[0] = -1;
  stl->neighbors_start[stl->stats.number_of_facets].neighbor[1] = -1;
  stl->neighbors_start[stl->stats.number_of_facets].neighbor[2] = -1;
  stl->stats.number_of_facets += 1;
}
