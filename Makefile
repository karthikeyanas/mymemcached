CC=g++

VPATH=lib:test

IDIR= ./include

CFLAGS= -I /usr/local/include -I  -Wall -g -fPIC

DEPS=./*.h

LFLAGS = -L /usr/local/lib/

CLIENT_LIBS=-lmemcached -lpthread 

LIBS=-lresolv -lnsl -lpthread 


TEST=MemCachedTest
TEST_OBJS=MemCachedTest.o

MEMCACHED=mymemcached
MEMCACHED_OBJS=Memcached.o

all: $(TEST) $(MEMCACHED)

%.o:%.cpp $(DEPS)
	$(CC) -std=gnu++0x -c -o  $@ $< $(CFLAGS)

$(TEST): $(TEST_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) -L. $(LFLAGS) $(CLIENT_LIBS) -lrt

$(MEMCACHED): $(MEMCACHED_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) -L. $(LFLAGS) $(LIBS) -lrt

clean: 
	rm -rf $(TEST) $(TEST_OBJS) $(MEMCACHED) $(MEMCACHED_OBJS) *.log

