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
#include <set>

namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
namespace net = boost::asio;
using nlohmann::json;

namespace wudi_server {

struct ws_subscription_result {
  struct sub_task_result {
    uint32_t status{};
    uint32_t processed{};
  };
  uint32_t task_id{};
  uint32_t total{};
  std::vector<sub_task_result> sub_tasks{};
};

enum class RequestType {
  Login = 0x1,
  Subscribe = 0x2,
  AddSubscription = 0x3,
  None = 0x4
};
enum class ResponseStatus { Error = 0x0, Success = 0x1 };
enum class ResponseType {
  UpdateSuccessful = 0x2,
  LoginSuccessful,
  SubscriptionSuccessful,
  NewUpdate
};

enum class WebsocketErrorType {
  InvalidRequest = 0xA,
  MustLogin,
  InvalidLoginCreds,
  SubscriptionFailed
};

void to_json(json &j, ws_subscription_result const &item);
void to_json(json &j, ws_subscription_result::sub_task_result const &item);

class websocket_updates
    : public std::enable_shared_from_this<websocket_updates> {
  websocket::stream<beast::tcp_stream> websock_stream_;
  beast::flat_buffer read_buffer_;
  std::string write_buffer_;
  bool logged_in_ = false;
  net::deadline_timer timer_;
  std::set<int> task_ids_;
  std::vector<ws_subscription_result> result_{};
  std::vector<std::unique_ptr<std::string>> queue_{};
  // std::vector<boost::signals2::connection> signal_connections_{};
  static std::string error(WebsocketErrorType, RequestType);

  template <typename T>
  static std::string success(T const &response, ResponseType type) {
    json::object_t success_object;
    success_object["status"] = static_cast<uint32_t>(ResponseStatus::Success);
    success_object["type"] = static_cast<uint32_t>(type);
    success_object["what"] = response;
    return json(success_object).dump();
  }

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
  void on_task_progressed(std::size_t, uint32_t, uint32_t, uint32_t);
  void on_data_written(beast::error_code ec);
  void send_message(std::string_view const message);
  void start_ping_timer();

public:
  websocket_updates(net::io_context &io, net::ip::tcp::socket &&tcp_socket)
      : websock_stream_{std::move(tcp_socket)}, timer_{io} {}
  void run(beast::http::request<beast::http::empty_body> &&request);
};

} // namespace wudi_server
