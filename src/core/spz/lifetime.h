// SPDX-License-Identifier: GPL-3.0-or-later
// The lifetime of an object that hands callbacks to others: a job on a worker, a message to the UI thread,
// a timer of the clock. A callback can come back after the object is gone; it holds a Token, and does
// nothing once the object's Lifetime has ended. The object ends its Lifetime first thing in its destructor
// (or the Lifetime ends itself, as the member it is, when the object goes).
//
// A Token may be copied to and asked on any thread. Whether a callback that has begun may go on while the
// Lifetime ends on another thread is the owner's to settle: here, every owner ends its Lifetime and runs
// the callbacks it guards on the same thread (the UI thread), so a callback that sees the object alive has
// it alive until it returns.
#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace spz {

class Lifetime {
public:
    class Token {
    public:
        // The object is still there.
        bool alive() const { return flag_ && flag_->load(); }

    private:
        friend class Lifetime;
        explicit Token(std::shared_ptr<const std::atomic<bool>> flag) : flag_(std::move(flag)) {}
        std::shared_ptr<const std::atomic<bool>> flag_;
    };

    Lifetime() = default;
    ~Lifetime() { end(); }
    Lifetime(const Lifetime&) = delete;
    Lifetime& operator=(const Lifetime&) = delete;

    // From here on, every Token of this Lifetime says the object is gone. Ending it twice does nothing more.
    void end() { flag_->store(false); }

    Token token() const { return Token(flag_); }

    // `fn` wrapped so that it runs only while the object is alive, with the arguments it is called with.
    template <typename Fn>
    auto guard(Fn fn) const {
        return [alive = token(), fn = std::move(fn)](auto&&... arguments) mutable {
            if (alive.alive()) fn(std::forward<decltype(arguments)>(arguments)...);
        };
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_ = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace spz
