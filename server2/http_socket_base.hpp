#pragma once

#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/signals2.hpp>
#include <optional>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using utilities::search_result_type_e;

template <typename DerivedClass, typename Proxy> class http_socket_base_t {
protected:
  net::io_context &io_;
  std::optional<beast::tcp_stream> tcp_stream_;
  utilities::number_stream_t &numbers_;
  Proxy &proxy_provider_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  typename Proxy::value_type current_proxy_{nullptr};

protected:
  void close_socket();
  void connect();
  void receive_data();
  void reconnect();
  void close_stream();
  void resend_http_request();
  void choose_next_proxy(bool is_first_request = false);
  void send_http_data();
  void set_authentication_header();
  void on_data_sent(beast::error_code, std::size_t const);
  void current_proxy_assign_prop(typename Proxy::Property);
  void prepare_request_data(bool use_auth = false);
  void on_data_received(beast::error_code, std::size_t const);
  void send_first_request();
  virtual void on_connected(beast::error_code,
                            tcp::resolver::results_type::endpoint_type);
  virtual void send_next();

public:
  http_socket_base_t(bool &stopped, net::io_context &, Proxy &,
                     utilities::number_stream_t &);
  void start_connect();
  auto &signal() { return signal_; }

public:
  virtual ~http_socket_base_t();
};

template <typename DerivedClass, typename Proxy>
http_socket_base_t<DerivedClass, Proxy>::~http_socket_base_t() {
  close_socket();
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::close_socket() {
  tcp_stream_->cancel();
  signal_.disconnect_all_slots();
  beast::error_code ec{};
  beast::get_lowest_layer(*tcp_stream_)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(*tcp_stream_).socket().close(ec);
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  } else {
    send_http_data();
  }
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::send_http_data() {
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(
      *tcp_stream_, request_,
      beast::bind_front_handler(&http_socket_base_t::on_data_sent, this));
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::on_data_sent(
    beast::error_code ec, std::size_t const s) {
  if (ec) {
    resend_http_request();
  } else
    receive_data();
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::receive_data() {
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds * 4)); // 4*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(
      *tcp_stream_, buffer_, response_,
      beast::bind_front_handler(&http_socket_base_t::on_data_received, this));
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::start_connect() {
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::send_first_request() {
  if (stopped_) {
    if (!current_number_.empty()) {
      numbers_.push_back(current_number_);
    }
    current_number_.clear();
    return;
  }
  try {
    current_number_ = numbers_.get();
    prepare_request_data();
    connect();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::send_next() {
  if (stopped_) {
    if (!current_number_.empty()) {
      numbers_.push_back(current_number_);
    }
    current_number_.clear();
    return;
  }
  try {
    current_number_ = numbers_.get();
    prepare_request_data();
    send_http_data();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  temp_list_ = {*current_proxy_};
  tcp_stream_->async_connect(
      temp_list_,
      beast::bind_front_handler(&http_socket_base_t::on_connected, this));
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::on_connected(
    beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec)
    return reconnect();
  send_http_data();
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::close_stream() {
  tcp_stream_->close();
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::choose_next_proxy(
    bool const is_first_request) {
  send_count_ = 0;
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    spdlog::error("error getting next endpoint");
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (!is_first_request) {
    tcp_stream_->close();
    tcp_stream_.emplace(net::make_strand(io_));
    return connect();
  }
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::current_proxy_assign_prop(
    typename Proxy::Property property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::prepare_request_data(
    bool const use_auth) {
  static_cast<DerivedClass *>(this)->prepare_request_data(use_auth);
}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::on_data_received(
    beast::error_code ec, std::size_t const bytes_received) {
  static_cast<DerivedClass *>(this)->data_received(ec, bytes_received);
}

template <typename DerivedClass, typename Proxy>
http_socket_base_t<DerivedClass, Proxy>::http_socket_base_t(
    bool &stopped, net::io_context &io_context, Proxy &proxy_provider,
    utilities::number_stream_t &numbers)
    : io_{io_context}, tcp_stream_{std::in_place, net::make_strand(io_)},
      numbers_{numbers}, proxy_provider_{proxy_provider}, stopped_{stopped} {}

template <typename DerivedClass, typename Proxy>
void http_socket_base_t<DerivedClass, Proxy>::set_authentication_header() {
  prepare_request_data(true);
}
} // namespace wudi_server