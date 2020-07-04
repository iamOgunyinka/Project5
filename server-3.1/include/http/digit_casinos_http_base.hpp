#pragma once

#include "http_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

class digit_casinos_http_base_t
    : public http_socket_base_t<digit_casinos_http_base_t> {
  using super_class = http_socket_base_t<digit_casinos_http_base_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  digit_casinos_http_base_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~digit_casinos_http_base_t() {}
  // fully qualifed name(FQN)
  virtual std::string fqn() const = 0;
  virtual std::string hostname() const = 0;
};

class devils_horn_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string hostname() const override { return "www.00852ttt.com:443"; }
  std::string fqn() const override { return "https://www.00852ttt.com"; }
};

class lottery81_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string fqn() const override { return "https://818685.com:8080"; }
  std::string hostname() const override { return "818685.com:8080"; }
};

class vip5_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string fqn() const override { return "http://77336vip5.com"; }
  std::string hostname() const override { return "77336vip5.com:80"; }
};

class vns_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string fqn() const override { return "https://www.330397.com"; }
  std::string hostname() const override { return "www.330397.com:443"; }
};

class fourty_four_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string fqn() const override { return "http://www.447349.com"; }
  std::string hostname() const override { return "www.447349.com:80"; }
};

class zed_three_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string fqn() const override { return "http://www.z32689.net"; }
  std::string hostname() const override { return "www.z32689.net:80"; }
};

class js_three_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string hostname() const override { return "www.js365555.com:80"; }
  std::string fqn() const override { return "http://www.js365555.com"; }
};

class sugar_raise_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string hostname() const override { return "www.611435.com:80"; }
  std::string fqn() const override { return "http://www.611435.com"; }
};

class tiger_fortress_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string hostname() const override { return "www.ddaa888.com:80"; }
  std::string fqn() const override { return "http://www.ddaa888.com"; }
};

class dragon_fish_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string hostname() const override { return "55222077.com:443"; }
  std::string fqn() const override { return "https://55222077.com"; }
};

class lebo_socket_http_t : public digit_casinos_http_base_t {
public:
  using digit_casinos_http_base_t::digit_casinos_http_base_t;
  std::string hostname() const override { return "302409.com:80"; }
  std::string fqn() const override { return "http://302409.com"; }
};

} // namespace wudi_server
