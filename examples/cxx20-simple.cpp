
#include <iostream>
#include "glad/AsyncCache.hpp"
#include <boost/asio/co_spawn.hpp>

int main(int argc, char **argv) {

  // A boost io context. This is the "thread-pool" for the cache.
  // In this example, we use only the applications main-thread.
  boost::asio::io_context ctx;

  // Create an instance of a cache with std::string as key and value types.
  auto cache = jgaa::glad::make_async_cache<std::string, std::string>(

      // Lambda that is called by the cache when it encounters an unknown key.
      [](const std::string &key, auto &&cb) {

          // Simple "database" for the "back-end"
          static const std::unordered_map<std::string, std::string> database = {
            {"Key1", "Dogs"},
            {"Key2", "Cats"}
          };

          std::clog << "Looking up key " << key << " in the database." << std::endl;
          cb({} /* no error*/, database.at(key));
      },
      // The asio context
      ctx);

  // Create an asio/C++20 co-routine
  boost::asio::co_spawn(ctx, [&]() mutable -> boost::asio::awaitable<void> {
        // Inside the co-routine

        // Lookup a value in the cache. This will call our lambda above,
        // since the cache has not seen "Key1" before, and return
        // after `cb()` is called.
        auto value = co_await cache.get("Key1", boost::asio::use_awaitable);
        std::clog << "Got value: " << value << std::endl;

        // Lookup another value. This will also create a round-trip to
        // our lambda.
        value = co_await cache.get("Key2", boost::asio::use_awaitable);
        std::clog << "Got value: " << value << std::endl;

        // Lookup the first key again. This key exists in the cache,
        // and the value is returend immediately.
        value = co_await cache.get("Key1", boost::asio::use_awaitable);
        std::clog << "Got value: " << value << std::endl;

        // Let's see what happens if we query for a key that don't exist in
        // our "database"
        try {
            value = co_await cache.get("unknown-key", boost::asio::use_awaitable);
            std::clog << "Got value: " << value << std::endl;
        } catch(const std::exception& ex) {
            std::clog << "Lookup failed with exception: " << ex.what() << std::endl;
        }
      },
      boost::asio::detached);

  // Let asio run the async code above in the applications thread.
  // When the thread reach the end of the co-routine, `run()` will return
  // and our application end.

  std::clog << "Starting \"thread-pool\"..." << std::endl;
  ctx.run();
  std::clog << "\"Thread-pool\" ended. Bye." << std::endl;
}

/* The application should give this output:
 *
 * Starting "thread-pool"...
 * Looking up key Key1 in the database.
 * Got value: Dogs
 * Looking up key Key2 in the database.
 * Got value: Cats
 * Got value: Dogs
 * Looking up key unknown-key in the database.
 * Lookup failed with exception: Interrupted system call [generic:4]
 * "Thread-pool" ended. Bye.
 */
