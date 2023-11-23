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

/* free object, called by other functions when iterating through the hashtable
 */
static inline void foreach_free_obj_locked(cache_obj_t *cache_obj, void *user_data) {
  my_free(sizeof(cache_obj_t), cache_obj);
}

/**
 *  This function finds an object in the hashtable bucket.
 *  @method find_in_bucket_locked
 *  @author Chaos
 *  @date   2023-11-23
 *  @param  hashtable             [Handle of the hashtable.]
 *  @param  bucket_id             [The id of the hashtable bucket.]
 *  @param  obj_id                [The id of the object to find.]
 *  @return                       [The pointer to the object. If not found, return NULL.]
 */
static inline cache_obj_t* find_in_bucket_locked(const hashtable_t *hashtable, uint64_t bucket_id, obj_id_t obj_id){
  cache_obj_t *cache_obj = hashtable->ptr_table[bucket_id];
  while (cache_obj) {
    if (cache_obj->obj_id == obj_id) {
      assert(verify_cache_obj_fingerprint(cache_obj));
      return cache_obj;
    }
    cache_obj = cache_obj->hash_next;
  }
  // cache_obj is NULL
  return cache_obj;
}

/**
 *  This function adds an object to the hashtable bucket.
 *  @method add_to_bucket_locked
 *  @author Chaos
 *  @date   2023-11-23
 *  @param  hashtable            [Handle of the hashtable.]
 *  @param  bucket_id            [The id of the hashtable bucket.]
 *  @param  cache_obj            [The pointer to the object to add.]
 *  @return                      [The pointer to the object. If the object is already in the hashtable, return the existing one.]
 */
static inline cache_obj_t *add_to_bucket_locked(hashtable_t *hashtable, uint64_t bucket_id, cache_obj_t *cache_obj) {
  // If the object is already in the hashtable, free the inserted object and return the existing one.
  cache_obj_t* curr_obj = find_in_bucket_locked(hashtable, bucket_id, cache_obj->obj_id);
  if(curr_obj != NULL){
    free_cache_obj(cache_obj);
    return curr_obj;
  }
  // If the object is not in the hashtable, insert it to the head of the bucket.
  curr_obj = hashtable->ptr_table[bucket_id];
  cache_obj->hash_next = curr_obj;
  hashtable->ptr_table[bucket_id] = cache_obj;
  __sync_fetch_and_add(&hashtable->n_obj, 1);
  return cache_obj;
}

/**
 *  This function deletes an object in the hashtable bucket.
 *  @method delete_in_bucket_locked
 *  @author Chaos
 *  @date   2023-11-23
 *  @param  hashtable            [Handle of the hashtable.]
 *  @param  bucket_id            [The id of the hashtable bucket.]
 *  @param  cache_obj            [The pointer to the object to delete.]
 *  @return                      [Success or not.]
 */
static inline bool delete_in_bucket_locked(hashtable_t *hashtable, uint64_t bucket_id, obj_id_t obj_id) {
  cache_obj_t *curr_obj = hashtable->ptr_table[bucket_id];
  cache_obj_t *prev_obj = curr_obj;

  if(curr_obj == NULL) return false;

  if(curr_obj->obj_id == obj_id){
    // the object to remove is the head of the bucket
    hashtable->ptr_table[bucket_id] = curr_obj->hash_next;
    if (!hashtable->external_obj) free_cache_obj(curr_obj);
    __sync_fetch_and_sub(&hashtable->n_obj, 1);
    return true;
  }
  // the object to remove is not the head of the bucket
  do {
    prev_obj = curr_obj;
    curr_obj = curr_obj->hash_next;
  } while(curr_obj != NULL && curr_obj->obj_id != obj_id);
  // the object to remove is in the bucket
  if (curr_obj != NULL) {
    prev_obj->hash_next = curr_obj->hash_next;
    if (!hashtable->external_obj) free_cache_obj(curr_obj);
    hashtable->n_obj -= 1;
    return true;
  }
  // the object to remove is not in the bucket (also not in the hashtable)
  return false;
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
  hashtable->rwlocks_ = init_RWLocks((hashpower>7) ? (hashpower-7) : 0);
  return hashtable;
}

cache_obj_t *concurrent_chained_hashtable_find_obj_id(const hashtable_t *hashtable,
                                              const obj_id_t obj_id) {
  uint64_t hv = get_hash_value_int_64(&obj_id) & hashmask(hashtable->hashpower);
  /** Add read lock for query */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, hv);
  pthread_rwlock_rdlock(rwlock_);

  cache_obj_t *cache_obj = find_in_bucket_locked(hashtable, hv, obj_id);

  pthread_rwlock_unlock(rwlock_);
  return cache_obj;
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
 *  If the object is not in the hashtable:
 *    - increase the number of objects in the hashtable
 *    - return the inserted object. 
 *  Else:
 *    - return the existing one.
 *  @Author Chaos
 *  @Date   2023-11-22
 */
cache_obj_t *concurrent_chained_hashtable_insert_obj(hashtable_t *hashtable,
                                             cache_obj_t *cache_obj) {
  // DEBUG_ASSERT(hashtable->external_obj);
  uint64_t hv = get_hash_value_int_64(&cache_obj->obj_id) & hashmask(hashtable->hashpower);
  /** Add write lock for insertion */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, hv);
  pthread_rwlock_wrlock(rwlock_);
  cache_obj_t *inserted_cache_obj = add_to_bucket_locked(hashtable, hv, cache_obj);
  // If successfully inserted, increase the number of objects in the hashtable.
  pthread_rwlock_unlock(rwlock_);
  return inserted_cache_obj;
}

cache_obj_t *concurrent_chained_hashtable_insert(hashtable_t *hashtable,
                                         const request_t *req) {
  cache_obj_t *new_cache_obj = create_cache_obj_from_request(req);
  return concurrent_chained_hashtable_insert_obj(hashtable, new_cache_obj);
}


/* you need to free the extra_metadata before deleting from hash table */
bool concurrent_chained_hashtable_delete_obj_id(hashtable_t *hashtable,
                                        const obj_id_t obj_id) {
  uint64_t hv = get_hash_value_int_64(&obj_id) & hashmask(hashtable->hashpower);
  /** Add write lock for removal */
  pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, hv);
  pthread_rwlock_wrlock(rwlock_);

  bool res = delete_in_bucket_locked(hashtable, hv, obj_id);

  pthread_rwlock_unlock(rwlock_);
  return res;
}

bool concurrent_chained_hashtable_try_delete(hashtable_t *hashtable,
                                     cache_obj_t *cache_obj) {
  uint64_t hv = get_hash_value_int_64(&cache_obj->obj_id) & hashmask(hashtable->hashpower);
  return concurrent_chained_hashtable_delete_obj_id(hashtable, cache_obj->obj_id);
}

void concurrent_chained_hashtable_delete(hashtable_t *hashtable,
                                 cache_obj_t *cache_obj) {
  concurrent_chained_hashtable_try_delete(hashtable, cache_obj);
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

void concurrent_chained_hashtable_foreach(hashtable_t *hashtable,
                                  hashtable_iter iter_func, void *user_data) {
  cache_obj_t *cur_obj, *next_obj;
  for (uint64_t i = 0; i < hashsize(hashtable->hashpower); i++) {
    /** Write lock for iter_func*/
    pthread_rwlock_t* rwlock_ = getRWLock(hashtable->rwlocks_, i);
    pthread_rwlock_wrlock(rwlock_);
    cur_obj = hashtable->ptr_table[i];
    while (cur_obj != NULL) {
      next_obj = cur_obj->hash_next;
      iter_func(cur_obj, user_data);
      cur_obj = next_obj;
    }
    pthread_rwlock_unlock(rwlock_);
  }
}

void free_concurrent_chained_hashtable(hashtable_t *hashtable) {
  if (!hashtable->external_obj)
    concurrent_chained_hashtable_foreach(hashtable, foreach_free_obj_locked, NULL);
  my_free(sizeof(cache_obj_t *) * hashsize(hashtable->hashpower),
          hashtable->ptr_table);
  destory_RWLocks(hashtable->rwlocks_);
}


static int count_n_obj_in_bucket_locked(cache_obj_t *curr_obj) {
  obj_id_t obj_id_arr[64];
  int chain_len = 0;
  while (curr_obj != NULL) {
    obj_id_arr[chain_len] = curr_obj->obj_id;
    for (int i = 0; i < chain_len; i++) {
      if (obj_id_arr[i] == curr_obj->obj_id) {
        ERROR("obj_id %lu is duplicated in hashtable\n", curr_obj->obj_id);
        abort();
      }
    }

    curr_obj = curr_obj->hash_next;
    chain_len += 1;
  }
  return chain_len;
}


#ifdef __cplusplus
}
#endif
