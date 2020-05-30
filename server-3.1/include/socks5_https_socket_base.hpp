#pragma once

#include "protocol.hpp"
#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = net::ssl;

using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using beast::error_code;
using utilities::search_result_type_e;

template <typename Derived, typename Proxy> class socks5_https_socket_base_t {
protected:
  net::io_context &io_;
  utilities::number_stream_t &numbers_;
  Proxy &proxy_provider_;
  ssl::context &ssl_context_;
  std::optional<beast::ssl_stream<beast::tcp_stream>> ssl_stream_;
  bool &stopped_;

  std::vector<char> reply_buffer{};
  std::vector<char> handshake_buffer{};

  std::optional<beast::flat_buffer> general_buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  typename Proxy::value_type current_proxy_{nullptr};
  int const scans_per_ip_;

protected:
  void send_first_request();
  void perform_socks5_handshake();
  void on_first_handshake_initiated(error_code, std::size_t const);
  void read_first_handshake_result();
  void on_first_handshake_response_received(error_code, std::size_t const);
  void perform_sock5_second_handshake();
  void on_auth_response_received(error_code, std::size_t);
  void process_ipv4_response(error_code, std::size_t);
  void on_handshake_response_received(error_code, std::size_t);
  void read_socks5_server_response();
  void set_authentication_header();
  void perform_ssl_handshake();
  void on_ssl_handshake(error_code);

  void close_socket();
  void close_stream();
  void perform_ssl_ritual();
  void connect();
  void receive_data();
  void reconnect();
  void resend_http_request();
  void choose_next_proxy(bool first_request = false);
  void send_https_data();
  void on_data_sent(error_code, std::size_t const);
  void current_proxy_assign_prop(typename Proxy::Property);
  void prepare_request_data(bool use_auth = false);
  void on_connected(error_code, tcp::resolver::results_type::endpoint_type);
  virtual void send_next();
  void on_data_received(error_code, std::size_t const);
  std::string hostname() const;

public:
  socks5_https_socket_base_t(net::ssl::context &, bool &, net::io_context &,
                             Proxy &, utilities::number_stream_t &, int);
  void start_connect();
  ~socks5_https_socket_base_t() {
    signal_.disconnect_all_slots();
    close_socket();
  }
  auto &signal() { return signal_; }
};

template <typename Derived, typename Proxy>
socks5_https_socket_base_t<Derived, Proxy>::socks5_https_socket_base_t(
    net::ssl::context &ssl_context, bool &stopped, net::io_context &io_context,
    Proxy &proxy_provider, utilities::number_stream_t &numbers,
    int const per_ip)
    : io_{io_context}, ssl_stream_{std::in_place, net::make_strand(io_),
                                   ssl_context},
      numbers_{numbers}, proxy_provider_{proxy_provider},
      ssl_context_{ssl_context}, stopped_{stopped}, scans_per_ip_{per_ip} {}

template <typename Derived, typename Proxy>
std::string socks5_https_socket_base_t<Derived, Proxy>::hostname() const {
  return static_cast<Derived const *>(this)->hostname();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::on_connected(
    error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    return reconnect();
  }
  return perform_socks5_handshake();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::perform_socks5_handshake() {
  handshake_buffer.clear();

  bool const using_auth = !current_proxy_->username().empty();

  std::size_t const bytes_to_write = using_auth ? 4 : 3;

  handshake_buffer.reserve(bytes_to_write);
  handshake_buffer.push_back(SOCKS_VERSION_5); // version
  if (using_auth) {
    handshake_buffer.push_back(2);                // using 2 methods
    handshake_buffer.push_back(SOCKS5_AUTH_NONE); // using no auth
    handshake_buffer.push_back(SOCKS5_AUTH);      // using auth
  } else {
    handshake_buffer.push_back(1);                // using 1 method
    handshake_buffer.push_back(SOCKS5_AUTH_NONE); // no auth
  }

  beast::get_lowest_layer(*ssl_stream_).expires_after(std::chrono::seconds(10));
  beast::get_lowest_layer(*ssl_stream_)
      .async_write_some(
          net::const_buffer(static_cast<char const *>(handshake_buffer.data()),
                            handshake_buffer.size()),
          [this](beast::error_code ec, std::size_t const sz) {
            on_first_handshake_initiated(ec, sz);
          });
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::on_first_handshake_initiated(
    error_code const ec, std::size_t const) {
  if (ec) { // could be timeout
    // spdlog::error("first handshake failed: {}", ec.message());
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  return read_first_handshake_result();
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<Derived,
                                ProxyProvider>::read_first_handshake_result() {
  reply_buffer.clear();
  reply_buffer.resize(2);
  beast::get_lowest_layer(*ssl_stream_).expires_after(std::chrono::seconds(10));
  beast::get_lowest_layer(*ssl_stream_)
      .async_read_some(
          net::mutable_buffer(reply_buffer.data(), reply_buffer.size()),
          [this](beast::error_code ec, std::size_t const sz) {
            on_first_handshake_response_received(ec, sz);
          });
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<Derived, ProxyProvider>::
    on_first_handshake_response_received(beast::error_code const ec,
                                         std::size_t const sz) {

  if (ec) {
    // spdlog::error(ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  char const *p1 = static_cast<char const *>(reply_buffer.data());
  BOOST_ASSERT(p1 != nullptr);

  auto const version = p1[0];
  auto const method = p1[1];
  if (version != SOCKS_VERSION_5) {
    // spdlog::error("version used on SOCKS server is not v5 but `{}`",
    // version);
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }

  if (method == SOCKS5_AUTH) {
    if (current_proxy_->username().empty()) {
      // spdlog::error("Proxy demanded username/password, but we don't have
      // it");
      current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
      return choose_next_proxy();
    }
    handshake_buffer.clear();

    auto &username = current_proxy_->username();
    auto &password = current_proxy_->password();
    std::size_t const buffers_to_write = username.size() + password.size() + 3;

    handshake_buffer.reserve(buffers_to_write);

    handshake_buffer.push_back(0x01); // auth version
    handshake_buffer.push_back(username.size());
    std::copy(username.cbegin(), username.cend(),
              std::back_inserter(handshake_buffer));
    handshake_buffer.push_back(password.size());
    std::copy(password.cbegin(), password.cend(),
              std::back_inserter(handshake_buffer));
    return beast::get_lowest_layer(*ssl_stream_)
        .async_write_some(
            net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
            [this](beast::error_code const ec, std::size_t const) {
              if (ec) {
                // spdlog::error("Unable to write to stream: {}", ec.message());
                return choose_next_proxy();
              }
              reply_buffer.clear();
              reply_buffer.resize(2);
              beast::get_lowest_layer(*ssl_stream_)
                  .expires_after(std::chrono::seconds(10));
              return beast::get_lowest_layer(*ssl_stream_)
                  .async_read_some(
                      net::mutable_buffer(reply_buffer.data(),
                                          reply_buffer.size()),
                      [this](beast::error_code ec, std::size_t const sz) {
                        on_auth_response_received(ec, sz);
                      });
            });
  }
  if (method == SOCKS5_AUTH_NONE) {
    return perform_sock5_second_handshake();
  }
  // spdlog::error("unsupported socks version");
  current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
  choose_next_proxy();
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<Derived, ProxyProvider>::
    on_auth_response_received(beast::error_code const ec,
                              std::size_t const sz) {
  if (ec) {
    // spdlog::error("[auth_response_received] {}", ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  char const *p1 = static_cast<char const *>(reply_buffer.data());
  BOOST_ASSERT(p1 != nullptr);
  auto const version = p1[0];
  auto const status = p1[1];
  if (version != 0x01) {
    // spdlog::error("unsupported authentication type");
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  if (status != 0x00) {
    // spdlog::error("authentication error");
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  perform_sock5_second_handshake();
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<
    Derived, ProxyProvider>::perform_sock5_second_handshake() {
  handshake_buffer.clear();

  std::string const host_name = hostname();
  std::size_t const bytes_to_write = host_name.size() + 7;

  handshake_buffer.reserve(std::max<std::size_t>(bytes_to_write, 22));

  handshake_buffer.push_back(SOCKS_VERSION_5);
  handshake_buffer.push_back(SOCKS_CMD_CONNECT);
  handshake_buffer.push_back(0x00);
  handshake_buffer.push_back(SOCKS5_ATYP_DOMAINNAME);
  handshake_buffer.push_back(host_name.size());
  std::copy(host_name.cbegin(), host_name.cend(),
            std::back_inserter(handshake_buffer));

  uint16_t const port = 443;
  handshake_buffer.push_back(((char *)&port)[1]);
  handshake_buffer.push_back(((char *)&port)[0]);

  beast::get_lowest_layer(*ssl_stream_)
      .async_write_some(
          net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
          [this](beast::error_code ec, std::size_t const) {
            if (ec) {
              // spdlog::error("[second_socks_write] {}", ec.message());
              current_proxy_assign_prop(
                  ProxyProvider::Property::ProxyUnresponsive);
              return choose_next_proxy();
            }
            return read_socks5_server_response();
          });
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<Derived,
                                ProxyProvider>::read_socks5_server_response() {
  reply_buffer.clear();
  reply_buffer.resize(10);
  beast::get_lowest_layer(*ssl_stream_).expires_after(std::chrono::seconds(10));
  beast::get_lowest_layer(*ssl_stream_)
      .async_read_some(
          net::mutable_buffer(reply_buffer.data(), reply_buffer.size()),
          [this](beast::error_code ec, std::size_t const sz) {
            on_handshake_response_received(ec, sz);
          });
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<Derived, ProxyProvider>::
    on_handshake_response_received(beast::error_code ec, std::size_t const sz) {
  if (ec) {
    // spdlog::error("[second_handshake_read_error] {} {}", ec.message(), sz);
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  char const *p1 = static_cast<char const *>(reply_buffer.data());
  BOOST_ASSERT(p1 != nullptr);

  using utilities::read_byte;
  auto const version = read_byte<uint8_t>(p1);
  read_byte<uint8_t>(p1); // response, not needed
  read_byte<uint8_t>(p1); // reserved byte, not needed
  int const a_type = read_byte<uint8_t>(p1);

  if (version != SOCKS_VERSION_5) {
    // spdlog::error("version supported is not sk5");
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }

  if (a_type != SOCKS5_ATYP_IPV4 && a_type != SOCKS5_ATYP_DOMAINNAME &&
      a_type != SOCKS5_ATYP_IPV6) {
    // spdlog::error("SOCKS5 general failure");
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }

  if (a_type == SOCKS5_ATYP_DOMAINNAME) {
    std::size_t const domain_length = read_byte<uint8_t>(p1);
    std::size_t const current_size = reply_buffer.size();
    reply_buffer.resize(current_size + domain_length - 3);
    beast::get_lowest_layer(*ssl_stream_)
        .expires_after(std::chrono::seconds(10));
    auto buf = static_cast<char *>(reply_buffer.data());
    return beast::get_lowest_layer(*ssl_stream_)
        .async_read_some(
            net::mutable_buffer(buf + current_size, domain_length - 3),
            [this](beast::error_code ec, std::size_t const sz) {
              process_ipv4_response(ec, sz);
            });
  }

  if (a_type == SOCKS5_ATYP_IPV6) {
    auto const curr_size = reply_buffer.size();
    reply_buffer.resize(curr_size + 12);
    auto buf = static_cast<char *>(reply_buffer.data());
    beast::get_lowest_layer(*ssl_stream_)
        .expires_after(std::chrono::seconds(10));
    return beast::get_lowest_layer(*ssl_stream_)
        .async_read_some(net::mutable_buffer(buf + curr_size, 12),
                         [this](beast::error_code ec, std::size_t const sz) {
                           process_ipv4_response(ec, sz);
                         });
  }
  return process_ipv4_response({}, sz);
}

template <typename Derived, typename ProxyProvider>
void socks5_https_socket_base_t<Derived, ProxyProvider>::process_ipv4_response(
    beast::error_code ec, std::size_t const sz) {
  if (ec) {
    // spdlog::error("[process_ipv4_response] {}", ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }

  char const *p1 = static_cast<char const *>(reply_buffer.data());
  BOOST_ASSERT(p1 != nullptr);

  using utilities::read_byte;
  read_byte<uint8_t>(p1); // version
  auto const rep = read_byte<uint8_t>(p1);
  read_byte<uint8_t>(p1);
  auto const a_type = read_byte<uint8_t>(p1);

  if (a_type == SOCKS5_ATYP_DOMAINNAME) {
    auto const domain_length = read_byte<uint8_t>(p1);
    std::string domain_name{};
    domain_name.reserve(domain_length);
    for (int i = 0; i != domain_length; ++i) {
      domain_name.push_back(read_byte<uint8_t>(p1));
    }
    auto const port = read_byte<uint16_t>(p1);
    (void)port;
    (void)domain_name;
    // spdlog::info("DN: {} and {}", domain_name, port);
  } else if (a_type == SOCKS5_ATYP_IPV4) {
    tcp::endpoint remote_endp(net::ip::address_v4(read_byte<uint32_t>(p1)),
                              read_byte<uint16_t>(p1));
    // spdlog::info("v4: {} and {}", remote_endp.address().to_string(),
    //             remote_endp.port());
    (void)remote_endp;
  } else if (a_type == SOCKS5_ATYP_IPV6) {
    net::ip::address_v6::bytes_type bytes;
    for (auto i = 0; i != 16; ++i) {
      bytes[i] = read_byte<uint8_t>(p1);
    }
    tcp::endpoint remote_endp(net::ip::address_v6(bytes),
                              read_byte<uint16_t>(p1));
    // spdlog::info("v6: {} and {}", remote_endp.address().to_string(),
    //             remote_endp.port());
    (void)remote_endp;
  } else {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  return perform_ssl_handshake();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::perform_ssl_handshake() {
  beast::get_lowest_layer(*ssl_stream_).expires_after(std::chrono::seconds(15));
  ssl_stream_->async_handshake(
      net::ssl::stream_base::client,
      [=](beast::error_code ec) { return on_ssl_handshake(ec); });
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::on_ssl_handshake(
    error_code ec) {
  if (ec.category() == net::error::get_ssl_category() &&
      ec.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ)) {
    return send_https_data();
  }
  if (ec) {
    // spdlog::error("SSL handshake: {}", ec.message());
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  send_https_data();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::perform_ssl_ritual() {
  auto const host_name = hostname();
  if (!SSL_set_tlsext_host_name(ssl_stream_->native_handle(),
                                host_name.c_str())) {
    beast::error_code ec{static_cast<int>(::ERR_get_error()),
                         net::error::get_ssl_category()};
    // spdlog::error("Unable to set TLS because: {}", ec.message());
  }
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::prepare_request_data(
    bool use_authentication_header) {
  static_cast<Derived *>(this)->prepare_request_data(use_authentication_header);
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::choose_next_proxy(
    bool const is_first_request) {
  send_count_ = 0;
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (!is_first_request) {
    ssl_stream_->async_shutdown([=](beast::error_code) {
      ssl_stream_.emplace(net::make_strand(io_), ssl_context_);
      perform_ssl_ritual();
      connect();
    });
  }
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::current_proxy_assign_prop(
    typename Proxy::Property property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  beast::get_lowest_layer(*ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  temp_list_ = {*current_proxy_};
  beast::get_lowest_layer(*ssl_stream_)
      .async_connect(temp_list_, [=](auto const &ec, auto const &ep_type) {
        on_connected(ec, ep_type);
      });
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::send_first_request() {
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

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::receive_data() {
  response_ = {};
  general_buffer_.emplace();

  beast::get_lowest_layer(*ssl_stream_).expires_after(std::chrono::seconds(10));
  http::async_read(*ssl_stream_, *general_buffer_, response_,
                   [this](beast::error_code ec, std::size_t const sz) {
                     on_data_received(ec, sz);
                   });
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::start_connect() {
  perform_ssl_ritual();
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::send_https_data() {
  beast::get_lowest_layer(*ssl_stream_).expires_after(std::chrono::seconds(10));
  http::async_write(*ssl_stream_, request_,
                    beast::bind_front_handler(
                        &socks5_https_socket_base_t::on_data_sent, this));
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::on_data_sent(
    beast::error_code ec, std::size_t const s) {
  if (ec) {
    // spdlog::error(ec.message());
    return resend_http_request();
  }
  receive_data();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  } else {
    send_https_data();
  }
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::close_socket() {
  beast::get_lowest_layer(*ssl_stream_).cancel();
  beast::error_code ec{};
  ec = {};
  ssl_stream_->async_shutdown([this](beast::error_code) {
    beast::get_lowest_layer(*ssl_stream_).close();
  });
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::close_stream() {
  beast::get_lowest_layer(*ssl_stream_).close();
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::send_next() {
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
    if (scans_per_ip_ != 0 && current_proxy_->number_scanned >= scans_per_ip_) {
      current_proxy_assign_prop(Proxy::Property::ProxyMaxedOut);
      return choose_next_proxy();
    }
    ++current_proxy_->number_scanned;
    return send_https_data();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::set_authentication_header() {
  prepare_request_data(true);
}

template <typename Derived, typename Proxy>
void socks5_https_socket_base_t<Derived, Proxy>::on_data_received(
    beast::error_code ec, std::size_t const s) {
  static_cast<Derived *>(this)->data_received(ec, s);
}

} // namespace wudi_server
