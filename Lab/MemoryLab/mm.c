/*
 * mm.c
 * Memory Allocation with Segregated Free Lists (Segregated Fits)
 *
 * Free blocks are categorized into 32 size classes.
 * All blocks have boundary tag. It will be used for coalescing & mm_exit.
 * find_fit function searches proper size class. If failed, it moves onto next size class list.
 * If new allocation request with small alligned size, It generates big block
 * which size is (BULK_CREATE_NUM) times as big as request. (I use 8 for this value)
 * It was more effective to manage external fragmentation.
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


// Variables
static void *size_class_p;
static void *prologue;

////////////////////////////// DEFINES ///////////////////////////////

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<9)
#define BULK_CREATE_NUM (1<<3)

#define MIN_BLOCK_SIZE (2 * DSIZE)

#define MAX(x, y) 	((x) > (y)? (x) : (y))
#define MIN(x, y)	((x) > (y)? (y) : (x))

// 2nd LSB will be used as check bit.
// It will be marked as 1 if that block was generetaed as bulk block.
//#define PACK(size, alloc) 	((size) | (alloc))
#define FREETAG 0x0
#define ALLOCTAG 0x1
#define BULKTAG 0x2
#define PACK(size, tag) ((size) | (tag))

#define GET(p)			(*(unsigned int*)(p))
#define PUT(p, val)		(*(unsigned int*)(p) = (val))

#define GET_SIZE(p)		(GET(p) & ~0x7)
#define GET_ISBULK(p)	(GET(p) & 0x2)
#define GET_ALLOC(p)	(GET(p) & 0x1)

#define HDRP(bp)		((char*)(bp) - WSIZE)
#define FTRP(bp)		((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_FREE(bp)	((void*)(GET(bp)))
#define PREV_FREE(bp)	((void*)(GET((char*)(bp) + WSIZE)))
#define SET_NEXT_F(bp, val)	(PUT((bp), (unsigned int)(val)))
#define SET_PREV_F(bp, val)	(PUT(((char*)(bp) + WSIZE), (unsigned int)(val)))

#define NEXT_BLKP(bp)	((char*)(bp) + GET_SIZE((char*)(bp) - WSIZE))
#define PREV_BLKP(bp)	((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))

// FOR mm_check function
#define ERROR_ALLOC_IN_FREE_LIST	1
#define ERROR_WRONG_SIZE_CLASS		2
#define ERROR_COALESCE_FAILED		3
#define ERROR_ISOLATED_FREE_BLOCK	4
#define FREE_MARK(p)			(PUT(HDRP(p), (GET(HDRP(p)) | 0x4)))
#define FREE_MARK_ERASE(p)		(PUT(HDRP(p), (GET(HDRP(p)) & ~0x4)))
#define GET_ISMARKED(p)			(GET(HDRP(p)) & 0x4)

//////////////////////////////// SIZE CLASS /////////////////////////
//#define CLASS_NUM_16	// The number of size class become 16
/////////////////////////////////////////////////////////////////////

// 16 LEVEL SIZE CLASS
#ifdef CLASS_NUM_16
#define SIZE_CLASS_NUM	16

#define SIZE_CLASS_1	0x10
#define SIZE_CLASS_2	0x20
#define SIZE_CLASS_3	0x30
#define SIZE_CLASS_4	0x40
#define SIZE_CLASS_5	0x60
#define SIZE_CLASS_6	0x80
#define SIZE_CLASS_7	0xc0
#define SIZE_CLASS_8	0x100
#define SIZE_CLASS_9	0x200
#define SIZE_CLASS_10	0x400
#define SIZE_CLASS_11	0x800
#define SIZE_CLASS_12	0x1000
#define SIZE_CLASS_13	0x2000
#define SIZE_CLASS_14	0x4000
#define SIZE_CLASS_15	0x8000
#endif

// 32 LEVEL SIZE CLASS
#ifndef CLASS_NUM_16
#define SIZE_CLASS_NUM	32

#define SIZE_CLASS_1	0x10
#define SIZE_CLASS_2	0x20
#define SIZE_CLASS_3	0x30
#define SIZE_CLASS_4	0x40
#define SIZE_CLASS_5	0x50
#define SIZE_CLASS_6	0x60
#define SIZE_CLASS_7	0x70
#define SIZE_CLASS_8	0x80
#define SIZE_CLASS_9	0xa0
#define SIZE_CLASS_10	0xc0
#define SIZE_CLASS_11	0xe0
#define SIZE_CLASS_12	0x100
#define SIZE_CLASS_13	0x140
#define SIZE_CLASS_14	0x180
#define SIZE_CLASS_15	0x1c0

#define SIZE_CLASS_16	0x200
#define SIZE_CLASS_17	0x280
#define SIZE_CLASS_18	0x300
#define SIZE_CLASS_19	0x380
#define SIZE_CLASS_20	0x400
#define SIZE_CLASS_21	0x500
#define SIZE_CLASS_22	0x600
#define SIZE_CLASS_23	0x700
#define SIZE_CLASS_24	0x800
#define SIZE_CLASS_25	0xc00
#define SIZE_CLASS_26	0x1000
#define SIZE_CLASS_27	0x1800
#define SIZE_CLASS_28	0x2000
#define SIZE_CLASS_29	0x3000
#define SIZE_CLASS_30	0x4000
#define SIZE_CLASS_31	0x5000
#endif

// returns proper size class number (Integer value 0 ~ 31)
// It works with get_class_root function.
static int get_size_class(size_t asize) {
	if (asize <= SIZE_CLASS_1) return 0;
	else if (asize <= SIZE_CLASS_2) return 1;
	else if (asize <= SIZE_CLASS_3) return 2;
	else if (asize <= SIZE_CLASS_4) return 3;
	else if (asize <= SIZE_CLASS_5) return 4;
	else if (asize <= SIZE_CLASS_6) return 5;
	else if (asize <= SIZE_CLASS_7) return 6;
	else if (asize <= SIZE_CLASS_8) return 7;

	else if (asize <= SIZE_CLASS_9) return 8;
	else if (asize <= SIZE_CLASS_10) return 9;
	else if (asize <= SIZE_CLASS_11) return 10;
	else if (asize <= SIZE_CLASS_12) return 11;
	else if (asize <= SIZE_CLASS_13) return 12;
	else if (asize <= SIZE_CLASS_14) return 13;
	else if (asize <= SIZE_CLASS_15) return 14;

#ifdef CLASS_NUM_16
	else return 15;
#endif

#ifndef CLASS_NUM_16
	else if (asize <= SIZE_CLASS_16) return 15;

	else if (asize <= SIZE_CLASS_17) return 16;
	else if (asize <= SIZE_CLASS_18) return 17;
	else if (asize <= SIZE_CLASS_19) return 18;
	else if (asize <= SIZE_CLASS_20) return 19;
	else if (asize <= SIZE_CLASS_21) return 20;
	else if (asize <= SIZE_CLASS_22) return 21;
	else if (asize <= SIZE_CLASS_23) return 22;
	else if (asize <= SIZE_CLASS_24) return 23;

	else if (asize <= SIZE_CLASS_25) return 24;
	else if (asize <= SIZE_CLASS_26) return 25;
	else if (asize <= SIZE_CLASS_27) return 26;
	else if (asize <= SIZE_CLASS_28) return 27;
	else if (asize <= SIZE_CLASS_29) return 28;
	else if (asize <= SIZE_CLASS_30) return 29;
	else if (asize <= SIZE_CLASS_31) return 30;
	else return 31;
#endif
}

// Returns root node of seg-list.
static void *get_class_root(int class) {
	return size_class_p + (DSIZE * class);
}

// Returns lower bound of size class
static size_t get_lower_bound(int class) {
	switch (class) {
		case 0: return 0;	
		case 1: return SIZE_CLASS_1;
		case 2: return SIZE_CLASS_2;
		case 3: return SIZE_CLASS_3;
		case 4: return SIZE_CLASS_4;
		case 5: return SIZE_CLASS_5;
		case 6: return SIZE_CLASS_6;
		case 7: return SIZE_CLASS_7;
		case 8: return SIZE_CLASS_8;
		case 9: return SIZE_CLASS_9;
		case 10: return SIZE_CLASS_10;
		case 11: return SIZE_CLASS_11;
		case 12: return SIZE_CLASS_12;
		case 13: return SIZE_CLASS_13;
		case 14: return SIZE_CLASS_14;
		case 15: return SIZE_CLASS_15;

#ifdef CLASS_NUM_16
		default: return 0x7fffffff;
#endif

#ifndef CLASS_NUM_16
		case 16: return SIZE_CLASS_16;
		case 17: return SIZE_CLASS_17;
		case 18: return SIZE_CLASS_18;
		case 19: return SIZE_CLASS_19;
		case 20: return SIZE_CLASS_20;
		case 21: return SIZE_CLASS_21;
		case 22: return SIZE_CLASS_22;
		case 23: return SIZE_CLASS_23;
		case 24: return SIZE_CLASS_24;
		case 25: return SIZE_CLASS_25;
		case 26: return SIZE_CLASS_26;
		case 27: return SIZE_CLASS_27;
		case 28: return SIZE_CLASS_28;
		case 29: return SIZE_CLASS_29;
		case 30: return SIZE_CLASS_30;
		case 31: return SIZE_CLASS_31;
		default: return 0x7fffffff;
#endif
	}
}

// Function Declaration
void mm_check(void);
static void set_boundary_tag(void*, size_t, size_t);
static void insert_block(void*, size_t);
static void remove_from_list(void*);
static void *extend_heap(size_t);
static void *extend_heap_bulk(size_t);
static void *coalesce(void*);
static void *find_fit(size_t, int*);
static void place(void*, size_t, int);

/*
 * mm_init - initialize the malloc package.
 * Setting up roots for free lists and prologue & epilogue
 */
int mm_init(range_t **ranges)
{
	int i;
	void *temp;
	if ((size_class_p = mem_sbrk((SIZE_CLASS_NUM+2) * DSIZE)) == (void*)-1) return -1;

	// Size Class Root Setup
	for (i = 0; i < SIZE_CLASS_NUM; i++) {
		temp = size_class_p + i * DSIZE;
		// Direct itself
		SET_NEXT_F(temp, temp);
		SET_PREV_F(temp, temp);
	}

	// Create Prologue and Epilogue for manageing edge cases when coalescing & mm_exit.
	prologue = size_class_p + (SIZE_CLASS_NUM + 1) * DSIZE;
	set_boundary_tag(prologue, DSIZE, ALLOCTAG);
	PUT(prologue + WSIZE, PACK(0, ALLOCTAG));

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
	int found_class;

	if (size == 0) return NULL;

	if ((bp = find_fit(asize, &found_class)) != NULL) {
		place(bp, asize, found_class);
		return bp;
	}

	// Create new block as bulk (Only if the request size is small enough)
	// CHUNKSIZE == 1 << 9 == 512
	if (asize <= CHUNKSIZE) {
		if ((bp = extend_heap_bulk(asize)) == NULL) return NULL;
	}

	// Normal case
	else {
		if ((bp = extend_heap(asize)) == NULL) return NULL;
	}

	// No need to check size class becuase bp is newly created block.
	place(bp, asize, 0);
//	mm_check();
	return bp;
}

/*
 * mm_free - Freeing a block
 * Set header & footer as free.
 * If it was assigned from bulk block, It keeps bulk tag bit.
 */
void mm_free(void *ptr)
{
	size_t size = GET_SIZE(HDRP(ptr));
	set_boundary_tag(ptr, size, GET_ISBULK(HDRP(ptr)));
	insert_block(ptr, size);

	coalesce(ptr);

	/* DON't MODIFY THIS STAGE AND LEAVE IT AS IT WAS */
	if (gl_ranges)
		remove_range(gl_ranges, ptr);
}

/*
 * mm_realloc - Standalone Realloc
 * ptr == NULL : same with malloc
 * t == 0 : same with free
 * if new size is smaller than orignal : shrink current block
 * if new size is big && next is free : merge 2 blocks and then realloc again.
 * && next is allocaed : allocate new memory space with mm_malloc. and copy data
 */
void* mm_realloc(void *ptr, size_t t)
{
	void *newbp;
	size_t copy_index = 0;
	size_t orig_size;
	size_t next_size;
	size_t new_asize;

	if (ptr == NULL) {
		return mm_malloc(t);
	}
	if (t == 0) {
		mm_free(ptr);
		return NULL;
	}

	orig_size = GET_SIZE(HDRP(ptr));
	new_asize = ALIGN(t + DSIZE);

	// Shrink if newsize is smaller than orignal size.
	if (orig_size - new_asize >= MIN_BLOCK_SIZE) {
		set_boundary_tag(ptr, new_asize, GET_ISBULK(HDRP(ptr)) | ALLOCTAG);
		set_boundary_tag(NEXT_BLKP(ptr), orig_size - new_asize, GET_ISBULK(HDRP(ptr)));
		insert_block(NEXT_BLKP(ptr), orig_size - new_asize);
		coalesce(NEXT_BLKP(ptr));
		return ptr;
	}
	else if (new_asize <= orig_size) {
		return ptr;
	}

	// Merge with next block if next is free
	if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {
		next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		remove_from_list(NEXT_BLKP(ptr));
		set_boundary_tag(ptr, orig_size + next_size, ALLOCTAG);
		// After merge, start from the beginning by recursive call.
		return mm_realloc(ptr, t);
	}

	// Allocate New Block
	newbp = mm_malloc(t);
	// Copy Data
	while (copy_index < orig_size - DSIZE) {
		PUT(newbp + copy_index, GET(ptr + copy_index));
		copy_index += 4;
	}
	// Free original memory block
	mm_free(ptr);
	return newbp;
}

/*
 * mm_exit - finalize the malloc package.
 * Check all blocks from prologue to epilogue.
 * If it meets allocated one, frees it.
 */
void mm_exit(void)
{
	void *probe = NEXT_BLKP(prologue);
	while (GET_SIZE(HDRP(probe))) {
		if (GET_ALLOC(HDRP(probe))) mm_free(probe);
		probe = NEXT_BLKP(probe);
	}
}


/*
 * mm_check - Check if all blocks are correctly tagged and linked.
 */
void mm_check(void) {
	int error_int1, error_int2;
	void *error_address = NULL;

	void *classroot;
	void *probe, *hdrp;
	size_t classlb, classub, size;
	unsigned int error = 0;
	int i;

	// Check if all blocks are in proper size-class free list.
	for (i = 0; i < SIZE_CLASS_NUM; i++) {
		classroot = get_class_root(i);
		probe = NEXT_FREE(classroot);
		classlb = get_lower_bound(i);
		classub = get_lower_bound(i+1);
		while (probe != classroot) {
			hdrp = HDRP(probe);
			// Check if allocated block is connected
			if (GET_ALLOC(hdrp)) {
				error = ERROR_ALLOC_IN_FREE_LIST;
				error_int1 = i;
				error_address = probe;
				break;
			}
			size = GET_SIZE(hdrp);
			// Check if block is in proper size-class free list.
			if (size <= classlb || ((!GET_ISBULK(hdrp)) && (classub < size))) {
				error = ERROR_WRONG_SIZE_CLASS;
				error_int1 = i;
				error_int2 = size;
				error_address = probe;
				break;
			}
			// Mark all free blocks which are accessible from root node of each seglist.
			FREE_MARK(probe);
			probe = NEXT_FREE(probe);
		}
		if (error) break;
	}

	// Check if 2 free blocks are adjacent
	// Check if unmarked free block exists.
	if (!error) {
		probe = NEXT_BLKP(prologue);
		// until epilogue
		while (GET_SIZE(HDRP(probe))) {
			if (!GET_ALLOC(HDRP(PREV_BLKP(probe))) && !GET_ALLOC(HDRP(probe))) {
				if (!(GET_ISBULK(HDRP(PREV_BLKP(probe))) && GET_ISBULK(HDRP(probe)))) {
					error = ERROR_COALESCE_FAILED;
					error_address = probe;
					break;
				}
			}
			// check if unmarked free block exists
			if (!GET_ALLOC(HDRP(probe))) {
				if (GET_ISMARKED(probe)) {
					FREE_MARK_ERASE(probe);
				}
				else {
					error = ERROR_ISOLATED_FREE_BLOCK;
					error_address = probe;
					break;
				}
			}
			probe = NEXT_BLKP(probe);
		}
	}

	switch (error) {
		case ERROR_ALLOC_IN_FREE_LIST:
			printf("Allocated block is in free list.\n");
			printf("Size Class: %d\nError Location: %p\n", error_int1, error_address);
			break;
		case ERROR_WRONG_SIZE_CLASS:
			printf("Free block is not in valid size class.\n");
			printf("Size Class: %d\nCurrent Size: %d\n", error_int1, error_int2);
			printf("Error Location: %p\n", error_address);
			break;
		case ERROR_COALESCE_FAILED:
			printf("Coalesce Error\n");
			printf("Error Location: %p\n", error_address);
			break;
		case ERROR_ISOLATED_FREE_BLOCK:
			printf("Not accessible free block exists\n");
			printf("Error Location: %p\n", error_address);
			break;
	}
}

// Set boundary tag
static void set_boundary_tag(void *bp, size_t asize, size_t tagbit) {
	PUT(HDRP(bp), PACK(asize, tagbit));
	PUT(FTRP(bp), PACK(asize, tagbit));
}

// Insert block into proper size class
// Size class is determined by parameter 'asize' only. It does not check bp's header.
// It works with bulk block implementation.
// Insert new block at the beginning of the list.
static void insert_block(void *bp, size_t asize) {
	void *sclassp = get_class_root(get_size_class(asize));
	SET_PREV_F(bp, sclassp);
	SET_NEXT_F(bp, NEXT_FREE(sclassp));
	SET_PREV_F(NEXT_FREE(sclassp), bp);
	SET_NEXT_F(sclassp, bp);
}

// Remove block bp from size class list.
// After remove the block, re-connect the pointers.
static void remove_from_list(void *bp) {
	SET_NEXT_F(PREV_FREE(bp), NEXT_FREE(bp));
	SET_PREV_F(NEXT_FREE(bp), PREV_FREE(bp));
}

// Extend heap
// Extend heap with mem_sbrk function. Then, update epilogue block
// Insert newly extended block and coalesce.
static void *extend_heap(size_t asize) {
	char *bp;

	if ((bp = mem_sbrk(asize)) == (void*)-1) return NULL;

	set_boundary_tag(bp, asize, FREETAG);
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, ALLOCTAG));
	// Insert new block into proper free list.
	insert_block(bp, asize);
	
	return coalesce(bp);
}

// Extend heap as bulk free block.
// Basically same structure with extend_heap function.
// But newly created block will be inserted into smaller class list.
// This function not calls coalesce. It uses it's own coalescing mechanism.
static void *extend_heap_bulk(size_t asize) {
	char *bp;
	size_t bulksize = BULK_CREATE_NUM * asize;

	// New block is created with bulksize big
	if ((bp = mem_sbrk(bulksize)) == (void*)-1) return NULL;

	// This function uses it's own coalescing mechanism.
	// When previous block is free but generated as bulk block,
	// that's the only case that 2 free blocks become adjacent.
	if (!GET_ALLOC(HDRP(PREV_BLKP(bp))) && !GET_ISBULK(HDRP(PREV_BLKP(bp)))) {
		bp = PREV_BLKP(bp);
		remove_from_list(bp);
		bulksize += GET_SIZE(HDRP(bp));
	}

	set_boundary_tag(bp, bulksize, BULKTAG);
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, ALLOCTAG));

	// insert_block uses asize NOT bulksize.
	// This blocks will be inserted into smaller size-class list than it's real size.
	insert_block(bp, asize);

	// It returns bp without calling coalesce function.
	return bp;
}

// Coalesce free blocks
// This function does not check if it was generated as bulk block or not
// to prevent unnecessary external fragmentation.
static void *coalesce(void *bp) {
	void *nextbp;
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && next_alloc) return bp;

	remove_from_list(bp);

	// Check next block.
	if (!next_alloc) {
		nextbp = NEXT_BLKP(bp);
		size += GET_SIZE(HDRP(nextbp));
		remove_from_list(nextbp);
	}

	// Check previous block.
	if (!prev_alloc) {
		bp = PREV_BLKP(bp);
		remove_from_list(bp);
		size += GET_SIZE(HDRP(bp));
	}

	set_boundary_tag(bp, size, FREETAG);
	insert_block(bp, size);
	return bp;
}

// Find blocks from size class lists.
// Implemented with Segregated Free Lists (Segregated fits)
// Choosen classnum will be sent through class variable
static void *find_fit(size_t asize, int *class) {
	int classnum = get_size_class(asize);
	void *sclassp;
	void *probe;
	for (;classnum < SIZE_CLASS_NUM; classnum++) {
		sclassp = get_class_root(classnum);
		probe = NEXT_FREE(sclassp);
		while (probe != sclassp) {
			if (GET_SIZE(HDRP(probe)) >= asize) {
				*class = classnum;
				return probe;
			}
			probe = NEXT_FREE(probe);
		}
	}
	return NULL;
}

// Place new blocks
// If the block is big enough and it was generated as bulk block,
// New free block which was created from split remains same size-class free list.
static void place(void *bp, size_t asize, int class) {
	size_t bsize = GET_SIZE(HDRP(bp));

	if (bsize - asize >= MIN_BLOCK_SIZE) {
		// If selected free block was generated as bulk block
		// Remaining space should be big enough to fit into same size-class list.
		// If it is not handled, new small free block will never be allocated.
		// Because it's in seg-list, it should be re-inserted.
		if (GET_ISBULK(HDRP(bp)) && (bsize - asize > get_lower_bound(class))) {
			set_boundary_tag(bp, asize, BULKTAG | ALLOCTAG);

			// Reorganize free block list
			// Block shrinks. Not re-inserted into free list.
			SET_PREV_F(NEXT_BLKP(bp), PREV_FREE(bp));
			SET_NEXT_F(NEXT_BLKP(bp), NEXT_FREE(bp));
			SET_NEXT_F(PREV_FREE(bp), NEXT_BLKP(bp));
			SET_PREV_F(NEXT_FREE(bp), NEXT_BLKP(bp));

			bp = NEXT_BLKP(bp);
			set_boundary_tag(bp, bsize - asize, BULKTAG | FREETAG);
		}
		// Same implementation with textbook from here.
		else {
			remove_from_list(bp);
			set_boundary_tag(bp, asize, ALLOCTAG);

			bp = NEXT_BLKP(bp);
			set_boundary_tag(bp, bsize - asize, FREETAG);
			insert_block(bp, bsize - asize);
		}
	}
	else {
		remove_from_list(bp);
		set_boundary_tag(bp, bsize, ALLOCTAG);
	}
}
