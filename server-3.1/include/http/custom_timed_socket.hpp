#pragma once

#include "http_uri.hpp"
#include "request_handler.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>

namespace woody_server {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using net::ip::tcp;

template <typename T> class custom_timed_socket_t {
  beast::tcp_stream m_tcpStream;
  net::ip::tcp::resolver m_resolver;
  int const m_timeoutSec;
  std::promise<T> m_promise;
  beast::flat_buffer m_buffer;
  http_uri_t const m_uri;
  http::request<http::empty_body> m_httpRequest;
  http::response<http::string_body> m_httpResponse;

public:
  custom_timed_socket_t(net::io_context &ioContext, std::string const &url,
                        int const timeoutSec, std::promise<T> &&promise)
      : m_tcpStream(net::make_strand(ioContext)), m_resolver(ioContext),
        m_timeoutSec(timeoutSec), m_promise(std::move(promise)), m_uri{url} {}

  void start() { get(); }

private:
  void connect(net::ip::tcp::resolver::results_type const &resolves) {
    m_tcpStream.expires_after(std::chrono::seconds(m_timeoutSec));
    m_tcpStream.async_connect(
        resolves, [=](beast::error_code ec, auto const &) {
          if (ec) {
            try {
              throw std::runtime_error(ec.message());
            } catch (std::runtime_error const &) {
              return m_promise.set_exception(std::current_exception());
            }
          }
          return sendRequest();
        });
  }

  void receiveData() {
    m_tcpStream.expires_after(std::chrono::seconds(m_timeoutSec));
    http::async_read(m_tcpStream, m_buffer, m_httpResponse,
                     [=](beast::error_code ec, std::size_t const) {
                       if (ec) {
                         return m_promise.set_value(T{});
                       }
                       return m_promise.set_value(T{
                           m_httpResponse.body(), m_httpResponse.result_int()});
                     });
  }

  void sendRequest() {
    m_httpRequest.method(http::verb::get);
    m_httpRequest.target(m_uri.target());
    m_httpRequest.version(11);
    m_httpRequest.set(http::field::host, m_uri.host());
    m_httpRequest.set(http::field::user_agent, utilities::getRandomUserAgent());

    m_tcpStream.expires_after(std::chrono::seconds(5));
    http::async_write(m_tcpStream, m_httpRequest,
                      [=](beast::error_code ec, std::size_t const) {
                        if (ec) {
                          try {
                            throw std::runtime_error(ec.message());
                          } catch (std::runtime_error const &) {
                            return m_promise.set_exception(
                                std::current_exception());
                          }
                        }
                        return receiveData();
                      });
  }
  void get() {
    m_resolver.async_resolve(m_uri.host(), m_uri.protocol(),
                             [=](auto const &ec, auto const &resolves) {
                               if (ec) {
                                 try {
                                   throw std::runtime_error(ec.message());
                                 } catch (std::runtime_error const &) {
                                   return m_promise.set_exception(
                                       std::current_exception());
                                 }
                               }
                               connect(resolves);
                             });
  }
};
} // namespace woody_server
