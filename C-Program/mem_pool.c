/*
 * Kyle Etsler
 * UCDenver Spring 2016
 * OS Assignment 1 - C Assignment
 * 3/20/2016
 */
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/


static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
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
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
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
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {
    pool_store = (pool_mgr_pt *) calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_t));
    if(pool_store == NULL) {
        printf("Memory Allocation failed/\nFunction: mem_init()\n");
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
                for(size_t i = pool_store[i]->pool.num_gaps ; i >= 0; i--){
                    free(&pool_store[i]->gap_ix[i]);
                }
                mem_pool_close(&pool_store[i]->pool);
                node_pt pt = pool_store[i]->node_heap;
                for(size_t i = pool_store[i]->total_nodes; i >= 0; i--){
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

pool_pt mem_pool_open(size_t size, alloc_policy pool_policy) {
    assert(pool_store != NULL);
    alloc_status status = _mem_resize_pool_store();
    pool_mgr_pt pool_mgr = (pool_mgr_pt)malloc(sizeof(pool_mgr_t));
    pool_mgr->pool.mem = (char*)malloc(size);
    pool_mgr->node_heap = (node_pt)malloc(MEM_NODE_HEAP_INIT_CAPACITY * sizeof(node_t));
    pool_mgr->gap_ix = (gap_pt)malloc(MEM_GAP_IX_INIT_CAPACITY * sizeof(gap_t));
    if (pool_mgr == NULL || pool_mgr->pool.mem == NULL || pool_mgr->node_heap == NULL || pool_mgr->gap_ix == NULL) {
        free(pool_mgr->pool.mem);
        free(pool_mgr->node_heap);
        free(pool_mgr->gap_ix);
        free(pool_mgr);
        return NULL;
    }
    pool_mgr->pool.policy = pool_policy;
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

    pool_mgr_pt mgrpt = (pool_mgr_pt) pool;

    if (!(mgrpt->used_nodes == 1 && mgrpt->node_heap[0].allocated == 0)) {
        return ALLOC_NOT_FREED;
    }
    free(pool->mem);
    free(mgrpt->node_heap);
    free(mgrpt->gap_ix);

    int i = 0;
    for (i = 0; i < pool_store_size; i++){

        if (mgrpt == pool_store[i]){
            pool_store[i] = NULL;
        }
    }
    free(mgrpt);

    return ALLOC_OK;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
    pool_mgr_pt mgr = (pool_mgr_pt)pool;
    if (pool->num_gaps == 0) {return NULL;}
    assert(_mem_resize_node_heap(mgr) != ALLOC_FAIL);
    assert(mgr->used_nodes < mgr->total_nodes);
    node_pt alloc = NULL;
    int length = 1;

    if (pool->policy == FIRST_FIT)
    {
        alloc = mgr->node_heap;
        int hit = 0;
        while (alloc != NULL && (hit == 0))
        {
            length = length + 1;
            if (alloc->allocated == 0 && alloc->used == 1 && alloc->alloc_record.size >= size)
            {
                hit = 1;
            } else {
                alloc = alloc->next;
            }
        }
        if (hit == 0)
        {
            return NULL;
        }
    }
    if (pool->policy == BEST_FIT)
    {
        for (int i = 0; i < mgr->pool.num_gaps; i++)
        {
            if (mgr->gap_ix[i].size >= size)
            {
                alloc = (&mgr->gap_ix[i])->node;
            }
        }
    }
    if (alloc == NULL)
    {
        return NULL;
    }
    if (length == 0)
    {
        pool->num_allocs = 1;
        pool->alloc_size = size;
    } else {
        pool->num_allocs = pool->num_allocs + 1;
        pool->alloc_size = pool->alloc_size + size;
    }

    if (alloc->alloc_record.size == size)
    {
        alloc->allocated = 1;
        _mem_remove_from_gap_ix(mgr, size, alloc);
    }
    else
    {
        int i = 0;
        while (mgr->node_heap[i].used == 1) {i++;}
        node_pt newNode = &mgr->node_heap[i];
        newNode->next = alloc->next;
        newNode->prev = alloc;
        if (alloc->next != NULL) {alloc->next->prev = newNode;}
        alloc->next = newNode;
        _mem_remove_from_gap_ix(mgr, alloc->alloc_record.size, alloc);
        newNode->allocated = 0;
        newNode->used = 1;
        newNode->alloc_record.size = alloc->alloc_record.size - size;
        newNode->alloc_record.mem = alloc->alloc_record.mem + size;
        alloc->alloc_record.size = size;
        alloc->allocated = 1;
        _mem_add_to_gap_ix(mgr, newNode->alloc_record.size, newNode);
        mgr->used_nodes = mgr->used_nodes + 1;
    }

    return &(alloc->alloc_record);
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    pool_mgr_pt mgrpt = (pool_mgr_pt) pool;
    node_pt current = (node_pt) alloc;
    if (current->allocated == 0) {return ALLOC_NOT_FREED;}
    current->allocated = 0;
    pool->num_allocs--;
    pool->alloc_size = pool->alloc_size - alloc->size;
    node_pt next = current->next;
    if ((next != NULL) && (next->allocated == 0)){
        if (_mem_remove_from_gap_ix(mgrpt, next->alloc_record.size, next) == ALLOC_FAIL){
            return ALLOC_FAIL;
        }
        current->alloc_record.size = next->alloc_record.size + current->alloc_record.size;
        if (current->next != NULL) {current->next->used = 0;}
        mgrpt->used_nodes--;
        if (next->next != NULL){
            next->next->prev = current;
            current->next = next->next;
        }
        else {
            current->next = NULL;
        }
        next->next = NULL;
        next->prev = NULL;
    }
    node_pt prev = current->prev;
    if ((prev != NULL) && (prev->allocated == 0)){
        if (_mem_remove_from_gap_ix(mgrpt, prev->alloc_record.size, prev) == ALLOC_FAIL){
            return ALLOC_FAIL;
        }
        prev->alloc_record.size = prev->alloc_record.size + current->alloc_record.size;
        current->used = 0;
        mgrpt->used_nodes--;
        if (current->next != NULL){
            prev->next = current->next;
            current->next->prev = prev;
        }
        else {
            prev->next = NULL;
        }
        current->next = NULL;
        current->prev = NULL;

        return _mem_add_to_gap_ix(mgrpt, prev->alloc_record.size, prev);
    }
    return _mem_add_to_gap_ix(mgrpt, current->alloc_record.size, current);
}

void mem_inspect_pool(pool_pt pool, pool_segment_pt *seg, unsigned *num_segments)  {
    const pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;
    *seg = (pool_segment_pt) calloc(pool_mgr->used_nodes, sizeof(pool_segment_t));
    if(seg == NULL) {
        return;
    }
    int current = 0;
    node_pt currentNode = pool_mgr->node_heap;
    while(currentNode) {

        if(currentNode->used != 0) {
            (*seg)[current].size = currentNode->alloc_record.size;
            (*seg)[current].allocated = currentNode->allocated;
            ++current;
        }
        currentNode = currentNode->next;
    }
    *num_segments = current;
    return;
}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    pool_mgr_pt * mgrpt = (pool_mgr_pt *) realloc(pool_store, pool_store_capacity * MEM_POOL_STORE_EXPAND_FACTOR * sizeof(pool_mgr_pt));
    if(mgrpt == NULL) {
        printf("Failed to resize node heap.\r\n");
        return ALLOC_FAIL;
    } else {
        pool_store = mgrpt;
        pool_store_capacity *= MEM_POOL_STORE_EXPAND_FACTOR;
    }
    return ALLOC_OK;

}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    if(((float)pool_mgr->used_nodes / pool_mgr->total_nodes) > MEM_NODE_HEAP_FILL_FACTOR){
        pool_mgr->total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
        pool_mgr->node_heap = (node_pt) realloc(pool_mgr->node_heap, sizeof(node_pt) * pool_mgr->total_nodes);
        for(size_t i = pool_mgr->used_nodes ; i < pool_mgr->total_nodes; i++){
            node_t node;
            node.used = 0;
            node.allocated = 0;
            pool_mgr->node_heap[i] = node;
        }

    }
    return ALLOC_OK;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    if(((float)pool_mgr->pool.num_gaps /pool_mgr->gap_ix_capacity) > MEM_GAP_IX_FILL_FACTOR){
        unsigned int initial = pool_mgr->gap_ix_capacity;
        pool_mgr->gap_ix_capacity *= MEM_GAP_IX_EXPAND_FACTOR;
        pool_mgr->gap_ix = (gap_pt) realloc(pool_mgr->gap_ix,sizeof(gap_pt)*pool_mgr->gap_ix_capacity);

        for(size_t i = initial; i < pool_mgr->gap_ix_capacity;i++){
            gap_t gap;
            gap.node = NULL;
            gap.size = 0;
            pool_mgr->gap_ix[i] = gap;
        }
        if(pool_mgr->gap_ix != NULL) {
            return ALLOC_OK;
        }
    }
    return ALLOC_FAIL;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {
    _mem_resize_gap_ix(pool_mgr);
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;
    pool_mgr->pool.num_gaps++;
    if (_mem_sort_gap_ix(pool_mgr) == ALLOC_FAIL){
        return ALLOC_FAIL;
    }

    return ALLOC_OK;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    size_t pos;
    for (pos = 0; pos < pool_mgr->pool.num_gaps; pos++)	{
        if (pool_mgr->gap_ix[pos].node == node) {break;}
    }

    for (pos; pos < pool_mgr->pool.num_gaps; pos++) {
        pool_mgr->gap_ix[pos] = pool_mgr->gap_ix[pos+1];
    }
    pool_mgr->pool.num_gaps = pool_mgr->pool.num_gaps - 1;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = 0;
    pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = NULL;

    return ALLOC_OK;
}

static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    int num_of_gaps = pool_mgr->pool.num_gaps;
    if ( num_of_gaps < 2)
    {
        return ALLOC_OK;
    }
    for (int current =  num_of_gaps; current >= 1;current--) {
        if (pool_mgr->gap_ix[current-1].size < pool_mgr->gap_ix[current].size
            || (pool_mgr->gap_ix[current-1].size == pool_mgr->gap_ix[current].size
                && pool_mgr->gap_ix[current-1].node->alloc_record.mem < pool_mgr->gap_ix[current].node->alloc_record.mem))
        {
            gap_t temp_gap = pool_mgr->gap_ix[current];
            pool_mgr->gap_ix[current] = pool_mgr->gap_ix[current-1];
            pool_mgr->gap_ix[current-1] = temp_gap;
        }
    }
    return ALLOC_OK;
}

