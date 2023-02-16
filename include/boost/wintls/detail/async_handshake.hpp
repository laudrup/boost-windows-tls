//
// Copyright (c) 2020 Kasper Laudrup (laudrup at stacktrace dot dk)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_WINTLS_DETAIL_ASYNC_HANDSHAKE_HPP
#define BOOST_WINTLS_DETAIL_ASYNC_HANDSHAKE_HPP

#include <boost/wintls/handshake_type.hpp>

#include <boost/wintls/detail/sspi_handshake.hpp>

#include <boost/asio/coroutine.hpp>

namespace boost {
namespace wintls {
namespace detail {

template<typename NextLayer>
struct async_handshake : boost::asio::coroutine {
  async_handshake(NextLayer& next_layer, detail::sspi_handshake& handshake, handshake_type type)
      : next_layer_(next_layer)
      , handshake_(handshake)
      , entry_count_(0) {
    handshake_(type);
  }

  template<typename Self>
  void operator()(Self& self, boost::system::error_code ec = {}, std::size_t length = 0) {
    if (ec) {
      self.complete(ec);
      return;
    }

    ++entry_count_;
    auto is_continuation = [this] {
      return entry_count_ > 1;
    };

    sspi_handshake::state handshake_state;
    BOOST_ASIO_CORO_REENTER(*this) {
      while (true) {
        handshake_state = handshake_();
        if (handshake_state == sspi_handshake::state::data_needed) {
          BOOST_ASIO_CORO_YIELD {
            next_layer_.async_read_some(handshake_.in_buffer(), std::move(self));
          }
          handshake_.size_read(length);
          continue;
        }

        if (handshake_state == sspi_handshake::state::data_available) {
          BOOST_ASIO_CORO_YIELD {
            net::async_write(next_layer_, handshake_.out_buffer(), std::move(self));
          }
          handshake_.size_written(length);
          continue;
        }

        if (handshake_state == sspi_handshake::state::error) {
          break;
        }

        if (handshake_state == sspi_handshake::state::done) {
          BOOST_ASSERT(!handshake_.last_error());
          handshake_.manual_auth();
          break;
        }
      }

      // If this is the first call to this function, it would cause the completion handler
      // (invoked by self.complete()) to be executed on the wrong executor.
      // Ensure that doesn't happen by posting the completion handler instead of calling it directly.
      if (!is_continuation()) {
        BOOST_ASIO_CORO_YIELD {
          auto e = self.get_executor();
          net::post(e, [self = std::move(self), ec, length]() mutable { self(ec, length); });
        }
      }
      self.complete(handshake_.last_error());
    }
  }

private:
  NextLayer& next_layer_;
  sspi_handshake& handshake_;
  int entry_count_;
};

} // namespace detail
} // namespace wintls
} // namespace boost

#endif //BOOST_WINTLS_DETAIL_ASYNC_HANDSHAKE_HPP
