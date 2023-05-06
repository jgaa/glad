# Asio building bricks

My intent is to create a handful of useful algorithms / building bricks built on top of boost.Asio

The first one is a smart cache designed for use-cases like caching API-keys/RBAC data in web-services. 

The project is named after a little dog I found in my garden one evening. Despite a very touch and difficult start of his life, he has always been very very happy. *Glad* is the Norwegian word for happy ;)

## SmartCache

The cache is a trivial key/value store. If an item is not found,
a fetch method (supplied by you) are called to asynchronously get the
value. The request is paused, and the thread freed to do other work.

If more requests comes in for a key that is in the process of being looked up,
they are added to a list of pending requests. A key is only fetched once. When
the value is available, any and all pending requests are resumed.

**Features:**

- Uses asio composed completion templates. Fully asynchronous continuations includes:
    - asio/C++ 20 co-routines
    - asio stackless co-routines
    - asio stackfull co-routines
    - callbacks
    - fututes
- A value is only fetched once from the outside. If a thousand almost simultaneous requests for the same key occurs, one request is made to lookup the key using the user-supplied *fetch* functor. When the value (or an error) is available, all the requests pending for that key are resumed (as threads becomes available). 
- A key can be invalidated at any time, also when there is a fetch operation in progress and one or more requests are waiting for that key. If there are requests waiting, a new fetch operation will be initiated to ensure that the requesters get the correct value.
- A key can be erased at any time. 
- Internally, the cache use sharding to allow fast, simultaneous processing of different keys.

Examples:
- [Simple example with C++ 20 coroutine](examples/cxx20-simple.cpp)


