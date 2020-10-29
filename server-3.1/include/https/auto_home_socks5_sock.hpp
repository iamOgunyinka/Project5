#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace utilities {
std::string get_random_agent();
}

namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;

enum class request_type_e { GetRequest, PostRequest };

class auto_home_socks5_socket_t
    : public socks5_https_socket_base_t<auto_home_socks5_socket_t> {
private:
  request_type_e request_type = request_type_e::GetRequest;
  std::string session_id{};
  std::string user_agent{};
  int session_id_request_count{};
  int session_used_count{};

private:
  void process_get_response(beast::string_view const &body);
  void process_post_response(std::string const &body);
  void clear_session_cache();

public:
  using super_class = socks5_https_socket_base_t<auto_home_socks5_socket_t>;
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  auto_home_socks5_socket_t(ssl::context &ssl_context, Args &&...args)
      : super_class(ssl_context, std::forward<Args>(args)...) {}
  ~auto_home_socks5_socket_t() {}
  std::string hostname() const;
};
} // namespace wudi_server
