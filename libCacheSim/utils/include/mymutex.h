#pragma once

//
// Created by Chaos on 11/20/23.
//

#include <pthread.h>
#include <inttypes.h>

typedef struct RWLocks
{
	uint64_t locksMask_;
	pthread_rwlock_t* locks_;
}RWLocks_t;


RWLocks_t* init_RWLocks(uint32_t locksPower);

void expand_RWLocks(RWLocks_t* rwlocks);

void destory_RWLocks(RWLocks_t* rwlocks);

pthread_rwlock_t* getRWLock(RWLocks_t* rwlocks, uint64_t hash);

