/*
 * Created by ChaosD on 11/22/23.
 * This file is used to test the concurrent throughput of hashtable structures.
 * Now the test only supports two types of hashtable: chainedHashTableV2, and cChainedHashTable.
 * It supports three types of operations: read obj, insert obj, and delete obj.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

#include "../../utils/include/mymath.h"
#include "../../include/libCacheSim/cacheObj.h"
#include "../hashtable/chainedHashTableV2.h"
#include "../hashtable/cChainedHashTable.h"

/** Power of number of cache objects to insert. */
size_t g_power = 19; 

/** Number of cache objects to insert. */
size_t g_numkeys; // 2^18=262144

/** Number of threads to spawn for each type of operation. */
size_t g_thread_num = 1;

/** Number of seconds to run read test. */
size_t g_test_len = 20; // 10s

/** Type of test. */
size_t g_test_type = 0; // 0: Three tests: Insert, Read, and Delete. Each test is non-overlapping.
                         // 1: Mixed. Each thread performs insert, read, and delete in turn. 
                         //    The three operations are overlapping between different threads.

/** Type of hashtable. */
size_t g_ht_type = 0; // 0: cChainedHashTable; 1: chainedHashTableV2; 


/** Function pointer to find a cache object in the cache instance. */
typedef cache_obj_t* (*func_hashtable_find_obj_id_t)(const hashtable_t *hashtable,
                                              const obj_id_t obj_id);
/** Function pointer to insert a cache object in the cache instance. */
typedef cache_obj_t* (*func_hashtable_insert_obj_t)(hashtable_t *hashtable,
                                             cache_obj_t *cache_obj);
/** Function pointer to remove a cache object in the cache instance. */
typedef cache_obj_t* (*func_hashtable_delete_obj_id_t)(hashtable_t *hashtable,
                                             const obj_id_t obj_id);

typedef hashtable_t *(*func_create_hashtable_t)(const uint16_t hashpower_init);

/** Function pointer to free a hashtable instance. */
typedef void (*func_free_hashtable_t)(hashtable_t *hashtable);


struct handle_hashtable_t{
  func_hashtable_find_obj_id_t func_find_obj;
  func_hashtable_insert_obj_t func_insert_obj;
  func_hashtable_delete_obj_id_t func_delete_obj;
  func_create_hashtable_t func_create_hashtable;
  func_free_hashtable_t func_free_hashtable;
} ht_handle;

/**
 *  Struct of parameters for test threads.
 *  @author Chaos
 *  @date   2023-11-22
 *  @number  table       [Handle of a cache instance]
 *  @number  r_counter   [Number of cache objects read]
 *  @number  i_counter   [Number of cache objects inserted]
 *  @number  d_counter   [Number of cache objects deleted]
 *  @number  finished    [Signal of the end of read]
 *  @number  range       [Range of obj ID to read]
 *  @number  thread_id   [thread ID number]
 *  @number  func_find   [Function pointer to find a cache object]
 *  @number  func_insert [Function pointer to insert a cache object]
 *  @number  func_delete [Function pointer to delete a cache object]
 */
typedef struct thread_para{
  hashtable_t* table;
  uint64_t* r_counter;
  uint64_t* i_counter;
  uint64_t* d_counter;
  bool* finished;
  uint64_t range;
  uint32_t thread_id;
}thread_para_t;

/**
 *  [This function reads a cache object from the cache instance. It is used as a thread function.]
 *  @method func_read
 *  @author Chaos
 *  @date   2023-11-22
 */
void *func_read(void* arg){
  thread_para_t* para = (thread_para_t*) arg;
  hashtable_t* table = para->table;
  uint64_t* r_counter = para->r_counter;
  bool* finished = para->finished;
  uint64_t range = para->range;
  uint32_t thread_id = para->thread_id;

// We keep track of our own local counter for reads, to avoid
// over-burdening the shared atomic counter
  size_t reads = 0;
  size_t success_reads = 0;
  size_t fail_reads = 0;

  struct timeval start, end;
  // We use thread id as seed to generate a reproducible pseudo random sequence. 
  // The items in the sequence are operated obj IDs.
  obj_id_t cur_obj_id = thread_id;
  // The function keeps runing until the flag `finished` == True.
  gettimeofday(&start, NULL);
  for (uint64_t it = 0;; it++) {
    if(it == range){
      it = 0;
      cur_obj_id = thread_id;
    }
    if (*finished) {
      __sync_fetch_and_add(r_counter, reads);
      gettimeofday(&end, NULL);
      double timeuse = (end.tv_sec - start.tv_sec) +
                       (end.tv_usec - start.tv_usec) / 1000000.0;
      INFO("Thread %d read %zu objects in %.1f seconds, throughput is %.2f MOPS. %zu success, %zu fail\n", \
        thread_id, reads, timeuse, reads / timeuse / 1000000, success_reads, fail_reads);
      
      return NULL;
    }
    cur_obj_id = get_next_rand(cur_obj_id);
    if(ht_handle.func_find_obj(table, cur_obj_id)){
      success_reads++;
    }
    else{
      fail_reads++;
    }
    reads++;
  }
}

/**
 *  [This function inserts a cache object to the cache instance. It is used as a thread function.]
 *  @method func_insert
 *  @author Chaos
 *  @date   2023-11-22
 */
void *func_insert(void* arg){
  thread_para_t* para = (thread_para_t*) arg;
  hashtable_t* table = para->table;
  uint64_t* i_counter = para->i_counter;
  uint64_t range = para->range;
  uint32_t thread_id = para->thread_id;

// We keep track of our own local counter for inserts, to avoid
// over-burdening the shared atomic counter
  size_t inserts = 0;
  // We use thread id as seed to generate a reproducible pseudo random sequence. 
  // The items in the sequence are operated obj IDs.
  obj_id_t cur_obj_id = thread_id;
  struct timeval start, end;
  gettimeofday(&start, NULL);
  for (uint64_t it = 0; it < range; it++) {
    cur_obj_id = get_next_rand(cur_obj_id);
    cache_obj_t* cur_obj = create_cache_obj_from_obj_id(cur_obj_id);
    cache_obj_t* result = ht_handle.func_insert_obj(table, cur_obj);
    if(result){
      free_cache_obj(result);
    }
    inserts++;
  }
  /** Update the shared atomic counter */
  __sync_fetch_and_add(i_counter, inserts);
  gettimeofday(&end, NULL);
  double timeuse = (end.tv_sec - start.tv_sec) +
                   (end.tv_usec - start.tv_usec) / 1000000.0;
  INFO("Thread %d inserted %zu objects in %.1f seconds, throughput is %.2f MOPS\n", \
    thread_id, inserts, timeuse, inserts / timeuse / 1000000);
}

/**
 *  [This function inserts a cache object to the cache instance. It is used as a thread function.]
 *  @method func_remove
 *  @author Chaos
 *  @date   2023-11-22
 */
void *func_remove(void* arg){
  thread_para_t* para = (thread_para_t*) arg;
  hashtable_t* table = para->table;
  uint64_t* d_counter = para->d_counter;
  uint64_t range = para->range;
  uint32_t thread_id = para->thread_id;

// We keep track of our own local counter for removals, to avoid
// over-burdening the shared atomic counter
  size_t removals = 0;
  size_t success_removal = 0;
  size_t fail_removal = 0;

  // We use thread id as seed to generate a reproducible pseudo random sequence. 
  // The items in the sequence are operated obj IDs.
  obj_id_t cur_obj_id = thread_id;
  struct timeval start, end;
  gettimeofday(&start, NULL);
  for (uint64_t it = 0; it < range; it++) {
    cur_obj_id = get_next_rand(cur_obj_id);
    cache_obj_t* result = ht_handle.func_delete_obj(table, cur_obj_id);
    if(result){
      success_removal++;
      free_cache_obj(result);
    }
    else{
      fail_removal++;
    }
    removals++;
  }
  // Update the shared atomic counter
  __sync_fetch_and_add(d_counter, removals);
  gettimeofday(&end, NULL);
  double timeuse = (end.tv_sec - start.tv_sec) +
                   (end.tv_usec - start.tv_usec) / 1000000.0;
  INFO("Thread %d removed %zu objects in %.1f seconds, throughput is %.2f MOPS. %zu success, %zu fail\n", \
    thread_id, removals, timeuse, removals / timeuse / 1000000, success_removal, fail_removal);
}


/**
 *  This function is for mixed tests. It is used as a thread function. It works as follows:
 *  1. Insert `range` objects to the cache instance.
 *  2. Read `range` objects from the cache instance for 10 rounds.
 *  3. Delete `range` objects from the cache instance.
 *  4. Repeat step 1-3 until the flag `finished` == True.
 *  The `range` objects are generated by a pseudo random sequence. The seed of the sequence is the thread ID.
 *  @method func_mixed
 *  @author Chaos
 *  @date   2023-11-23
 */
void *func_mixed(void* arg){
  thread_para_t* para = (thread_para_t*) arg;
  hashtable_t* table = para->table;
  uint64_t* r_counter = para->r_counter;
  uint64_t* i_counter = para->i_counter;
  uint64_t* d_counter = para->d_counter;
  bool* finished = para->finished;
  uint64_t range = para->range;
  uint32_t thread_id = para->thread_id;


// We keep track of our own local counter for reads, to avoid
// over-burdening the shared atomic counter
  size_t reads = 0;
  size_t inserts = 0;
  size_t removals = 0;

  // We use thread id as seed to generate a reproducible pseudo random sequence. 
  // The items in the sequence are operated obj IDs.
  obj_id_t cur_obj_id = thread_id;
  // The loop is broken when the flag `finished` == True.
  while(true){
    cur_obj_id = thread_id;
    inserts = 0;
    removals = 0;
    for(uint64_t it = 0; it < range; it++){
      cur_obj_id = get_next_rand(cur_obj_id);
      cache_obj_t* cur_obj = create_cache_obj_from_obj_id(cur_obj_id);
      cache_obj_t* old_item = ht_handle.func_insert_obj(table, cur_obj);
      if(old_item)
        free_cache_obj(old_item);
      inserts++;
    }
    for(uint64_t rounds = 0; rounds < 10; rounds++){
      cur_obj_id = thread_id;
      reads = 0;
      for (uint64_t it = 0; it < range; it++){
        cur_obj_id = get_next_rand(cur_obj_id);
        ht_handle.func_find_obj(table, cur_obj_id);
        reads++;
      }
      __sync_fetch_and_add(r_counter, reads);
    }
    for(uint64_t it = 0; it < range; it++){
      cur_obj_id = get_next_rand(cur_obj_id);
      cache_obj_t* removed_item = ht_handle.func_delete_obj(table, cur_obj_id);
      if(removed_item)
        free_cache_obj(removed_item);
      removals++;
    }
    __sync_fetch_and_add(i_counter, inserts);
    __sync_fetch_and_add(d_counter, removals);
    if(*finished){
      return NULL;
    }
  }
}

/**
 *  [This function parses the command line arguments.]
 *  @method parse_arg
 *  @author Chaos
 *  @date   2023-11-22
 *  @param  argc            [count of input arguments]
 *  @param  argv            [input arguments]
 *  @param  description     [description of the arguments]
 *  @param  args            [optional arguments]
 *  @param  arg_vars        [variables to store the argument values]
 *  @param  arg_help        [help message for each argument]
 *  @param  arg_num         [number of optional arguments]
 */
void parse_arg(int argc, char const **argv, const char *description,
                 const char *args[], size_t *arg_vars[], const char *arg_help[],
                 size_t arg_num) {
  errno = 0;
  for (int i = 0; i < argc; i++) {
    for (size_t j = 0; j < arg_num; j++) {
      if (strcmp(argv[i], args[j]) == 0) {
        if (i == argc - 1) {
          ERROR("You must provide a positive integer argument" \
                 " after the %s argument\n", args[j]);
        } else {
          size_t argval = strtoull(argv[i + 1], NULL, 10);
          if (errno != 0) {
            ERROR("The argument to %s must be a valid size_t\n", args[j]);
          } else {
            *(arg_vars[j]) = argval;
          }
        }
      }
    }

    if (strcmp(argv[i], "--help") == 0) {
      printf("%s\n", description);
      printf("Arguments:\n");
      for (size_t j = 0; j < arg_num; j++) {
        printf("%s\t(default %zu):\t%s\n", args[j], *arg_vars[j], arg_help[j]);
      }
      exit(0);
    }
  }
}

/**
 *  This function runs a stress test on inserts, finds, and deletes.
 *  @method StressTest
 *  @author Chaos
 *  @date   2023-11-22
 */
void stress_test() {
  // Initialize threads;
  pthread_t insert_threads[g_thread_num];
  pthread_t read_threads[g_thread_num];
  pthread_t remove_threads[g_thread_num];

  int rc;
  uint64_t num_inserts = 0;
  uint64_t num_removals = 0;
  uint64_t num_reads = 0;
  bool finished = false;

  // Based on the hashtable type, we assign the function pointers to the function handles.
  // If hashtable type is cChainedHashTable.
  if(g_ht_type == 0){
    ht_handle.func_find_obj = concurrent_chained_hashtable_find_obj_id;
    ht_handle.func_insert_obj = concurrent_chained_hashtable_insert_obj;
    ht_handle.func_delete_obj = concurrent_chained_hashtable_delete_obj_id;
    ht_handle.func_create_hashtable = create_concurrent_chained_hashtable;
    ht_handle.func_free_hashtable = free_concurrent_chained_hashtable;
  }   
  // If hashtable type is chainedHashTableV2.
  // else if(g_ht_type == 1){
  //   ht_handle.func_find_obj = chained_hashtable_find_obj_id_v2;
  //   ht_handle.func_insert_obj = chained_hashtable_insert_obj_v2;
  //   ht_handle.func_delete_obj = chained_hashtable_delete_obj_id_v2;
  //   ht_handle.func_create_hashtable = create_chained_hashtable_v2;
  //   ht_handle.func_free_hashtable = free_chained_hashtable_v2;
  // }

  // If hashtable type is invalid.
  else{
    ERROR("ERROR: Invalid hashtable type\n");
  }

  // Creates a hashtable
  hashtable_t *table = ht_handle.func_create_hashtable(g_power);

  // If the user select the first test.
  if(g_test_type == 0){
    // Spawns insert threads
    for (size_t i = 0; i < g_thread_num; i++) {
      thread_para_t para = {
        .table = table,
        .i_counter = &num_inserts,
        .range = g_numkeys / g_thread_num,
        .thread_id = i,
      };
      rc = pthread_create(&insert_threads[i], NULL, func_insert, (void *)&para);
      if(rc){
        ERROR("ERROR; return code from pthread_create() is %d\n", rc);
      }
    }

    // Joins insert threads
    for (size_t i = 0; i < g_thread_num; i++) {
      rc = pthread_join(insert_threads[i], NULL);
      if(rc){
        ERROR("ERROR; return code from pthread_join() is %d\n", rc);
      }
    }

    // Spawns read threads
    for(size_t i = 0; i < g_thread_num; i++){
      thread_para_t para = {
        .table = table,
        .r_counter = &num_reads,
        .finished = &finished,
        .range = g_numkeys / g_thread_num,
        .thread_id = i,
      };
      rc = pthread_create(&read_threads[i], NULL, func_read, (void *)&para);
      if(rc){
        ERROR("ERROR; return code from pthread_create() is %d\n", rc);
      }
    }

    // Sleeps before ending the threads
    sleep(g_test_len);
    finished = true;
    // Joins read threads
    for (size_t i = 0; i < g_thread_num; i++) {
      rc = pthread_join(read_threads[i], NULL);
      if(rc){
        ERROR("ERROR; return code from pthread_join() is %d\n", rc);
      }
    }
    
    // Spawns remove threads
    for(size_t i = 0; i < g_thread_num; i++){
      thread_para_t para = {
        .table = table,
        .d_counter = &num_removals,
        .range = g_numkeys / g_thread_num,
        .thread_id = i,
      };
      rc = pthread_create(&remove_threads[i], NULL, func_remove, (void *)&para);
      if(rc){
        ERROR("ERROR; return code from pthread_create() is %d\n", rc);
      }
    }

    // Joins delete threads
    for (size_t i = 0; i < g_thread_num; i++) {
      rc = pthread_join(remove_threads[i], NULL);
      if(rc){
        ERROR("ERROR; return code from pthread_join() is %d\n", rc);
      }
    }
  }
  // If the user select the second test -- mixed test.
  else if(g_test_type == 1){
    uint64_t num_inserts_last_sec = 0;
    uint64_t num_removals_last_sec = 0;
    uint64_t num_reads_last_sec = 0;

    // Spawns mixed threads
    for(size_t i = 0; i < g_thread_num; i++){
      thread_para_t para = {
        .table = table,
        .r_counter = &num_reads,
        .i_counter = &num_inserts,
        .d_counter = &num_removals,
        .finished = &finished,
        .range = g_numkeys / g_thread_num,
        .thread_id = i,
      };
      rc = pthread_create(&read_threads[i], NULL, func_mixed, (void *)&para);
      if(rc){
        ERROR("ERROR; return code from pthread_create() is %d\n", rc);
      }
    }
    printf("----------Throughput MOPS----------\n");
    printf("Seconds\tInsert\tRead\tDelete\tTotal\n");

    // Prints the throughput every second
    for(int i = 0; i < g_test_len; i++){
      sleep(1);
      uint64_t num_inserts_this_sec = num_inserts - num_inserts_last_sec;
      uint64_t num_reads_this_sec = num_reads - num_reads_last_sec;
      uint64_t num_removals_this_sec = num_removals - num_removals_last_sec;
      printf("%d\t%.2lf\t%.2lf\t%.2lf\t%.2lf\n", i+1, num_inserts_this_sec/1000000.0, num_reads_this_sec/1000000.0, num_removals_this_sec/1000000.0, (num_inserts_this_sec + num_reads_this_sec + num_removals_this_sec)/1000000.0);
      num_inserts_last_sec = num_inserts;
      num_reads_last_sec = num_reads;
      num_removals_last_sec = num_removals;
    }
    // Ends the threads. The threads will end when the flag `finished` == True.
    finished = true;
    // Joins threads
    for (size_t i = 0; i < g_thread_num; i++) {
      rc = pthread_join(read_threads[i], NULL);
      if(rc){
        ERROR("ERROR; return code from pthread_join() is %d\n", rc);
      }
    }
  }
  // If the user select an invalid test type.
  else{
    ERROR("Invalid test type: %lu. It should be 0 or 1.\n", g_test_type);
  }
  // Frees the hashtable
  ht_handle.func_free_hashtable(table);

  printf("----------Results----------\n");
  printf("Number of inserts:\t%lu\n", num_inserts);
  printf("Number of reads:\t%lu\n", num_reads);
  printf("Number of removals:\t%lu\n", num_removals);
  printf("Total throughput:\t%.2lf MOPS\n", (num_inserts + num_reads + num_removals)/g_test_len/1000000.0);
}


int main(int argc, char const *argv[])
{
  const char *args[] = {"--power", "--thread-num", "--time", "--test-type", "--ht-type"};
  size_t *arg_vars[] = {&g_power, &g_thread_num, &g_test_len, &g_test_type, &g_ht_type};
  const char *arg_help[] = {
    "The number of keys to size the table with, expressed as a power of 2",
    "The number of threads to spawn for each type of operation",
    "The number of seconds to run the test for lookup",
    "The type of test. \n\
    0: Three tests: Insert, Read, and Delete. Each test is non-overlapping. \n\
    1: Mixed. Each thread performs insert, read, and delete in turn. The three operations are overlapping between different threads.",
    "The type of hashtable. \n\
    0: cChainedHashTable; \n\
    1:chainedHashTableV2"};
  parse_arg(argc, argv, "Runs a stress test on concurrent hashtables for inserts, finds, and deletes.",
              args, arg_vars, arg_help, sizeof(args) / sizeof(const char *));
  g_numkeys = 1 << g_power;
  stress_test();
  return 0;
}

#ifdef __cplusplus
}
#endif