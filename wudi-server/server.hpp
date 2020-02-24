#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <list>
#include <memory>

namespace wudi_server {
namespace utilities {
struct command_line_interface {
  std::size_t thread_count{};
  uint16_t port{80};
  uint16_t timeout_mins{15};
  std::string ip_address{"127.0.0.1"};
  std::string scheduled_snapshot;
  std::string launch_type{"development"};
  std::string database_config_filename{"../scripts/config/database.ini"};
};
} // namespace utilities
namespace asio = boost::asio;
namespace beast = boost::beast;

using utilities::command_line_interface;
class session;

class server : public std::enable_shared_from_this<server> {
  asio::io_context &io_context_;
  asio::ip::tcp::endpoint const endpoint_;
  asio::ip::tcp::acceptor acceptor_;
  bool is_open{false};
  command_line_interface const &args_;
  std::list<std::shared_ptr<session>> sessions_;

public:
  server(asio::io_context &context, command_line_interface const &args);
  void run();

private:
  void accept_connections();
  void on_connection_accepted(beast::error_code const &ec,
                              asio::ip::tcp::socket socket);
};
} // namespace wudi_server

#undef private_functions
