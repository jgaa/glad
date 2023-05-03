#pragma once

#include <any>
#include <memory>
#include <deque>
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <variant>
#include <iostream>

#include <boost/leaf.hpp>
#include <boost/asio.hpp>


/* Todo
 *
 * v Add optimization to not create a queue when there is only one request pending
 * - Add sharding for keys, based on hash (optional) for faster access on machines with many cores
 * - Performance-testing
 * - Add expiration
 * v Handle invalidated keys, also for pending requests
 */

namespace jgaa::abb {

/*! Generic cache with asio composed completion
 *
 *
 */

template <typename valueT, typename keyT, typename executorT>
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

    using self_t = std::unique_ptr<SelfBase>;

    // Data regarding pending requests
    struct Pending {
        using list_t = std::vector<self_t>;
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
    using cache_t = std::unordered_map<keyT, value_t>;
public:
    using fetch_cb_t = std::function<void(boost::system::error_code e, valueT value)>;
    using fetch_t = std::function<void(const keyT&, fetch_cb_t &&)>;

    Cache(fetch_t && fetch, executorT& executor)
        : executor_{executor}
        , fetch_{std::move(fetch)}
        , strand_{executor_}
        //, timer_{strand_.get_executor()}
    {
    }

    template <typename CompletionToken>
    auto get(keyT key, CompletionToken&& token) {
        return boost::asio::async_compose<CompletionToken,
            void(boost::system::error_code e, valueT value)>
            ([this, &key](auto& self) mutable {
                boost::asio::post(strand_, [this, self=std::move(self), key=std::move(key)]() mutable {
                    get_(key, std::move(self));
                });
            }, token, strand_);
    }

    template <typename CompletionToken>
    auto exists(keyT key, bool needsValue, CompletionToken&& token) {
        return boost::asio::async_compose<CompletionToken,void(bool)>([this, needsValue, &key](auto& self) mutable {
                boost::asio::post(strand_, [this, needsValue, self=std::move(self), key=std::move(key)]() mutable {
                if (auto it = cache_.find(key); it != cache_.end()) {
                    if (needsValue) {
                        self.complete(std::holds_alternative<valueT>(it->second));
                        return;
                    }
                    self.complete(true);
                    return;
                }
                self.complete(false);
                });
            }, token, strand_);
    }

    void erase(keyT key, boost::system::error_code ec = boost::system::errc::make_error_code(boost::system::errc::operation_canceled)) {
        strand_.dispatch([this, key=std::move(key), ec] {
            if (auto it = cache_.find(key); it != cache_.end()) {
                auto& v = it->second;
                if (std::holds_alternative<Pending>(v)) {
                    auto& pending = std::get<Pending>(v);
                    pending.complete(ec, {});
                }
                cache_.erase(it);
            }
        });
    }

    void invalidate(keyT key) {
        strand_.dispatch([this, key=std::move(key)] {
            if (auto it = cache_.find(key); it != cache_.end()) {
                auto& v = it->second;
                if (std::holds_alternative<valueT>(v)) {
                    cache_.erase(it);
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
        if (auto it = cache_.find(key); it != cache_.end()) {
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
        cache_[key].template emplace<Pending>(make_self(std::move(self)));
        fetch(key);
    }

    void fetch(const keyT& key) {
        strand_.post([key=key, this] {
            fetch_(key, [key=key, this](boost::system::error_code e, valueT value) {
                strand_.dispatch([this, e, key=std::move(key), value=std::move(value)] {
                    // Only do something if the key exists. It may have been erased since we started the call.
                    if (auto it = cache_.find(key); it != cache_.end()) {
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
                            cache_.erase(key);
                        } else {
                            v = value;
                        }
                    }
                });
            });
        });
    }

    cache_t cache_;
    executorT& executor_;
    boost::asio::io_context::strand strand_;
    //boost::asio::deadline_timer timer_;
    fetch_t fetch_;
};

} // ns
