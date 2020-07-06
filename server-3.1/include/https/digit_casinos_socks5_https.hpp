#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

class digit_casinos_ssocks5_base_t
    : public socks5_https<digit_casinos_ssocks5_base_t> {
  using super_class = socks5_https<digit_casinos_ssocks5_base_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  digit_casinos_ssocks5_base_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  virtual ~digit_casinos_ssocks5_base_t() {}
  virtual std::string hostname() const = 0;
  virtual std::string fqn() const = 0;
};

///////////////////////////////////////////////////////////////////////

class vns_socket_socks5_t : public digit_casinos_ssocks5_base_t {
public:
  using digit_casinos_ssocks5_base_t::digit_casinos_ssocks5_base_t;
  std::string hostname() const override { return "www.330397.com"; }
  std::string fqn() const override { return "https://www.330397.com"; }
};

class lottery81_socket_socks5_t : public digit_casinos_ssocks5_base_t {
public:
  using digit_casinos_ssocks5_base_t::digit_casinos_ssocks5_base_t;
  std::string hostname() const override { return "818685.com"; }
  uint16_t port() const override { return 8080; }
  std::string fqn() const override { return "https://818685.com:8080"; }
};

class grand_lisboa_socks5_socket_t : public digit_casinos_ssocks5_base_t {
public:
  using digit_casinos_ssocks5_base_t::digit_casinos_ssocks5_base_t;
  std::string hostname() const override { return "11333xpj.com"; }
  uint16_t port() const override { return 8787; }
  std::string fqn() const override { return "https://11333xpj.com:8787"; }
};

class macau_baccarat_socks5_socket_t : public digit_casinos_ssocks5_base_t {
public:
  using digit_casinos_ssocks5_base_t::digit_casinos_ssocks5_base_t;
  std::string hostname() const override { return "55222077.com"; }
  std::string fqn() const override { return "https://55222077.com"; }
};

class lisboa_macau_socks5_socket_t : public digit_casinos_ssocks5_base_t {
public:
  using digit_casinos_ssocks5_base_t::digit_casinos_ssocks5_base_t;
  std::string hostname() const override { return "yy99345.am"; }
  std::string fqn() const override { return "https://yy99345.am"; }
};

} // namespace wudi_server
