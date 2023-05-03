
#include <atomic>
#include <future>
#include <random>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <fstream>

#include <sys/time.h>
#include <sys/resource.h>

#include <boost/program_options.hpp>


#include "asio-building-bricks/SmartCache.hpp"
#include "asio-building-bricks/config.h"

using namespace jgaa::abb;
using namespace std;
using namespace std::string_literals;

namespace {

using cache_t = Cache<string, string, boost::asio::io_context>;
const auto valid =  "This is a test"s;
const auto invalid =  "Wrong value"s;
const string key = "whatever";

struct Config {
    size_t xSize = 1000;
    size_t ySize = 10000;
    size_t failedKeys = 1000;
    size_t numThreads = std::thread::hardware_concurrency() * 2;
    std::string reportPath;
};

Config config;

} // ns


void perftests() {
    auto get_key = [](size_t x, size_t y) {
        assert(x < config.xSize);
        assert(y < config.ySize);
        return "test-x="s + to_string(x) + "-y=" + to_string(y);
    };

    auto get_value = [](string_view key) {
        return "value: "s + string{key};
    };

    boost::asio::io_context ctx;
    cache_t cache([&get_value](const string_view& key, cache_t::fetch_cb_t cb) {
        if (key == invalid) {
            static const auto err = boost::system::errc::make_error_code(boost::system::errc::bad_message);
            cb(err, {});
            return;
        }

        cb({}, get_value(key));
    }, ctx);

    // Prevent the context from running out of work
    auto work = boost::asio::make_work_guard(ctx);

    // Start 64 threads
    clog << "Starting threads" << endl;
    deque<thread> workers;
    for(auto i = 0; i < config.numThreads; ++i) {
        workers.emplace_back([&ctx]{
            ctx.run();
        });
    }

    // Populate the cache
    clog << "Populating cache" << endl;
    {
        deque<promise<void>> promises;
        for(auto x = 0; x < config.xSize; ++x) {
            promises.emplace_back();
            auto& p = promises.back();
            boost::asio::co_spawn(ctx, [x, &ctx, &cache, &p, &get_key, &get_value]() mutable -> boost::asio::awaitable<void> {
                    for(auto y = 0; y < config.ySize; ++y) {
                        const auto key = get_key(x, y);
                        const auto value = co_await cache.get(key, boost::asio::use_awaitable);
                        const auto expected = get_value(key);
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
    for(auto x = 0; x < config.xSize; ++x) {
        boost::asio::co_spawn(ctx, [x, &ctx, &cache, &mt, &get_key, &get_value]() mutable -> boost::asio::awaitable<void> {
                uniform_int_distribution<size_t> xdist(0, config.xSize - 1);
                uniform_int_distribution<size_t> ydist(0, config.ySize - 1);
                for(auto y = 0; y < config.ySize; ++y) {
                    const auto xx =  xdist(mt);
                    const auto yy = ydist(mt);
                    const auto key = get_key(xx, yy);
                    const auto value = co_await cache.get(key, boost::asio::use_awaitable);
                    const auto expected = get_value(key);
                }
            }, boost::asio::detached);
    }


    for(auto i = 0; i < config.failedKeys; ++i) {
        boost::asio::co_spawn(ctx, [i, &ctx, &cache]() mutable -> boost::asio::awaitable<void> {
            co_await cache.get(invalid, boost::asio::use_awaitable);
        }, boost::asio::detached);
    }

    // Now, allow the ctx to run out of work
    work.reset();

    clog << "Waiting for workers" << endl;
    for(auto& t : workers) {
        t.join();
    }

}

int main(int argc, char **argv) {
    try {
        locale loc("");
    } catch (const std::exception& e) {
        std::cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    namespace po = boost::program_options;
    po::options_description general("Options");
    general.add_options()
        ("help,h", "Print help and exit")
        ("threads",
         po::value<uint64_t>(&config.numThreads)->default_value(config.numThreads),
         "Number of threads to use.")
        ("x-size",
             po::value<size_t>(&config.xSize)->default_value(config.xSize),
             "Number of coroutines to run simultaneously")
        ("y-size",
         po::value<size_t>(&config.ySize)->default_value(config.ySize),
         "Number of requests in each coroutine")
        ("report-path,r",
         po::value<string>(&config.reportPath)->default_value(config.reportPath),
         "CSV file where to append the results")
        ;

    po::options_description cmdline_options;
    cmdline_options.add(general);
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << filesystem::path(argv[0]).stem().string() << " [options]";
        cout << cmdline_options << endl;
        return -1;
    }

    const auto numObjects = config.xSize * config.ySize;

    cout << "Starting up (using jgaa::abb " << ABB_VERSION_STR << ", boost " << BOOST_LIB_VERSION << ")." << endl
         << "I will crate " << numObjects << " and then read "
         << numObjects << " random keys and "
         << config.failedKeys << " nonexisting keys. " << endl;

    perftests();
    rusage ru = {};
    getrusage(RUSAGE_SELF, &ru);

    cout << "cputime=" << (ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1000000.0)
         << ", system=" << (ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1000000.0)
         << ", minflt=" << ru.ru_minflt
         << ", majflt=" << ru.ru_majflt
         << ", nswap=" << ru.ru_nswap
         << ", nsignals=" << ru.ru_nsignals
         << ", nvcsw=" << ru.ru_nvcsw
         << ", nivcsw=" << ru.ru_nivcsw
         << ", maxrss=" << ru.ru_maxrss
         << ", ixrss=" << ru.ru_ixrss
         << ", idrss=" << ru.ru_idrss
         << ", isrss=" << ru.ru_isrss
         << endl;

    if (!config.reportPath.empty()) {
        if (!filesystem::is_regular_file(config.reportPath)) {
            ofstream hdr{config.reportPath};
            hdr << "Approach, cputime, system, minflt, majflt, nswap, "
                << "nsignals, nvcsw, nivcsw, maxrss, ixrss, idrss, isrss" << endl;
        }

        ofstream data{config.reportPath, ::ios_base::app};
        data << "default"
             << "," << (ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1000000.0)
             << "," << (ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1000000.0)
             << "," << ru.ru_minflt
             << "," << ru.ru_majflt
             << "," << ru.ru_nswap
             << "," << ru.ru_nsignals
             << "," << ru.ru_nvcsw
             << "," << ru.ru_nivcsw
             << "," << ru.ru_maxrss
             << "," << ru.ru_ixrss
             << "," << ru.ru_idrss
             << "," << ru.ru_isrss
             << endl;
    }

    cout << "Done" << endl;
}
