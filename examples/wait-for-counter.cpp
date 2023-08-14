/*
 * This example simulates counter, for example a version of something,
 * where co-routines waits (with a time-out) for the counter to reach a
 * treashold.
 *
 * This is the original use-case for this class. My dns server "nsblast"
 * allows users to change a "Resource Record" via a REST API. Some times,
 * for example when you want to renew a "Let's Encrypt" cert via DNS,
 * you set a spcial value in the domanin for the cert, and "Let's Encrypt"
 * will use that fied to verify that you can change the DNS settings for
 * that domain name. However, in the world of DNS, there are always more
 * than one server. If the API call for a change return immediately when
 * the "leader" DNS server is updated, "Let's Encrypt" may query one of
 * replicas before the change is replicated to it. The leader will therefore
 * use this class to allow clients to wait until a change is replicated
 * to all the replicas until the API request it returns.
 */

#include <iostream>
#include <syncstream>

#include <boost/leaf.hpp>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>

#include "glad/AsyncWaitFor.hpp"

using namespace std;
using namespace chrono_literals;

int main(int argc, char **argv) {

    // A boost io context. This is the "thread-pool" for the cache.
    // In this example, we use only the applications main-thread.
    boost::asio::io_context ctx;

    auto counter = jgaa::glad::make_async_wait_for<size_t>
        (ctx, [](const size_t condition, const size_t itemsValue) {
        return itemsValue <= condition;
    });

    // Add some things to wait.
    for (auto i = 0; i < 16; ++i) {
        // Create an asio/C++20 co-routine
        boost::asio::co_spawn(ctx, [&counter, i]() mutable -> boost::asio::awaitable<void> {
            // Inside the co-routine

            // If we time out, we get the exception!
            try {
                co_await counter.wait(i, 10s, boost::asio::use_awaitable);
                osyncstream{clog} << "  what=" << i << " was released" << endl;
            } catch(const boost::system::system_error& ec) {
                osyncstream{clog} << "  what=" << i << " failed: " << ec.what() << endl;
            }
        }, boost::asio::detached);
    }

    osyncstream{clog} << "Starting \"thread-pool\"..." << endl;
    thread worker1{[&] () mutable {
        ctx.run();
    }};

    thread worker2{[&] () mutable {
        ctx.run();
    }};

    // Give the threads a moment to actually start the coroutines.
    this_thread::sleep_for(chrono::seconds{2});

    osyncstream{clog} << "Releasing the first 5 items..." << endl;
    for(auto i = 0; i < 5; ++i) {
        counter.onChange(i);
    }

    this_thread::sleep_for(chrono::seconds{1});
    osyncstream{clog} << "Releasing the next 5 items..." << endl;
    // Let's release the next 5
    counter.onChange(10);

    this_thread::sleep_for(chrono::seconds{2});
    // Release two more
    osyncstream{clog} << "Releasing the final 2 items..." << endl;
    counter.onChange(12);

    osyncstream{clog} << "Waiting for the rest to time out..." << endl;
    while(!ctx.stopped()) {
        this_thread::sleep_for(chrono::milliseconds{100});
        counter.clean();
    }

    worker1.join();
    worker2.join();

    osyncstream{clog} << "\"Thread-pool\" ended. Bye." << endl;
}

/* The application should give an output somewhat like this:

Starting "thread-pool"...
Releasing the first 5 items...
  what=0 was released
  what=2 was released
  what=3 was released
  what=4 was released
  what=1 was released
Releasing the next 5 items...
  what=5 was released
  what=7 was released
  what=9 was released
  what=10 was released
  what=6 was released
  what=8 was released
Releasing the final 2 items...
Waiting for the rest to time out...
  what=11 was released
  what=12 was released
  what=13 failed with exception: Connection timed out [generic:110]
  what=14 failed with exception: Connection timed out [generic:110]
  what=15 failed with exception: Connection timed out [generic:110]
"Thread-pool" ended. Bye.

 */
