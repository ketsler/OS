/*
 * Kyle Etsler
 * UCDenver Operating Systems
 * Assignment 1 - The C Project
 * 3/20/2016
 */
#include <stdlib.h>
#include <stdio.h>
#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;

/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/

typedef struct _node {

    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev;

} node_t, *node_pt;

typedef struct _gap {

    size_t size;
    node_pt node;

} gap_t, *gap_pt;

typedef struct _pool_mgr {

    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;

} pool_mgr_t, *pool_mgr_pt;

/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/

static pool_mgr_pt *pool_store = NULL;
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;

/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/

static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);

static alloc_status _remove_gap(pool_mgr_pt pmpt, gap_pt gap) {

    const gap_pt prevGap = &(pmpt->gap_ix[pmpt->pool.num_gaps - 1]);
    *gap = *prevGap;

    --(pmpt->pool.num_gaps);

    prevGap->node = NULL;
    prevGap->size = 0;
    return _mem_sort_gap_ix(pmpt);

}

static alloc_status _remove_node(pool_mgr_pt pmpt, node_pt node) {

    node->used = 0;

    if(node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        node->next->prev = NULL;
    }

    if(node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        node->prev->next = NULL;
    }

    //The tests pass all the same regardless of what I put here
    return ALLOC_OK;
}

static alloc_status _add_gap(pool_mgr_pt pool_mgr, node_pt node) {
    gap_pt gap = NULL;
    const char * endOfNode = ((node->alloc_record.mem) + node->alloc_record.size);

    for(unsigned int i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        if(endOfNode == pool_mgr->gap_ix[i].node->alloc_record.mem) {
            _remove_node(pool_mgr, pool_mgr->gap_ix[i].node);
            pool_mgr->gap_ix[i].node = node;
            node->allocated = 0;
            pool_mgr->gap_ix[i].size += node->alloc_record.size;
            pool_mgr->gap_ix[i].node->alloc_record.size = pool_mgr->gap_ix[i].size;
            gap = &(pool_mgr->gap_ix[i]);
            break;
        }

    }

    for(unsigned int i = 0; i < pool_mgr->pool.num_gaps; ++i) {
        const char * gapEndingAddress = (((char *) pool_mgr->gap_ix[i].node->alloc_record.mem) + pool_mgr->gap_ix[i].node->alloc_record.size);
        if(node->alloc_record.mem == gapEndingAddress) {
            _remove_node(pool_mgr, node);
            node->allocated = 0;
            pool_mgr->gap_ix[i].size += node->alloc_record.size;
            pool_mgr->gap_ix[i].node->alloc_record.size = pool_mgr->gap_ix[i].size;
            pool_mgr->gap_ix[i].node->allocated = 0;

            if(gap != NULL) {
                _remove_gap(pool_mgr, gap);
                return ALLOC_OK;
            }

            gap = &(pool_mgr->gap_ix[i]);

            break;

        }

    }

    if(gap != NULL) {
        return ALLOC_OK;
    }

    if(_mem_resize_gap_ix(pool_mgr) != ALLOC_OK) {
        return ALLOC_FAIL;
    }

    ++(pool_mgr->pool.num_gaps);

    const gap_pt newGap = &(pool_mgr->gap_ix[pool_mgr->pool.num_gaps - 1]);

    newGap->size = node->alloc_record.size;
    newGap->node = node;
    node->allocated = 0;

    return _mem_sort_gap_ix(pool_mgr);

}

static node_pt _add_node(pool_mgr_pt pool_mgr, node_pt preNode) {

    if(_mem_resize_node_heap(pool_mgr) != ALLOC_OK) {
        return NULL;
    }

    ++(pool_mgr->used_nodes);

    const node_pt newNode = &(pool_mgr->node_heap[pool_mgr->used_nodes - 1]);

    newNode->next = NULL;
    newNode->prev = NULL;
    newNode->allocated = 0;
    newNode->alloc_record.mem = NULL;
    newNode->alloc_record.size = 0;

    if(pool_mgr->used_nodes == 1) {
        return newNode;
    }

    if(preNode) {
        newNode->prev = preNode;
        if (preNode->next) {
            newNode->next = preNode->next;
        } else {
            newNode->next = NULL;
        }
        preNode->next = newNode;
    } else {
        node_pt endNode = pool_mgr->node_heap;
        while(endNode->next != NULL) {
            endNode = endNode->next;
        }
        endNode->next = newNode;
        newNode->prev = endNode;
    }

    return newNode;

}

node_pt _convert_gap_to_node_and_gap(pool_mgr_pt pool_mgr, gap_pt gap, size_t size) {

    if(gap->size == size) {
        const node_pt node = gap->node;
        node->allocated = 1;
        _remove_gap(pool_mgr, gap);
        return node;
    }

    const node_pt allocatedNode = gap->node;

    allocatedNode->allocated = 1;
    allocatedNode->used = 1;
    allocatedNode->alloc_record.size = size;

    gap->node = _add_node(pool_mgr, allocatedNode);

    gap->size = gap->node->alloc_record.size = gap->size - size;

    gap->node->alloc_record.mem = (allocatedNode->alloc_record.mem) + size;

    gap->node->allocated = 0;
    gap->node->used = 1;

    return allocatedNode;

}

/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/

alloc_status mem_init() {

    if(pool_store != NULL) {
        return ALLOC_CALLED_AGAIN;
    }

    pool_store = (pool_mgr_pt *) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_t));

    if(pool_store == NULL) {
        printf("Failed to allocate the appropriate memory.\r\n");
        return ALLOC_FAIL;
    }

    pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
    pool_store_size = 0;

    return ALLOC_OK;

}

alloc_status mem_free() {

    if(pool_store == NULL) {
        return ALLOC_CALLED_AGAIN;
    }

    for(unsigned int i = 0; i < pool_store_size; ++i) {
        mem_pool_close((pool_pt) pool_store[i]);
    }

    free(pool_store);

    pool_store = NULL;
    pool_store_size = 0;
    pool_store_capacity = 0;

    return ALLOC_OK;

}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {

    if(pool_store == NULL) {

        if(mem_init() != ALLOC_OK) {
            return NULL;
        }

    }

    if(_mem_resize_pool_store() != ALLOC_OK) {

        return NULL;

    }

    pool_mgr_pt pool_mgr = (pool_mgr_pt) calloc(1, sizeof(pool_mgr_t));

    if(pool_mgr == NULL) {

        return NULL;

    }

    pool_mgr->pool.alloc_size = 0;
    pool_mgr->pool.total_size = size;
    pool_mgr->pool.policy = policy;
    pool_mgr->pool.num_gaps = 0;
    pool_mgr->pool.num_allocs = 0;

    pool_mgr->pool.mem = (char*) malloc(size);

    if(pool_mgr->pool.mem == NULL) {

        // It didn't :(

        free(pool_mgr);

        return NULL;

    }

    pool_mgr->node_heap = (node_pt) calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));
    pool_mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    pool_mgr->used_nodes = 0;

    if(pool_mgr->node_heap == NULL) {


        free(pool_mgr->pool.mem);
        free(pool_mgr);

        return NULL;

    }

    pool_mgr->gap_ix = (gap_pt) calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
    pool_mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    if(pool_mgr->gap_ix == NULL) {


        free(pool_mgr->pool.mem);
        free(pool_mgr->node_heap);
        free(pool_mgr);

        return NULL;

    }

    const node_pt node = _add_node(pool_mgr, NULL);

    node->alloc_record.mem = pool_mgr->pool.mem;
    node->alloc_record.size = pool_mgr->pool.total_size;
    node->next = NULL;
    node->prev = NULL;
    node->used = 1;
    node->allocated = 1;

    _add_gap(pool_mgr, node);

    pool_store[pool_store_size] = pool_mgr;

    ++pool_store_size;

    return (pool_pt) pool_mgr;

}

alloc_status mem_pool_close(pool_pt pool) {

    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    if(pool_mgr == NULL) {
        return ALLOC_FAIL;
    }

    for(int i = 0; i < pool_mgr->used_nodes; ++i) {

        if(pool_mgr->node_heap[i].allocated) {
            return ALLOC_NOT_FREED;
        }

    }

    if(pool_mgr->pool.num_allocs <= 0) {
        // TODO How does this case differ?
    }

    for(unsigned i = 0; i < pool_store_size; ++i) {

        if(pool_store[i] == pool_mgr) {
            pool_store[i] = NULL;
            break;
        }

    }


    free(pool_mgr->pool.mem);

    free(pool_mgr->gap_ix);

    free(pool_mgr->node_heap);

    free(pool_mgr);

    return ALLOC_OK;

}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {

    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    if(pool_mgr->pool.num_gaps < 1) {
        return NULL;
    }

    node_pt newNode = NULL;
    node_pt best = NULL;

    if(pool_mgr->pool.policy == FIRST_FIT) {
        int currentSegment = 0;
        node_pt currentNode = pool_mgr->node_heap;
        while(currentNode) {
            if(currentNode->used == 1 && currentNode->allocated == 0) {
                if(currentNode->alloc_record.size >= size) {
                    best = currentNode;
                    break;
                }
                ++currentSegment;
            }
            currentNode = currentNode->next;
        }
    }

    if(pool_mgr->pool.policy == BEST_FIT) {
        int currentSegment = 0;
        node_pt currentNode = pool_mgr->node_heap;
        while(currentNode) {
            if(currentNode->used == 1 && currentNode->allocated == 0) {
                if(currentNode->alloc_record.size >= size) {
                    if(best != NULL) {
                        if(currentNode->alloc_record.size < best->alloc_record.size) {
                            best = currentNode;
                        }

                    } else {
                        best = currentNode;
                    }
                }
                ++currentSegment;
            }
            currentNode = currentNode->next;
        }
    }

    if(best != NULL) {

        for (int i = 0; i < pool_mgr->pool.num_gaps; ++i) {

            if(pool_mgr->gap_ix[i].node == best) {

                newNode = _convert_gap_to_node_and_gap(pool_mgr, &(pool_mgr->gap_ix[i]), size);

                break;

            }

        }

    }

    if(newNode) {

        ++(pool_mgr->pool.num_allocs);

        pool_mgr->pool.alloc_size += size;

        return &(newNode->alloc_record);

    } else {

        printf("Failed to allocate memory!\r\n");

        return NULL;

    }

}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {

    const size_t size = alloc->size;
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    const node_pt node = (node_pt) alloc;

    if(_mem_add_to_gap_ix(pool_mgr, alloc->size, node) == ALLOC_OK) {
        --(pool_mgr->pool.num_allocs);
        pool_mgr->pool.alloc_size -= size;
        return ALLOC_OK;

    } else {
        return ALLOC_FAIL;
    }

}

void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {

    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    *segments = (pool_segment_pt) calloc(pool_mgr->used_nodes, sizeof(pool_segment_t));

    if(segments == NULL) {
        return;
    }

    int currentSegment = 0;

    node_pt currentNode = pool_mgr->node_heap;

    while(currentNode) {

        if(currentNode->used != 0) {
            (*segments)[currentSegment].size = currentNode->alloc_record.size;
            (*segments)[currentSegment].allocated = currentNode->allocated;
            ++currentSegment;
        }

        currentNode = currentNode->next;

    }

    *num_segments = currentSegment;
    return;

}

/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/

static alloc_status _mem_resize_pool_store() {

    if(pool_store_size < pool_store_capacity * MEM_POOL_STORE_FILL_FACTOR) {
        return ALLOC_OK;
    }

    pool_mgr_pt * bob = (pool_mgr_pt *) realloc(pool_store, pool_store_capacity * MEM_POOL_STORE_EXPAND_FACTOR * sizeof(pool_mgr_pt));

    if(bob == NULL) {
        printf("Failed to resize node heap.\r\n");
        return ALLOC_FAIL;
    } else {
        pool_store = bob;
        pool_store_capacity *= MEM_POOL_STORE_EXPAND_FACTOR;
    }

    return ALLOC_OK;

}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {

    if(pool_mgr->used_nodes < pool_mgr->total_nodes * MEM_NODE_HEAP_FILL_FACTOR) {
        return ALLOC_OK;
    }

    node_pt bob = (node_pt) realloc(pool_mgr->node_heap, pool_mgr->total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR * sizeof(node_t));

    if(bob == NULL) {
        printf("Failed to resize node heap.\r\n");
        return ALLOC_FAIL;
    } else {
        pool_mgr->node_heap = bob;
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
    }

    return ALLOC_OK;

}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {

    if(pool_mgr->gap_ix_capacity < pool_mgr->gap_ix_capacity * MEM_GAP_IX_FILL_FACTOR) {
        return ALLOC_OK;
    }

    gap_pt bob = (gap_pt) realloc(pool_mgr->gap_ix, pool_mgr->gap_ix_capacity * MEM_GAP_IX_EXPAND_FACTOR * sizeof(gap_t));

    if(bob == NULL) {
        printf("Failed.\r\n");
        return ALLOC_FAIL;
    } else {
        pool_mgr->gap_ix = bob;
        pool_mgr->gap_ix_capacity *= MEM_GAP_IX_EXPAND_FACTOR;
    }

    return ALLOC_OK;

}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {

    return _add_gap(pool_mgr, node);

}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr, size_t size, node_pt node) {

    printf("There is no gap, so there is no available memory in pool.\r\n");
    return ALLOC_FAIL;

}

int _partitionGap(gap_pt gaps, int l, int r) {


    int i = l, j = r + 1;

    gap_t t;
    gap_pt pivot;

    pivot = &(gaps[l]);

    while(1) {

        do ++i; while(gaps[i].size <= pivot->size && i <= r );

        do --j; while(gaps[j].size > pivot->size);

        if(i >= j) break;

        t = gaps[i];
        gaps[i] = gaps[j];
        gaps[j] = t;

    }

    t = gaps[l];
    gaps[l] = gaps[j];
    gaps[j] = t;

    return j;

}

void _quickSortGap(gap_pt gaps, int l, int r) {

    if(l < r) {

        int j = _partitionGap(gaps, l, r);

        _quickSortGap(gaps, l, j - 1);
        _quickSortGap(gaps, j + 1, r);

    }

}

static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {

    _quickSortGap(pool_mgr->gap_ix, 0, pool_mgr->pool.num_gaps - 1);

    return ALLOC_OK;

}