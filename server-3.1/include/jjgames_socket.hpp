#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace utilities {
std::string md5(std::string const &);
}

class jjgames_socket_t : public socks5_https<jjgames_socket_t> {
  void process_response(std::string const &);

  using super_class = socks5_https<jjgames_socket_t>;

public:
  template <typename... Args>
  jjgames_socket_t(ssl::context &ssl_context, Args &&... args)
      : super_class{ssl_context, std::forward<Args>(args)...} {}

  ~jjgames_socket_t() {}
  void prepare_request_data(bool use_auth = false);
  void data_received(beast::error_code, std::size_t const);
  std::string hostname() const;
};

} // namespace wudi_server
