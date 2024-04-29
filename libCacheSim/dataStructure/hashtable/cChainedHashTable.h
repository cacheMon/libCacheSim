// This file defines a concurrent chained hashtable data structure. It is based on chainedHashTableV2. 
// Some Features:
//  - Static hashtable size
//  - Thread safe
//  - No global lock
//
// Created by Chaos on 11/23/2023.
//

#ifndef libCacheSim_CCHAINEDHASHTABLE_H
#define libCacheSim_CCHAINEDHASHTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>

#include "../../include/libCacheSim/cacheObj.h"
#include "../../include/libCacheSim/request.h"
#include "hashtableStruct.h"

hashtable_t *create_concurrent_chained_hashtable(const uint16_t hashpower_init);

void free_concurrent_chained_hashtable(hashtable_t *hashtable);

cache_obj_t *concurrent_chained_hashtable_find_obj_id(const hashtable_t *hashtable,
                                              const obj_id_t obj_id);

cache_obj_t *concurrent_chained_hashtable_find(const hashtable_t *hashtable,
                                       const request_t *req);

cache_obj_t *concurrent_chained_hashtable_find_obj(const hashtable_t *hashtable,
                                           const cache_obj_t *obj_to_evict);

/* return an empty cache_obj_t */
cache_obj_t *concurrent_chained_hashtable_insert(hashtable_t *hashtable,
                                         const request_t *req);

cache_obj_t *concurrent_chained_hashtable_insert_obj(hashtable_t *hashtable,
                                             cache_obj_t *cache_obj);

bool concurrent_chained_hashtable_try_delete(hashtable_t *hashtable,
                                     cache_obj_t *cache_obj);

void concurrent_chained_hashtable_delete(hashtable_t *hashtable,
                                 cache_obj_t *cache_obj);

cache_obj_t *concurrent_chained_hashtable_delete_obj_id(hashtable_t *hashtable,
                                        const obj_id_t obj_id);

cache_obj_t *concurrent_chained_hashtable_rand_obj(const hashtable_t *hashtable);

void concurrent_chained_hashtable_foreach(hashtable_t *hashtable,
                                  hashtable_iter iter_func, void *user_data);


#ifdef __cplusplus
}
#endif

#endif  // libCacheSim_CCHAINEDHASHTABLE_H
