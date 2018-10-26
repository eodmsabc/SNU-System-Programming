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
#define CHUNKSIZE (1<<12)

#define MIN_BLOCK_SIZE (2 * DSIZE)

#define MAX(x, y) 	((x) > (y)? (x) : (y))
#define MIN(x, y)	((x) > (y)? (y) : (x))

#define PACK(size, alloc) 	((size) | (alloc))

#define GET(p)			(*(unsigned int*)(p))
#define PUT(p, val)		(*(unsigned int*)(p) = (val))

#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ALLOC(p)	(GET(p) & 0x1)

#define HDRP(bp)	((char*)(bp) - WSIZE)
#define FTRP(bp)	((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp)	((char*)(bp) + GET_SIZE((char*)(bp) - WSIZE))
#define PREV_BLKP(bp)	((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))

static void *prologue;

static void *coalesce(void*);
static void *find_fit(size_t);
static void place(void*, size_t);

static void *extend_heap(size_t size) {
	char *bp;
	if ((bp = mem_sbrk(size)) == (void*)-1) return NULL;
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	
	return coalesce(bp);
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(range_t **ranges)
{
  /* YOUR IMPLEMENTATION */
  if ((prologue = mem_sbrk(4*WSIZE)) == (void*)-1) return -1;

  PUT(prologue, 0);
  PUT(prologue + (1*WSIZE), PACK(DSIZE, 1));
  PUT(prologue + (2*WSIZE), PACK(DSIZE, 1));
  PUT(prologue + (3*WSIZE), PACK(0, 1));
  prologue += (2*WSIZE);

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
	/*
  int newsize = ALIGN(size + SIZE_T_SIZE);
  void *p = mem_sbrk(newsize);
  if (p == (void *)-1)
    return NULL;
  else {
    *(size_t *)p = size;
    return (void *)((char *)p + SIZE_T_SIZE);
  }
  */
  char *bp;
  size_t extendsize;
  if (size == 0) return NULL;
  size = ALIGN(size + DSIZE);
  if ((bp = find_fit(size)) != NULL) {
	  place(bp, size);
	  return bp;
  }
  extendsize = MAX(size, CHUNKSIZE);
  if ((bp = extend_heap(extendsize)) == NULL) {
	  return NULL;
  }
  place(bp, size);
  return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
  /* YOUR IMPLEMENTATION */
  size_t size = GET_SIZE(HDRP(ptr));
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
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

	else if (prev_alloc && !next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}

	else if (!prev_alloc && next_alloc) {
		bp = PREV_BLKP(bp);
		size += GET_SIZE(HDRP(bp));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}

	else {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		bp = PREV_BLKP(bp);
		size += GET_SIZE(HDRP(bp));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	return bp;
}

static void *find_fit(size_t size) {
	void *bp = prologue;
	while(GET_SIZE(HDRP(bp)) > 0) {
		if (!GET_ALLOC(HDRP(bp)) && (size <= GET_SIZE(HDRP(bp)))) {
			return bp;
		}
		bp = NEXT_BLKP(bp);
	}
	return NULL;
}

static void place(void *bp, size_t size) {
	size_t bs = GET_SIZE(HDRP(bp));
	if (bs - size >= MIN_BLOCK_SIZE) {
		PUT(HDRP(bp), PACK(size, 1));
		PUT(FTRP(bp), PACK(size, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(bs-size, 0));
		PUT(FTRP(bp), PACK(bs-size, 0));
	}
	else {
		PUT(HDRP(bp), PACK(bs, 1));
		PUT(FTRP(bp), PACK(bs, 1));
	}
}
