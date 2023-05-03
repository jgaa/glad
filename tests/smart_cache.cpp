#include <future>
#include <random>
#include <atomic>
#include <list>

#include "gtest/gtest.h"
#include "asio-building-bricks/SmartCache.hpp"
#include <boost/asio/spawn.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/chrono.hpp>


using namespace jgaa::abb;
using namespace std;
using namespace std::string_literals;

namespace {

using cache_t = Cache<string, string, boost::asio::io_context>;
const auto valid =  "This is a test"s;
const auto invalid =  "Wrong value"s;
const string key = "whatever";

} // ns


TEST(Cache, AddOneCb) {

    boost::asio::io_context ctx;
    cache_t cache([this](const string_view& key, cache_t::fetch_cb_t cb) {
        cb({}, valid);
    }, ctx);

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_FALSE(e);
        EXPECT_EQ(rv, valid);
    });

    ctx.run();
}

TEST(Cache, AddOneStackfulCoro) {

    boost::asio::io_context ctx;
    cache_t cache([this](const string_view& key, cache_t::fetch_cb_t cb) {
        cb({}, valid);
    }, ctx);

    std::promise<pair<boost::system::error_code, string>> promise_rv;

    boost::asio::spawn(ctx, [&](auto yield) {
        boost::system::error_code ec;
            auto value = cache.get(key, yield[ec]);
            EXPECT_FALSE(ec);
            EXPECT_EQ(value, valid);
        }, boost::asio::detached);

    ctx.run();
}

TEST(Cache, AddOneCxx20Coro) {

    boost::asio::io_context ctx;
    cache_t cache([this](const string_view& key, cache_t::fetch_cb_t cb) {
        cb({}, valid);
    }, ctx);

    boost::asio::co_spawn(ctx, [&]() mutable -> boost::asio::awaitable<void> {
        boost::system::error_code ec;

        // Sets the ec on error
        auto value = co_await cache.get(key,
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        EXPECT_FALSE(ec);
        EXPECT_EQ(value, valid);

        // Throws on error
        value.clear();
        EXPECT_NO_THROW(value = co_await cache.get(key, boost::asio::use_awaitable));
        EXPECT_EQ(value, valid);
    }, boost::asio::detached);

    ctx.run();
}

TEST(Cache, FailOneCxx20Coro) {

    boost::asio::io_context ctx;
    cache_t cache([this](const string_view& key, cache_t::fetch_cb_t cb) {
        cb(boost::system::errc::make_error_code(boost::system::errc::io_error), {});
    }, ctx);


    boost::asio::co_spawn(ctx, [&]() mutable -> boost::asio::awaitable<void> {
            boost::system::error_code ec;

            // Sets the ec on error
            auto value = co_await cache.get(key,
                                            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            EXPECT_TRUE(ec);
            EXPECT_NE(value, valid);

            // Throws on error
            value.clear();
            EXPECT_THROW(value = co_await cache.get(key, boost::asio::use_awaitable), runtime_error);
            EXPECT_NE(value, valid);
        }, boost::asio::detached);

    ctx.run();
}

TEST(Cache, TestWithManyKeys) {

    static constexpr size_t x_size = 1000;
    static constexpr size_t y_size = 100;
    static constexpr size_t num_threads = 48;
    atomic_size_t created_keys_count{0};

    auto get_key = [](size_t x, size_t y) {
        assert(x < x_size);
        assert(y < y_size);
        return "test-x="s + to_string(x) + "-y=" + to_string(y);
    };

    auto get_value = [](string_view key) {
        return "value: "s + string{key};
    };

    boost::asio::io_context ctx;
    cache_t cache([this, &get_value, &created_keys_count](const string_view& key, cache_t::fetch_cb_t cb) {
        cb({}, get_value(key));
        ++created_keys_count;
    }, ctx);

    // Prevent the context from running out of work
    auto work = boost::asio::make_work_guard(ctx);

    // Start 64 threads
    clog << "Starting threads" << endl;
    deque<thread> workers;
    for(auto i = 0; i < num_threads; ++i) {
        workers.emplace_back([&ctx]{
            ctx.run();
        });
    }

    // Populate the cache
    clog << "Populating cache" << endl;
    {
        deque<promise<void>> promises;
        for(auto x = 0; x < x_size; ++x) {
            promises.emplace_back();
            auto& p = promises.back();
            boost::asio::co_spawn(ctx, [x, &ctx, &cache, &p, &get_key, &get_value]() mutable -> boost::asio::awaitable<void> {
                for(auto y = 0; y < y_size; ++y) {
                    const auto key = get_key(x, y);
                    const auto value = co_await cache.get(key, boost::asio::use_awaitable);
                    const auto expected = get_value(key);
                    EXPECT_EQ(value, expected);
                }
                p.set_value();
            }, boost::asio::detached);
        }

        // Wait for populate to complete
        clog << "Waiting for populating to finish" << endl;
        for(auto& p : promises) {
            p.get_future().get();
        }
    }

    // Pull data
    random_device rd;
    mt19937 mt(rd());
    mutex mtx;

    clog << "Initiating pulling data" << endl;
    for(auto x = 0; x < x_size; ++x) {
        boost::asio::co_spawn(ctx, [x, &ctx, &cache, &mt, &get_key, &get_value]() mutable -> boost::asio::awaitable<void> {
            uniform_int_distribution<size_t> xdist(0, x_size - 1);
            uniform_int_distribution<size_t> ydist(0, y_size - 1);
            for(auto y = 0; y < y_size; ++y) {
                const auto xx =  xdist(mt);
                const auto yy = ydist(mt);
                const auto key = get_key(xx, yy);
                const auto value = co_await cache.get(key, boost::asio::use_awaitable);
                const auto expected = get_value(key);
                EXPECT_EQ(value, expected);
            }
        }, boost::asio::detached);
    }

    // Now, allow the ctx to run out of work
    work.reset();

    clog << "Waiting for workers" << endl;
    for(auto& t : workers) {
        t.join();
    }

    EXPECT_EQ(created_keys_count, (x_size * y_size));
}

TEST(Cache, TestWithSimultaneousRequests) {

    static constexpr size_t sim_requests = 10000;
    atomic_size_t created_requests{0};
    atomic_size_t returned_requests{0};
    static constexpr size_t num_threads = 48;

    boost::asio::io_context ctx;
    // Prevent the context from running out of work
    auto work = boost::asio::make_work_guard(ctx);

    // Start threads
    clog << "Starting threads" << endl;
    deque<thread> workers;
    for(auto i = 0; i < num_threads; ++i) {
        workers.emplace_back([&ctx]{
            ctx.run();
        });
    }

    cache_t cache([&](const string_view& key, cache_t::fetch_cb_t cb) {

        // We use only one key in this test, so the lookup should only happen once
        static atomic_size_t called{0};
        ++called;
        EXPECT_EQ(called, 1);

        // Wait until all the requests have been sent before we return.
        boost::asio::co_spawn(ctx, [&created_requests, &ctx, cb=std::move(cb)]() mutable -> boost::asio::awaitable<void> {
                boost::asio::deadline_timer timer{ctx};
                while(created_requests < sim_requests) {
                    timer.expires_from_now(boost::posix_time::millisec{2});
                    co_await timer.async_wait(boost::asio::use_awaitable);
                }

                // Return the value
                cb({}, valid);
        }, boost::asio::detached);
    }, ctx);

    for(auto i = 0; i < sim_requests; ++i) {
        boost::asio::co_spawn(ctx, [&]() mutable -> boost::asio::awaitable<void> {
                ++created_requests;
                auto value = co_await cache.get(key, boost::asio::use_awaitable);
                ++returned_requests;
                EXPECT_EQ(value, valid);
            }, boost::asio::detached);
    }

    // Now, allow the ctx to run out of work
    work.reset();

    clog << "Waiting for workers" << endl;
    for(auto& t : workers) {
        t.join();
    }

    EXPECT_EQ(created_requests, sim_requests);
    EXPECT_EQ(returned_requests, sim_requests);
}

TEST(Cache, TestInvalidate) {
    static constexpr size_t num_threads = 2;

    boost::asio::io_context ctx;
    optional<cache_t::fetch_cb_t> pending_cb;
    std::promise<void> called_once;

    cache_t cache([&](const string_view& key, cache_t::fetch_cb_t cb) {
        if (!pending_cb) {
            pending_cb.emplace(std::move(cb));
            called_once.set_value();
            return;
        }

        cb({}, valid);

    }, ctx);

    // Prevent the context from running out of work
    auto work = boost::asio::make_work_guard(ctx);

    // Start threads
    clog << "Starting threads" << endl;
    deque<thread> workers;
    for(auto i = 0; i < num_threads; ++i) {
        workers.emplace_back([&ctx]{
            ctx.run();
        });
    }

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_FALSE(e);
        EXPECT_EQ(rv, valid);
    });

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_FALSE(e);
        EXPECT_EQ(rv, valid);
    });

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_FALSE(e);
        EXPECT_EQ(rv, valid);
    });

    // Wait for fecth() to be called
    called_once.get_future().get();

    cache.invalidate(key);

    // Respond to the initial key lookup
    assert(pending_cb);
    pending_cb.value()({}, invalid);

    // Now, allow the ctx to run out of work
    work.reset();

    clog << "Waiting for workers" << endl;
    for(auto& t : workers) {
        t.join();
    }
}

TEST(Cache, TestEraseKey) {
    static constexpr size_t num_threads = 2;

    boost::asio::io_context ctx;
    optional<cache_t::fetch_cb_t> pending_cb;
    std::promise<void> called_once;
    std::promise<void> called_twice;

    cache_t cache([&](const string_view& key, cache_t::fetch_cb_t cb) {
        if (!pending_cb) {
            pending_cb.emplace(std::move(cb));
            called_once.set_value();
            return;
        }

        cb({}, valid);
        called_twice.set_value();
    }, ctx);

    // Prevent the context from running out of work
    auto work = boost::asio::make_work_guard(ctx);

    // Start threads
    clog << "Starting threads" << endl;
    deque<thread> workers;
    for(auto i = 0; i < num_threads; ++i) {
        workers.emplace_back([&ctx]{
            ctx.run();
        });
    }

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_EQ(e, boost::system::errc::make_error_code(boost::system::errc::operation_canceled));
        EXPECT_EQ(rv, "");
    });

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_EQ(e, boost::system::errc::make_error_code(boost::system::errc::operation_canceled));
        EXPECT_EQ(rv, "");
    });

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_EQ(e, boost::system::errc::make_error_code(boost::system::errc::operation_canceled));
        EXPECT_EQ(rv, "");
    });

    // Wait for fecth() to be called
    called_once.get_future().get();

    // The key should exist, but niot have a value yet
    cache.exists(key, true,[](bool value) {
        EXPECT_FALSE(value);
    });

    cache.exists(key, false,[](bool value) {
        EXPECT_TRUE(value);
    });

    cache.erase(key);

    // At this point, the key shuold not exist
    cache.exists(key, false,[](bool value) {
        EXPECT_FALSE(value);
    });

    // Respond to the initial key lookup
    assert(pending_cb);
    pending_cb.value()({}, invalid);

    // At this point the key should not exist, and we should get a new lookup with
    // the valid value.
    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_FALSE(e);
        EXPECT_EQ(rv, valid);
    });

    cache.get(key, [](boost::system::error_code e, const string& rv) {
        EXPECT_FALSE(e);
        EXPECT_EQ(rv, valid);
    });

    called_twice.get_future().get();
    // At this point the key should exist and have a value
    cache.exists(key, true,[](bool value) {
        EXPECT_TRUE(value);
    });

    // Now, allow the ctx to run out of work
    work.reset();

    clog << "Waiting for workers" << endl;
    for(auto& t : workers) {
        t.join();
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
