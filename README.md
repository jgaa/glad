# The Glad Project

My intention is to create and implement a handful of useful algorithms in C++ 20, primarily for use in my own applications. But if you find something useful here, feel free to use it and to contribute suggestions and pull requests.

The first one is an asynchronous "smart" cache designed for use-cases like caching API-keys/RBAC data in web-services. 

The project is named after a tiny, very ill puppy one of my dogs found in my garden late one evening. Despite a tough and difficult start of his life, he has always been very very happy. *Glad* is the Norwegian word for happy ;)

![Glad the dog](glad-the-dog/images/glad02.jpg)
(Glad, here with his big friend Ares)

## AsyncCache

The cache is a trivial key/value store. If an item is not found,
a fetch method (supplied by you) are called to asynchronously get the
value. The request is paused, and the thread freed up to do other work.

If more requests comes in for a key that is in the process of being looked up,
they are added to a list of pending requests. When the value is available,
any and all pending requests are resumed.

**Features:**
- Uses asio composed completion templates. Fully asynchronous continuations includes:
    - asio/C++ 20 co-routines
    - asio stackless co-routines
    - asio stackfull co-routines
    - callbacks
    - futures
- A value is only fetched once from the outside. If a thousand almost simultaneous requests for the same key occurs, one request is made to lookup the key using the user-supplied *fetch* functor. When the value (or an error) is available, all the requests pending for that key are resumed (as threads becomes available). 
- A key can be invalidated at any time, also when there is a fetch operation in progress and one or more requests are waiting for that key. If there are requests waiting, a new fetch operation will be initiated to ensure that the requesters get the correct value.
- A key can be erased at any time. 
- Internally, the cache use sharding to allow fast, simultaneous processing of different keys.

**Performance**
The performance can certainly be improved on. But at the moment, it's fast enough for my use.

*perftest*, ran on my aging gaming laptop using the default settings.
```
Starting up (using jgaa::glad 0.1.0, boost 1_82).
Using 8 threads and 7 shards.
I will create 10,000,000 objects and then read 20,000,000 existing objects using sequential keys and 10,000 failed keys (errors on fetch). 
...
Spent 7.91382 seconds doing test stuff, including 4.33107 seconds for writes (2,308,900.62839 writes/sec) and 3.58275 seconds for reads (5,582,310.94078 reads/sec)
cputime=46.24357, system=5.56283, minflt=1,271,752, majflt=5, nswap=0, nsignals=0, nvcsw=560,072, nivcsw=256, maxrss=3,248,528, ixrss=0, idrss=0, isrss=0
Done
```

Examples:
- [Simple example with C++ 20 coroutine](examples/cxx20-simple.cpp)

Blog-post: [A smart asynchronous object-cache in C++](https://lastviking.eu/async_cache.html)
