#pragma once

//
// Created by Chaos on 11/20/23.
//

#include <pthread.h>
#include <inttypes.h>
#include <stdbool.h>

typedef struct RWLocks
{
	uint64_t locksMask_;
	pthread_rwlock_t* locks_;
}RWLocks_t;


RWLocks_t* init_RWLocks(uint32_t locksPower);

void expand_RWLocks(RWLocks_t* rwlocks);

void destory_RWLocks(RWLocks_t* rwlocks);

pthread_rwlock_t* getRWLock(RWLocks_t* rwlocks, uint64_t hash);

#define CAS(ptr, expected, desired) \
	__atomic_compare_exchange_n(ptr, expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)
#define fetch_add(ptr, val) __atomic_add_fetch(ptr, val, __ATOMIC_ACQ_REL)
#define fetch_sub(ptr, val) __atomic_sub_fetch(ptr, val, __ATOMIC_ACQ_REL)
#define fetch_and(ptr, val) __atomic_and_fetch(ptr, val, __ATOMIC_ACQ_REL)
#define fetch_or(ptr, val) __atomic_or_fetch(ptr, val, __ATOMIC_ACQ_REL)
