#pragma once

#include "http_socket_base.hpp"

namespace woody_server {
using tcp = boost::asio::ip::tcp;

class lazada_http_t : public http_socket_base_t {
public:
  void onDataReceived(beast::error_code, std::size_t) const override;
  void prepareRequestData(bool use_authentication_header) override;

  template <typename... Args>
  explicit lazada_http_t(Args &&...args)
      : http_socket_base_t(std::forward<Args>(args)...) {}
  ~lazada_http_t() override = default;
};

} // namespace woody_server
