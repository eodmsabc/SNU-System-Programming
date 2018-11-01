/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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
#define CHUNKSIZE (1<<6)

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

#define SIZE_CLASS_NUM 16

#define SIZE_CLASS_0	16
#define SIZE_CLASS_1	32
#define SIZE_CLASS_2	48
#define SIZE_CLASS_3	64
#define SIZE_CLASS_4	96
#define SIZE_CLASS_5	128
#define SIZE_CLASS_6	256
#define SIZE_CLASS_7	512
#define SIZE_CLASS_8	768
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
	else return 7;
}

static void *get_class_root(int class) {
	return size_class_p + (DSIZE * class);
}

static void *coalesce(void*);
static void *find_fit(size_t);
static void place(void*, size_t);

static void insert_block(void *bp, size_t asize) {
	void *sclassp = get_class_root(get_size_class(asize));
	SET_PREV_F(bp, sclassp);
	SET_NEXT_F(bp, NEXT_FREE(sclassp));
	SET_PREV_F(NEXT_FREE(sclassp), bp);
	SET_NEXT_F(sclassp, bp);
}

static void remove_block(void *bp) {
	SET_NEXT_F(PREV_FREE(bp), NEXT_FREE(bp));
	SET_PREV_F(NEXT_FREE(bp), PREV_FREE(bp));
}

static void *extend_heap(size_t asize) {
	char *bp;

	if ((bp = mem_sbrk(asize)) == (void*)-1) return NULL;
	
	PUT(HDRP(bp), PACK(asize, 0));
	PUT(FTRP(bp), PACK(asize, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	insert_block(bp, asize);
	
	return coalesce(bp);
}

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
		SET_NEXT_F(temp, temp);
		SET_PREV_F(temp, temp);
	}

	// Dummy Block
	prologue = size_class_p + (SIZE_CLASS_NUM + 1) * DSIZE;
	PUT(prologue - WSIZE, PACK(DSIZE, 1));
	PUT(prologue, PACK(DSIZE, 1));
	PUT(prologue + WSIZE, PACK(0, 1));

	if (extend_heap(CHUNKSIZE) == NULL) return -1;
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
	size_t extendsize;

	if (size == 0) return NULL;
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize)) == NULL) {
		return NULL;
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
	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
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
//	mem_reset_brk();
}

static void *coalesce(void *bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) return bp;

	remove_block(bp);
	if (!next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		remove_block(NEXT_BLKP(bp));
	}

	if (!prev_alloc) {
		bp = PREV_BLKP(bp);
		remove_block(bp);
		size += GET_SIZE(HDRP(bp));
	}
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

	insert_block(bp, size);
	return bp;
}

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

static void place(void *bp, size_t asize) {
	size_t bsize = GET_SIZE(HDRP(bp));

	// Remove bp from size class list
//	SET_NEXT_F(PREV_FREE(bp), NEXT_FREE(bp));
//	SET_PREV_F(NEXT_FREE(bp), PREV_FREE(bp));
	remove_block(bp);

	if (bsize - asize >= MIN_BLOCK_SIZE) {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		// Splited Part
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(bsize - asize, 0));
		PUT(FTRP(bp), PACK(bsize - asize, 0));
		insert_block(bp, bsize - asize);
	}
	else {
		PUT(HDRP(bp), PACK(bsize, 1));
		PUT(FTRP(bp), PACK(bsize, 1));
	}
}
