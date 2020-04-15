#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {

class jjgames_socket : public socks5_https_socket_base_t<jjgames_socket> {
  static std::string jjgames_hostname;
  std::size_t success_sent_count_{};
  void process_response(std::string const &);

public:
  jjgames_socket(bool &stopped, net::io_context &, proxy_provider_t &,
                 utilities::number_stream_t &, net::ssl::context &);
  ~jjgames_socket() {}
  void send_next() override;
  void prepare_request_data(bool use_auth = false);
  void on_data_received(beast::error_code, std::size_t const);
  std::string hostname() const;
};

} // namespace wudi_server
