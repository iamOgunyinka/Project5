#pragma once

#include "socks5_https_socket_base.hpp"

using tcp = boost::asio::ip::tcp;

namespace woody_server {

class lazada_socks5_t : public socks5_https_socket_base_t {
public:
  void onDataReceived(beast::error_code, std::size_t const) override;
  void prepareRequestData(bool useAuthHeader) override;
  [[nodiscard]] std::string hostname() const override;

  template <typename... Args>
  lazada_socks5_t(ssl::context &ssl_context, Args &&...args)
      : socks5_https_socket_base_t(ssl_context, std::forward<Args>(args)...) {}
  ~lazada_socks5_t() = default;
};
} // namespace woody_server
