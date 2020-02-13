#pragma once

#include <boost/asio.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include <nlohmann/json.hpp>

namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
namespace net = boost::asio;
using nlohmann::json;

namespace wudi_server {

struct ws_subscription_result {
  uint32_t task_id;
  uint32_t status;
  uint32_t processed;
  uint32_t total;
};

enum class RequestType {
  Login = 0x1,
  Subscribe = 0x2,
  AddSubscription = 0x3,
  None = 0x4
};
enum class ResponseType {
  Error = 0x0,
  Success = 0x1,
  Update = 0x2,
  LoginSuccessful,
  SubscriptionSuccessful
};
enum class WebsocketErrorType {
  InvalidRequest = 0xA,
  MustLogin,
  InvalidLoginCreds,
  SubscriptionFailed
};

void to_json(json &j, ws_subscription_result const &item);

class websocket_updates
    : public std::enable_shared_from_this<websocket_updates> {
  websocket::stream<beast::tcp_stream> websock_stream_;
  beast::flat_buffer read_buffer_;
  std::string write_buffer_;
  bool logged_in_ = false;
  net::deadline_timer timer_;
  static std::string error(WebsocketErrorType, RequestType);
  static std::string success(std::string_view const message, ResponseType type);

private:
  bool process_login(json::object_t login_info);
  bool add_subscription(json::array_t const &task_ids);
  bool process_subscription(json::array_t const &task_ids,
                            std::vector<ws_subscription_result> &);
  void read_websock_data();
  void on_data_read(beast::error_code const ec, std::size_t const);
  void on_error_occurred(beast::error_code);
  void on_websocket_accepted(beast::error_code ec);
  void interpret_message(beast::flat_buffer::const_buffers_type const &data);
  void on_task_progressed(uint32_t, uint32_t, uint32_t, uint32_t);
  void on_data_written(beast::error_code ec);
  void do_write(std::string_view const message);
  void start_ping_timer();

public:
  websocket_updates(net::io_context &io, net::ip::tcp::socket &&tcp_socket)
      : websock_stream_{std::move(tcp_socket)}, timer_{io} {}
  void run(beast::http::request<beast::http::empty_body> &&request);
};

} // namespace wudi_server
