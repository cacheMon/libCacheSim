//
// TODO
// High level view of the concurrent chained hash table. 
// - Rwlock pool: a pool of rwlocks. The size of the pool is 2^(hashpower-7). 
// - Hashtable: 
//  - Each bucket is a linked list of cache_obj_t. The head of the linked list is stored in the ptr_table. The size of the ptr_table is 2^hashpower.
//  - Each bucket maps to a rw_lock in the rwlock pool. Mulpitle buckets likely maps to the same rwlock.
//    Mapping: rwlock_id = bucket_id & (rw_count-1)
// 
// Rwlock pool (count=4)      A hashtable
// |-----------------|        | ---------------|
// |   rw_lock 0     |        |     bucket 0   | ----> cache_obj_t* ----> cache_obj_t* ----> NULL
// |-----------------|        | ---------------|
// |   rw_lock 1     |        |     bucket 1   | ----> cache_obj_t*
// |-----------------|        | ---------------|
// |   rw_lock 2     |        |     bucket 2   | ----> NULL
// |-----------------|        | ---------------|
// |   rw_lock 3     |        |     bucket 3   | ----> cache_obj_t* ----> cache_obj_t* ----> nULL
// |-----------------|        | ---------------|
//                            |     bucket 4   | ----> NULL
//                            | ---------------|
//                            |     bucket 5   | ----> NULL
//                            | ---------------|

#ifdef __cplusplus
extern "C" {
#endif

#include "cChainedHashTable.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../../include/libCacheSim/logging.h"
#include "../../include/libCacheSim/macro.h"
#include "../../utils/include/mymath.h"
#include "../hash/hash.h"

/************************ help func ************************/

/**
 *  This function finds an object in the hashtable bucket.
 *  @method find_pointer_locked
 *  @author Chaos
 *  @date   2024-4-19
 *  @param  hashtable             [Handle of the hashtable.]
 *  @param  bucket_id             [Id of the hashtable bucket.]
 *  @param  obj_id                [Id of the object to find.]
 *  @return                       [Double pointer to the object. (The pointer to 'obj->hash_next')]
 */

static inline cache_obj_t** find_pointer_locked(const hashtable_t *hashtable, uint64_t bucket_id, obj_id_t obj_id){
    cache_obj_t **ptr = &hashtable->ptr_table[bucket_id];
    while(*ptr != NULL && obj_id != (*ptr)->obj_id){
      ptr = &(*ptr)->hash_next;
    }
    return ptr;
}


/************************ hashtable func ************************/
/**
 *  [This function is not thread-safe. Other threads muse wait for the return of this function.]
 *  @method create_concurrent_chained_hashtable
 *  @author Chaos
 *  @date   2023-11-21
 *  @param  hashpower                   [The power of 2 of the size of the hashtable. Default value is 20.]
 *  @return                             [Handle of the created hashtable.]
 */
hashtable_t *create_concurrent_chained_hashtable(const uint16_t hashpower) {
  hashtable_t *hashtable = my_malloc(hashtable_t);
  memset(hashtable, 0, sizeof(hashtable_t));
  size_t size = sizeof(cache_obj_t *) * hashsize(hashtable->hashpower);
  hashtable->ptr_table = my_malloc_n(cache_obj_t *, hashsize(hashpower));
  if (hashtable->ptr_table == NULL) {
    ERROR("allcoate hash table %zu entry * %lu B = %ld MiB failed\n",
          sizeof(cache_obj_t *), (unsigned long)(hashsize(hashpower)),
          (long)(sizeof(cache_obj_t *) * hashsize(hashpower) / 1024 / 1024));
    exit(1);
  }
  memset(hashtable->ptr_table, 0, size);

#ifdef USE_HUGEPAGE
  madvise(hashtable->table, size, MADV_HUGEPAGE);
#endif
  hashtable->external_obj = false;
  hashtable->hashpower = hashpower;
  hashtable->n_obj = 0;
  hashtable->rwlocks_ = init_RWLocks((hashpower > 10) ? (hashpower-10) : 0);
  return hashtable;
}

/**
 *  This function finds an object in the hashtable. 
 *  If the object is in the hashtable:
 *    - return the pointer. 
 *  Else:
 *    - return NULL.
 *  @Author Chaos
 *  @Date   2024-04-18
 */

cache_obj_t *concurrent_chained_hashtable_find_obj_id(const hashtable_t *hashtable,
                                              const obj_id_t obj_id) {
  uint64_t hv = get_hash_value_int_64(&obj_id) & hashmask(hashtable->hashpower);
  /** Add read lock for query */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, hv);
  pthread_rwlock_rdlock(rwlock_);
  /************within lock******************/
  cache_obj_t* entry = *find_pointer_locked(hashtable, hv, obj_id);
  /************out lock******************/
  pthread_rwlock_unlock(rwlock_);
  return entry;
}

cache_obj_t *concurrent_chained_hashtable_find(const hashtable_t *hashtable,
                                       const request_t *req) {
  return concurrent_chained_hashtable_find_obj_id(hashtable, req->obj_id);
}

cache_obj_t *concurrent_chained_hashtable_find_obj(const hashtable_t *hashtable,
                                           const cache_obj_t *obj_to_find) {
  return concurrent_chained_hashtable_find_obj_id(hashtable, obj_to_find->obj_id);
}

/**
 *  This function inserts an object to the hashtable.
 *  If the object is in the hashtable:
 *    - delete the old one.
 *    
 *  Then, increase the number of objects in the hashtable and
 *  return the inserted object. 
 *  @Author Chaos
 *  @Date   2024-04-18
 */
cache_obj_t *concurrent_chained_hashtable_insert_obj(hashtable_t *hashtable,
                                             cache_obj_t *cache_obj) {
  // DEBUG_ASSERT(hashtable->external_obj);
  uint64_t hv = get_hash_value_int_64(&cache_obj->obj_id) & hashmask(hashtable->hashpower);
  /** Add write lock for insertion */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, hv);
  pthread_rwlock_wrlock(rwlock_);
  
  /************within lock******************/
  cache_obj_t** ptr = find_pointer_locked(hashtable, hv, cache_obj->obj_id);
  cache_obj_t* old = *ptr;
  cache_obj->hash_next = (old == NULL ? NULL : old->hash_next);
  // Initialize the in_cache flag. Set to true after the object is inserted into the cache eviction.
  cache_obj_set_in_cache(cache_obj, false); 
  *ptr = cache_obj;
  // If overwritten
  if (old != NULL) {
    cache_obj_set_in_cache(old, false);
  }
  else{
    // If successfully inserted, increase the number of objects in the hashtable.
    hashtable->n_obj += 1;
  }
  /************out lock******************/

  pthread_rwlock_unlock(rwlock_);
  return old;
}

cache_obj_t *concurrent_chained_hashtable_insert(hashtable_t *hashtable,
                                         const request_t *req) {
  cache_obj_t *new_cache_obj = create_cache_obj_from_request(req);
  return concurrent_chained_hashtable_insert_obj(hashtable, new_cache_obj);
}


/**
 *  This function deletes an object in the hashtable. 
 *  If the object is in the hashtable:
 *    - decrease the number of objects in the hashtable
 *    - return true. 
 *  Else:
 *    - return false.
 *  @Author Chaos
 *  @Date   2023-11-22
 */
cache_obj_t *concurrent_chained_hashtable_delete_obj_id(hashtable_t *hashtable,
                                        const obj_id_t obj_id) {
  uint64_t hv = get_hash_value_int_64(&obj_id) & hashmask(hashtable->hashpower);
  /** Add write lock for removal */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, hv);
  pthread_rwlock_wrlock(rwlock_);
  /************within lock******************/
  cache_obj_t** ptr = find_pointer_locked(hashtable, hv, obj_id);
  cache_obj_t* result = *ptr;
  if (result != NULL) {
    cache_obj_set_in_cache(result, false);
    *ptr = result->hash_next;
    hashtable->n_obj -= 1;
  }
  /************out lock******************/
  pthread_rwlock_unlock(rwlock_);
  return result;
}


void concurrent_chained_hashtable_delete(hashtable_t *hashtable,
                                 cache_obj_t *cache_obj) {
  concurrent_chained_hashtable_delete_obj_id(hashtable, cache_obj->obj_id);
}

bool concurrent_chained_hashtable_try_delete(hashtable_t *hashtable,
                                 cache_obj_t *cache_obj) {
  cache_obj_t* result = concurrent_chained_hashtable_delete_obj_id(hashtable, cache_obj->obj_id);
  return result != NULL;
}

cache_obj_t *concurrent_chained_hashtable_rand_obj(const hashtable_t *hashtable) {
  uint64_t pos = next_rand() & hashmask(hashtable->hashpower);
  /** Add read lock for random query */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, pos);
  pthread_rwlock_rdlock(rwlock_);  

  while (hashtable->ptr_table[pos] == NULL){
    pthread_rwlock_unlock(rwlock_);
    pos = next_rand() & hashmask(hashtable->hashpower);
    /** Add read lock for random query */
    rwlock_ = getRWLock(hashtable->rwlocks_, pos);
    pthread_rwlock_rdlock(rwlock_);  
  }
  pthread_rwlock_unlock(rwlock_);
  return hashtable->ptr_table[pos];
}

// The hashtable is not responsible for freeing the objects. The release of objects is managed by the cache eviciton policies.
void free_concurrent_chained_hashtable(hashtable_t *hashtable) {
  // if (!hashtable->external_obj)
  //   concurrent_chained_hashtable_foreach(hashtable, foreach_free_obj_locked, NULL);
  my_free(sizeof(cache_obj_t *) * hashsize(hashtable->hashpower),
          hashtable->ptr_table);
  destory_RWLocks(hashtable->rwlocks_);
}


#ifdef __cplusplus
}
#endif
