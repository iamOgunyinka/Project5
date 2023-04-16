#pragma once

#include "socks5_https_socket_base.hpp"

using tcp = boost::asio::ip::tcp;

namespace woody_server {

class lazada_socks5_t : public socks5_https_socket_base_t {
public:
  void onDataReceived(beast::error_code, size_t) override;
  void prepareRequestData(bool useAuthHeader) override;
  [[nodiscard]] std::string hostname() const override;

  template <typename... Args>
  lazada_socks5_t(ssl::context &sslContext, Args &&...args)
      : socks5_https_socket_base_t(sslContext, std::forward<Args>(args)...) {}
  ~lazada_socks5_t() = default;
};
} // namespace woody_server
