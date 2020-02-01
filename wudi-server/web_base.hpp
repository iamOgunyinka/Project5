#pragma once

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include "web_base.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using utilities::ProxyAddress;
using utilities::SearchResultType;
using tcp = boost::asio::ip::tcp;
using CustomStringList = std::vector<std::string>;
using ProxyList = std::vector<ProxyAddress>;

template <typename T> class web_base {
protected:
  net::io_context &io_;
  beast::tcp_stream tcp_stream_;
  beast::flat_buffer buffer_{};
  http::request<http::string_body> post_request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};

  std::size_t connect_count_{};
  std::size_t send_count_{};
  bool tried_unresposiveness_{false};
  utilities::threadsafe_container<std::string> &numbers_;
  EndpointList temp_list_;
  safe_proxy &proxy_provider_;
  endpoint_ptr current_endpoint_;

protected:
  void connect();
  void on_connected(beast::error_code,
                    tcp::resolver::results_type::endpoint_type);
  void send_http_data();
  void on_data_sent(beast::error_code, std::size_t const);
  void receive_data();
  void reconnect();
  void resend_http_request();

  void choose_next_proxy();
  void send_next();
  void set_authentication_header();
  void current_proxy_assign_prop(ProxyProperty);

protected:
  void result_available(SearchResultType, std::string_view);
  void prepare_request_data(bool use_auth = false);
  void on_data_received(beast::error_code, std::size_t const);

public:
  web_base(net::io_context &, safe_proxy &,
           utilities::threadsafe_container<std::string> &);
  void start_connect();
  virtual ~web_base() = default;
};

template <typename T> void web_base<T>::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
    connect();
  } else {
    send_http_data();
  }
}

template <typename T> void web_base<T>::send_http_data() {
  tcp_stream_.expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(tcp_stream_, post_request_,
                    beast::bind_front_handler(&web_base::on_data_sent, this));
}

template <typename T>
void web_base<T>::on_data_sent(beast::error_code ec, std::size_t const) {
  if (ec)
    resend_http_request();
  else
    receive_data();
}

template <typename T> void web_base<T>::receive_data() {
  tcp_stream_.expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds * 3)); // 3*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(
      tcp_stream_, buffer_, response_,
      beast::bind_front_handler(&web_base::on_data_received, this));
}

template <typename T> void web_base<T>::start_connect() {
  choose_next_proxy();
  if (current_endpoint_)
    send_next();
}

template <typename T> void web_base<T>::send_next() {
  try {
    current_number_ = numbers_.get();
    prepare_request_data();
    connect();
  } catch (utilities::empty_container_exception &) {
    return;
  }
}

template <typename T> void web_base<T>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
  }
  connect();
}

template <typename T> void web_base<T>::connect() {
  if (temp_list_.empty())
    return;
  tcp_stream_.expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  tcp_stream_.async_connect(
      temp_list_, beast::bind_front_handler(&web_base::on_connected, this));
}

template <typename T>
void web_base<T>::on_connected(beast::error_code ec,
                               tcp::resolver::results_type::endpoint_type) {
  if (ec)
    reconnect();
  else
    send_http_data();
}

template <typename T> void web_base<T>::choose_next_proxy() {
  send_count_ = 0;
  connect_count_ = 0;
  temp_list_.clear();
  if (auto proxy = proxy_provider_.next_endpoint(); proxy.has_value()) {
    current_endpoint_ = proxy.value();
    temp_list_.push_back(*current_endpoint_);
  } else {
    current_endpoint_ = nullptr;
    result_available(SearchResultType::Unknown, current_number_);
  }
}

template <typename T>
void web_base<T>::current_proxy_assign_prop(ProxyProperty property) {
  if (current_endpoint_) {
    current_endpoint_->property = property;
  }
}

template <typename T>
inline void web_base<T>::result_available(SearchResultType t,
                                          std::string_view number) {
  static_cast<T *>(this)->result_available(t, number);
}

template <typename T>
inline void web_base<T>::prepare_request_data(bool const use_auth) {
  static_cast<T *>(this)->prepare_request_data(use_auth);
}

template <typename T>
inline void web_base<T>::on_data_received(beast::error_code ec,
                                          std::size_t const bytes_received) {
  static_cast<T *>(this)->on_data_received(ec, bytes_received);
}

template <typename T>
web_base<T>::web_base(net::io_context &io_context, safe_proxy &proxy_provider,
                      utilities::threadsafe_container<std::string> &numbers)
    : io_{io_context}, tcp_stream_{net::make_strand(io_)}, numbers_{numbers},
      proxy_provider_{proxy_provider} {}

template <typename T> void web_base<T>::set_authentication_header() {
  prepare_request_data(true);
}
} // namespace wudi_server
