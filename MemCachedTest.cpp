#include <libmemcached/memcached.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <pthread.h>

using namespace std;

#define KEY_PER_THREAD 5

// Simple test to store and retreive a key.
// Try to retrieve a non-existent key 
void simplePresentAbsentKeyTest() {
  fprintf( stderr, "RUNNING %s \n", __func__ );
  memcached_server_st *servers = NULL;
  memcached_st *memc;
  memcached_return rc;
  char key[] = "keystring";
  char absentKey[] = "keystring-notpresent";
  char value[] = "keyvalue";

  char *retrieved_value;
  size_t value_length;
  uint32_t flags;

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, "localhost", 11211, &rc);
  rc = memcached_server_push(memc, servers);

  // Store a key.
  if (rc == MEMCACHED_SUCCESS)
    fprintf(stderr, "Added server successfully\n");
  else
    fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));

  rc = memcached_set(memc, key, strlen(key), value, strlen(value), (time_t)0, (uint32_t)0);

  if (rc == MEMCACHED_SUCCESS)
    fprintf(stderr, "Key stored successfully\n");
  else
    fprintf(stderr, "Couldn't store key: %s\n", memcached_strerror(memc, rc));

  // Retrive the stored key.
  retrieved_value = memcached_get(memc, key, strlen(key), &value_length, &flags, &rc);

  if (rc == MEMCACHED_SUCCESS) {
    fprintf(stderr, "Key retrieved successfully\n");
    fprintf(stderr, "The key '%s' returned value '%s'.\n", key, retrieved_value);
    free(retrieved_value);
  }
  else
    fprintf(stderr, "Couldn't retrieve key : %s : %s\n", key, memcached_strerror(memc, rc));

  // Retrieve a non-existent key.
  retrieved_value = memcached_get(memc, absentKey, strlen(absentKey), &value_length, &flags, &rc);

  if (rc == MEMCACHED_SUCCESS) {
    fprintf(stderr, "Key retrieved successfully\n");
    fprintf(stderr, "The key '%s' returned value '%s'.\n", absentKey, retrieved_value);
    free(retrieved_value);
  }
  else
    fprintf(stderr, "Couldn't retrieve key: %s : %s\n", absentKey, memcached_strerror(memc, rc));

  memcached_free( memc );
  fprintf( stderr, "FINISHED %s \n\n", __func__ );
}

// This tries to store more than 1024 keys which is the current configured
// size of mymemcached's LRU queue. This verifies the LRU aspect of
// mymemcacehd.
void lruCacheEvictionTest() {
  fprintf( stderr, "RUNNING %s \n", __func__ );
  memcached_server_st *servers = NULL;
  memcached_st *memc;
  memcached_return rc;
  string key = "keystring";
  string value = "keyvalue";

  int numKeys = 1500;

  char *retrieved_value;
  size_t value_length;
  uint32_t flags;

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, "localhost", 11211, &rc);
  rc = memcached_server_push(memc, servers);

  // Store a key.
  if (rc == MEMCACHED_SUCCESS)
    fprintf(stderr, "Added server successfully\n");
  else
    fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));

  int successCount = 0, failureCount = 0;
  for( int i = 0; i < numKeys ; i++ ) {
      string currKey = key + to_string( (long long int)i );
      string currValue = value + to_string( (long long int)i );
      rc = memcached_set(memc, currKey.c_str(), currKey.size(), currValue.c_str(), currValue.size(), (time_t)0, (uint32_t)0);
    
      if (rc == MEMCACHED_SUCCESS)
          successCount++;
      else
          failureCount++;
  }
  fprintf( stderr, "Key store successful %d, failed %d \n", successCount, failureCount );

  successCount = 0;
  failureCount = 0;
  for( int i = 0; i < numKeys ; i++ ) {
      string currKey = key + to_string( (long long int)i );
      // Retrive the stored key.
      retrieved_value = memcached_get(memc, currKey.c_str(), currKey.size(), &value_length, &flags, &rc);
    
      if (rc == MEMCACHED_SUCCESS) {
        free(retrieved_value);
        successCount++;
      }
      else
        failureCount++;
  }
  fprintf( stderr, "Retrieve key successful %d, failed %d \n", successCount, failureCount );

  memcached_free( memc );
  fprintf( stderr, "FINISHED %s \n\n", __func__ );
}

void *setThreadFunc( void *arg) {
  int threadNo = *((int *)arg); 
  memcached_server_st *servers = NULL;
  memcached_st *memc;
  memcached_return rc;
  string key = "keystring";
  string value = "keyvalue";

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, "localhost", 11211, &rc);
  rc = memcached_server_push(memc, servers);

  // Store a key.
  if (rc != MEMCACHED_SUCCESS)
    fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));

  int successCount = 0, failureCount = 0;
  for( int i = threadNo * KEY_PER_THREAD + 1 ; i <= threadNo * KEY_PER_THREAD + KEY_PER_THREAD ; i++ ) {
      string currKey = key + to_string( (long long int)i );
      string currValue = value + to_string( (long long int)i );
      rc = memcached_set(memc, currKey.c_str(), currKey.size(), currValue.c_str(), currValue.size(), (time_t)0, (uint32_t)0);
    
      if (rc == MEMCACHED_SUCCESS)
          successCount++;
      else
          failureCount++;
   }
  fprintf( stderr, "%u Key store successful %d, failed %d \n", pthread_self(), successCount, failureCount );
  memcached_free( memc ); 
  pthread_exit(NULL);
}

void *getThreadFunc( void *arg) {
  int threadNo = *((int *)arg); 
  memcached_server_st *servers = NULL;
  memcached_st *memc;
  memcached_return rc;
  string key = "keystring";
  string value = "keyvalue";

  int numKeys = 1500;

  char *retrieved_value;
  size_t value_length;
  uint32_t flags;

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, "localhost", 11211, &rc);
  rc = memcached_server_push(memc, servers);

  if (rc != MEMCACHED_SUCCESS)
    fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));

  int successCount = 0;
  int failureCount = 0;
  for( int i = threadNo * KEY_PER_THREAD + 1; i <= threadNo * KEY_PER_THREAD + KEY_PER_THREAD ; i++ ) {
      string currKey = key + to_string( (long long int)i );
      // Retrive the stored key.
      retrieved_value = memcached_get(memc, currKey.c_str(), currKey.size(), &value_length, &flags, &rc);
    
      if (rc == MEMCACHED_SUCCESS) {
        free(retrieved_value);
        successCount++;
      }
      else
        failureCount++;
  }
  fprintf( stderr, "Thread %u Retrieve key successful %d, failed %d \n", pthread_self(),successCount, failureCount );
  memcached_free( memc ); 
  pthread_exit(NULL);
}

// This tries to store and retrive keys from multiple threads at the
// same time.
void multipleThreadStressTest() {
  fprintf( stderr, "RUNNING %s \n", __func__ );
  int numThreads = 5;
  pthread_t threads[numThreads];

  int threadRanges[numThreads];
  for( int i = 0; i < numThreads; i++ ) {
        threadRanges[i] = i;
  }

  fprintf( stderr, "Starting set threads \n");
  for( int i = 0; i < numThreads ; i++ ) {
        int ret = pthread_create( &threads[i], NULL, setThreadFunc, &threadRanges[i]);
        if( ret ) {
            fprintf( stderr, "ERROR creating threads %d\n", ret);
        }
  }

  sleep( 10 );

  pthread_t threads2[numThreads];
  fprintf( stderr, "Starting get threads \n");
  for( int i = 0; i < numThreads ; i++ ) {
        int ret = pthread_create( &threads2[i], NULL, getThreadFunc, &threadRanges[i]);
        if( ret ) {
            fprintf( stderr, "ERROR creating threads %d\n", ret);
        }
  }

  sleep( 10 );
  fprintf( stderr, "FINISHED %s \n\n", __func__ );
  pthread_exit( NULL);
}


int main(int argc, char **argv) {
  simplePresentAbsentKeyTest();
  lruCacheEvictionTest();
  multipleThreadStressTest();
  return 0;
}
