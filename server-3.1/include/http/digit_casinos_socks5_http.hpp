#pragma once

#include "socks5_http_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

class digit_casinos_socks5_base_t
    : public socks5_http<digit_casinos_socks5_base_t> {
  using super_class = socks5_http<digit_casinos_socks5_base_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  digit_casinos_socks5_base_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  virtual ~digit_casinos_socks5_base_t() {}
  virtual std::string hostname() const = 0;
  virtual std::string fqn() const = 0;
};

///////////////////////////////////////////////////////////////////////

class vip5_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "77336vip5.com"; }
  std::string fqn() const override { return "http://77336vip5.com"; }
};

class devils_horn_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "www.00852ttt.com"; }
  std::string fqn() const override { return "http://www.00852ttt.com"; }
};

class fourty_four_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "www.447349.com"; }
  std::string fqn() const override { return "http://www.447349.com"; }
};

class zed_three_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "www.z32689.net"; }
  std::string fqn() const override { return "http://www.z32689.net"; }
};

class js_three_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "www.js365555.com"; }
  std::string fqn() const override { return "http://www.js365555.com"; }
};

class sugar_raise_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "www.611435.com"; }
  std::string fqn() const override { return "http://www.611435.com"; }
};

class tiger_fortress_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "www.ddaa888.com"; }
  std::string fqn() const override { return "http://www.ddaa888.com"; }
};

class lebo_socket_socks5_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "302409.com"; }
  std::string fqn() const override { return "http://302409.com.com"; }
};

class wines_socks5_socket_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "wnsr9488.com"; }
  std::string fqn() const override { return "http://wnsr9488.com"; }
};

class xpuji_socks5_socket_t : public digit_casinos_socks5_base_t {
public:
  using digit_casinos_socks5_base_t::digit_casinos_socks5_base_t;
  std::string hostname() const override { return "s32689.net"; }
  std::string fqn() const override { return "http://s32689.net"; }
};

} // namespace wudi_server
