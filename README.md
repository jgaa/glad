# asio-building-bricks

My intent is to create a handful of useful algorithms / building bricks built on top of boost.Asio

The first one is a smart cache designed for use-cases like caching API-keys/RBAC data in web-services. 

## SmartCache

The cache is a trivial key/value store. If an item is not found,
a fetch method (supplied by you) are called to asynchronously get the
value. The request is paused, and the thread freed to do other work.

If more requests comes in for a key that is in the process of being looked up,
they are added to a list of pending requests. A key is only fetched once. When
the value is available, any and all pending requests are resumed.

**Features:**

- Uses asio composed completion templates. Supports asynchronous use of callbacks, 
asio stackless co-routines, asio stackfull co-routines, asio/C++ 20 co-routines.
- A value is only fetched once from the outside. If a thousand almost simultaneous requests for the same key occurs, one request is made to lookup the key using the user-supplied *fetch* functor. When the value (or an error) is available, all the requests pending for that key are resumed (as threads becomes available). 
- A key can be invalidated at any time, also when there is a fetch operation in progress and one or more requests are waiting for that key. If there are requests waiting, a few fetch operation will be initiated to ensure that the requesters get the correct value.
- A key can be erased at any time. 
- Internally, the cache use sharding to allow simultaneous processing of different keys.

Example: 
```C++

#include <boost/asio/co_spawn.hpp>
#include "asio-building-bricks/SmartCache.hpp"

int main(int argc, char **argv) {

    // A boost io context. This is the "thread-pool" for the cache.
    // In this example, we use only the applications main-thread.
    boost::asio::io_context ctx;
    
    // Create an instance of a cache with std::string as key and value types. 
    auto cache = jgaa::abb::make_cache<std::string, std::string>(
    
        // Lambda that is called by the cache when it encounters an unknown key.
        [this](const std::string& key, auto && cb) {
            // A normal app would return different values based on the key,
            // for example by calling a back-end server via gRPC or REST,
            // or lookup the value in database. 
            // However, for this example, we always return the same value.
            cb({} /* No error */, "Congratulations. Here This is your value");
        }, 
        // The asio context
        ctx);
     
    // Create an asio/C++20 co-routine
    boost::asio::co_spawn(ctx, [&]() mutable -> boost::asio::awaitable<void> {
        // Inside the co-routine    
        
        // Lookup a value in the cache. This will call our lambda above,
        // since the cache has not seen "Key1" before, and return 
        // after `cb()` is called. 
        auto value = co_await cache.get("Key1", boost::asio::use_awaitable)
        
        // Lookup another value. This will also create a round-trip to 
        // our lambda.
        value = co_await cache.get("Key2", boost::asio::use_awaitable)
        
        // Lookup the first key. Now this key one exists in the cache, and
        // the value is returend immediately.
        value = co_await cache.get("Key1", boost::asio::use_awaitable)

    }, boost::asio::detached);
    
    // Let asio run the async code above in the applications thread.
    // When the thread reach the end of the co-routine, `run()` will return
    // and our application end.
    ctx.run();
}
```


