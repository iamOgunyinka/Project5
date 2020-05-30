#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace wudi_server {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using net::ip::tcp;

struct uri {
  uri(std::string const &url_s);
  std::string path() const;
  std::string host() const;
  std::string target() const;
  std::string protocol() const;

private:
  void parse(std::string const &);
  std::string host_;
  std::string path_;
  std::string protocol_;
  std::string query_;
};
uri::uri(std::string const &url_s) { parse(url_s); }

std::string uri::target() const { return path_ + "?" + query_; }

std::string uri::protocol() const { return protocol_; }

std::string uri::path() const { return path_; }

std::string uri::host() const { return host_; }

void uri::parse(std::string const &url_s) {
  std::string const prot_end{"://"};
  std::string::const_iterator prot_i =
      std::search(url_s.begin(), url_s.end(), prot_end.begin(), prot_end.end());
  protocol_.reserve(
      static_cast<std::size_t>(std::distance(url_s.cbegin(), prot_i)));
  std::transform(url_s.begin(), prot_i, std::back_inserter(protocol_),
                 [](int c) { return std::tolower(c); });
  if (prot_i == url_s.end()) {
    prot_i = url_s.begin();
  } else {
    std::advance(prot_i, prot_end.length());
  }
  std::string::const_iterator path_i = std::find(prot_i, url_s.end(), '/');
  host_.reserve(static_cast<std::size_t>(std::distance(prot_i, path_i)));
  std::transform(prot_i, path_i, std::back_inserter(host_),
                 [](int c) { return std::tolower(c); });
  std::string::const_iterator query_i = std::find(path_i, url_s.end(), '?');
  path_.assign(path_i, query_i);
  if (query_i != url_s.end())
    ++query_i;
  query_.assign(query_i, url_s.end());
}

template <typename T> class custom_timed_socket_t {
  beast::tcp_stream http_tcp_socket_;
  net::ip::tcp::resolver resolver_;
  int const connect_timeout_ms;
  int const read_timeout_ms;
  std::promise<T> promise_;
  beast::flat_buffer buffer_;
  uri const uri_;
  http::request<http::empty_body> http_request_;
  http::response<http::string_body> http_response_;

public:
  custom_timed_socket_t(net::io_context &io_context, std::string url,
                        int const connect_timeout_sec,
                        int const read_timeout_sec, std::promise<T> &&prom)
      : http_tcp_socket_(net::make_strand(io_context)), resolver_{io_context},
        connect_timeout_ms{connect_timeout_sec * 1'000},
        read_timeout_ms{read_timeout_sec * 1'000},
        promise_(std::move(prom)), uri_{std::move(url)} {}

  void start() { return get(); }

private:
  void connect(net::ip::tcp::resolver::results_type const &resolves) {
    http_tcp_socket_.expires_after(
        std::chrono::milliseconds(connect_timeout_ms));
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
    http_tcp_socket_.expires_after(std::chrono::milliseconds(read_timeout_ms));
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
    http_request_.set(
        http::field::user_agent,
        "Mozilla/5.0 (Windows NT 6.3; rv:36.0) Gecko/20100101 Firefox/36.0");

    http_tcp_socket_.expires_after(std::chrono::milliseconds(10));
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
