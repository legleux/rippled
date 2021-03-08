//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_OVERLAY_TIMER_H_INCLUDED
#define RIPPLE_OVERLAY_TIMER_H_INCLUDED

#include <ripple/overlay/impl/Child.h>


#include <memory>

namespace ripple {

template <typename OverlayImplmnt>
struct Timer : Child<OverlayImplmnt>,
               std::enable_shared_from_this<Timer<OverlayImplmnt>> {
    using error_code = boost::system::error_code;
    using clock_type = std::chrono::steady_clock;

    boost::asio::basic_waitable_timer <clock_type> timer_;

    explicit Timer(OverlayImplmnt &overlay);

    void
    stop() override;

    void
    run();

    void
    on_timer(error_code ec);
};

template <typename OverlayImplmnt>
Timer<OverlayImplmnt>::Timer(OverlayImplmnt& overlay)
        : Child<OverlayImplmnt>(overlay), timer_(this->overlay_.io_service_)
{
}

template <typename OverlayImplmnt>
void
Timer<OverlayImplmnt>::stop()
{
    error_code ec;
    timer_.cancel(ec);
}

template <typename OverlayImplmnt>
void
Timer<OverlayImplmnt>::run()
{
    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(this->overlay_.strand_.wrap(std::bind(
            &Timer::on_timer, this->shared_from_this(), std::placeholders::_1)));
}

template <typename OverlayImplmnt>
void
Timer<OverlayImplmnt>::on_timer(error_code ec)
{
    if (ec || this->overlay_.isStopping())
    {
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(this->overlay_.journal_.error()) << "on_timer: " << ec.message();
        }
        return;
    }

    this->overlay_.m_peerFinder->once_per_second();
    this->overlay_.sendEndpoints();
    this->overlay_.autoConnect();

    if ((++this->overlay_.timer_count_ % Tuning::checkIdlePeers) == 0)
        this->overlay_.deleteIdlePeers();

    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(this->overlay_.strand_.wrap(std::bind(
            &Timer::on_timer, this->shared_from_this(), std::placeholders::_1)));
}

}

#endif
