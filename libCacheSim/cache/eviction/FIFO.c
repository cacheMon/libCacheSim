//
//  a FIFO module that supports different obj size
//
//
//  FIFO.c
//  libCacheSim
//
//  Created by Juncheng on 12/4/18.
//  Copyright © 2018 Juncheng. All rights reserved.
//

#include "../include/libCacheSim/evictionAlgo/FIFO.h"
#include "../dataStructure/hashtable/hashtable.h"
#include <assert.h>


#ifdef __cplusplus
extern "C" {
#endif


cache_t *FIFO_init(common_cache_params_t ccache_params, void *init_params) {
  cache_t *cache = cache_struct_init("FIFO", ccache_params);
  cache->cache_init = FIFO_init;
  cache->cache_free = FIFO_free;
  cache->get = FIFO_get;
  cache->check = FIFO_check;
  cache->insert = FIFO_insert;
  cache->evict = FIFO_evict;
  cache->remove = FIFO_remove;

  return cache;
}

void FIFO_free(cache_t *cache) { cache_struct_free(cache); }

cache_ck_res_e FIFO_check(cache_t *cache, request_t *req, bool update_cache) {
  return cache_check_base(cache, req, update_cache, NULL);
}

cache_ck_res_e FIFO_get(cache_t *cache, request_t *req) {
  return cache_get_base(cache, req);
}

void FIFO_insert(cache_t *cache, request_t *req) {
  cache_insert_LRU(cache, req);
}

void FIFO_evict(cache_t *cache, request_t *req, cache_obj_t *evicted_obj) {
  cache_evict_LRU(cache, req, evicted_obj);
}

void FIFO_remove_obj(cache_t *cache, cache_obj_t *obj_to_remove) {
  if (obj_to_remove == NULL) {
    ERROR("remove NULL from cache\n");
    abort();
  }

  cache->occupied_size -= (obj_to_remove->obj_size + cache->per_obj_overhead);
  cache->n_obj -= 1;
  remove_obj_from_list(&cache->list_head, &cache->list_tail, obj_to_remove);
  hashtable_delete(cache->hashtable, obj_to_remove);
}


void FIFO_remove(cache_t *cache, obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    ERROR("remove object %"PRIu64 "that is not cached\n", obj_id);
    return;
  }
  FIFO_remove_obj(cache, obj);
}

#ifdef __cplusplus
}
#endif