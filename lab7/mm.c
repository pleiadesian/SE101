/*
 * mm-naive.c - The correct, efficient and fast malloc package.
 * 
 * In this new approach, a block is allocated by scanning best fit free
 * blocks incrementing.　Free blocks is maintained by explicit free list.
 * If no suitable block is found, then increase the brk pointer. A block
 * is consisted of payload, padding, header and footer. Contiguous free
 * blocks are coalesced. Realloc is implemented based on logic of mm_malloc
 * and mm_free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE   (1<<12) /* Extend heap by this amount (bytes) */

#define MAX(x, y)   ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)   ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)      (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~0x7)
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)    ((char *)(bp) - WSIZE)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/* Get successor and predecessor pointer in the free block */
#define PRED(bp)    ((char *)(bp))
#define SUCC(bp)    ((char *)(bp) + WSIZE)

/* Get previous and next free block ptr */
#define NEXT_FBLKP(bp)  ((char *)GET(SUCC(bp)))
#define PREV_FBLKP(bp)  ((char *)GET(PRED(bp)))


static char* heap_listp = NULL;     /* Point to the start of heap */
static char* free_listp[7] = NULL;    /* Point to the list of free blocks */

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
int mm_check();

/*
 * get_block_index - get index of free block where it should be inserted
 */
int get_block_index(int size)
{
    if (size <= 512) {
        return 0;
    } else if (size > 512 && size <= 1024) {
        return 1;
    } else if (size > 1024 && size <= 2048) {
        return 2;
    } else if (size > 2048 && size <= 4096) {
        return 3;
    } else if (size > 4096 && size <= 8192) {
        return 4;
    } else if (size > 8192 && size <= 16384) {
        return 5;
    } else{
        return 6;
    }
}

/*
 * insert_block - insert a free block into corresponding free list
 */
void insert_block(char *bp)
{
    if (bp == NULL)
        return;

    /* find corresponding free list */
    int csize = GET_SIZE(HDRP(bp));
    int index = get_block_index(csize);
    char *listp = free_listp[index];

    if (listp == NULL) {
        PUT(SUCC(bp), 0);
    } else {
        PUT(SUCC(bp), (long)listp);
        PUT(PRED(listp), (long)bp);
    }
    PUT(PRED(bp), 0);
    &free_listp[index] = bp;
}

/*
 * remove_block - remove a free block from corresponding free list
 */
void remove_block(char *bp)
{
    if (bp == NULL)
        return;

    /* find corresponding free list */
    int csize = GET_SIZE(HDRP(bp));
    int index = get_block_index(csize);
    char *listp = free_listp[index];

    char *prev = PREV_FBLKP(bp);
    char *next = NEXT_FBLKP(bp);
    if (!prev && !next) {
        &free_listp[index] = NULL;
    } else if (!prev && next) {
        PUT(PRED(next), 0);
        &free_listp[index] = next;
    } else if (prev && !next) {
        PUT(SUCC(prev), 0);
    } else {
        PUT(SUCC(prev), GET(SUCC(bp)));
        PUT(PRED(next), GET(PRED(bp)));
    }
    PUT(PRED(bp), 0);
    PUT(SUCC(bp), 0);
}


/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                             /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));    /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));    /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));        /* Epilogue header */
    heap_listp += (2*WSIZE);

    /* extend heap */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * extend_heap - extends the heap with a new free block.
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

//    mm_check();

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));           /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));           /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue header */

    /* Coalesce if the previous block is free */
    return coalesce(bp);
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;       /* Adjusted block size */
    size_t extendsize;  /* Amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;

//    int newsize = ALIGN(size + SIZE_T_SIZE);
//    void *p = mem_sbrk(newsize);
//    if (p == (void *)-1)
//	return NULL;
//    else {
//        *(size_t *)p = size;
//        return (void *)((char *)p + SIZE_T_SIZE);
//    }
}

static void *find_fit(size_t asize)
{
    void *bp;
    void *best_fit = NULL;
    int diff = 1000000; /* Gap between current block size and required block size */

    /* First-fit search */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }

    /* Best-fit search */
//    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
//        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
//            int curr_diff = (int)GET_SIZE(HDRP(bp)) - (int)asize;
//            if (curr_diff < diff) {
//                diff = curr_diff;
//                best_fit = bp;
//            }
//        }
//    }

    return best_fit;
//    return NULL;
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= (2*DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {             /* Prev block and next block has been allocated */
        return bp;
    }else if(prev_alloc && !next_alloc) {       /* Coalesce with next free block */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }else if(!prev_alloc && next_alloc) {       /* Coalesce with prev free block */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }else{                                      /* Coalesce with prev and next free block */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

/*
 * mm_realloc - Check if csize can be the reallocated space before call mm_malloc and mm_free.
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (size == 0) {
        mm_free(ptr);
        return ptr;
    }

    if (ptr == NULL) {
        return mm_malloc(size);
    }

    size_t asize = 0;
    size_t csize = GET_SIZE(HDRP(ptr));
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((csize >= asize) && (csize < asize + DSIZE)) {  /* Csize is enough space for reallocated block */
        return ptr;
    } else if(csize >= asize + DSIZE) {                 /* Csize should be split */
        size_t diff = csize - asize;
        void *next_block = NEXT_BLKP(ptr);
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));
        void *remain_block = NEXT_BLKP(ptr);

        if (GET_ALLOC(HDRP(next_block))) {          /* No coalescing with next block */
            PUT(HDRP(remain_block), PACK(diff, 0));
            PUT(FTRP(remain_block), PACK(diff, 0));
        } else {                                    /* Coalescing with next free block */
            diff += GET_SIZE(HDRP(next_block));
            PUT(HDRP(remain_block), PACK(diff, 0));
            PUT(FTRP(remain_block), PACK(diff, 0));
        }
        return ptr;
    } else {                                            /* Heap should be extended for reallocated space */
        void *next_block = NEXT_BLKP(ptr);

        /* Need coalescing? */
        if (!GET_ALLOC(HDRP(next_block))) {
            size_t tot_size = csize + GET_SIZE(HDRP(next_block));
            /* Coalescing current block and next free block to be the reallocated block */
            if (tot_size >= asize) {
                size_t diff = tot_size - asize;
                /* Need splitting? */
                if (diff < DSIZE) {
                    PUT(HDRP(ptr), PACK(tot_size, 1));
                    PUT(FTRP(ptr), PACK(tot_size, 1));
                } else {
                    PUT(HDRP(ptr), PACK(asize, 1));
                    PUT(FTRP(ptr), PACK(asize, 1));
                    PUT(HDRP(NEXT_BLKP(ptr)), PACK(diff, 0));
                    PUT(FTRP(NEXT_BLKP(ptr)), PACK(diff, 0));
                }
                return ptr;
            }
        }

        void *newptr = mm_malloc(size);
        if (newptr != ptr)
            memcpy(newptr, ptr, csize - DSIZE);
        mm_free(ptr);
        return newptr;
    }

//    void *oldptr = ptr;
//    void *newptr;
//    size_t copySize;


//    newptr = mm_malloc(size);
//    if (newptr == NULL)
//      return NULL;
//    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
//    if (size < copySize)
//      copySize = size;
//    memcpy(newptr, oldptr, copySize);
//    mm_free(oldptr);
//    return newptr;
}

/*
 * mm_check -  A heap checker that scans the heap and checks it for consistency.
 */
int mm_check()
{
    char *bp;

    /* Is every block in the free list marked as free? */
//    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
//NULL
//    }

//    long size = (long)mem_sbrk(0);
//    fprintf(stderr, "%d \n", size);

    /* Are there any contiguous free blocks that somehow escaped coalescing? */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
            fprintf(stderr, "contiguous free blocks");
            return -1;
        }
    }
    return 0;
}