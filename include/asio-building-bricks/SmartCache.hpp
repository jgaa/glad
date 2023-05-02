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
 * - Add optimization to not create a queue when there is only one request pending
 * - Add sharding for keys, based on hash (optional) for faster access on machines with many cores
 * - Performance-testing
 * - Add expiration
 * - Handle invalidated keys, also for pending requests
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
    using pending_t = std::vector<self_t>;
    using value_t = std::variant<valueT, pending_t>;
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

    void erase(key_t key, boost::system::error_code ec) {
        strand_.dispatch([this, key=std::move(key), ec] {
            if (auto it = cache_.find(key); it != cache_.end()) {
                auto& v = it->second;
                if (std::holds_alternative<pending_t>(v)) {
                    for(auto & s : std::get<pending_t>(v)) {
                        s.fail(ec);
                    }
                }
                cache_.erase(it);
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
            if (std::holds_alternative<pending_t>(v)) {
                // We don't have the value, but we are in the process of querying for it.
                // Add the requester to the list of requesters.
                auto& pending = std::get<pending_t>(v);
                pending.emplace_back(make_self(std::move(self)));
                return;
            }
            assert(false && "The cache holds neither a value or a list of pending requests!");
            self.complete(boost::system::errc::make_error_code(boost::system::errc::invalid_argument), {});
            return;
        }

        // The key is not in the list.
        pending_t pending;
        pending.emplace_back(make_self(std::move(self)));
        cache_[key] = std::move(pending);

        fetch_(key, [key=key, this](boost::system::error_code e, valueT value) {
            strand_.dispatch([this, e, key=std::move(key), value=std::move(value)] {
                auto& v = cache_[key];
                if (std::holds_alternative<pending_t>(v)) {
                    for(auto & s : std::get<pending_t>(v)) {
                        if (e) {
                            s->fail(e);
                        } else {
                            s->complete(value);
                        }
                    }
                }

                if (e) {
                    // Failed
                    cache_.erase(key);
                } else {
                    v = value;
                }
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
