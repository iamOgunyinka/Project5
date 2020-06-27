#pragma once

#include "protocol.hpp"
#include "sockets_interface.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <optional>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using tcp = boost::asio::ip::tcp;

template <typename Derived, typename Proxy>
class socks5_http_socket_base_t : public sockets_interface {
  net::io_context &io_;
  std::optional<beast::tcp_stream> tcp_stream_;

  number_stream_t &numbers_;
  Proxy &proxy_provider_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  std::size_t connect_count_{};
  typename Proxy::value_type current_proxy_{nullptr};
  int const scans_per_ip_;
  std::vector<uint8_t> handshake_buffer{};
  std::vector<uint8_t> reply_buffer{};

protected:
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};

protected:
  void send_first_request();
  void read_first_handshake_result();
  void perform_socks5_handshake();
  void set_authentication_header();
  void on_first_handshake_response_received(beast::error_code,
                                            std::size_t const);
  void on_first_handshake_initiated(beast::error_code, std::size_t const);
  void on_auth_response_received(beast::error_code, std::size_t const);
  void read_socks5_server_response();
  void on_handshake_response_received(beast::error_code, std::size_t const);
  void process_ipv4_response(beast::error_code, std::size_t const);
  void perform_sock5_second_handshake();

  void close_socket();
  void connect();
  void receive_data();
  void reconnect();
  void choose_next_proxy(bool is_first_request = false);
  void on_data_sent(beast::error_code, std::size_t const);
  void current_proxy_assign_prop(typename Proxy::Property);
  void prepare_request_data(bool use_auth = false);
  void on_connected(beast::error_code);
  void send_http_data();
  void on_data_received(beast::error_code, std::size_t const);
  std::string hostname() const;
  void send_next();
  void close_stream() { tcp_stream_->close(); }

public:
  socks5_http_socket_base_t(bool &, net::io_context &, Proxy &,
                            number_stream_t &, int);
  void start_connect() override;
  virtual ~socks5_http_socket_base_t() {
    signal_.disconnect_all_slots();
    close_socket();
  }
};

template <typename Derived, typename Proxy>
socks5_http_socket_base_t<Derived, Proxy>::socks5_http_socket_base_t(
    bool &stopped, net::io_context &io_context, Proxy &proxy_provider,
    number_stream_t &numbers, int const scans_per_ip)
    : sockets_interface{}, io_{io_context}, tcp_stream_{net::make_strand(io_)},
      numbers_{numbers}, proxy_provider_{proxy_provider}, stopped_{stopped},
      scans_per_ip_(scans_per_ip) {}

template <typename Derived, typename ProxyProvider>
std::string
socks5_http_socket_base_t<Derived, ProxyProvider>::hostname() const {
  return static_cast<Derived const *>(this)->hostname();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::on_connected(
    beast::error_code ec) {
  if (ec) {
    return reconnect();
  }
  return perform_socks5_handshake();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::perform_socks5_handshake() {
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

  tcp_stream_->expires_after(std::chrono::milliseconds(5'000));
  tcp_stream_->async_write_some(
      net::const_buffer(reinterpret_cast<char const *>(handshake_buffer.data()),
                        handshake_buffer.size()),
      std::bind(&socks5_http_socket_base_t::on_first_handshake_initiated, this,
                std::placeholders::_1, std::placeholders::_2));
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    on_first_handshake_initiated(beast::error_code const ec,
                                 std::size_t const) {
  if (ec) { // could be timeout
    // spdlog::error("first handshake failed: {}", ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  return read_first_handshake_result();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::read_first_handshake_result() {
  reply_buffer.clear();
  reply_buffer.resize(2);
  tcp_stream_->expires_after(std::chrono::seconds(10));
  tcp_stream_->async_read_some(
      net::mutable_buffer(reply_buffer.data(), reply_buffer.size()),
      [this](beast::error_code ec, std::size_t const sz) {
        on_first_handshake_response_received(ec, sz);
      });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    on_first_handshake_response_received(beast::error_code const ec,
                                         std::size_t const sz) {
  if (ec) {
    // spdlog::error(ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }

  char const *p1 = reinterpret_cast<char const *>(reply_buffer.data());
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
    return tcp_stream_->async_write_some(
        net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
        [this](beast::error_code const ec, std::size_t const) {
          if (ec) {
            // spdlog::error("Unable to write to stream: {}", ec.message());
            current_proxy_assign_prop(
                ProxyProvider::Property::ProxyUnresponsive);
            return choose_next_proxy();
          }
          reply_buffer.clear();
          reply_buffer.resize(2);
          tcp_stream_->expires_after(std::chrono::seconds(10));
          tcp_stream_->async_read_some(
              net::mutable_buffer(reply_buffer.data(), reply_buffer.size()),
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
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    on_auth_response_received(beast::error_code const ec,
                              std::size_t const sz) {
  if (ec) {
    // spdlog::error("[auth_response_received] {}", ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  char const *p1 = reinterpret_cast<char const *>(reply_buffer.data());
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
void socks5_http_socket_base_t<
    Derived, ProxyProvider>::perform_sock5_second_handshake() {
  handshake_buffer.clear();

  std::string const host_name = hostname();
  std::size_t const bytes_to_write = host_name.size() + 7;
  handshake_buffer.reserve(std::max<std::size_t>(bytes_to_write, 22));

  handshake_buffer.push_back(SOCKS_VERSION_5);        // socks version
  handshake_buffer.push_back(SOCKS_CMD_CONNECT);      // CONNECT command
  handshake_buffer.push_back(0x00);                   // RESERVED
  handshake_buffer.push_back(SOCKS5_ATYP_DOMAINNAME); // use domain name
  handshake_buffer.push_back(host_name.size());
  std::copy(host_name.cbegin(), host_name.cend(),
            std::back_inserter(handshake_buffer));
  handshake_buffer.push_back(80 >> 8);
  handshake_buffer.push_back(80 & 0xFF);

  tcp_stream_->async_write_some(
      net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
      [this](beast::error_code ec, std::size_t const) {
        if (ec) {
          // spdlog::error("[second_socks_write] {}", ec.message());
          current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
          return choose_next_proxy();
        }
        return read_socks5_server_response();
      });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::read_socks5_server_response() {
  reply_buffer.clear();
  reply_buffer.resize(10);
  tcp_stream_->expires_after(std::chrono::seconds(10));
  tcp_stream_->async_read_some(
      net::mutable_buffer(reply_buffer.data(), reply_buffer.size()),
      [this](beast::error_code ec, std::size_t const sz) {
        on_handshake_response_received(ec, sz);
      });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    on_handshake_response_received(beast::error_code ec, std::size_t const sz) {
  if (ec) {
    // spdlog::error("[second_handshake_read_error] {}", ec.message());
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  char const *p1 = reinterpret_cast<char const *>(reply_buffer.data());
  BOOST_ASSERT(p1 != nullptr);

  auto const version = p1[0];
  auto const reserved_byte = p1[2];
  int const a_type = p1[3];

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
    std::size_t const domain_length = p1[4];
    std::size_t const current_size = reply_buffer.size();
    reply_buffer.resize(current_size + domain_length - 3);
    tcp_stream_->expires_after(std::chrono::seconds(10));
    return tcp_stream_->async_read_some(
        net::mutable_buffer(reply_buffer.data() + current_size,
                            domain_length - 3),
        [this](beast::error_code ec, std::size_t const sz) {
          process_ipv4_response(ec, sz);
        });
  }

  if (a_type == SOCKS5_ATYP_IPV6) {
    auto const curr_size = reply_buffer.size();
    reply_buffer.resize(curr_size + 12);
    tcp_stream_->expires_after(std::chrono::seconds(10));
    return tcp_stream_->async_read_some(
        net::mutable_buffer(reply_buffer.data() + curr_size, 12),
        [this](beast::error_code ec, std::size_t const sz) {
          process_ipv4_response(ec, sz);
        });
  }

  return process_ipv4_response({}, sz);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::process_ipv4_response(
    beast::error_code ec, std::size_t const sz) {
  if (ec) {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }

  char const *p1 = reinterpret_cast<char const *>(reply_buffer.data());
  BOOST_ASSERT(p1 != nullptr);
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
  } else if (a_type == SOCKS5_ATYP_IPV4) {
    tcp::endpoint remote_endp(net::ip::address_v4(read_byte<uint32_t>(p1)),
                              read_byte<uint16_t>(p1));
    (void)remote_endp;
  } else if (a_type == SOCKS5_ATYP_IPV6) {
    net::ip::address_v6::bytes_type bytes;
    for (auto i = 0; i != 16; ++i) {
      bytes[i] = read_byte<uint8_t>(p1);
    }
    tcp::endpoint remote_endp(net::ip::address_v6(bytes),
                              read_byte<uint16_t>(p1));
    (void)remote_endp;
  } else {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  return send_http_data();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::prepare_request_data(
    bool use_authentication_header) {
  static_cast<Derived *>(this)->prepare_request_data(use_authentication_header);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= socket_constants_e::max_retries) {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::choose_next_proxy(
    bool const is_first_request) {
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    // spdlog::error("error getting next endpoint");
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (!is_first_request) {
    close_stream();
    tcp_stream_.emplace(net::make_strand(io_));
    return connect();
  }
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    current_proxy_assign_prop(typename ProxyProvider::Property property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  tcp_stream_->expires_after(
      std::chrono::milliseconds(socket_constants_e::timeout_millisecs));
  tcp_stream_->async_connect(static_cast<tcp::endpoint>(*current_proxy_),
                             [=](auto const &ec) { on_connected(ec); });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::send_first_request() {
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
  } catch (empty_container_exception_t &) {
  }
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::receive_data() {
  tcp_stream_->expires_after(std::chrono::milliseconds(
      socket_constants_e::timeout_millisecs * 4)); // 4*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(*tcp_stream_, buffer_, response_,
                   beast::bind_front_handler(
                       &socks5_http_socket_base_t::on_data_received, this));
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::start_connect() {
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::send_http_data() {
  tcp_stream_->expires_after(
      std::chrono::milliseconds(socket_constants_e::timeout_millisecs));
  http::async_write(*tcp_stream_, request_,
                    beast::bind_front_handler(
                        &socks5_http_socket_base_t::on_data_sent, this));
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::on_data_sent(
    beast::error_code ec, std::size_t const s) {
  if (ec) {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  } else
    receive_data();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::close_socket() {
  tcp_stream_->cancel();
  signal_.disconnect_all_slots();
  beast::error_code ec{};
  beast::get_lowest_layer(*tcp_stream_)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(*tcp_stream_).socket().close(ec);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::send_next() {
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
      current_proxy_assign_prop(ProxyProvider::Property::ProxyMaxedOut);
      return choose_next_proxy();
    }
    ++current_proxy_->number_scanned;
    return send_http_data();
  } catch (empty_container_exception_t &) {
  }
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::set_authentication_header() {
  prepare_request_data(true);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::on_data_received(
    beast::error_code ec, std::size_t const s) {
  static_cast<Derived *>(this)->data_received(ec, s);
}

template <typename T, typename U>
using socks5_http = socks5_http_socket_base_t<T, U>;
} // namespace wudi_server
