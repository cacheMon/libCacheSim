//
// Created by Chaos on 11/20/23.
//

#ifdef __cplusplus
extern "C" {
#endif

#include "include/mymutex.h"
#include "../include/libCacheSim/mem.h"
#include <stdio.h>
/**
 * [init_RWLocks: Initiate a rw_lock pool. The size of the pool is determined by locksPower. 
 * 		For example, if the locksPower is 4, the size is 2^4 = 16. ]
 * @author Chaos
 * @date   2023-11-20
 * @param  locksPower [The power of the number of rwlocks.]
 * @return            [Handler of the created rw_locks. ]
 */
RWLocks_t* init_RWLocks(uint32_t locksPower){
	RWLocks_t* rwlocks = my_malloc(RWLocks_t);
	rwlocks->locksMask_ = (1ULL << locksPower) - 1;
	rwlocks->locks_ = my_malloc_n(pthread_rwlock_t, 1ULL << locksPower);

	for(uint64_t s = 0; s <= rwlocks->locksMask_; s++){
		pthread_rwlock_init(rwlocks->locks_ + s, NULL);
	}
	return rwlocks;
}

/**
 *  [expand_RWLocks: Grow the rw_locks pool to the next power of 2..]
 *  @method expand_RWLocks
 *  @author Chaos
 *  @date   2023-11-21
 *  @param  rwlocks        [Handler of operated rw_locks. ]
 */
void expand_RWLocks(RWLocks_t* rwlocks){
	uint64_t oldLocksMask = rwlocks->locksMask_;
	rwlocks->locksMask_ = (oldLocksMask << 1) | 1;
	pthread_rwlock_t* oldLocks = rwlocks->locks_;
	rwlocks->locks_ = my_malloc_n(pthread_rwlock_t, rwlocks->locksMask_ + 1);
	for(uint64_t s = 0; s <= oldLocksMask; s++){
		pthread_rwlock_init(rwlocks->locks_ + s, NULL);
	}
	for(uint64_t s = oldLocksMask + 1; s <= rwlocks->locksMask_; s++){
		pthread_rwlock_init(rwlocks->locks_ + s, NULL);
	}
	my_free(sizeof(pthread_rwlock_t) * (oldLocksMask + 1), oldLocks);
}

/**
 *  [destory_RWLocks: Destory the rw_locks.]
 *  @method destory_RWLocks
 *  @author Chaos
 *  @date   2023-11-20
 *  @param  rwlocks         [Handler of the destoryed rw_locks. ]
 */
void destory_RWLocks(RWLocks_t* rwlocks){
	for(uint64_t s = 0; s <= rwlocks->locksMask_; s++){
		pthread_rwlock_destroy(rwlocks->locks_ + s);
	}
	my_free(sizeof(pthread_rwlock_t) * (rwlocks->locksMask_ + 1), rwlocks->locks_);
	my_free(sizeof(RWLocks_t), rwlocks);
}

/**
 *  [getRWLock: Get a rw_lock by a random hash number.]
 *  @method getRWLock
 *  @author Chaos
 *  @date   2023-11-20
 *  @param  rwlocks    [Handler]
 *  @param  hash       [A random hash number]
 *  @return            [Current rw_lock.]
 */
pthread_rwlock_t* getRWLock(RWLocks_t* rwlocks, uint64_t hash) {
	uint64_t index = hash & rwlocks->locksMask_;
    return rwlocks->locks_ + index;
}


#ifdef __cplusplus
}
#endif