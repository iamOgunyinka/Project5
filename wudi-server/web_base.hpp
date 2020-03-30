#pragma once

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/signals2.hpp>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using utilities::proxy_address_t;
using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using utilities::search_result_type_e;
using utilities::task_status_e;

template <typename DerivedClass> class web_base {
protected:
  net::io_context &io_;
  beast::tcp_stream tcp_stream_;
  utilities::number_stream_t &numbers_;
  proxy_provider_t &proxy_provider_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  std::size_t ep_index_{};
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
  web_base(bool &stopped, net::io_context &, proxy_provider_t &,
           utilities::number_stream_t &);
  void start_connect();
  virtual ~web_base();
  auto &signal() { return signal_; }
};

template <typename DerivedClass> web_base<DerivedClass>::~web_base() {
  close_socket();
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
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
    connect();
  } else {
    send_http_data();
  }
}

template <typename DerivedClass> void web_base<DerivedClass>::send_http_data() {
  tcp_stream_.expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(tcp_stream_, request_,
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
  if (ep_index_ != std::numeric_limits<std::size_t>::max())
    send_next();
}

template <typename DerivedClass> void web_base<DerivedClass>::send_next() {
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
    // connect();
    send_http_data();
  } catch (utilities::empty_container_exception_t &) {
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
  if (ep_index_ == std::numeric_limits<std::size_t>::max() || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  tcp_stream_.expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  temp_list_ = {proxy_provider_.endpoint(ep_index_)};
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
  send_count_ = 0;
  connect_count_ = 0;
  if (ep_index_ = proxy_provider_.next_endpoint();
      ep_index_ == std::numeric_limits<std::size_t>::max()) {
    spdlog::error("error getting next endpoint");
    numbers_.push_back(current_number_);
    current_number_.clear();
    signal_(search_result_type_e::RequestStop, current_number_);
  }
}

template <typename DerivedClass>
void web_base<DerivedClass>::current_proxy_assign_prop(ProxyProperty property) {
  proxy_provider_.assign_property(ep_index_, property);
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
                                 proxy_provider_t &proxy_provider,
                                 utilities::number_stream_t &numbers)
    : io_{io_context}, tcp_stream_{net::make_strand(io_)}, numbers_{numbers},
      proxy_provider_{proxy_provider}, stopped_{stopped} {}

template <typename DerivedClass>
void web_base<DerivedClass>::set_authentication_header() {
  prepare_request_data(true);
}
} // namespace wudi_server
