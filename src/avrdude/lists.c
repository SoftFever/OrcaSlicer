/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 1990-2004  Brian S. Dean <bsd@bsdhome.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

 /* $Id$ */



/*----------------------------------------------------------------------
  Id: lists.c,v 1.4 2001/08/19 23:26:20 bsd Exp $
  ----------------------------------------------------------------------*/
/*------------------------------------------------------------------------
  lists.c

  General purpose linked list routines.  These routines implement a
  generic doubly linked list.  Any data type may be placed in the
  lists.  Stacking and Queuing routines are provided via #defines
  declared in 'lists.h'.

  Author : Brian Dean
  Date   : 10 January, 1990
  ------------------------------------------------------------------------*/

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>

#include "libavrdude.h"

#define MAGIC 0xb05b05b0

#define CHECK_MAGIC 0 /* set to 1 to enable memory overwrite detection */

#ifdef BOS
#define MALLOC(size,x) kmalloc(size,x)
#define FREE           kfree
#else
#define MALLOC(size,x) malloc(size)
#define FREE           free
#endif


/*------------------------------------------------------------
|  required private data structures
 ------------------------------------------------------------*/
typedef struct LISTNODE {
#if CHECK_MAGIC
  unsigned int      magic1;
#endif
  struct LISTNODE * next; /* chain to next item in the list */
  struct LISTNODE * prev; /* chain to previous item in the list */
  void            * data; /* pointer to user data */
#if CHECK_MAGIC
  unsigned int      magic2;
#endif
} LISTNODE;


typedef struct NODEPOOL {
#if CHECK_MAGIC
  unsigned int      magic1;
#endif
  struct NODEPOOL * chain_next;  /* chain to next node pool */
  struct NODEPOOL * chain_prev;  /* chain to previous node pool */
#if CHECK_MAGIC
  unsigned int      magic2;
#endif
} NODEPOOL;


typedef struct LIST {
#if CHECK_MAGIC
  unsigned int magic1;
#endif
  int        num;           /* number of elements in the list    */
  short int  free_on_close; /* free the LIST memory on close T/F */
  short int  poolsize;      /* list node allocation size         */
  int        n_ln_pool;     /* number of listnodes in a pool     */
  LISTNODE * top;           /* top of the list                   */
  LISTNODE * bottom;        /* bottom of the list                */
  LISTNODE * next_ln;       /* next available list node          */
  NODEPOOL * np_top;        /* top of the node pool chain        */
  NODEPOOL * np_bottom;     /* bottom of the node pool chain     */
#if CHECK_MAGIC
  unsigned int magic2;
#endif
} LIST;


/* allocate list nodes in 512 byte chunks, giving 42 elements */
#define DEFAULT_POOLSIZE 512


#if CHECK_MAGIC
#define CKMAGIC(p) { if (p->magic1 != MAGIC) breakpoint(); \
                     if (p->magic2 != MAGIC) breakpoint(); }

#define CKNPMAGIC(p) cknpmagic(p)

#define CKLNMAGIC(p) cklnmagic(p)

#define CKLMAGIC(p)  cklmagic(p)
#else
#define CKMAGIC(p)

#define CKNPMAGIC(p)

#define CKLNMAGIC(p)

#define CKLMAGIC(p)
#endif


static int insert_ln ( LIST * l, LISTNODE * ln, void * data_ptr );


#if CHECK_MAGIC
static int cknpmagic ( LIST * l )
{
  NODEPOOL * np;
  int i;

  i = 0;
  np = l->np_top;
  while (np) {
    i++;
    CKMAGIC(np);
    np = np->chain_next;
  }

  i = 0;
  np = l->np_bottom;
  while (np) {
    i++;
    CKMAGIC(np);
    np = np->chain_prev;
  }

  return 0;
}



static int cklnmagic ( LIST * l )
{
  LISTNODE * ln;
  int i;

  i = 0;
  ln = l->top;
  while (ln) {
    i++;
    CKMAGIC(ln);
    ln = ln->next;
  }

  i = 0;
  ln = l->bottom;
  while (ln) {
    i++;
    CKMAGIC(ln);
    ln = ln->prev;
  }

  return 0;
}


static int cklmagic ( LIST * l )
{
  CKMAGIC(l);
  CKNPMAGIC(l);
  CKLNMAGIC(l);
  CKMAGIC(l);
  return 0;
}
#endif


/*------------------------------------------------------------
|  new_node_pool
|
|  Create and initialize a new pool of list nodes.  This is
|  just a big block of memory with the first sizeof(NODEPOOL)
|  bytes reserved.  The first available list node resides at
|  offset sizeof(NODEPOOL).
 ------------------------------------------------------------*/
static
NODEPOOL * 
new_nodepool ( LIST * l )
{
  NODEPOOL * np;
  LISTNODE * ln;
  int i;

  CKLMAGIC(l);

  /*--------------------------------------------------
  |  get a block of memory for the new pool
   --------------------------------------------------*/
  np = (NODEPOOL *) MALLOC ( l->poolsize, "list node pool" );
  if (np == NULL) {
    return NULL;
  }

  /*--------------------------------------------------
  |  initialize the chaining information at the
  |  beginning of the pool.
   --------------------------------------------------*/
#if CHECK_MAGIC
  np->magic1 = MAGIC;
#endif
  np->chain_next = NULL;
  np->chain_prev = NULL;
#if CHECK_MAGIC
  np->magic2 = MAGIC;
#endif

  /*--------------------------------------------------
  |  initialize all the list nodes within the node
  |  pool, which begin just after the NODEPOOL
  |  structure at the beginning of the memory block
   --------------------------------------------------*/
  ln = (LISTNODE *) (&np[1]);

#if CHECK_MAGIC
  ln[0].magic1 = MAGIC;
#endif
  ln[0].data = NULL;
  ln[0].next = &ln[1];
  ln[0].prev = NULL;
#if CHECK_MAGIC
  ln[0].magic2 = MAGIC;
#endif

  for (i=1; i<l->n_ln_pool-1; i++) {
#if CHECK_MAGIC
    ln[i].magic1 = MAGIC;
#endif
    ln[i].data = NULL;
    ln[i].next = &ln[i+1];
    ln[i].prev = &ln[i-1];
#if CHECK_MAGIC
    ln[i].magic2 = MAGIC;
#endif
  }

#if CHECK_MAGIC
  ln[l->n_ln_pool-1].magic1 = MAGIC;
#endif
  ln[l->n_ln_pool-1].data = NULL;
  ln[l->n_ln_pool-1].next = NULL;
  ln[l->n_ln_pool-1].prev = &ln[l->n_ln_pool-2];
#if CHECK_MAGIC
  ln[l->n_ln_pool-1].magic2 = MAGIC;
#endif

  CKMAGIC(np);

  CKLMAGIC(l);

  return np;
}



/*------------------------------------------------------------
|  get_listnode
|
|  Get the next available list node.  If there are no more
|  list nodes, another pool of list nodes is allocated.  If
|  that fails, NULL is returned.
 ------------------------------------------------------------*/
static
LISTNODE * 
get_listnode ( LIST * l )
{
  LISTNODE * ln;
  NODEPOOL * np;

  CKLMAGIC(l);

  if (l->next_ln == NULL) {
    /*--------------------------------------------------
    | allocate a new node pool and chain to the others
     --------------------------------------------------*/
    np = new_nodepool(l);
    if (np == NULL) {
      CKLMAGIC(l);
      return NULL;
    }

    if (l->np_top == NULL) {
      /*--------------------------------------------------
      |  this is the first node pool for this list,
      |  directly assign to the top and bottom.
       --------------------------------------------------*/
      l->np_top = np;
      l->np_bottom = np;
      np->chain_next = NULL;
      np->chain_prev = NULL;
    }
    else {
      /*--------------------------------------------------
      |  this is an additional node pool, add it to the
      |  chain.
       --------------------------------------------------*/
      np->chain_next = NULL;
      np->chain_prev = l->np_bottom;
      l->np_bottom->chain_next = np;
      l->np_bottom = np;
    }

    /*--------------------------------------------------
    |  set the list's pointer to the next available
    |  list node to the first list node in this new
    |  pool.
     --------------------------------------------------*/
    l->next_ln = (LISTNODE *)&np[1];

    CKMAGIC(np);
  }

  /*--------------------------------------------------
  |  get the next available list node, set the list's
  |  next available list node to the next one in the
  |  list.
   --------------------------------------------------*/
  ln = l->next_ln;
  l->next_ln = ln->next;

  CKMAGIC(ln);

  /*--------------------------------------------------
  |  initialize the new list node and return
   --------------------------------------------------*/
  ln->next = NULL;
  ln->prev = NULL;
  ln->data = NULL;

  CKLMAGIC(l);

  return ln;
}



/*------------------------------------------------------------
|  free_listnode
|
|  Return a list node to the pool of list nodes.  This puts
|  the node at the head of the free list, so that the next
|  call to 'get_listnode', with return the most recently
|  freed one.
 ------------------------------------------------------------*/
static
int 
free_listnode ( LIST * l, LISTNODE * ln )
{
  CKLMAGIC(l);

  /*--------------------------------------------------
  |  insert the list node at the head of the list of
  |  free list nodes.
   --------------------------------------------------*/
  ln->prev = NULL;
  ln->data = NULL;
  ln->next = l->next_ln;
  l->next_ln = ln;

  CKLMAGIC(l);

  return 0;
}



/*----------------------------------------------------------------------
  lcreat

  Create a new list data structure.  
  
  If liststruct is not NULL, it is used to provide the memory space
  for the list structure instance, otherwise, the necessary memory is
  malloc'd.

  If elements is zero, the default poolsize is used, otherwise,
  poolsizes of 'elements' elements are malloc'd to obtain the memory
  for list nodes.  Minimum element count is 5.

  The first node pool is not preallocated; instead it is malloc'd at
  the time of the first use.
  ----------------------------------------------------------------------*/
LISTID
lcreat ( void * liststruct, int elements )
{
  LIST * l;

  if (liststruct == NULL) {
    /*--------------------------------------------------
      allocate memory for the list itself
      --------------------------------------------------*/
    l = (LIST *) MALLOC ( sizeof(LIST), "list struct" );
    if (l == NULL) {
      return NULL;
    }
    l->free_on_close = 1;
  }
  else {
    /*-----------------------------------------------------------------
      use the memory given to us for the list structure
      -----------------------------------------------------------------*/
    l = liststruct;
    l->free_on_close = 0;
  }

  /*--------------------------------------------------
  |  initialize the list
   --------------------------------------------------*/
#if CHECK_MAGIC
  l->magic1 = MAGIC;
  l->magic2 = MAGIC;
#endif
  l->top = NULL;
  l->bottom = NULL;
  l->num = 0;

  if (elements == 0) {
    l->poolsize = DEFAULT_POOLSIZE;
  }
  else {
    l->poolsize = elements*sizeof(LISTNODE)+sizeof(NODEPOOL);
  }

  l->n_ln_pool = (l->poolsize-sizeof(NODEPOOL))/sizeof(LISTNODE);

  if (l->n_ln_pool < 5) {
    if (!liststruct) {
      FREE(l);
    }
    return NULL;
  }

  l->np_top = NULL;
  l->np_bottom = NULL;
  l->next_ln = NULL;

  CKLMAGIC(l);

  return (LISTID)l;
}


/*--------------------------------------------------
|  ldestroy_cb
|
|  destroy an existing list data structure, calling
|  the user routine 'ucleanup' on the data pointer
|  of each list element.  Allows the user to free
|  up a list data structure and have this routine
|  call their function to free up each list element
|  at the same time.
 --------------------------------------------------*/
void 
ldestroy_cb ( LISTID lid, void (*ucleanup)(void * data_ptr) )
{
  LIST * l;
  LISTNODE * ln;

  l = (LIST *)lid;

  CKLMAGIC(l);

  ln = l->top;
  while (ln != NULL) {
    ucleanup ( ln->data );
    ln = ln->next;
  }

  ldestroy ( l );
}



/*--------------------------------------------------
|  ldestroy
|
|  destroy an existing list data structure.
|
|  assumes that each data element does not need to
|  be freed.
 --------------------------------------------------*/
void 
ldestroy ( LISTID lid )
{
  LIST * l;
  NODEPOOL * p1, * p2;

  l = (LIST *)lid;

  CKLMAGIC(l);

  /*--------------------------------------------------
  |  free each node pool - start at the first node
  |  pool and free each successive until there are
  |  no more.
   --------------------------------------------------*/
  p1 = l->np_top;
  while (p1 != NULL) {
    p2 = p1->chain_next;
    FREE(p1);
    p1 = p2;
  }

  /*--------------------------------------------------
  |  now free the memory occupied by the list itself
   --------------------------------------------------*/
  if (l->free_on_close) {
    FREE ( l );
  }
}




/*------------------------------------------------------------
|  ladd
|
|  add list - add item p to the list
 ------------------------------------------------------------*/
int 
ladd ( LISTID lid, void * p )
{
  LIST * l;
  LISTNODE *lnptr;

  l = (LIST *)lid;

  CKLMAGIC(l);

  lnptr = get_listnode(l);
  if (lnptr==NULL) {
#ifdef BOS
    breakpoint();
#endif
    return -1;
  }

  CKMAGIC(lnptr);

  lnptr->data = p;

  if (l->top == NULL) {
    l->top = lnptr;
    l->bottom = lnptr;
    lnptr->next = NULL;
    lnptr->prev = NULL;
  }
  else {
    lnptr->prev = l->bottom;
    lnptr->next = NULL;
    l->bottom->next = lnptr;
    l->bottom = lnptr;
  }
  l->num++;

  CKLMAGIC(l);

  return 0;
}



/*------------------------------------------------------------
|  laddo
|
|  add list, ordered - add item p to the list, use 'compare'
|  function to place 'p' when the comparison 'p' is less than
|  the next item.  Return 0 if this was a unique entry,
|  else return 1 indicating a duplicate entry, i.e., the
|  compare function returned 0 while inserting the element.
 ------------------------------------------------------------*/
int 
laddo ( LISTID lid, void * p, int (*compare)(const void *p1,const void *p2), 
	LNODEID * firstdup )
{
  LIST * l;
  LISTNODE * ln;
  int dup, cmp;

  l = (LIST *)lid;

  CKLMAGIC(l);

  dup = 0;
  ln = l->top;

  while (ln!=NULL) {
    CKMAGIC(ln);
    cmp = compare(p,ln->data);
    if (cmp == 0) {
      dup = 1;
      if (firstdup)
	*firstdup = ln;
    }
    if (cmp < 0) {
      insert_ln(l,ln,p);
      CKLMAGIC(l);
      return dup;
    }
    else {
      ln = ln->next;
    }
  }

  ladd(l,p);

  CKLMAGIC(l);

  return dup;
}


/*---------------------------------------------------------------------------
|  laddu
|
|  add list, ordered, unique - add item p to the list, use 'compare'
|  function to place 'p' when the comparison 'p' is less than the next
|  item.  Return 1 if the item was added, 0 if not.
|
 --------------------------------------------------------------------------*/
int 
laddu ( LISTID lid, void * p, int (*compare)(const void *p1,const void *p2) )
{
  LIST * l;
  LISTNODE * ln;
  int cmp;

  l = (LIST *)lid;

  CKLMAGIC(l);

  ln = l->top;

  while (ln!=NULL) {
    CKMAGIC(ln);
    cmp = compare(p,ln->data);
    if (cmp == 0) {
      CKLMAGIC(l);
      return 0;
    }
    if (cmp < 0) {
      insert_ln(l,ln,p);
      CKLMAGIC(l);
      return 1;
    }
    else {
      ln = ln->next;
    }
  }

  ladd(l,p);

  CKLMAGIC(l);

  return 1;
}




LNODEID 
lfirst ( LISTID lid )
{
  CKLMAGIC(((LIST *)lid));
  return ((LIST *)lid)->top;
}


LNODEID 
llast  ( LISTID lid )
{
  CKLMAGIC(((LIST *)lid));
  return ((LIST *)lid)->bottom;
}


LNODEID 
lnext  ( LNODEID lnid )
{
  CKMAGIC(((LISTNODE *)lnid));
  return ((LISTNODE *)lnid)->next;
}


LNODEID 
lprev  ( LNODEID lnid )
{
  CKMAGIC(((LISTNODE *)lnid));
  return ((LISTNODE *)lnid)->prev;
}


void * 
ldata ( LNODEID lnid )
{
  CKMAGIC(((LISTNODE *)lnid));
  return ((LISTNODE *)lnid)->data;
}



int
lsize ( LISTID lid )
{
  CKLMAGIC(((LIST *)lid));
  return ((LIST *)lid)->num;
}



/*------------------------------------------------------------
|  lcat
|
|  catenate - catenate l2 to l1, return pointer to l1.
 ------------------------------------------------------------*/
LISTID
lcat ( LISTID lid1, LISTID lid2 )
{
  CKLMAGIC(((LIST *)lid1));
  CKLMAGIC(((LIST *)lid2));
  while (lsize(lid2)) {
    ladd ( lid1, lrmv_n(lid2,1) );
  }

  CKLMAGIC(((LIST *)lid1));
  CKLMAGIC(((LIST *)lid2));

  return lid1;
}



/*----------------------------------------------------------------------
|  lget
|
|  get from list, last item - return pointer to the data of the last
|  item in the list, non-destructive
 ----------------------------------------------------------------------*/
void * 
lget ( LISTID lid )
{
  LIST * l;
  LISTNODE * p;

  l = (LIST *)lid;

  CKLMAGIC(l);

  p = l->bottom;

  if (p == NULL) {
    CKLMAGIC(l);
    return NULL;
  }
  else {
    CKLMAGIC(l);
    return p->data;
  }
}



/*---------------------------------------------------------------
|  lget_n
|
|  get from list, index - return the nth list item, 
|  non-destructive
 ---------------------------------------------------------------*/
void * 
lget_n ( LISTID lid, unsigned int n )
{
  int i;
  LIST * l;
  LISTNODE * ln;

  l = (LIST *)lid;

  CKLMAGIC(l);

  if ((n<1)||(n>lsize(l))) {
    return NULL;
  }

  ln = l->top;
  i = 1;
  while (ln && (i!=n)) {
    CKMAGIC(ln);
    ln = ln->next;
    i++;
  }

  if (ln) {
    CKLMAGIC(l);
    return ln->data;
  }
  else {
    CKLMAGIC(l);
    return NULL;
  }
}



/*---------------------------------------------------------------
|  lget_ln
|
|  get from list, listnode - return the nth list item, the
|  listnode is returned instead of the data, non-destructive
 ---------------------------------------------------------------*/
LNODEID
lget_ln ( LISTID lid, unsigned int n )
{
  int i;
  LIST * l;
  LISTNODE * ln;

  l = (LIST *)lid;

  CKLMAGIC(l);

  if ((n<1)||(n>lsize(l))) {
    return NULL;
  }

  ln = l->top;
  i = 1;
  while (i!=n) {
    CKMAGIC(ln);
    ln = ln->next;
    i++;
  }

  CKLMAGIC(l);
  return (LNODEID)ln;
}



/*----------------------------------------------------------------------
|  insert_ln
|
|  insert data, listnode - insert data just before the list item 
|  pointed to by 'ln'.
|
|  This routine is not intended to be called directly by the user
|  because the validity of ln is not checked.  This routine is called
|  by list manipulation routines within this module, in which ln is
|  known to point to a valid list node.
 ----------------------------------------------------------------------*/
static 
int 
insert_ln ( LIST * l, LISTNODE * ln, void * data_ptr )
{
  LISTNODE * lnptr;

  CKLMAGIC(l);

  if (ln==NULL) {
    ladd ( l, data_ptr );
    CKLMAGIC(l);
    return 0;
  }

  lnptr = get_listnode(l);
  if (lnptr == NULL) {
#ifdef BOS
    breakpoint();
#endif
    return -1;
  }

  CKMAGIC(lnptr);

  lnptr->data = data_ptr;

  if (ln==l->top) {
    /*------------------------------
    |  insert before the list head
     ------------------------------*/
    lnptr->next = ln;
    lnptr->prev = NULL;
    ln->prev = lnptr;
    l->top = lnptr;
  }
  else if (ln==NULL) {
    /*-----------------
    |  list was empty
     -----------------*/
    lnptr->next = NULL;
    lnptr->prev = l->bottom;
    l->bottom->next = lnptr;
    l->bottom = lnptr;
  }
  else {
    /*-----------------------------------
    |  insert in the middle of the list
     -----------------------------------*/
    lnptr->next = ln;
    lnptr->prev = ln->prev;
    lnptr->next->prev = lnptr;
    lnptr->prev->next = lnptr;
  }

  l->num++;

  CKLMAGIC(l);

  return 0;
}



/*-----------------------------------------------------------------
|  lins_n
|
|  Insert data before the nth item in the list.
 -----------------------------------------------------------------*/
int 
lins_n ( LISTID lid, void * data_ptr, unsigned int n )
{
  int i;
  LIST * l;
  LISTNODE * ln;

  l = (LIST *)lid;

  CKLMAGIC(l);

  if ((n<1)||(n>(l->num+1))) {
    return -1;
  }

  if (l->num == 0) {
    return ladd ( lid, data_ptr );
  }

  /*----------------------------------
  |  locate the nth item in the list
   ----------------------------------*/
  ln = l->top;
  i = 1;
  while (ln && (i!=n)) {
    CKMAGIC(ln);
    ln = ln->next;
    i++;
  }

  if (!ln) {
    CKLMAGIC(l);
    return -1;
  }

  CKLMAGIC(l);

  /*-----------------------------------------
  |  insert before the nth item in the list
   -----------------------------------------*/
  return insert_ln ( l, ln, data_ptr );
}


/*-----------------------------------------------------------------
|  lins_ln
|
|  Insert data before the list node pointed to by ln.
 -----------------------------------------------------------------*/
int 
lins_ln ( LISTID lid, LNODEID lnid, void * data_ptr )
{
  LIST * l;
  LISTNODE * ln;
  LISTNODE * ln_ptr;

  l = (LIST *)lid;
  ln = (LISTNODE *)lnid;

  CKLMAGIC(l);

  CKMAGIC(ln);

  /*-----------------------------------------
  |  validate that ln is indeed in the list
   -----------------------------------------*/
  ln_ptr = l->top;
  while ((ln_ptr!=NULL)&&(ln_ptr!=ln)) {
    CKMAGIC(ln_ptr);
    ln_ptr = ln_ptr->next;
  }

  if (ln_ptr == NULL) {
    CKLMAGIC(l);
    return -1;
  }

  CKLMAGIC(l);

  /*--------------------------------
  |  insert the data into the list
   --------------------------------*/
  return insert_ln ( l, ln, data_ptr );
}



/*----------------------------------------------------------------------
|  remove_ln
|
|  Remove the item in the list pointed to by ln.  This routine is not
|  intended to be called directly by the user because the validity
|  of ln is not checked.  This routine is called by list manipulation
|  routines within this module, in which ln is known to point to a
|  valid list node.
 ----------------------------------------------------------------------*/
static
void * 
remove_ln ( LIST * l, LISTNODE * ln )
{
  void * r;

  CKLMAGIC(l);

  CKMAGIC(ln);

  if (ln==l->top) {
    /*------------------------------
    |  remove the head of the list
     ------------------------------*/
    l->top = ln->next;
    if (l->top != NULL) {
      l->top->prev = NULL;
    }
    else {
      /*----------------------------------------
      |  this was the only item in the list
       ----------------------------------------*/
      l->bottom = NULL;
    }
  }
  else if (ln==l->bottom) {
    /*------------------------------
    |  remove the tail of the list
     ------------------------------*/
    l->bottom = ln->prev;
    if (l->bottom != NULL) {
      l->bottom->next = NULL;
    }
  }
  else {
    /*-------------------------------------
    |  remove from the middle of the list
     -------------------------------------*/
    ln->prev->next = ln->next;
    ln->next->prev = ln->prev;
  }

  /*-----------------------------
  |  prepare to return the data
   -----------------------------*/
  r = ln->data;

  /*-----------------------------------------------
  |  free the listnode for re-use
   -----------------------------------------------*/
  free_listnode(l,ln);

  /*------------------------------------
  |  adjust the item count of the list
   ------------------------------------*/
  l->num--;

  CKLMAGIC(l);

  return r;
}



/*-------------------------------------------------------------------------
|  lrmv_d
|
|  remove from list, data - removes the data element from the list,
|  destructive
 -------------------------------------------------------------------------*/
void * 
lrmv_d ( LISTID lid, void * data_ptr )
{
  LIST * l;
  LISTNODE * ln;
  int i;

  l = (LIST *)lid;

  CKLMAGIC(l);

  i = 0;
  ln = l->top;
  while (ln && (ln->data != data_ptr)) {
    i++;
    CKMAGIC(ln);
    ln = ln->next;
  }

  if (ln == NULL) {
    CKLMAGIC(l);
    return NULL;
  }
  else {
    CKLMAGIC(l);
    return remove_ln ( l, ln );
  }
}



/*-------------------------------------------------------------------------
|  lrmv_ln
|
|  remove from list, by list node - remove the data element pointed to
|  by 'ln' from the list, destructive
 -------------------------------------------------------------------------*/
void * 
lrmv_ln ( LISTID lid, LNODEID lnid )
{
  LIST * l;
  LISTNODE * ln;
  LISTNODE * p;

  l = (LIST *)lid;
  ln = (LISTNODE *)lnid;

  CKLMAGIC(l);

  CKMAGIC(ln);

  p = l->top;
  while ((p!=NULL)&&(p!=ln)) {
    CKMAGIC(p);
    p = p->next;
  }
  
  if (p==NULL) {
    CKLMAGIC(l);
    return NULL;
  }
  else {
    CKLMAGIC(l);
    return remove_ln ( l, p );
  }
}



/*----------------------------------------------------------------------
|  lrmv_n
|
|  remove from list, by item number - remove the nth element from
|  the list.
 ----------------------------------------------------------------------*/
void * 
lrmv_n ( LISTID lid, unsigned int n )
{
  int i;
  LIST * l;
  LISTNODE * ln;

  l = (LIST *)lid;

  CKLMAGIC(l);

  if ((n<1)||(n>l->num)) {
    return NULL;
  }

  ln = l->top;
  i = 1;
  while (ln && (i!=n)) {
    CKMAGIC(ln);
    ln = ln->next;
    i++;
  }

  if (ln) {
    CKLMAGIC(l);
    return remove_ln ( l, ln );
  }
  else {
    CKLMAGIC(l);
    return NULL;
  }
}


/*----------------------------------------------------------------------
|  lrmv
|
|  remove from list, last item - remove the last item from the list,
|  destructive
 ----------------------------------------------------------------------*/
void * 
lrmv ( LISTID lid )
{
  LIST * l;
  LISTNODE * p;

  l = (LIST *)lid;

  CKLMAGIC(l);

  p = l->bottom;

  if (p == NULL) {
    CKLMAGIC(l);
    return NULL;
  }
  else {
    CKLMAGIC(l);
    return remove_ln ( l, p );
  }
}



/*----------------------------------------------------------------------
|  lsrch
|
|  search list - return data element pointed to by 'p', NULL if not
|  found
 ----------------------------------------------------------------------*/
void * 
lsrch ( LISTID lid, void * p, int (* compare)(void * p1, void * p2) )
{
  LIST * l;
  LISTNODE * ln;

  l = (LIST *)lid;

  CKLMAGIC(l);

  ln = l->top;

  while (ln!=NULL) {
    CKMAGIC(ln);
    if (compare(p,ln->data) == 0) {
      CKLMAGIC(l);
      return ln->data;
    }
    else {
      ln = ln->next;
    }
  }

  CKLMAGIC(l);
  return NULL;
}

/*----------------------------------------------------------------------
|  lsort
|
|  sort list - sorts list inplace (using bubble sort)
|
 ----------------------------------------------------------------------*/
void
lsort ( LISTID lid, int (* compare)(void * p1, void * p2) )
{
  LIST * l;
  LISTNODE * lt; /* this */
  LISTNODE * ln; /* next */
  int unsorted = 1;

  l = (LIST *)lid;

  CKLMAGIC(l);

  while(unsorted){
    lt = l->top;
    unsorted = 0;
    while (lt!=NULL) {
      CKMAGIC(lt);
      ln = lt->next;
      if (ln!= NULL && compare(lt->data,ln->data) > 0) {
        void * p = ln->data;
        ln->data = lt->data;
        lt->data = p;
        unsorted = 1;
      }
      lt = ln;
    }
  }

  CKLMAGIC(l);
}


int lprint ( FILE * f, LISTID lid )
{
  LIST * l;
  LISTNODE * ln;
  NODEPOOL * np;
  int count;

  l = (LIST *)lid;

  fprintf ( f, "list id %p internal data structures:\n", 
            lid );
#if CHECK_MAGIC
  if ((l->magic1 != MAGIC) || (l->magic2 != MAGIC)) {
    fprintf ( f, "  *** WARNING: LIST MAGIC IS CORRUPT ***\n" );
  }
  fprintf ( f, 
            "  magic1=0x%08x\n"
            "  magic2=0x%08x\n",
            l->magic1, l->magic2 );
#endif
  fprintf ( f, "   num f pool n_ln        top     bottom    next_ln     np_top  np_bottom\n" );
  fprintf ( f, "  ---- - ---- ---- ---------- ---------- ---------- ---------- ----------\n" );
  fprintf ( f, "  %4d %1d %4d %4d %10p %10p %10p %10p %10p\n",
            l->num, l->free_on_close, l->poolsize, l->n_ln_pool, 
            l->top, l->bottom,
            l->next_ln, l->np_top, l->np_bottom );
  

  fprintf ( f, 
            "  node pools:\n"
            "     idx         np     magic1       next       prev     magic2\n"
            "    ---- ---------- ---------- ---------- ---------- ----------\n" );
  count = 0;
  np = l->np_top;
  while (np != NULL) {
    count++;
    fprintf ( f, "    %4d %10p 0x%08x %10p %10p 0x%08x\n", 
              count, np, 
#if CHECK_MAGIC
              np->magic1, 
#else
              0,
#endif
              np->chain_next, np->chain_prev, 
#if CHECK_MAGIC
              np->magic2
#else
              0
#endif
      );
    np = np->chain_next;
  }

  if (f) {
    fprintf ( f, 
              "  list elements:\n"
              "       n         ln     magic1       next       prev       data     magic2\n"
              "    ---- ---------- ---------- ---------- ---------- ---------- ----------\n" );
    count = 0;
    ln = l->top;
    while (ln != NULL) {
      count++;
      fprintf ( f, "    %4d %10p %10x %10p %10p %10p %10x\n", 
                count, ln, 
#if CHECK_MAGIC
                ln->magic1,
#else
                0,
#endif
                ln->next, ln->prev, ln->data, 
#if CHECK_MAGIC
                ln->magic2
#else
                0
#endif
        );
      ln = lnext(ln);
    }
    if (count != l->num) {
      fprintf ( f, 
                "  *** list count is not correct\n"
                "  *** list id indicates %d, counted items = %d\n", 
                l->num, count );
    }
  }

  return 0;
}
