
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


#include "asio-building-bricks/AsyncCache.hpp"
#include "asio-building-bricks/config.h"

using namespace jgaa::abb;
using namespace std;
using namespace std::string_literals;

namespace {

//using cache_t = Cache<string, string, boost::asio::io_context>;
const auto valid =  "This is a test"s;
const auto invalid =  "Wrong value"s;
const auto key = "whatever";

struct Config {
    bool randomValuesForRead = false;
    size_t xSize = 1000;
    size_t ySize = 10000;
    size_t failedKeys = 10000;
    size_t readAmplification = 2;
    size_t numThreads = min<size_t>(thread::hardware_concurrency(), 8);
    size_t numShards = 7;
    string reportPath;
};

Config config;

class Timer {
public:
    Timer()
        : start_{std::chrono::steady_clock::now()} {}

    // Return elapsed time in nanoseconds
    auto elapsed() const {
        const auto ended = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(ended - start_).count();
    }

    double elapsedSeconds() const {
        return elapsed() / 1000000000.0;
    }

    const decltype (std::chrono::steady_clock::now()) start_;
};

} // ns


auto perftests() {
    random_device rd;
    mt19937 mt(rd());
    mutex mtx;

    auto get_key = [](size_t x, size_t y) {
        assert(x < config.xSize);
        assert(y < config.ySize);
        return "test-x="s + to_string(x) + "-y=" + to_string(y);
    };

    auto get_value = [](string_view key) {
        return "value: "s + string{key};
    };

    boost::asio::io_context ctx;
    //AsyncCache<string, string, decltype(ctx)> cache([&get_value](const string& key, auto && cb) {
    auto cache = make_async_cache<string, shared_ptr<string>>([&get_value](const string& key, auto && cb) {
        if (key == invalid) {
            static const auto err = boost::system::errc::make_error_code(boost::system::errc::bad_message);
            cb(err, {});
            return;
        }

        cb({}, make_shared<string>(get_value(key)));
    }, ctx, config.numShards);

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

    Timer elapsed;
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

    const auto writeSeconds = elapsed.elapsedSeconds();

    clog << "Initiating pulling data" << endl;
    Timer elapsedDuringRead;
    for(auto x = 0; x < config.xSize; ++x) {
        for(auto a = 0; a < config.readAmplification; ++a) {
            boost::asio::co_spawn(ctx, [x, &ctx, &cache, &mt, &get_key, &get_value]() mutable -> boost::asio::awaitable<void> {
                    uniform_int_distribution<size_t> xdist(0, config.xSize - 1);
                    uniform_int_distribution<size_t> ydist(0, config.ySize - 1);
                    for(auto y = 0; y < config.ySize; ++y) {
                        const auto xx = config.randomValuesForRead ? xdist(mt) : x;
                        const auto yy = config.randomValuesForRead ? ydist(mt) : y;
                        const auto key = get_key(xx, yy);
                        const auto value = co_await cache.get(key, boost::asio::use_awaitable);
                        const auto expected = get_value(key);
                    }
                }, boost::asio::detached);
        }
    }


    for(auto i = 0; i < config.failedKeys; ++i) {
        uniform_int_distribution<size_t> dist(0, numeric_limits<size_t>::max());
        boost::asio::co_spawn(ctx, [i, &ctx, &cache, &mt, &dist]() mutable -> boost::asio::awaitable<void> {
                co_await cache.get(invalid + to_string(dist(mt)), boost::asio::use_awaitable);
        }, boost::asio::detached);
    }

    // Now, allow the ctx to run out of work
    work.reset();

    clog << "Waiting for workers" << endl;
    for(auto& t : workers) {
        t.join();
    }

    const auto readSeconds = elapsedDuringRead.elapsedSeconds();

    return make_tuple(elapsed.elapsedSeconds(), writeSeconds, readSeconds);
}

int main(int argc, char **argv) {
    try {
        locale loc("");
    } catch (const std::exception& e) {
        std::clog << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    //clog.imbue(std::locale("en_US.UTF-8"));
    clog.imbue(std::locale(""));

    namespace po = boost::program_options;
    po::options_description general("Options");
    general.add_options()
        ("help,h", "Print help and exit")
        ("threads",
         po::value<uint64_t>(&config.numThreads)->default_value(config.numThreads),
         "Number of shards to use.")
        ("shards",
         po::value<uint64_t>(&config.numShards)->default_value(config.numShards),
         "Number of shardsto use.")
        ("x-size",
             po::value<size_t>(&config.xSize)->default_value(config.xSize),
             "Number of coroutines to run simultaneously")
        ("y-size",
         po::value<size_t>(&config.ySize)->default_value(config.ySize),
         "Number of requests in each coroutine")
        ("read-amplification-factor, a",
         po::value<size_t>(&config.readAmplification)->default_value(config.readAmplification),
         "Read amplification facter (how many times each existing key is read)")
        ("read-random-keys,",
         po::value<bool>(&config.randomValuesForRead)->default_value(config.randomValuesForRead),
         "Read existing keys in random order")
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
        clog << filesystem::path(argv[0]).stem().string() << " [options]";
        clog << cmdline_options << endl;
        return -1;
    }

    const auto numObjects = config.xSize * config.ySize;
    const auto readObjects = numObjects * config.readAmplification;

    clog << "Starting up (using jgaa::abb " << ABB_VERSION_STR << ", boost " << BOOST_LIB_VERSION << ")." << endl
         << "Using " << config.numThreads << " threads and " << config.numShards << " shards." << endl
         << "I will create " << numObjects << " objects and then read "
         << readObjects << " existing objects using "
         << (config.randomValuesForRead ? "random" : "sequential") << " keys and "
         << config.failedKeys << " failed keys (errors on fetch). " << endl;

    const auto [elapsedSeconds, writeSeconds, readSeconds] = perftests();

    clog << fixed << setprecision(5)
         << "Spent " << elapsedSeconds << " seconds doing test stuff, including "
         << writeSeconds << " seconds for writes ("
         << (numObjects/ writeSeconds) << " writes/sec) and "
         << readSeconds << " seconds for reads ("
         << (readObjects/ readSeconds) << " reads/sec)"
         << endl;


    rusage ru = {};
    getrusage(RUSAGE_SELF, &ru);

    clog << "cputime=" << (ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1000000.0)
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
            hdr << "Threads; shards; writes; reads; faildReads; elapsedTime; writeTime; readTime; "
                << "cputime; system; minflt; majflt; nswap; "
                << "nsignals; nvcsw; nivcsw; maxrss; ixrss; idrss; isrss" << endl;
        }

        ofstream data{config.reportPath, ::ios_base::app};
        data << config.numThreads
                 << ";" << config.numShards
                 << ";" << numObjects
                 << ";" << readObjects
                 << ";" << config.failedKeys
                 << ";" << elapsedSeconds
                 << ";" << writeSeconds
                 << ";" << readSeconds
                 << ";" << (ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1000000.0)
                 << ";" << (ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1000000.0)
                 << ";" << ru.ru_minflt
                 << ";" << ru.ru_majflt
                 << ";" << ru.ru_nswap
                 << ";" << ru.ru_nsignals
                 << ";" << ru.ru_nvcsw
                 << ";" << ru.ru_nivcsw
                 << ";" << ru.ru_maxrss
                 << ";" << ru.ru_ixrss
                 << ";" << ru.ru_idrss
                 << ";" << ru.ru_isrss
             << endl;
    }

    clog << "Done" << endl;
}
