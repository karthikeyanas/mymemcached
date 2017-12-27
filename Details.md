# mymemcached
My minimal implementation of Memcached server.

MYMEMCACHED
Karthikeyan Srinivasan

MyMemcached implements a subset of memcached protocol. It supports
• Set – Set a key with certain value in the memcached server. Doesn’t implement flags,
exptime or no reply.
• Get – Get the value for a given key from memcached server.

We have three important classes in MyMemcached and one test program.

BufferedReader
This is a buffered reader used to read from the socket. It tries to read in chunk sizes of 1024 bytes or whatever is available from the socket. We can extract commands and values from this buffer like we would be reading from any stream like socket, but it hides the abstraction of how many times we might have to read form the socket to read a command or a value.

LRUMemCache
This serves as the LRU cache to store the key-value for MyMemcached. It has the following.
• Linked list that has items ordered with most recently accessed or stored item in the
front.
• A Map that store the Nodes in a linked list based of the key. This map helps identify
the Node in the linked list in O(1) and we can remove it or promote it in linked list in O(1) again.

Memcached
This implements the main server functionality. It spawns a new thread for every connection. From then on all the commands from that connection is handled by that thread. Clients may send multiple commands in a single connection and the spawned thread would serve those commands. If the server cannot read data from the client for 5 seconds, it assumes client has closed the connection and closes the connection from its end.
It uses BufferedReader to read commands, parses them and use LRUMemcache store or retrieve keys.

MemcachedTest
MemcachedTest uses libmemcached API to test the functionalities of MyMemcached. It implements three tests.
• simplePresentAbsentKeyTest - Simple test to store and retrieve a key. Try to retrieve a non-existent key.
• lruCacheEvictionTest - This tries to store more than 1024 keys which is the current configured size of MyMemcached's LRU queue. This verifies the LRU aspect of MyMemcached.
• multipleThreadStressTest - This tries to store and retrieve keys from multiple threads at the same time.

Build Instructions
Please refer to the README.txt in the code base to build instructions.

Improvement and Optimizations
• MyMemcached spawns a new thread for each connection. A better approach would be to have a thread pool that picks up a connection from a queue where we enqueue the connections. This way we can limit the number of threads and avoid overhead of creating and destroying threads.
• Another level of optimization is to have another threadpool to handle multiple command requests from a single connection. In the current mode if the client opens a connection, all the commands sent by the client serialized in a single thread which is not good for performance.
• LRU cache is implemented using C++ lists. C++ containers do a lot of small mallocs to create the wrapper objects for the list. A better and more efficient way would be to store the next and previous in the object we store in the C++ list and maintain our own list. C++ containers are known to cause memory fragmentation and hence affect performance and I have had first hand experience of this.
• As an extension to previous point, we could implement our own hash table for same performance reasons.
• We use a single LRU cache protected by a Mutex. Better way would be to have multiple LRU lists that handle different key ranges. On top of that we can try to replace the mutex with a reader writer lock.
• Current code base dynamically allocates memory for each of the objects to be stored in the cache and uses std::vector and string extensively due to time constraints. Better approach would be have a Slab allocator or even better a Buddy slab allocator which can manage multiple size objects and balance the different sized pools based on demand.
• Current MyMemcached server relies on hash function for string on unordered_map. It has been implemented this way due to the scope of the project and can be improved.
• MyMemcached implement just TCP protocol. Since memcached is a mainly used as a performance layer, overhead of maintaining a connection could be huge and not all clients require TCP guarantees. As optional UDP implementation would help in performance for some class of clients.

