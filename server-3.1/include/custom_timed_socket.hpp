#pragma once
#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <optional>

namespace wudi_server {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace csock = net::detail::socket_ops;

using net::ip::tcp;

class custom_timed_socket_t {
  net::io_context &io_context_;
  net::ip::basic_resolver_results<tcp> &resolves_;
  int http_status_code_ = -1;
  bool data_read_ = false;

public:
  bool data_read() const { return data_read_; }
  int http_status_code() const { return http_status_code_; }

  custom_timed_socket_t(net::io_context &io_context,
                        net::ip::basic_resolver_results<tcp> &resolves)
      : io_context_{io_context}, resolves_{resolves} {}

  std::optional<std::string> get(std::string const &url,
                                 int connect_timeout_sec, int read_timeout_sec,
                                 boost::system::error_code &ec) {

    utilities::uri const more_ip_uri_{url};
    net::ip::basic_resolver_results<tcp>::iterator resolver_iter{};
    bool is_ipv4 = true;
    if (resolves_.empty()) {
      net::ip::tcp::resolver resolver{io_context_};
      resolves_ =
          resolver.resolve(more_ip_uri_.host(), more_ip_uri_.protocol());
    }
    resolver_iter =
        std::find_if(resolves_.begin(), resolves_.end(),
                     [](net::ip::basic_resolver_entry<net::ip::tcp> const &ep) {
                       return ep.endpoint().address().is_v4();
                     });
    is_ipv4 = resolver_iter != resolves_.end();
    if (!is_ipv4) {
      resolver_iter = std::find_if(
          resolves_.begin(), resolves_.end(),
          [](net::ip::basic_resolver_entry<net::ip::tcp> const &ep) {
            return ep.endpoint().address().is_v6();
          });
      if (resolver_iter == resolves_.end()) {
        return std::nullopt;
      }
    }

    tcp::socket http_tcp_stream(net::make_strand(io_context_));
    {
      if (is_ipv4) {
        http_tcp_stream.open(net::ip::tcp::v4(), ec);
      } else {
        http_tcp_stream.open(net::ip::tcp::v6(), ec);
      }
      if (ec) {
        return std::nullopt;
      }
      ec = {};
      auto &underlying_socket = http_tcp_stream;
      if (!underlying_socket.native_non_blocking())
        underlying_socket.native_non_blocking(true, ec);

      if (ec) {
        return std::nullopt;
      }
      http_tcp_stream.connect(*resolver_iter);

      ec = {};
      int const connect_time_ms = connect_timeout_sec * 1'000;
      auto result = csock::poll_connect(underlying_socket.native_handle(),
                                        connect_time_ms, ec);
      if (ec || result < 0) {
        return std::nullopt;
      }
      int connect_error = 0;
      size_t connect_error_len = sizeof(connect_error);
      auto const get_sock_result =
          csock::getsockopt(underlying_socket.native_handle(), 0, SOL_SOCKET,
                            SO_ERROR, &connect_error, &connect_error_len, ec);
      if (get_sock_result != 0 || ec) {
        return std::nullopt;
      }
      ec = {};
      if (underlying_socket.native_non_blocking()) {
        underlying_socket.native_non_blocking(false, ec);
      }
      if (ec) {
        return std::nullopt;
      }
    }
    http::request<http::empty_body> http_request{};
    http_request.method(http::verb::get);
    http_request.target(more_ip_uri_.target());
    http_request.version(11);
    http_request.set(http::field::host, more_ip_uri_.host());
    http_request.set(http::field::user_agent, utilities::get_random_agent());

    http::write(http_tcp_stream, http_request, ec);
    beast::flat_buffer buffer{};
    http::response<http::string_body> server_response{};

#ifndef _WIN32
    timeval time_val{};
    time_val.tv_sec = read_timeout_sec;
    time_val.tv_usec = 0;
    setsockopt(http_tcp_stream.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
               (char *)&time_val, sizeof(timeval));
#else
    int const read_time_ms = read_timeout_sec * 1'000;
    http_tcp_stream.set_option(
        net::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>(
            read_time_ms));
#endif // !_WIN32

    http::read(http_tcp_stream, buffer, server_response, ec);
    if (ec) {
      data_read_ = true;
      return std::nullopt;
    }
    ec = {};
    http_tcp_stream.shutdown(tcp::socket::shutdown_both, ec);
    http_status_code_ = server_response.result_int();
    return server_response.body();
  }
};

} // namespace wudi_server
