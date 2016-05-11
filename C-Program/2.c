/*
 * Kyle Etsler
 * UCDenver Operating Systems
 * Assignment 1 - The C Project
 * 3/20/2016
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
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
    pool_store = (pool_mgr_pt *) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_t));
    if(pool_store == NULL)
    {
        printf("Unable to allocate memory\nFunction:mem_init()");
        return ALLOC_FAIL;
    }

    pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
    pool_store_size = 0;

    return ALLOC_OK;
}

alloc_status mem_free() {

    if(pool_store != NULL){
        for(size_t i = 0; i< pool_store_size;i++){
            if(pool_store[i] != NULL) {

                for(size_t i = pool_store[i]->pool.num_gaps ; i >= 0; i--)
                {
                    free(&pool_store[i]->gap_ix[i]);
                }

                mem_pool_close(&pool_store[i]->pool);
                node_pt pt = pool_store[i]->node_heap;
                for(size_t i = pool_store[i]->total_nodes; i >= 0; i--)
                {
                    free(&pool_store[i]->node_heap[i]);
                }

                free(&pool_store[i]);
                pool_store[i] = NULL;
            }
        }
        free(pool_store);
        pool_store = NULL;
        pool_store_size = 0;
        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
        return ALLOC_OK;
    }

    return ALLOC_CALLED_AGAIN;

}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    assert(pool_store != NULL);
    alloc_status status = _mem_resize_pool_store();
    if (status == ALLOC_FAIL) {return NULL;}

    pool_mgr_pt pool_mgr = (pool_mgr_pt)malloc(sizeof(pool_mgr_t));
    pool_mgr->pool.mem = (char*)malloc(size);
    pool_mgr->node_heap = (node_pt)malloc(MEM_NODE_HEAP_INIT_CAPACITY * sizeof(node_t));
    pool_mgr->gap_ix = (gap_pt)malloc(MEM_GAP_IX_INIT_CAPACITY * sizeof(gap_t));

    if (pool_mgr == NULL || pool_mgr->pool.mem == NULL || pool_mgr->node_heap == NULL || pool_mgr->gap_ix == NULL)
    {
        free(pool_mgr->pool.mem);
        free(pool_mgr->node_heap);
        free(pool_mgr->gap_ix);
        free(pool_mgr);
        return NULL;
    }

    pool_mgr->pool.policy = policy;
    pool_mgr->pool.total_size = size;
    pool_mgr->pool.num_gaps = 1;
    pool_mgr->node_heap[0].alloc_record.size = size;
    pool_mgr->node_heap[0].alloc_record.mem = pool_mgr->pool.mem;
    pool_mgr->node_heap[0].used = 1;
    pool_mgr->node_heap[0].allocated = 0;
    pool_mgr->node_heap[0].next = NULL;
    pool_mgr->node_heap[0].prev = NULL;
    pool_mgr->gap_ix[0].node = pool_mgr->node_heap;
    pool_mgr->gap_ix[0].size = size;
    pool_mgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
    pool_mgr->used_nodes = 1;
    pool_mgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    pool_store[pool_store_size] = pool_mgr;
    pool_store_size++;

    return (pool_pt)pool_mgr;
}

alloc_status mem_pool_close(pool_pt pool) {

    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    if(pool_mgr == NULL)
    {
        return ALLOC_FAIL;
    }

    for(int i = 0; i < pool_mgr->used_nodes; ++i)
    {
        if(pool_mgr->node_heap[i].allocated) {
            return ALLOC_NOT_FREED;
        }
    }

    if(pool_mgr->pool.num_allocs <= 0) {}

    for(unsigned i = 0; i < pool_store_size; ++i)
    {

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

    pool_mgr_pt pmt = (pool_mgr_pt) pool;
    if(pool->num_gaps > 0){
        _mem_resize_node_heap(pmt);

        if(pmt->used_nodes < pmt->total_nodes){
            size_t idx =0;
            size_t bestIdx = 0;
            unsigned int bestSize = pmt->pool.total_size;
            while(idx < pmt->pool.num_gaps){
                if(pmt->gap_ix[idx].size >= size){
                    if(pool->policy != BEST_FIT){
                        bestIdx = idx;
                        break;
                    }else{
                        if(pmt->gap_ix[idx].size < bestSize){
                            bestIdx = idx;
                            bestSize = pmt->gap_ix[idx].size;
                        }
                    }
                }
                idx += 1;
            }

            idx = bestIdx;
            if(idx < pmt->pool.num_gaps){
                //assign new allocation
                node_pt bestNode = pmt->gap_ix[idx].node;
                size_t remaining = pmt->gap_ix[idx].size - size;

                if(remaining > pool->total_size - pool->alloc_size){
                    printf("Remaining Allocation Size Too Large\n");
                    return NULL;
                }

                _mem_remove_from_gap_ix(pmt,size,bestNode);

                bestNode->allocated = 1;
                bestNode->alloc_record.size = size;
                bestNode->alloc_record.mem = pmt->pool.mem;
                pmt->pool.alloc_size += size;



                //take remaining and assign new gap if so
                if(remaining > 0){

                    //get new node
                    size_t ridx = 0;
                    while(idx < pmt->total_nodes && pmt->node_heap[ridx].used == 1){
                        ridx += 1;
                    }

                    if(ridx < pmt->total_nodes && pmt->node_heap[ridx].used == 0) {
                        if(&pmt->node_heap[ridx] == NULL){
                            return NULL;
                        }else{
                            pmt->node_heap[ridx].alloc_record.size = 0;
                            pmt->node_heap[ridx].alloc_record.mem = pool->mem;
                            pmt->node_heap[ridx].used = 1;
                        }
                        pmt->used_nodes += 1;

                        //add to gap_ix
                        node_pt ng = &pmt->node_heap[ridx];
                        ng->used = 1;
                        ng->allocated = 0;
                        ng->next = NULL;
                        ng->prev = NULL;
                        alloc_t nalloc;
                        nalloc.size =0;
                        nalloc.mem = pmt->pool.mem;
                        ng->alloc_record = nalloc;

                        if(_mem_add_to_gap_ix(pmt, remaining,ng) == ALLOC_FAIL){
                            printf("FAILED TO ADD TO GAPS\n");
                            return NULL;
                        }

                        //reset pointers
                        //set next of gap to actual next
                        ng->next = bestNode->next;
                        if(bestNode->next != NULL){
                            bestNode->next->prev = ng;
                        }

                        ng->prev = bestNode;
                        bestNode->next = ng;

                        //check next to see if gap and fold
                        if(ng->next != NULL && ng->next->allocated == 0 && ng->next->used ==1){
                            size_t gapIdx = 0;
                            while(gapIdx < pool->num_gaps && &pmt->gap_ix[gapIdx] != NULL && pmt->gap_ix[gapIdx].node != pmt->node_heap[idx].next){
                                gapIdx += 1;
                            }

                            //roll gap
                            if(gapIdx < pool->num_gaps) {
                                pmt->gap_ix[pmt->pool.num_gaps - 1].size += pmt->gap_ix[gapIdx].size;
                                pmt->gap_ix[gapIdx].node->used = 0;
                                _mem_remove_from_gap_ix(pmt,pmt->gap_ix[gapIdx].size,pmt->gap_ix[gapIdx].node);
                                pmt->used_nodes -= 1;
                            }else{
                                printf("Failed to Find Next Gap\n");
                                return NULL;
                            }
                        }

                    }else{
                        printf("IDX > Available Nodes\n");
                        return NULL;
                    }
                }
                bestNode->alloc_record.mem = (char *) malloc(sizeof(char)*size);
                pmt->pool.num_allocs += 1;
                return (alloc_pt) bestNode;
            }
        }
    }
    printf("WARNING: No Allocation\n");
    return NULL;
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