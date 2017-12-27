#ifndef _MEMCACHED_H
#define _MEMCACHED_H

#include <stdio.h> 
#include <string.h>
#include <stdlib.h> 
#include <errno.h> 
#include <unistd.h>
#include <arpa/inet.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <list>
#include <unordered_map>

using namespace std;

// This is used to enable more verbose logging
//#define DEBUG 1

#define MEMCACHED_PORT 11211
// Chunk size we try to read from the socket
#define BUFFSIZE 1024
// Back log for listen system call.
#define BACK_LOG 1024
// Size at which we will start evicting from the LRU cache
#define MAX_LRU_CACHE_SIZE 1024

// Constant for memcached protocol reply
static const char *storedReply = "STORED\r\n";
static const char *endReply = "END\r\n";
// Storing size to avoid strlen every time
static const int storedReplySize = 8;
static const int endReplySize = 5;
static const char *getReplyStart = "VALUE";

// A thread service the connection will sleep for 1 second 
// if it read zero bytes. If it slept for threadTimeOutSecs, 
// it assume client has closed the connection and quits. 
static const int threadTimeOutSecs = 5;

#ifdef DEBUG
#define pr_debug(fmt,arg...) \
        fprintf( stderr, fmt,##arg)
#else
static inline int pr_debug(const char * fmt, ...)
{
        return 0;
}
#endif

#define pr_info(fmt,arg...) \
        fprintf( stderr,fmt,##arg)

class Memcached;

// Set and get are only commands supported.
enum MemcacheCommand {
    COMMAND_INVALID = 0,
    COMMAND_GET,
    COMMAND_SET
};

// We create a thread for handling each connection to memcached
// server. This class is passed as arguement to the thread function.
class ThreadArg {
public:
    Memcached *memcached_;
    int sockfd_;

    ThreadArg( Memcached *pMemcached, int pSockfd) {
        memcached_ = pMemcached;
        sockfd_ = pSockfd;
    }
};

// Once we parse memcached commands we store it in this structure.
class MCCommand {
public:
    MemcacheCommand command_;
    string key;
    int size;
    
    void printCommand( void ) {
        pr_debug( "Command:%s, Key:%s, Size:%d\n", 
                  command_== COMMAND_GET ? "get" : "set",
                  key.c_str(), size );
    }

};

// Each key-value pair is maintained as this Class in the LRU cache. 
// LRU cache is list of these items.
class MemcachedItem {
public: 
    string key_;
    int size_;
    char *value_;
   
    MemcachedItem( string key, int size, char *buffer ) {
        key_ = key;
        size_ = size;
        value_ = ( char *) malloc( sizeof( char ) * size );
        memcpy( value_, buffer, size_ );
    }

    ~MemcachedItem() {
        free( value_ );
    }           
};

#endif // _MEMCACHED_H
