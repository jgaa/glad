#pragma once

#include <list>
#include <chrono>
#include <boost/asio.hpp>


#include "glad/config.h"


namespace jgaa::glad {

/*! Generic async wait-for-something implementation
 *
 *  The idea is to allow a composed completion when a coroutine needs
 *  to wait for "something" to become ready.
 *
 */

template <typename T, typename asioCtxT, typename validateT>
class AsyncWaitFor {
public:

    struct Item {
        Item(AsyncWaitFor& parent, T what, boost::posix_time::milliseconds waitDuration)
            : timer_{parent.asioCtx_, waitDuration}, what_{std::move(what)}
        {
        }

        boost::asio::deadline_timer timer_;
        T what_;
        bool ok_ = false;
        bool done_ = false;
    };

    using container_t = std::list<std::shared_ptr<Item>>;

    /*! Async wait for something to complete.
     *
     *  \param what A variable that states what we are waiting for.
     *  \param duration How long we are willing to wait before we time out.
     *  \param token asio continuation.
     */
    template <typename CompletionToken, typename Rep, typename Period>
    auto wait(T what, std::chrono::duration<Rep, Period> duration, CompletionToken&& token) {
        return boost::asio::async_compose<CompletionToken,
                                          void(boost::system::error_code e)>
            ([this, what=std::move(what), duration](auto& self) mutable {

            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

                std::lock_guard lock{mutex_};
                auto item = std::make_shared<Item>(*this, std::move(what), boost::posix_time::milliseconds{ms});
                storage_.emplace_back(item);

                item->timer_.async_wait([this, item, self=std::move(self)]
                                       (boost::system::error_code ec) mutable {
                    assert(!item->done_);
                    item->done_ = true;

                    if (item->ok_) {
                        self.complete({});
                        return;
                    }

                    if (!ec) {
                        ec = boost::system::errc::make_error_code(boost::system::errc::timed_out);
                    }
                    self.complete(ec);
                });
            }, token);
    }

    /*! Evaluate all waiting items against the condition.
     *
     *  Any items that match the condition, using the
     *  static `validateT` template functor will be
     *  resumed.
     *
     *  The method returns immediately.
     */
    template <typename cT>
    void onChange(const cT& condition) {
        std::lock_guard lock{mutex_};

        for(auto it = storage_.begin(); it != storage_.end();) {
            auto curr = it;
            auto &item = **curr;
            ++it;

            if (item.done_) {
                // Already timed out.
                storage_.erase(curr);
                continue;
            }

            if (validate_(condition, item.what_)) {
                item.ok_ = true;
                boost::system::error_code ec;
                item.timer_.cancel(ec);
                storage_.erase(curr);
            }
        }
    }

    /*! Remove expired waiter remains.
     *
     *  This is a housekeeping task that should be called from time to time unless
     *  `onChange()` is called relatively frequently. OnChange will also clean up.
     */
    void clean() {
        std::lock_guard lock{mutex_};

        storage_.remove_if([](const auto& v) {
            return v->done_;
        });
    }

    AsyncWaitFor(asioCtxT& ctx)
        : asioCtx_{ctx} {}

private:

    // Some kind of containter that stores the stuff that is waiting for a completion.
    container_t storage_;
    asioCtxT& asioCtx_;
    mutable std::mutex mutex_;
    validateT validate_;
};


template <typename T, typename asioCtxT, typename validateT>
auto make_async_wait_for(asioCtxT& asioCtx, validateT validate) {
    return AsyncWaitFor<T, asioCtxT, validateT>(asioCtx);
}

} // ns
