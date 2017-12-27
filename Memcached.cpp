#include "Memcached.h" 

// This is a buffered reader used to read from the socket. It tries to
// read in chunk size of 1024 bytes or whatever is available from the
// socket. We can just extract commands and values from this buffer
// like we would be reading from any stream like socket, but it hides
// or abstraction of how many time we might have to read form the
// socket to read a command or a value.
class BufferedReader {
private:   
    char buff_[BUFFSIZE]; // Buffer to buffer reads
    int buffOffset_;      // Offset at which next read from buff_ should happen.
    int pendingBytes_;    // Number of bytes pending to be read from buff_.
    int connfd_;          // Connection file descriptor. 
    int zeroBytesRead_;   // Counter to identify socketimeout if the socket 
                          // doesn't have any more data.
public:
    BufferedReader( int pConnfd ) {
        connfd_ = pConnfd;
        buffOffset_ = 0;
        pendingBytes_ = 0;
    }

    // Reads 1K or how much every is avaiable from the socket.
    int read1K( char *buffer ) {
        int readBytes = read( connfd_, buffer, BUFFSIZE);
        return readBytes; 
    }

    // This returns first available command from the buffer.
    // It first tries to read pendingBytes_ in buffer.
    // If we dont find a full command in that, we try to read 
    // more from socket.
    void readCommand( vector<char> *buffer ) {
        static int rSeen = false;

        while ( pendingBytes_ > 0 && buffOffset_ < BUFFSIZE ) {
            if ( buff_[buffOffset_] == '\r' ) {
                rSeen = true;
                pendingBytes_--;
                buffOffset_++;
                continue;
            } else if ( buff_[buffOffset_] == '\n' && rSeen ) {
                rSeen = false;
                pendingBytes_--;
                buffOffset_++;
                return;
            }
            buffer->push_back( buff_[buffOffset_]);
            pendingBytes_--;
            buffOffset_++;
        }

        int readBytes;
        while( 1 ) {
            readBytes = read1K( buff_ );

            // If we read zero bytes threadTimeOutSecs number of
            // times, we assume client has closed the connection.
            if( readBytes == 0 ) {
                zeroBytesRead_++;
                if ( zeroBytesRead_ > threadTimeOutSecs ) {
                    break;
                } 
                sleep( 1 );
                continue;
            }

            buffOffset_ = 0;     
            pendingBytes_ += readBytes;

            while( pendingBytes_ > 0 && buffOffset_ < BUFFSIZE ) {
                if ( buff_[buffOffset_] == '\r' ) {
                    rSeen = true;
                    pendingBytes_--;
                    buffOffset_++;
                    continue;
                } else if ( buff_[buffOffset_] == '\n' && rSeen ) {
                    rSeen = false;
                    pendingBytes_--;
                    buffOffset_++;
                    return;
                }
                buffer->push_back( buff_[buffOffset_]);
                pendingBytes_--;
                buffOffset_++;
           }
        }
    }

    // This is used to read the value section of the set command.
    // It works similar to command above.
    int readValue( char *buffer, int bytes ) {
        int totalToRead = bytes;
        if ( pendingBytes_ > 0 ) {
            int toRead = pendingBytes_; 
            if( bytes < pendingBytes_ ) {
                toRead = bytes;
            }
            memcpy( buffer, &(buff_[buffOffset_]), toRead );
            bytes -= toRead;
            pendingBytes_ -=toRead;
            buffOffset_ += toRead;
            if ( bytes == 0 ) {
                return totalToRead;
            }
        }

        int readBytes = 0;
        while( 1 ) {
            readBytes = read1K( buff_ );

            if( readBytes == 0 ) {
                zeroBytesRead_++;
                if ( zeroBytesRead_ > threadTimeOutSecs ) {
                    return totalToRead - bytes;
                } 
                sleep( 1 );
                continue;
            }

            buffOffset_ = 0;     
            pendingBytes_ += readBytes;
            
            int toRead = pendingBytes_; 
            if( bytes < pendingBytes_ ) {
                toRead = bytes;
            }
            memcpy( buffer, &(buff_[buffOffset_]), toRead );
            bytes -= toRead;
            pendingBytes_ -=toRead;
            buffOffset_ += toRead;
            if ( bytes == 0 ) {
                return totalToRead;
            }
        }
    }
};

// This serves as the LRU cache to store the key-value for Memcached.
// It has the following.
//
// 1. Linked list that has item ordered in most recently accessed or
// stored item in the front.
// 2. A Map that stored the Node in linked list based of this key.
// This map helps identify the Node in the linked list in O(1) and we
// can remove it or promote it in linked list in O(1) again.
//
class LRUMemCache {
private:
    list< MemcachedItem * > cacheQueue_;
    unordered_map< string, list< MemcachedItem * >::iterator> cacheMap_;
    int maxCacheSize_;

    // Ensure LRC cache accesses from different threads are isolated.
    pthread_mutex_t cacheLock;

public:
    LRUMemCache( int size ) {
        maxCacheSize_ = size;
        pthread_mutex_init( &cacheLock, NULL );
    }       

    // If the item is present, return it amd move it to front of LRU
    // list.
    MemcachedItem * getItem( string key ) {
        MemcachedItem *retVal = NULL;
        pthread_mutex_lock ( &cacheLock );
        if( cacheMap_.find( key) != cacheMap_.end() ) {
             list< MemcachedItem * >::iterator val = 
                 cacheMap_.find( key )->second;
             cacheQueue_.erase( val );
             cacheQueue_.push_front( *val );
             retVal = *val;
        }
        pthread_mutex_unlock ( &cacheLock );
        return retVal;
    }

    // If the item is present update it and move it to front or LRU
    // list.
    // If it is not present
    // 1. If cache is not full, add it to front of list
    // 2. If the cache is full, evict the last item from list and add
    // the new item to front
    void setItem( string key, MemcachedItem * val ) {
        pthread_mutex_lock ( &cacheLock );
        // If the value is already present remove it from the queue.
        if( cacheMap_.find( key) != cacheMap_.end() ) {
            cacheQueue_.erase( cacheMap_[key] );
        } else {
            // If the cache is full evict an item
            if ( cacheQueue_.size() == maxCacheSize_ ) {
                MemcachedItem *last = cacheQueue_.back();
                pr_debug( "Evicting key %s\n", last->key_.c_str() );
                // Remove the last item in queue.
                cacheQueue_.pop_back();
                // Update the map.
                cacheMap_.erase( last->key_ );
                delete last;
            }
        }

        // Update the map.
        cacheQueue_.push_front( val );
        cacheMap_[key] = cacheQueue_.begin();
        pthread_mutex_unlock ( &cacheLock );
    }

};

// Main Memcached server instance
class Memcached {
private:
   LRUMemCache *lruCache_; // LRU cache that Memcached maintains.
public:

    // Opens TCP servers in the specified port.
    int tcpServerOpen(int port)
    {
        int sockfd;
        struct sockaddr_in serveraddr;
    
        if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
        {
            pr_info(" Socket Creation Error \n");
            exit(1);
        }
    
        bzero(&serveraddr,sizeof(serveraddr));
        serveraddr.sin_family=AF_INET;
        serveraddr.sin_port=htons(port);
        serveraddr.sin_addr.s_addr=INADDR_ANY;
    
        pr_info("mymemcached started on %s \n",inet_ntoa(serveraddr.sin_addr));
    
        if(bind(sockfd,(struct sockaddr *)&serveraddr,sizeof(struct sockaddr))==-1)
        {
            pr_info(" Error in Binding with the socket \n");
            exit(2);
        }
    
        if(listen(sockfd, BACK_LOG )==-1)
        {
            pr_info(" Error in Listening \n");
            exit(3);
        }
        return sockfd;
    }
   
    // After we have read the first line of the command, this function
    // tokenizes it based on space delimiter and extract the command
    // into MCCommand.
    void extractCommand( vector<char> *commandBuffer,
                         MCCommand *mcCommand ) {
        commandBuffer->push_back( '\0' );
        char *buffStr = commandBuffer->data();
        char *token;
        int i = 1;
        token = strtok( buffStr, " ");
        while ( token !=NULL ) {
            if( i == 1) {
                // Checking command
                if ( strcmp( token, "set") == 0 ) {
                    mcCommand->command_ = COMMAND_SET;
                } else if ( strcmp( token, "get") == 0 ) { 
                    mcCommand->command_ = COMMAND_GET;
                } else {
                    // Unsupported command
                    mcCommand->command_ = COMMAND_INVALID;
                    return;
                }
            } else if( i == 2 ) {
                mcCommand->key = string( token );
                if ( mcCommand->command_ == COMMAND_GET ) {
                    // We dont have to read anymore for get.
                    return;
                }
            }  else if ( i == 5 ) {
                // We ignore parametere 3 and 4 in our version of
                // memcached.
                // Extract size of get
                mcCommand->size = atoi( token );
                // We don't care about noreply
                return;
            }
            i++;
            token = strtok(NULL, " ");
        } 
    }

    // Once we identify the command that has been recevied as set,
    // this handles.
    //
    // 1. Read the value for the set command.
    // 2. Storing it in LRU cache.
    // 3. Send response to client.
    void handleSetCommand( int connfd, BufferedReader *buffReader, MCCommand *mcCommand ) {
        char valueBuffer[ mcCommand->size + 2];

        // Adding plus two include /r/n
        int bytesRead = buffReader->readValue( valueBuffer, mcCommand->size + 2 );
        if ( bytesRead != mcCommand->size + 2 ) {
            pr_info( "Timeout waiting for value on key : %s", mcCommand->key.c_str() );
        }
        
        pr_debug( "Set Command Value :  ");
        for( int i = 0; i < mcCommand->size; i++ ) {
            pr_debug( "%c", valueBuffer[i] );
        }
        pr_debug( "\n" );

        MemcachedItem *mcItem = new MemcachedItem( mcCommand->key, 
                                                   mcCommand->size + 2, 
                                                   &(valueBuffer[0]) );

        lruCache_->setItem( mcCommand->key, mcItem );

        // Key has been store. Send reponse back to client
        int written = write( connfd, storedReply, storedReplySize );
        if ( written < 0 ) {
            pr_info( "Error write to socket %d", connfd);
        }
    }

    // Once we identify the command that has been recevied as set,
    // this handle it by getting it from the LRU cache and sending
    // right reponse back to client.
    void handleGetCommand( int connfd, MCCommand *mcCommand ) {
        pr_debug( "Get command key : %s\n", mcCommand->key.c_str() );    
        MemcachedItem *mcItem = lruCache_->getItem( mcCommand->key );

        // If item is not in the cache just send END.
        if( mcItem != NULL ) {
            pr_debug( "Value :");
            for( int i = 0; i < mcItem->size_; i++ ) {
                pr_debug( "%c", mcItem->value_[i] );
            }
            pr_debug( "\n" );
    
            // Reduce 2 as size stored includes /r/n
            int retSize = mcItem->size_ - 2;
    
            // Write the text line for response
            string returnBuffer = string( getReplyStart ) + " " + 
                                   mcItem->key_ + " 0 " +
                                   to_string( (long long int)retSize )+ "\r\n";
            int written = write( connfd, returnBuffer.c_str(), returnBuffer.size());
            if ( written < 0 ) {
                pr_info( "Error write to socket %d", __func__, connfd);
            }
    
            written = write( connfd, mcItem->value_, mcItem->size_ );
            if ( written < 0 ) {
                pr_info( "Error write to socket %d", __func__, connfd);
            }
         } else {
            pr_debug( "Key %s not present\n", mcCommand->key.c_str() );
         }

        int written = write( connfd, endReply, endReplySize );
        if ( written < 0 ) {
            pr_info( "Error write to socket %d", __func__, connfd);
        }
    }

    void handleInvalidCommand() {
        pr_debug( "Invalid memcached command\n");
    }

    // Function that handles every connection opened by the client.
    // Client can send as many commands as it may with a single
    // connection. 
    //
    // We close the connection if we cannot read from anything from
    // socket for threadTimeOutSecs.
    void handleConnection( int connfd ) {
        BufferedReader *buffReader = new BufferedReader( connfd );
        while( 1 ) {
            vector<char> commandBuffer;
            buffReader->readCommand( &commandBuffer );

            if( commandBuffer.size() == 0 ) {
                // We are done with timeout. close the connection and
                // exit the thread.
                close( connfd );
                break;
            }
    
            MCCommand mcCommand;
            extractCommand( &commandBuffer, &mcCommand );
    
            if  ( mcCommand.command_ == COMMAND_SET ) {
                mcCommand.printCommand();
                handleSetCommand( connfd, buffReader, &mcCommand );
            } else if ( mcCommand.command_ == COMMAND_GET ) {
                mcCommand.printCommand();
                handleGetCommand( connfd, &mcCommand );
            } else {
                // return error to client. Command is not supported.
                handleInvalidCommand();
            } 
        }
    }

    // Worker function called when we create a new thread to server a
    // connection.
    static void * workerFunc( void *threadArg ) {
       ThreadArg *tArg = (ThreadArg *)threadArg;
       int connfd = tArg->sockfd_;
       pr_info( "Created new thread %u for connection %d \n", pthread_self(), connfd );        

       tArg->memcached_->handleConnection( connfd );

       pr_info( "Exiting thread %u for connection %d \n", pthread_self(), connfd );        

       free( tArg );

       pthread_exit( NULL ); 
    }

    // Main memcached server. Spawns a new thread for each connection.
    void startServer() {
        int sockfd = tcpServerOpen( MEMCACHED_PORT );
        int newfd; 
        struct sockaddr_in clientaddr;

        while ( 1 ) {
            socklen_t sin_size=sizeof(struct sockaddr_in);

            newfd = accept( sockfd,(struct sockaddr *)&clientaddr,&sin_size );
            if ( newfd < 0 ) {
                pr_info("Error Accepting a client \n");
                continue;
            } else {
                pr_debug("Server: got connection from %s %d\n",inet_ntoa(clientaddr.sin_addr),newfd);
                pthread_t threadId;
                ThreadArg *tArg = new ThreadArg( this, newfd);
                pthread_create( &threadId, NULL, workerFunc, tArg); 
            }
 
        }
    }

    Memcached() {
        lruCache_ = new LRUMemCache( MAX_LRU_CACHE_SIZE );
    }

    ~Memcached() {
        delete lruCache_;
    }
};

void memcachedExit( int signo ) {
    pr_info("mymemcached exiting\n");
    exit(0);
}

int main( void ) {
    signal(SIGINT, memcachedExit);
    Memcached memcachedServer;
    memcachedServer.startServer();
    return 0;
}
