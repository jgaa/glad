#pragma once

#include <string>
#include <any>
#include <memory>
#include <deque>
#include <vector>
#include <string>
#include <unordered_map>
#include <boost/unordered/unordered_flat_map.hpp>
#include <atomic>
#include <variant>
#include <iostream>

#include <boost/leaf.hpp>
#include <boost/asio.hpp>

#include "asio-building-bricks/config.h"

/* Todo
 *
 * v Add optimization to not create a queue when there is only one request pending
 * v Add sharding for keys, based on hash (optional) for faster access on machines with many cores
 * v Performance-testing
 * - Add expiration
 * v Handle invalidated keys, also for pending requests
 */

namespace jgaa::abb {

/*! Generic cache with asio composed completion
 *
 * The cache is a trivial key/value store. If an item is not found,
 * a fetch method (supplied by you) are called to asyncrouneously get the
 * value. The request is paused, and the thread freed to do other work.
 *
 * If more requests comes in for a key that is in the process of being looked up,
 * they are added to a list of pending requests. A key is only fetched once. When
 * the value is available, any and all pending requests are resumed.
 *
 *
 * The templatye arguments are:
 *  - valueT The type of athe value. Normally a shared_ptr for complex
 *           objects and a value for trivial objects (that are fast to copy).
 *           The type must be copyable and should be movable
 *
 *  - keyT   The type of the key. It should be copyable and movable.
 *
 *  - executorT The asio executor to use for the cache itself.
 *           Normally a boost::asio::io_context used by your application.
 *
 *  - hashT  Hash function to use for the sharding based on keys.
 */

#ifndef JGAA_ABB_MAP_TYPE
#   define JGAA_ABB_MAP_TYPE boost::unordered_flat_map
#endif

template <typename valueT, typename keyT, typename executorT, typename hashT=std::hash<keyT>>
class Cache {

    struct SelfBase {
        virtual ~SelfBase() = default;
        virtual void complete(std::any res = nullptr) = 0;
        virtual void fail(boost::system::error_code ec) = 0;
    };

    // Storage for completion-handlers.
    template <typename SelfT, typename VarT>
    struct Self : public SelfBase {
        Self(SelfT&& self) : self_{std::move(self)} {}

        void complete(std::any res) override {
            std::call_once(once_, [&] {
                self_.complete({}, std::any_cast<VarT>(res));
            });
        }

        void fail(boost::system::error_code ec) override {
            std::call_once(once_, [&] {
                self_.complete(ec, {});
            });
        }

    private:
        SelfT self_;
        std::once_flag once_;
    };

    template <typename selfT>
    static auto make_self(selfT self) {
        return make_unique<Self<selfT, valueT>>(std::move(self));
    }

    // Data regarding pending requests
    struct Pending {
        using self_t = std::unique_ptr<SelfBase>;
        using list_t = std::deque<self_t>;
        bool invalidated = false;
        std::variant<self_t, list_t> requests_pending;

        Pending(self_t&& self)
            : requests_pending{std::move(self)}
        {
        }

        Pending& operator += (self_t && self) {
            if (std::holds_alternative<self_t>(requests_pending)) {
                auto& s = std::get<self_t>(requests_pending);

                list_t list;
                list.emplace_back(std::move(s));
                requests_pending = std::move(list);
            }

            assert(std::holds_alternative<list_t>(requests_pending));
            auto& list = std::get<list_t>(requests_pending);
            list.emplace_back(std::move(self));

            return *this;
        }

        void complete(const boost::system::error_code& e, std::any value) {
            if (std::holds_alternative<list_t>(requests_pending)) {
                auto& list = std::get<list_t>(requests_pending);
                for(auto& self : list) {
                    assert(self);
                    if (e) {
                        self->fail(e);
                    } else {
                        self->complete(value);
                    }
                }
                list.clear();
            } else if (std::holds_alternative<self_t>(requests_pending)) {
                auto& pending = std::get<self_t>(requests_pending);
                assert(pending);
                if (pending) {
                    if (e) {
                        pending->fail(e);
                    } else {
                        pending->complete(value);
                    }
                }
            } else {
                assert(false && "Invalid type in Pending.requests_pending");
            }
        }

        void cancel() {
            complete(boost::system::errc::make_error_code(boost::system::errc::operation_canceled), {});
        }
    };

    using value_t = std::variant<valueT, Pending>;
    using cache_t = JGAA_ABB_MAP_TYPE<keyT, value_t>;

    // Use "sharding" based on a hash from the key
    // This allows up to 'numShards' number of lookups or callbacks to be processed in parallel,
    // which gives a significant performance boost on machines with may cores.
    class Shards {
    public:
        struct Shard {
            Shard(executorT& e)
                : strand_{e} {};

            Shard() = delete;

            auto& strand() noexcept {
                return strand_;
            }

            auto& cache() noexcept {
                return cache_;
            }

        private:
            boost::asio::io_context::strand strand_;
            cache_t cache_;
        };


        Shards(size_t numShards, executorT& e)
            : numShards_{numShards}
        {
            shards_.reserve(numShards);
            for(auto i = 0; i < numShards; ++i) {
                shards_.emplace_back(e);
            }
        }

        auto& shard(const keyT& key) {
            size_t h = hasher_(key);
            return shards_[h % numShards_];
        }

        hashT hasher_;
        std::vector<Shard> shards_;
        const size_t numShards_;
    };

public:
    using fetch_cb_t = std::function<void(boost::system::error_code e, valueT value)>;
    using fetch_t = std::function<void(const keyT&, fetch_cb_t &&)>;

    /*! Constructor
     *
     *  \param fetch Functor to fetch values to the cache.
     *  \param executor Executor to use by the cache. Normally the
     *               boost::asio::io_context used by your application.
     */
    Cache(fetch_t && fetch, executorT& executor, size_t numShards=16)
        : executor_{executor}
        , shards_{numShards, executor}
        , fetch_{std::move(fetch)}
        //, timer_{strand_.get_executor()}
    {
    }

    /*! Asynchrouneosly get a value from the cache.
     *
     *  If the value is not currently in the cache,
     *  it will be attempted fetched before the completion
     *  handler is called.
     *
     *  \param key Key to get.
     *  \param token Your continuation handler.
     */
    template <typename CompletionToken>
    auto get(keyT key, CompletionToken&& token) {
        auto &strand = shard(key).strand();
        return boost::asio::async_compose<CompletionToken,
            void(boost::system::error_code e, valueT value)>
            ([this, &key, &strand](auto& self) mutable {
                boost::asio::post(strand, [this, self=std::move(self), key=std::move(key)]() mutable {
                    get_(key, std::move(self));
                });
            }, token, strand);
    }


    /*! Asynchrouneosly checks if a value exists in the cache
     *
     *  \param key Key to get.
     *  \param needsValue If true, the methid will only return true if the key holds a value.
     *      If the key is being looked up for don't exist, the result will be false.
     *  \param token Your continuation handler.
     */
    template <typename CompletionToken>
    auto exists(keyT key, bool needsValue, CompletionToken&& token) {
        auto &strand = shard(key).strand();
        return boost::asio::async_compose<CompletionToken,void(bool)>([this, needsValue, &key, &strand](auto& self) mutable {
                boost::asio::post(strand, [this, needsValue, self=std::move(self), key=std::move(key)]() mutable {
                auto& cache = shard(key).cache();
                if (auto it = cache.find(key); it != cache.end()) {
                    if (needsValue) {
                        self.complete(std::holds_alternative<valueT>(it->second));
                        return;
                    }
                    self.complete(true);
                    return;
                }
                self.complete(false);
                });
            }, token, strand);
    }

    /*! Erase a key from the cache
     *
     *  Pending requests will complete with the error code you supplied,
     *  and the key will be removed.
     *
     *  The method returns immediately, and the erase operation is scheduled
     *  to be run as soon as a thread is available.
     *
     *  \param key Key to get.
     *  \param ec Error-code to forward to any requests that are waiting for a
     *            fetch operation for this key.
     *            Defaults to boost::system::errc::operation_canceled.
     */
    void erase(keyT key, boost::system::error_code ec = boost::system::errc::make_error_code(boost::system::errc::operation_canceled)) {
        auto& shrd = shard(key);
        shrd.strand().dispatch([this, &shrd, key=std::move(key), ec] {
            auto& cache = shrd.cache();
            if (auto it = cache.find(key); it != cache.end()) {
                auto& v = it->second;
                if (std::holds_alternative<Pending>(v)) {
                    auto& pending = std::get<Pending>(v);
                    pending.complete(ec, {});
                }
                cache.erase(it);
            }
        });
    }

    /*! Invalidate a key from the cache
     *
     *  If there are pending requests waiting for a lookup,
     *  a new lookup will be performed when the current
     *  lookup operation completes. The assumption is that
     *  if a lookup is in progress while the key is invalidated, the
     *  result for the current operation is likely to be wrong.
     *  A new operation will therefor happen as soon as the current
     *  operaion finish (either wilh a value or an error).
     *
     *  This means that you can invalidate a key, without interrupting
     *  requests waiting for the value.
     *
     *  The method returns immediately, and the erase operation is scheduled
     *  to be run as soon as a thread is available.
     *
     *  \param key Key to get.
     */
    void invalidate(keyT key) {
        auto& shrd = shard(key);
        shrd.strand().dispatch([this, &shrd, key=std::move(key)] {
            auto& cache = shrd.cache();
            if (auto it = cache.find(key); it != cache.end()) {
                auto& v = it->second;
                if (std::holds_alternative<valueT>(v)) {
                    cache.erase(it);
                } else if (std::holds_alternative<Pending>(v)) {
                    auto& pending = std::get<Pending>(v);
                    pending.invalidated = true;
                }
            }
        });
    }

private:
    template <typename selfT>
    auto get_(const keyT& key, selfT&& self) {
        auto& cache = shard(key).cache();
        if (auto it = cache.find(key); it != cache.end()) {
            auto& v = it->second;
            if (std::holds_alternative<valueT>(v)) {
                // We have a valid value. Just hand it out.
                self.complete({}, std::get<valueT>(v));
                return;
            }
            if (std::holds_alternative<Pending>(v)) {
                // We don't have the value, but we are in the process of querying for it.
                // Add the requester to the list of requesters.
                auto& pending = std::get<Pending>(v);
                pending += make_self(std::move(self));
                return;
            }
            assert(false && "The cache holds neither a value or a list of pending requests!");
            self.complete(boost::system::errc::make_error_code(boost::system::errc::invalid_argument), {});
            return;
        }

        // The key is not in the list.
        cache[key].template emplace<Pending>(make_self(std::move(self)));
        fetch(key);
    }

    void fetch(const keyT& key) {
        auto& shrd = shard(key);
        shrd.strand().post([key=key, &shrd, this] {
            fetch_(key, [key=key, &shrd, this](boost::system::error_code e, valueT value) {
                shrd.strand().dispatch([this, &shrd, e, key=std::move(key), value=std::move(value)] {
                    auto& cache = shrd.cache();
                    // Only do something if the key exists. It may have been erased since we started the call.
                    if (auto it = cache.find(key); it != cache.end()) {
                        auto& v = it->second;
                        if (std::holds_alternative<Pending>(v)) [[unlikely]] {
                            auto& pending = std::get<Pending>(v);
                            if (pending.invalidated) [[unlikely]] {
                                // We need to re-try the request
                                pending.invalidated = false;
                                fetch(key);
                                return;
                            }
                            pending.complete(e, value);
                        }

                        if (e) [[unlikely]] {
                            // Failed
                            cache.erase(key);
                        } else {
                            v = value;
                        }
                    }
                });
            });
        });
    }

    auto& shard(const keyT& key) noexcept {
        return shards_.shard(key);
    }

    //cache_t cache_;
    executorT& executor_;
    Shards shards_;
    fetch_t fetch_;
};

} // ns
