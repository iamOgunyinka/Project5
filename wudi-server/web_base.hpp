#pragma once

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include "web_base.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/signals2.hpp>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using utilities::ProxyAddress;
using utilities::SearchResultType;
using tcp = boost::asio::ip::tcp;
using CustomStringList = std::vector<std::string>;
using ProxyList = std::vector<ProxyAddress>;

template <typename DerivedClass> class web_base {
protected:
  net::io_context &io_;
  beast::tcp_stream tcp_stream_;
  utilities::number_stream &numbers_;
  safe_proxy &proxy_provider_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  http::request<http::string_body> post_request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(utilities::SearchResultType, std::string_view)>
      signal_;
  std::size_t connect_count_{};
  std::size_t send_count_{};
  EndpointList temp_list_;
  endpoint_ptr current_endpoint_;

  void close_socket();

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
  void prepare_request_data(bool use_auth = false);
  void on_data_received(beast::error_code, std::size_t const);

public:
  web_base(bool &stopped, net::io_context &, safe_proxy &,
           utilities::number_stream &);
  void start_connect();
  ~web_base();
  auto &signal() { return signal_; }
};

template <typename DerivedClass> web_base<DerivedClass>::~web_base() {
  close_socket();
  spdlog::info("Closing sockets");
}

template <typename DerivedClass> void web_base<DerivedClass>::close_socket() {
  tcp_stream_.cancel();
  signal_.disconnect_all_slots();
  beast::error_code ec{};
  beast::get_lowest_layer(tcp_stream_)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(tcp_stream_).socket().close(ec);
}

template <typename DerivedClass>
void web_base<DerivedClass>::resend_http_request() {
  if (stopped_)
    return;
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
    connect();
  } else {
    send_http_data();
  }
}

template <typename DerivedClass> void web_base<DerivedClass>::send_http_data() {
  if (stopped_)
    return;
  tcp_stream_.expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(tcp_stream_, post_request_,
                    beast::bind_front_handler(&web_base::on_data_sent, this));
}

template <typename DerivedClass>
void web_base<DerivedClass>::on_data_sent(beast::error_code ec,
                                          std::size_t const) {
  if (ec)
    resend_http_request();
  else
    receive_data();
}

template <typename DerivedClass> void web_base<DerivedClass>::receive_data() {
  if (stopped_)
    return;
  tcp_stream_.expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds * 3)); // 3*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(
      tcp_stream_, buffer_, response_,
      beast::bind_front_handler(&web_base::on_data_received, this));
}

template <typename DerivedClass> void web_base<DerivedClass>::start_connect() {
  choose_next_proxy();
  if (current_endpoint_)
    send_next();
}

template <typename DerivedClass> void web_base<DerivedClass>::send_next() {
  if (stopped_)
    return;
  try {
    current_number_ = numbers_.get();
    prepare_request_data();
    connect();
  } catch (utilities::empty_container_exception &) {
    return;
  }
}

template <typename DerivedClass> void web_base<DerivedClass>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
  }
  connect();
}

template <typename DerivedClass> void web_base<DerivedClass>::connect() {
  if (temp_list_.empty() || stopped_)
    return;
  tcp_stream_.expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  tcp_stream_.async_connect(
      temp_list_, beast::bind_front_handler(&web_base::on_connected, this));
}

template <typename DerivedClass>
void web_base<DerivedClass>::on_connected(
    beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec)
    reconnect();
  else
    send_http_data();
}

template <typename DerivedClass>
void web_base<DerivedClass>::choose_next_proxy() {
  if (stopped_)
    return;
  send_count_ = 0;
  connect_count_ = 0;
  temp_list_.clear();
  if (auto proxy = proxy_provider_.next_endpoint(); proxy.has_value()) {
    current_endpoint_ = proxy.value();
    temp_list_.push_back(*current_endpoint_);
  } else {
    spdlog::error("error getting next endpoint");
    current_endpoint_ = nullptr;
    signal_(SearchResultType::Unknown, current_number_);
  }
}

template <typename DerivedClass>
void web_base<DerivedClass>::current_proxy_assign_prop(ProxyProperty property) {
  if (current_endpoint_) {
    current_endpoint_->property = property;
  }
}

template <typename DerivedClass>
inline void web_base<DerivedClass>::prepare_request_data(bool const use_auth) {
  static_cast<DerivedClass *>(this)->prepare_request_data(use_auth);
}

template <typename DerivedClass>
inline void
web_base<DerivedClass>::on_data_received(beast::error_code ec,
                                         std::size_t const bytes_received) {
  static_cast<DerivedClass *>(this)->on_data_received(ec, bytes_received);
}

template <typename DerivedClass>
web_base<DerivedClass>::web_base(bool &stopped, net::io_context &io_context,
                                 safe_proxy &proxy_provider,
                                 utilities::number_stream &numbers)
    : io_{io_context}, tcp_stream_{net::make_strand(io_)}, numbers_{numbers},
      proxy_provider_{proxy_provider}, stopped_{stopped} {}

template <typename DerivedClass>
void web_base<DerivedClass>::set_authentication_header() {
  prepare_request_data(true);
}
} // namespace wudi_server
