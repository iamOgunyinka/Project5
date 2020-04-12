#include "jjgames_socket.hpp"

#include <boost/asio/ssl/rfc2818_verification.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace wudi_server {
enum Constants { PROXY_REQUIRES_AUTHENTICATION = 407 };
using utilities::request_handler;
using namespace fmt::v6::literals;

 std::string jjgames_socket::jjgames_hostname{"a4.srv.jj.cn"};
//std::string jjgames_socket::jjgames_hostname{"o2tvseries.com"};

jjgames_socket::jjgames_socket(bool &stopped, net::io_context &io_context,
                               proxy_provider_t &proxy_provider,
                               utilities::number_stream_t &numbers,
                               net::ssl::context &ssl_context)
    : io_{io_context}, ssl_stream_{net::make_strand(io_), ssl_context},
      numbers_{numbers}, proxy_provider_{proxy_provider}, stopped_{stopped} {}

struct time_data_t {
  uint64_t current_time{};
  uint64_t callback_number{};
};

void jjgames_socket::on_connected(beast::error_code ec,
                                  tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    return reconnect();
  }
  return perform_socks5_handshake();
}

void jjgames_socket::perform_socks5_handshake() {
  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // method count
  handshake_buffer.push_back(0x00); // first method

  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(5'000));
  beast::get_lowest_layer(ssl_stream_)
      .async_write_some(
          net::const_buffer(
              reinterpret_cast<char const *>(handshake_buffer.data()),
              handshake_buffer.size()),
          std::bind(&jjgames_socket::on_first_handshake_initiated, this,
                    std::placeholders::_1, std::placeholders::_2));
}

void jjgames_socket::retry_first_handshake() {
  ++handshake_retries_;
  if (handshake_retries_ >= utilities::MaxRetries) {
    handshake_retries_ = 0;
    choose_next_proxy();
    return connect();
  }
  perform_socks5_handshake();
}

void jjgames_socket::on_first_handshake_initiated(beast::error_code const ec,
                                                  std::size_t const) {
  if (ec == net::error::eof) { // connection closed
    choose_next_proxy();
    return connect();
  } else if (ec) { // could be timeout
    spdlog::error("on first handshake: {}", ec.message());
    return retry_first_handshake();
  }
  return read_socks5_server_response(true);
}

void jjgames_socket::read_socks5_server_response(
    bool const is_first_handshake) {
  std::memset(reply_buffer, 0, 512);
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(
          std::chrono::milliseconds(utilities::TimeoutMilliseconds * 3));
  beast::get_lowest_layer(ssl_stream_)
      .async_read_some(
          net::mutable_buffer(reply_buffer, 512),
          [this, is_first_handshake](beast::error_code ec, std::size_t const) {
            on_handshake_response_received(ec, is_first_handshake);
          });
}

void jjgames_socket::on_handshake_response_received(
    beast::error_code ec, bool const is_first_handshake) {
  if (ec) {
    // spdlog::error("On handshake response: {}", ec.message());
    choose_next_proxy();
    return connect();
  }
  if (is_first_handshake) {
    if (reply_buffer[1] != 0x00) {
      // std::cout << "Could not finish handshake with server\n";
      choose_next_proxy();
      return connect();
    }
    return perform_sock5_second_handshake();
  }
  if (reply_buffer[1] != 0x00) {
    // std::cout << "Second HS failed: 0x" << std::hex << reply_buffer[1]
    //          << std::dec << "\n";
    beast::get_lowest_layer(ssl_stream_).close();
    choose_next_proxy();
    return connect();
  }
  std::cout << "Performing SSL handshake\n";
  return perform_ssl_handshake();
}

void jjgames_socket::perform_ssl_handshake() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(10'000));
  ssl_stream_.async_handshake(
      net::ssl::stream_base::client,
      beast::bind_front_handler(&jjgames_socket::on_ssl_handshake, this));
}

void jjgames_socket::on_ssl_handshake(beast::error_code ec) {
  if ((ec.category() == boost::asio::error::get_ssl_category()) &&
      (ERR_GET_REASON(ec.value()) == SSL_R_SHORT_READ)) {
    return send_https_data();
  }
  if (ec) {
    spdlog::error("Handshake failed: {}", ec.message());
    choose_next_proxy();
    return connect();
  }
  send_https_data();
}

void jjgames_socket::perform_sock5_second_handshake() {
  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // TCP/IP
  handshake_buffer.push_back(0x00); // must be 0x00 always
  handshake_buffer.push_back(0x03); // Domain=0x03. IPv4 = 0x01. IPv6=0x04
  handshake_buffer.push_back(jjgames_hostname.size());
  for (auto const &hn : jjgames_hostname) {
    handshake_buffer.push_back(hn);
  }
  // host to network short(htons)
  handshake_buffer.push_back(443 >> 8);
  handshake_buffer.push_back(443 & 0xff);

  beast::get_lowest_layer(ssl_stream_).expires_after(std::chrono::seconds(5));
  beast::get_lowest_layer(ssl_stream_)
      .async_write_some(
          net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
          [this](beast::error_code ec, std::size_t const) {
            if (ec) {
              std::cout << ec.message() << std::endl;
              return;
            }
            return read_socks5_server_response(false);
          });
}

time_data_t get_time_data() {
  static std::random_device
      rd; // Will be used to obtain a seed for the random number engine
  static std::mt19937 gen(
      rd()); // Standard mersenne_twister_engine seeded with rd()
  static std::uniform_real_distribution<> dis(0.0, 1.0);
  uint64_t const current_time = std::time(nullptr) * 1'000;
  std::size_t const random_number = std::round(1e3 * dis(gen));
  std::uint64_t const callback_number =
      static_cast<std::size_t>(current_time + random_number);
  return time_data_t{current_time, callback_number};
}

void jjgames_socket::get_form_hash() {
  prepare_hash_request();
  send_https_data();
}

void jjgames_socket::prepare_hash_request() {
  type_ = type_sent_e::GetHash;
  auto const time_data = get_time_data();
  std::string const target =
      "/formhash/get_form_hash.php?content={}&_={}&callback=JSONP_{}"_format(
          current_number_, time_data.current_time, time_data.callback_number);
  request_.clear();
  request_.method(http::verb::get);
  request_.version(11);
  request_.target(target);
  request_.keep_alive(false);
  request_.set(http::field::host, "o2tvseries.com:443");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::referer,
               "https://www.jj.cn/reg/reg.html?type=phone");
  request_.set(http::field::accept_language, "en-US,en;q=0.5 --compressed");
  request_.set(http::field::cache_control, "no-cache");
  request_.set("sec-fetch-dest", "script");
  request_.set("sec-fetch-site", "same-site");
  request_.set("sec-fetch-mode", "no-cors");
  request_.body() = {};
  request_.prepare_payload();
  // std::cout << request_ << std::endl;
}

void jjgames_socket::prepare_request_data(bool use_authentication_header) {
  type_ = type_sent_e::Normal;
  auto const time_data = get_time_data();
  std::string const target =
      "/reg/check_loginname.php?regtype=2&t={}&n=1&loginname={}&callback="
      "JSONP_{}"_format(time_data.current_time, current_number_,
                        time_data.callback_number);

  //std::string const target = "/a";
  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(http::field::host, jjgames_hostname);
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set("sec-fetch-dest", "script");
  request_.set(beast::http::field::accept, "*/*");
  request_.set(http::field::referer,
               "https://www.jj.cn/reg/reg.html?type=phone");
  request_.set("sec-fetch-site", "same-site");
  request_.set("sec-fetch-mode", "no-cors");
  request_.set( http::field::accept_language, "en-US,en;q=0.5 --compressed" );
  request_.set( http::field::cache_control, "no-cache" );
  request_.set( http::field::referer,
      "https://www.jj.cn/reg/reg.html?type=phone" );
  request_.set(http::field::accept_language, "en-US,en;q=0.5 --compressed");
  request_.body() = {};
  request_.prepare_payload();
  // std::cout << request_ << std::endl;
}

void jjgames_socket::process_gethash_response(std::string const &message_body) {
  try {
    json json_response = json::parse(message_body);
    json::object_t object = json_response.get<json::object_t>();
    json::object_t data = object["DATA"].get<json::object_t>();
    std::string const js_src = data["js_src"].get<json::string_t>();
    std::string const form_hash = data["formhash"].get<json::string_t>();

  } catch (std::exception const &e) {
  }
}

void jjgames_socket::process_sethash_response() {}

void jjgames_socket::process_normal_response(std::string const &message_body) {
  std::cout << message_body << std::endl;
  try {
    // badly formed JSON response, server's fault -> there's nothing we can do
    // about that
    json json_response = json::parse(message_body);
    json::object_t object = json_response.get<json::object_t>();
    bool const status = object["REV"].get<json::boolean_t>();
    if (status) {
      signal_(search_result_type_e::NotRegistered, current_number_);
      // return get_form_hash();
    } else {
      static char const *const already_registered{
          "%E8%AF%A5%E6%89%8B%E6%9C%BA%E5%8F%B7%E5%B7%B2%E6%B3%A8%E5%86%8C%EF%"
          "BC%8C%E8%AF%B7%E6%9B%B4%E6%8D%A2"};
      std::string const server_message = object["MSG"].get<json::string_t>();
      if (server_message.find(already_registered) != std::string::npos) {
        signal_(search_result_type_e::Registered, current_number_);
      } else {
        type_ = type_sent_e::Normal;
        choose_next_proxy();
        return connect();
      }
    }
  } catch (std::exception const &) {
    type_ = type_sent_e::Normal;
    choose_next_proxy();
    return connect();
  }

  ++success_sent_count_;
  current_number_.clear();
  send_next();
}

void jjgames_socket::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
  }
  connect();
}

void jjgames_socket::choose_next_proxy() {
  send_count_ = 0;
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    spdlog::error("error getting next endpoint");
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
}

void jjgames_socket::current_proxy_assign_prop(ProxyProperty property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

void jjgames_socket::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  temp_list_ = {*current_proxy_};
  beast::get_lowest_layer(ssl_stream_)
      .async_connect(temp_list_, beast::bind_front_handler(
                                     &jjgames_socket::on_connected, this));
}

void jjgames_socket::send_first_request() {
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

void jjgames_socket::receive_data() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds *
                                               4)); // 4*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(
      ssl_stream_, buffer_, response_,
      beast::bind_front_handler(&jjgames_socket::on_data_received, this));
}

void jjgames_socket::start_connect() {
  if (!SSL_set_tlsext_host_name(ssl_stream_.native_handle(),
                                jjgames_hostname.c_str())) {
    beast::error_code ec{static_cast<int>(::ERR_get_error()),
                         net::error::get_ssl_category()};
    return spdlog::error("Unable to set TLS because: {}", ec.message());
  }
  ssl_stream_.set_verify_mode(net::ssl::verify_none);
  choose_next_proxy();
  if (current_proxy_)
    send_first_request();
}

void jjgames_socket::send_https_data() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(
      ssl_stream_, request_,
      beast::bind_front_handler(&jjgames_socket::on_data_sent, this));
}

void jjgames_socket::on_data_sent(beast::error_code ec, std::size_t const s) {
  if (ec) {
    resend_http_request();
  } else
    receive_data();
}

void jjgames_socket::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
    connect();
  } else {
    send_https_data();
  }
}

void jjgames_socket::close_socket() {
  beast::get_lowest_layer(ssl_stream_).cancel();
  beast::error_code ec{};
  ec = {};
  // beast::get_lowest_layer(ssl_stream_).close();
  ssl_stream_.async_shutdown([this](beast::error_code) {
    beast::get_lowest_layer(ssl_stream_).close();
  });
}

void jjgames_socket::send_next() {
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
    if (success_sent_count_ == 8) {
      success_sent_count_ = 0;
      choose_next_proxy();
      return connect();
    }
    send_https_data();
  } catch (utilities::empty_container_exception_t &) {
  }
}

void jjgames_socket::set_authentication_header() { prepare_request_data(true); }

void jjgames_socket::on_data_received(beast::error_code ec, std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      beast::get_lowest_layer(ssl_stream_).close();
    }
    spdlog::error(ec.message());
    choose_next_proxy();
    type_ = type_sent_e::Normal;
    return connect();
  }
  std::size_t const status_code = response_.result_int();
  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    return connect();
  }

  auto &response_body{response_.body()};
  std::cout << response_body << std::endl;

  using utilities::search_result_type_e;
  std::size_t opening_brace_index = response_body.find_first_of('{');
  std::size_t closing_brace_index = response_body.find_last_of('}');
  if ((opening_brace_index == std::string::npos ||
       closing_brace_index == std::string::npos) &&
      type_ != type_sent_e::SetHash) {
    type_ = type_sent_e::Normal;
    choose_next_proxy();
    return connect();
  }
  std::string body_temp{};
  std::cout << body_temp << std::endl;
  if (type_ == type_sent_e::SetHash) {
    body_temp.clear();
  } else {
    body_temp = std::string(
        response_body.begin() + static_cast<int>(opening_brace_index),
        response_body.begin() + static_cast<int>(closing_brace_index + 1));
  }

  switch (type_) {
  case type_sent_e::Normal:
    return process_normal_response(body_temp);
  case type_sent_e::GetHash:
    return process_gethash_response(body_temp);
  case type_sent_e::SetHash:
  default:
    return process_sethash_response();
  }
}
} // namespace wudi_server
