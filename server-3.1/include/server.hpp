#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <list>
#include <memory>

namespace wudi_server {
namespace utilities {
struct command_line_interface_t {
  uint16_t port{3456};
  std::string ip_address{"127.0.0.1"};
  std::string launch_type{"development"};
  std::string database_config_filename{"../scripts/config/database.ini"};
};
} // namespace utilities
namespace asio = boost::asio;
namespace beast = boost::beast;

using utilities::command_line_interface_t;
class session_t;

class server_t : public std::enable_shared_from_this<server_t> {
  asio::io_context &io_context_;
  asio::ip::tcp::endpoint const endpoint_;
  asio::ip::tcp::acceptor acceptor_;
  bool is_open{false};
  command_line_interface_t const &args_;
  std::list<std::shared_ptr<session_t>> sessions_;

public:
  server_t(asio::io_context &context, command_line_interface_t const &args);
  void run();

private:
  void accept_connections();
  void on_connection_accepted(beast::error_code const &ec,
                              asio::ip::tcp::socket socket);
};
} // namespace wudi_server
