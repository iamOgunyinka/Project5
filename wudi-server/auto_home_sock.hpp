#pragma once

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include "web_base.hpp"
#include <boost/asio.hpp>

namespace wudi_server {
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class auto_home_socket : public web_base<auto_home_socket> {
  static std::string const password_base64_hash;
  std::string const &address_;
  
protected:
public:
  void on_data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  auto_home_socket(net::io_context &io, safe_proxy &proxy_provider,
                   utilities::number_stream &numbers,
                   std::string const &address );
  ~auto_home_socket() = default;
};
} // namespace wudi_server
