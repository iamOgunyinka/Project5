#include "websocket_updates.hpp"
#include "utilities.hpp"
#include <spdlog/spdlog.h>

namespace wudi_server {

void to_json(json &j, ws_subscription_result const &item) {
  j = json{{"id", item.task_id},
           {"processed", item.processed},
           {"status", item.status},
           {"total", item.total}};
}

void websocket_updates::on_websocket_accepted(beast::error_code const ec) {
  if (!ec)
    return read_websock_data();
  return on_error_occurred(ec);
}
void websocket_updates::run(
    beast::http::request<beast::http::empty_body> &&request) {
  boost::asio::dispatch(
      websock_stream_.get_executor(), [this, req = std::move(request)] {
        websock_stream_.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::server));
        websock_stream_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type &response) {
              response.set(beast::http::field::server, "wudi-websocket-server");
            }));
        websock_stream_.async_accept(req, [this](beast::error_code const ec) {
          on_websocket_accepted(ec);
        });
      });
}

void websocket_updates::on_error_occurred(beast::error_code const ec) {
  // perform error handling actions
  spdlog::error("WebSocket error[on_error_occurred]: {}", ec.message());
}

void websocket_updates::read_websock_data() {
  read_buffer_.consume(read_buffer_.size());
  read_buffer_.clear();
  websock_stream_.async_read(
      read_buffer_, std::bind(&websocket_updates::on_data_read, this,
                              std::placeholders::_1, std::placeholders::_2));
}

void websocket_updates::on_data_read(beast::error_code const ec,
                                     std::size_t const) {
  if (ec == websocket::error::closed) {
    return spdlog::info("A websocket session has been ended");
  }
  if (ec) {
    spdlog::error("websocket error: {}", ec.message());
    // send back some kind of error message
    return do_write(
        error(WebsocketErrorType::InvalidRequest, RequestType::None));
  }
  websock_stream_.text(websock_stream_.got_text());
  interpret_message(read_buffer_.data());
}

void websocket_updates::interpret_message(
    beast::flat_buffer::const_buffers_type const &buffer_data) {
  boost::string_view const data_view(
      static_cast<char const *>(buffer_data.data()), buffer_data.size());
  RequestType request_type = RequestType::None;
  try {
    json j = json::parse(data_view);
    json::object_t request = j.get<json::object_t>();
    auto const request_type_int = request["type"].get<json::number_integer_t>();
    request_type = static_cast<RequestType>(request_type_int);
    switch (request_type) {
    case RequestType::Login: {
      if (!process_login(request["login"].get<json::object_t>())) {
        return do_write(
            error(WebsocketErrorType::InvalidLoginCreds, request_type));
      }
      logged_in_ = true;
      return do_write(success("ok", ResponseType::LoginSuccessful));
    }
    case RequestType::Subscribe: {
      std::vector<ws_subscription_result> result{};
      auto task_id_list = request["subscribe"].get<json::array_t>();
      if (!process_subscription(task_id_list, result)) {
        return do_write(
            error(WebsocketErrorType::SubscriptionFailed, request_type));
      }
      start_ping_timer();
      return do_write(success(json(result).dump(), ResponseType::Update));
    }
    case RequestType::AddSubscription:
      if (!add_subscription(request["subscribe"].get<json::array_t>())) {
        return do_write(
            error(WebsocketErrorType::SubscriptionFailed, request_type));
      }
      return do_write(success("ok", ResponseType::SubscriptionSuccessful));
    default:
      return do_write(error(WebsocketErrorType::InvalidRequest, request_type));
    }
  } catch (std::exception const &e) {
    spdlog::error("websocket[interpret_message]: {}", e.what());
    do_write(error(WebsocketErrorType::InvalidRequest, request_type));
  }
}

void websocket_updates::start_ping_timer() {
  timer_.cancel();
  timer_.expires_from_now(boost::posix_time::seconds(20));
  timer_.async_wait([this](beast::error_code ec) {
    if (websock_stream_.is_open()) {
      websock_stream_.async_ping({}, [](beast::error_code) {});
      start_ping_timer();
    }
  });
}

bool websocket_updates::process_login(json::object_t login_info) {
  if (logged_in_)
    return true;
  try {
    json::string_t const username =
        login_info["username"].get<json::string_t>();
    json::string_t const password =
        login_info["password"].get<json::string_t>();
    auto db_connector = DatabaseConnector::GetDBConnector();
    return db_connector->get_login_role(username, password).first != -1;
  } catch (std::exception const &e) {
    spdlog::error("process login error: {}", e.what());
    return false;
  }
}

bool websocket_updates::process_subscription(
    json::array_t const &task_list,
    std::vector<ws_subscription_result> &result) {
  if (!logged_in_ || task_list.empty())
    return false;
  auto &task_queue = utilities::get_response_queue();
  result.clear();
  result.reserve(task_list.size());
  for (std::size_t i = 0; i != task_list.size(); ++i) {
    auto const user_task_id = task_list[i].get<json::number_integer_t>();
    auto task_range = task_queue.equal_range(user_task_id);
    for (auto beg = task_range.first; beg != task_range.second; ++beg) {
      uint32_t processed = beg->second->processed;
      uint32_t total = beg->second->total_numbers;
      uint32_t status = static_cast<int>(beg->second->operation_status);
      uint32_t task_id = beg->second->task_id;

      (void)beg->second->progress_signal.connect(
          [=](uint32_t task_id, uint32_t processed, uint32_t total) {
            on_task_progressed(std::distance(beg, task_range.second), task_id,
                               processed, total);
          });
      result.push_back({task_id, status, processed, total});
    }
  }
  if (!result.empty()) {
  }
  return true;
}

bool websocket_updates::add_subscription(json::array_t const &task_ids) {
  if (!logged_in_ || task_ids.empty())
    return false;
  return true;
}

void websocket_updates::on_task_progressed(uint32_t const total_sub_tasks,
                                           uint32_t const task_id,
                                           uint32_t const processed,
                                           uint32_t const total) {
  int const percentage = ((processed / total) * 100) / total_sub_tasks;
  json::object_t object;
  object["id"] = task_id;
  object["value"] = percentage;
  json::array_t arr;
  arr.push_back(object);
  do_write(success(json(arr).dump(), ResponseType::Update));
}

void websocket_updates::on_data_written(beast::error_code const ec) {
  if (ec == websocket::error::closed) {
    return websock_stream_.async_close(websocket::close_code::normal,
                                       [](beast::error_code) {});
  }
  if (ec) {
    return spdlog::error("websocket_updates::on_data_written: {}",
                         ec.message());
  }
  read_websock_data();
}

std::string websocket_updates::error(WebsocketErrorType error_type,
                                     RequestType request_type) {
  json::object_t error_object;
  error_object["status"] = static_cast<uint32_t>(ResponseType::Error);
  error_object["type"] = static_cast<uint32_t>(error_type);
  error_object["request"] = static_cast<uint32_t>(request_type);
  return json(error_object).dump();
}

std::string websocket_updates::success(std::string_view const message,
                                       ResponseType type) {
  json::object_t success_object;
  success_object["status"] = static_cast<uint32_t>(ResponseType::Success);
  success_object["type"] = static_cast<uint32_t>(type);
  success_object["what"] = message;
  return json(success_object).dump();
}

void websocket_updates::do_write(std::string_view const message) {
  write_buffer_ = std::string(message.data(), message.size());
  websock_stream_.async_write(
      net::buffer(write_buffer_),
      [this](beast::error_code ec, std::size_t const) { on_data_written(ec); });
}
} // namespace wudi_server
