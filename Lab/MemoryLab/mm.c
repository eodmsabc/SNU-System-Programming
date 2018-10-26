/*
 * mm.c
 * Memory Allocation with Segregated Fits
 *
 * Free blocks are categorized into 16 size classes. All blocks have boundary tag.
 * If new allocation request with small alligned size, It generates big block which
 * size is (GRP_CREATE_NUM) times as big as request.
 * It is effective to manage external fragmentation.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
  /* Team name : Your student ID */
  "2014-15395",
  /* Your full name */
  "Daeeun Kim",
  /* Your student ID, again  */
  "2014-15395",
  /* leave blank */
  "",
  /* leave blank */
  ""
};

/* DON'T MODIFY THIS VALUE AND LEAVE IT AS IT WAS */
static range_t **gl_ranges;

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * remove_range - manipulate range lists
 * DON'T MODIFY THIS FUNCTION AND LEAVE IT AS IT WAS
 */
static void remove_range(range_t **ranges, char *lo)
{
  range_t *p;
  range_t **prevpp = ranges;
  
  if (!ranges)
    return;

  for (p = *ranges;  p != NULL; p = p->next) {
    if (p->lo == lo) {
      *prevpp = p->next;
      free(p);
      break;
    }
    prevpp = &(p->next);
  }
}

////////////////////////////// DEFINES ///////////////////////////////

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<9)
#define GRP_CREATE_NUM (1<<4)

#define MIN_BLOCK_SIZE (2 * DSIZE)

#define MAX(x, y) 	((x) > (y)? (x) : (y))
#define MIN(x, y)	((x) > (y)? (y) : (x))

#define PACK(size, alloc) 	((size) | (alloc))
#define PACK_GRP(size, alloc) ((size) | (alloc) | 0x2)

#define GET(p)			(*(unsigned int*)(p))
#define PUT(p, val)		(*(unsigned int*)(p) = (val))

#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ISGRP(p)	(GET(p) & 0x2)
#define GET_ALLOC(p)	(GET(p) & 0x1)

#define HDRP(bp)		((char*)(bp) - WSIZE)
#define FTRP(bp)		((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_FREE(bp)	((void*)(GET(bp)))
#define PREV_FREE(bp)	((void*)(GET((char*)(bp) + WSIZE)))
#define SET_NEXT_F(bp, val)	(PUT((bp), (unsigned int)(val)))
#define SET_PREV_F(bp, val)	(PUT(((char*)(bp) + WSIZE), (unsigned int)(val)))

#define NEXT_BLKP(bp)	((char*)(bp) + GET_SIZE((char*)(bp) - WSIZE))
#define PREV_BLKP(bp)	((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))

#define SIZE_CLASS_NUM	16

#define SIZE_CLASS_0	16
#define SIZE_CLASS_1	32
#define SIZE_CLASS_2	48	// detailed small size classes
#define SIZE_CLASS_3	64
#define SIZE_CLASS_4	96	// detailed small size classes
#define SIZE_CLASS_5	128
#define SIZE_CLASS_6	256
#define SIZE_CLASS_7	384	// detailed small size classes
#define SIZE_CLASS_8	512
#define SIZE_CLASS_9	1024
#define SIZE_CLASS_10	2048
#define SIZE_CLASS_11	4096
#define SIZE_CLASS_12	8192
#define SIZE_CLASS_13	16384
#define SIZE_CLASS_14	32768

static void *size_class_p;
static void *prologue;

static int get_size_class(size_t asize) {
	if (asize <= SIZE_CLASS_0) return 0;
	else if (asize <= SIZE_CLASS_1) return 1;
	else if (asize <= SIZE_CLASS_2) return 2;
	else if (asize <= SIZE_CLASS_3) return 3;
	else if (asize <= SIZE_CLASS_4) return 4;
	else if (asize <= SIZE_CLASS_5) return 5;
	else if (asize <= SIZE_CLASS_6) return 6;
	else if (asize <= SIZE_CLASS_7) return 7;
	else if (asize <= SIZE_CLASS_8) return 8;
	else if (asize <= SIZE_CLASS_9) return 9;
	else if (asize <= SIZE_CLASS_10) return 10;
	else if (asize <= SIZE_CLASS_11) return 11;
	else if (asize <= SIZE_CLASS_12) return 12;
	else if (asize <= SIZE_CLASS_13) return 13;
	else if (asize <= SIZE_CLASS_14) return 14;
	else return 15;
}

static void *get_class_root(int class) {
	return size_class_p + (DSIZE * class);
}

// Function Declaration
static void insert_block(void*, size_t);
static void remove_from_list(void*);
static void *extend_heap(size_t);
static void *extend_heap_grp(size_t);
static void *coalesce(void*);
static void *find_fit(size_t);
static void place(void*, size_t);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(range_t **ranges)
{
	int i;
	void *temp;
	if ((size_class_p = mem_sbrk((SIZE_CLASS_NUM+2) * DSIZE)) == (void*)-1) return -1;

	// Size Class Root Setup
	for (i = 0; i < SIZE_CLASS_NUM; i++) {
		temp = size_class_p + i * DSIZE;
		// Self directing
		SET_NEXT_F(temp, temp);
		SET_PREV_F(temp, temp);
	}

	// Prologue and Epilogue
	prologue = size_class_p + (SIZE_CLASS_NUM + 1) * DSIZE;
	PUT(prologue - WSIZE, PACK(DSIZE, 1));
	PUT(prologue, PACK(DSIZE, 1));
	PUT(prologue + WSIZE, PACK(0, 1));

//	if (extend_heap(CHUNKSIZE) == NULL) return -1;
	/* DON't MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
	gl_ranges = ranges;

	return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void* mm_malloc(size_t size)
{
	void *bp;
	size_t asize = ALIGN(size + DSIZE);

	if (size == 0) return NULL;

	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	// Create new block as group (Request size is small enough)
	if (asize <= CHUNKSIZE) {
		if ((bp = extend_heap_grp(asize)) == NULL) return NULL;
	}

	// Normal case
	else {
		if ((bp = extend_heap(asize)) == NULL) return NULL;
	}

	place(bp, asize);
	return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
	size_t size = GET_SIZE(HDRP(ptr));
	if (GET_ISGRP(HDRP(ptr))) {
		PUT(HDRP(ptr), PACK_GRP(size, 0));
		PUT(FTRP(ptr), PACK_GRP(size, 0));
	}
	else {
		PUT(HDRP(ptr), PACK(size, 0));
		PUT(FTRP(ptr), PACK(size, 0));
	}
	insert_block(ptr, size);

	coalesce(ptr);

	/* DON't MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
	if (gl_ranges)
		remove_range(gl_ranges, ptr);
}

/*
 * mm_realloc - empty implementation; YOU DO NOT NEED TO IMPLEMENT THIS
 */
void* mm_realloc(void *ptr, size_t t)
{
  return NULL;
}

/*
 * mm_exit - finalize the malloc package.
 */
void mm_exit(void)
{
	void *scanner = NEXT_BLKP(prologue);
	size_t size;
	while((size = GET_SIZE(HDRP(scanner)))) {
		if (GET_ALLOC(HDRP(scanner))) mm_free(scanner);
		scanner = NEXT_BLKP(scanner);
	}
}

// Insert block into proper size class
static void insert_block(void *bp, size_t asize) {
	void *sclassp = get_class_root(get_size_class(asize));
	SET_PREV_F(bp, sclassp);
	SET_NEXT_F(bp, NEXT_FREE(sclassp));
	SET_PREV_F(NEXT_FREE(sclassp), bp);
	SET_NEXT_F(sclassp, bp);
}

// Remove block bp from size class list.
static void remove_from_list(void *bp) {
	SET_NEXT_F(PREV_FREE(bp), NEXT_FREE(bp));
	SET_PREV_F(NEXT_FREE(bp), PREV_FREE(bp));
}

// Extend heap
static void *extend_heap(size_t asize) {
	char *bp;

	if ((bp = mem_sbrk(asize)) == (void*)-1) return NULL;
	
	PUT(HDRP(bp), PACK(asize, 0));
	PUT(FTRP(bp), PACK(asize, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	insert_block(bp, asize);
	
	return coalesce(bp);
}

// Extend heap as group.
static void *extend_heap_grp(size_t asize) {
	char *bp;
	size_t grpsize = GRP_CREATE_NUM * asize;

	if ((bp = mem_sbrk(grpsize)) == (void*)-1) return NULL;
	PUT(HDRP(bp), PACK_GRP(grpsize, 0));
	PUT(FTRP(bp), PACK_GRP(grpsize, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	insert_block(bp, asize);

	return bp;
}

// Coalesce free blocks
static void *coalesce(void *bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) return bp;

	remove_from_list(bp);

	if (!next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		SET_NEXT_F(PREV_FREE(NEXT_BLKP(bp)), NEXT_FREE(NEXT_BLKP(bp)));
		SET_PREV_F(NEXT_FREE(NEXT_BLKP(bp)), PREV_FREE(NEXT_BLKP(bp)));
	}

	if (!prev_alloc) {
		bp = PREV_BLKP(bp);
		SET_NEXT_F(PREV_FREE(bp), NEXT_FREE(bp));
		SET_PREV_F(NEXT_FREE(bp), PREV_FREE(bp));
		size += GET_SIZE(HDRP(bp));
	}
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

	insert_block(bp, size);
	return bp;
}

// Find blocks from size class lists.
// Implemented with Segregated Fits Method
static void *find_fit(size_t asize) {
	int classnum = get_size_class(asize);
	void *sclassp;
	void *probe;
	for (;classnum < SIZE_CLASS_NUM; classnum++) {
		sclassp = get_class_root(classnum);
		probe = NEXT_FREE(sclassp);
		while(probe != sclassp) {
			if (GET_SIZE(HDRP(probe)) >= asize) {
				return probe;
			}
			probe = NEXT_FREE(probe);
		}
	}
	return NULL;
}

// Place new blocks
// If the block is big enough and it was generated as group,
// Splitted free block remains same size class.
static void place(void *bp, size_t asize) {
	size_t bsize = GET_SIZE(HDRP(bp));

	// Remove bp from size class list

	if (bsize - asize >= MIN_BLOCK_SIZE) {
		// GROUP
		if (GET_ISGRP(HDRP(bp))) {
			PUT(HDRP(bp), PACK_GRP(asize, 1));
			PUT(FTRP(bp), PACK_GRP(asize, 1));

			// Reorganize free block list
			SET_PREV_F(NEXT_BLKP(bp), PREV_FREE(bp));
			SET_NEXT_F(NEXT_BLKP(bp), NEXT_FREE(bp));
			SET_NEXT_F(PREV_FREE(bp), NEXT_BLKP(bp));
			SET_PREV_F(NEXT_FREE(bp), NEXT_BLKP(bp));

			bp = NEXT_BLKP(bp);
			PUT(HDRP(bp), PACK_GRP(bsize - asize, 0));
			PUT(FTRP(bp), PACK_GRP(bsize - asize, 0));
		}
		else {
			remove_from_list(bp);
			PUT(HDRP(bp), PACK(asize, 1));
			PUT(FTRP(bp), PACK(asize, 1));

			bp = NEXT_BLKP(bp);
			PUT(HDRP(bp), PACK(bsize - asize, 0));
			PUT(FTRP(bp), PACK(bsize - asize, 0));
			insert_block(bp, bsize - asize);
		}
	}
	else {
		remove_from_list(bp);
		PUT(HDRP(bp), PACK(bsize, 1));
		PUT(FTRP(bp), PACK(bsize, 1));
	}
}
