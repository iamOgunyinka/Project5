#pragma once

#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace wudi_server {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using net::ip::tcp;

template <typename T> class custom_timed_socket_t {
  beast::tcp_stream http_tcp_socket_;
  net::ip::tcp::resolver resolver_;
  int const timeout_sec_;
  std::promise<T> promise_;
  beast::flat_buffer buffer_;
  utilities::uri const uri_;
  http::request<http::empty_body> http_request_;
  http::response<http::string_body> http_response_;

public:
  custom_timed_socket_t(net::io_context &io_context, std::string url,
                        int const timeout_sec, std::promise<T> &&prom)
      : http_tcp_socket_(net::make_strand(io_context)), resolver_{io_context},
        timeout_sec_{timeout_sec},
        promise_(std::move(prom)), uri_{std::move(url)} {}

  void start() { return get(); }

private:
  void connect(net::ip::tcp::resolver::results_type const &resolves) {
    http_tcp_socket_.expires_after(std::chrono::seconds(timeout_sec_));
    http_tcp_socket_.async_connect(
        resolves, [=](beast::error_code ec, auto const &) {
          if (ec) {
            try {
              throw std::runtime_error(ec.message());
            } catch (std::runtime_error const &) {
              return promise_.set_exception(std::current_exception());
            }
          }
          return send_request();
        });
  }

  void receive_data() {
    http_tcp_socket_.expires_after(std::chrono::seconds(timeout_sec_));
    http::async_read(http_tcp_socket_, buffer_, http_response_,
                     [=](beast::error_code ec, std::size_t const) {
                       if (ec) {
                         return promise_.set_value(T{});
                       }
                       return promise_.set_value(T{
                           http_response_.body(), http_response_.result_int()});
                     });
  }

  void send_request() {
    http_request_.method(http::verb::get);
    http_request_.target(uri_.target());
    http_request_.version(11);
    http_request_.set(http::field::host, uri_.host());
    http_request_.set(http::field::user_agent, utilities::get_random_agent());

    http_tcp_socket_.expires_after(std::chrono::seconds(5));
    http::async_write(http_tcp_socket_, http_request_,
                      [=](beast::error_code ec, std::size_t const) {
                        if (ec) {
                          try {
                            throw std::runtime_error(ec.message());
                          } catch (std::runtime_error const &) {
                            return promise_.set_exception(
                                std::current_exception());
                          }
                        }
                        return receive_data();
                      });
  }
  void get() {
    resolver_.async_resolve(uri_.host(), uri_.protocol(),
                            [=](auto const &ec, auto const &resolves) {
                              if (ec) {
                                try {
                                  throw std::runtime_error(ec.message());
                                } catch (std::runtime_error const &) {
                                  return promise_.set_exception(
                                      std::current_exception());
                                }
                              }
                              connect(resolves);
                            });
  }
};
} // namespace wudi_server
