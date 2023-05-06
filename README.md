# The Glad Project

My intent is to create a handful of useful algorithms, primarily for use in my own applications. But if you find something useful here, feel free to use it and to contribute suggestions and pull requests.

The first one is a smart cache designed for use-cases like caching API-keys/RBAC data in web-services. 

The project is named after a little dog I found in my garden one evening. Despite a very touch and difficult start of his life, he has always been very very happy. *Glad* is the Norwegian word for happy ;)

![Glad the dog](glad-the-dog/images/glad01.jpg)

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

*perftest*, ran on my workstation using the default settings.
```
Starting up (using jgaa::glad 0.1.0, boost 1_81).
Using 8 threads and 7 shards.
I will create 10,000,000 objects and then read 20,000,000 existing objects using sequential keys and 10,000 failed keys (errors on fetch).
...
Spent 12.74809 seconds doing test stuff, including 6.45128 seconds for writes (1,550,081.11030 writes/sec) and 6.29680 seconds for reads (3,176,218.20089 reads/sec)
cputime=75.97142, system=6.10435, minflt=381,083, majflt=2, nswap=0, nsignals=0, nvcsw=511,708, nivcsw=1,035, maxrss=3,251,324, ixrss=0, idrss=0, isrss=0
Done
```

Examples:
- [Simple example with C++ 20 coroutine](examples/cxx20-simple.cpp)
