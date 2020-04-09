//
// sync_client.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "socks4.hpp"
#include <array>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ip = net::ip;
using ip::tcp;

#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

class sock {
  net::io_context &io_context_;
  beast::tcp_stream tcp_stream_;
  std::vector<tcp::endpoint> endpoints_;
  std::size_t connect_count_{};
  std::vector<uint8_t> handshake_buffer{};
  std::uint8_t ipv4[4]{};
  char reply_buffer[512]{};
  beast::http::request<beast::http::empty_body> request_{};

private:
  void connect();
  void on_connected(beast::error_code const &,
                    tcp::resolver::results_type::endpoint_type);
  void on_response_received(beast::error_code, bool);
  void reconnect();
  void perform_socks5_handshake();
  void read_socks5_server_response(bool);
  void on_first_handshake_completed(beast::error_code const, std::size_t const);
  void perform_sock5_second_handshake();
  void send_data();

public:
  sock(net::io_context &io, tcp::endpoint const &ep)
      : io_context_{io}, tcp_stream_{net::make_strand(io_context_)}, endpoints_{
                                                                         ep} {}
  void start_connect();
};

void sock::start_connect() { connect(); }

void sock::read_socks5_server_response(bool is_first_handshake) {
  std::memset(reply_buffer, 0, 512);
  std::cout << "Performing read\n";
  tcp_stream_.expires_after(std::chrono::milliseconds(10'000));
  tcp_stream_.async_read_some(
      net::mutable_buffer(reply_buffer, 512),
      [this, is_first_handshake](beast::error_code ec, std::size_t const) {
        on_response_received(ec, is_first_handshake);
      });
}

void sock::on_response_received(beast::error_code ec, bool is_first_handshake) {
  if (ec) {
    std::cout << ec.message() << std::endl;
    return;
  }
  if (is_first_handshake) {
    std::cout << "first handshake completed\n";
    if (reply_buffer[1] != 0x00) {
      std::cout << "Could not finish handshake with server\n";
      return;
    }
    return perform_sock5_second_handshake();
  }
  std::cout << "Second HS done.\n";
  if (reply_buffer[1] != 0x00) {
    std::cout << "Second HS failed: 0x" << std::hex << reply_buffer[1]
              << std::dec << "\n";
    return;
  }
  std::cout << "Sending data\n";
  return send_data();
}

void sock::send_data() {
  namespace http = beast::http;

  request_.clear();
  request_.method(http::verb::get);
  request_.version(11);
  request_.target("http://47.252.72.9:4561/398574385943");
  request_.keep_alive(false);
  request_.set(http::field::host, "47.252.72.9:4561");
  request_.set(http::field::user_agent, "Joshua");
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

  std::cout << request_ << std::endl;

  tcp_stream_.expires_after(std::chrono::milliseconds(5'000));
  beast::http::async_write(tcp_stream_, request_,
                           [this](beast::error_code ec, std::size_t const) {
                             if (ec) {
                               std::cout << ec.message() << std::endl;
                               return;
                             }
                             std::cout << "Yay! We won." << std::endl;
                           });
}

void sock::connect() {
  tcp_stream_.expires_after(std::chrono::milliseconds(5'000));
  tcp_stream_.async_connect(endpoints_, std::bind(&sock::on_connected, this,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2));
}

void sock::on_connected(beast::error_code const &ec,
                        tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    return reconnect();
  }
  std::cout << "Connected\n";
  return perform_socks5_handshake();
}

void sock::perform_socks5_handshake() {
  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // method count
  handshake_buffer.push_back(0x00); // first method

  tcp_stream_.expires_after(std::chrono::milliseconds(5'000));
  tcp_stream_.async_write_some(
      net::const_buffer(reinterpret_cast<char const *>(handshake_buffer.data()),
                        handshake_buffer.size()),
      std::bind(&sock::on_first_handshake_completed, this,
                std::placeholders::_1, std::placeholders::_2));
}

void sock::on_first_handshake_completed(beast::error_code const ec,
                                        std::size_t const) {
  if (ec) {
    std::cout << ec.message() << std::endl;
    return;
  }
  return read_socks5_server_response(true);
}

void sock::perform_sock5_second_handshake() {
  std::cout << "Performing second handshake\n";
  if (sscanf("47.252.72.9", "%hhu.%hhu.%hhu.%hhu", &ipv4[0], &ipv4[1], &ipv4[2],
             &ipv4[3]) != 4) {
    throw std::runtime_error("Could not parse IPv4 address: 47.252.72.9");
  }

  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // TCP/IP
  handshake_buffer.push_back(0x00); // must be 0x00 always
  handshake_buffer.push_back(0x01); // IPv4
  handshake_buffer.push_back(ipv4[0]);
  handshake_buffer.push_back(ipv4[1]);
  handshake_buffer.push_back(ipv4[2]);
  handshake_buffer.push_back(ipv4[3]);
  auto http_endpoint =
      tcp::endpoint(boost::asio::ip::make_address("47.252.72.9"), 4561);
  // host to network short(htons)
  handshake_buffer.push_back(http_endpoint.port() >> 8);
  handshake_buffer.push_back(http_endpoint.port() & 0xff);

  tcp_stream_.expires_after(std::chrono::seconds(5));
  tcp_stream_.async_write_some(
      net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
      [this](beast::error_code ec, std::size_t const) {
        if (ec) {
          std::cout << ec.message() << std::endl;
          return;
        }
        return read_socks5_server_response(false);
      });
}

void sock::reconnect() {
  if (connect_count_ > 5)
    return;
  ++connect_count_;
  connect();
}

int main(int argc, char *argv[]) {
  if (argc != 3)
    return -1;

  std::string const address = argv[1];
  std::int16_t port = std::stoi(argv[2]);
  tcp::endpoint ep(ip::make_address(address), port);

  net::io_context io_context;

  auto c_socket = std::make_shared<sock>(io_context, ep);
  c_socket->start_connect();

  io_context.run();
  return 0;
}
