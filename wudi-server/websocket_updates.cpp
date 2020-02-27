#include "websocket_updates.hpp"
#include "database_connector.hpp"
#include "utilities.hpp"
#include <spdlog/spdlog.h>

namespace wudi_server {

void to_json(json &j, ws_subscription_result const &item) {
  j = json{{"processed", item.processed},
           {"status", item.status},
           {"id", item.task_id},
           {"total", item.total}};
}

websocket_updates::~websocket_updates() {
  beast::error_code ec{};
  websock_stream_.close(beast::websocket::close_code::going_away, ec);
  read_buffer_.consume(read_buffer_.size());
  timer_.cancel();
  task_ids_.clear();
  result_.clear();
  queue_.clear();
  spdlog::info("WS destroyed");
}

void websocket_updates::on_websocket_accepted(beast::error_code const ec) {
  if (!ec)
    return read_websock_data();
  return on_error_occurred(ec);
}

void websocket_updates::run(
    beast::http::request<beast::http::empty_body> &&request) {
  boost::asio::dispatch(websock_stream_.get_executor(), [self =
                                                             shared_from_this(),
                                                         req = std::move(
                                                             request)] {
    self->websock_stream_.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));
    self->websock_stream_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type &response) {
          response.set(beast::http::field::server, "wudi-websocket-server");
        }));
    self->websock_stream_.async_accept(req, [self](beast::error_code const ec) {
      self->on_websocket_accepted(ec);
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
      read_buffer_,
      std::bind(&websocket_updates::on_data_read, shared_from_this(),
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
    return send_message(
        error(ws_error_type_e::InvalidRequest, ws_request_type_e::None));
  }
  websock_stream_.text(websock_stream_.got_text());
  interpret_message(read_buffer_.data());
  read_buffer_.consume(read_buffer_.size());
  read_buffer_.clear();
  websock_stream_.async_read(
      read_buffer_,
      std::bind(&websocket_updates::on_data_read, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

void websocket_updates::interpret_message(
    beast::flat_buffer::const_buffers_type const &buffer_data) {

  boost::string_view const data_view(
      static_cast<char const *>(buffer_data.data()), buffer_data.size());
  ws_request_type_e request_type = ws_request_type_e::None;
  try {
    json j = json::parse(data_view);
    json::object_t request = j.get<json::object_t>();
    auto const request_type_int = request["type"].get<json::number_integer_t>();
    request_type = static_cast<ws_request_type_e>(request_type_int);
    switch (request_type) {
    case ws_request_type_e::Login: {
      if (!process_login(request["login"].get<json::object_t>())) {
        return send_message(
            error(ws_error_type_e::InvalidLoginCreds, request_type));
      }
      task_ids_.clear();
      result_.clear();
      logged_in_ = true;
      return send_message(success("ok", ws_response_type_e::LoginSuccessful));
    }
    case ws_request_type_e::Subscribe: {
      auto task_id_list = request["subscribe"].get<json::array_t>();
      if (!process_subscription(task_id_list, result_)) {
        return send_message(
            error(ws_error_type_e::SubscriptionFailed, request_type));
      }
      return send_message(
          success(json(result_), ws_response_type_e::UpdateSuccessful));
    }
    case ws_request_type_e::AddSubscription:
      if (!add_subscription(request["subscribe"].get<json::array_t>())) {
        return send_message(
            error(ws_error_type_e::SubscriptionFailed, request_type));
      }
      return send_message(
          success("ok", ws_response_type_e::SubscriptionSuccessful));
    default:
      return send_message(error(ws_error_type_e::InvalidRequest, request_type));
    }
  } catch (std::exception const &e) {
    spdlog::error("websocket[interpret_message]: {}", e.what());
    send_message(error(ws_error_type_e::InvalidRequest, request_type));
  }
}

bool websocket_updates::process_login(json::object_t login_info) {
  if (logged_in_)
    return true;
  try {
    json::string_t const username =
        login_info["username"].get<json::string_t>();
    json::string_t const password =
        login_info["password"].get<json::string_t>();
    auto db_connector = database_connector_t::s_get_db_connector();
    return db_connector->get_login_role(username, password).first != -1;
  } catch (std::exception const &e) {
    spdlog::error("process login error: {}", e.what());
    return false;
  }
}

bool websocket_updates::process_subscription(
    json::array_t const &task_list,
    std::vector<ws_subscription_result> &result) {
  using utilities::task_status_e;

  if (!logged_in_ || task_list.empty())
    return false;
  auto &task_queue = utilities::get_response_queue();
  for (std::size_t i = 0; i != task_list.size(); ++i) {
    auto const user_task_id = task_list[i].get<json::number_integer_t>();
    spdlog::info(user_task_id);
    if (task_ids_.find(user_task_id) != task_ids_.cend())
      continue;
    task_ids_.insert(user_task_id);
    auto task_iter = task_queue.find(user_task_id);
    if (task_iter == task_queue.cend())
      continue;
    result.push_back(ws_subscription_result{});
    auto &ws_result = result.back();
    ws_result.task_id = task_iter->second->task_id;
    ws_result.total = task_iter->second->total_numbers;
    ws_result.processed = task_iter->second->processed;
    ws_result.status = static_cast<int>(task_iter->second->operation_status);
    (void)task_iter->second->progress_signal().connect(
        [=](uint32_t task_id, uint32_t processed, task_status_e status) {
          on_task_progressed(task_id, processed, (uint32_t)status);
        });
  }
  return !result.empty();
} // namespace wudi_server

bool websocket_updates::add_subscription(json::array_t const &task_ids) {
  if (!logged_in_ || task_ids.empty())
    return false;
  return process_subscription(task_ids, result_);
}

void websocket_updates::on_task_progressed(uint32_t const task_id,
                                           uint32_t const processed,
                                           uint32_t const status) {
  auto iter = std::find_if(result_.begin(), result_.end(),
                           [task_id](ws_subscription_result const &item) {
                             return item.task_id == task_id;
                           });
  if (iter == result_.end())
    return;
  using utilities::task_status_e;
  iter->processed = processed;
  iter->status = status;
  return send_message(success(*iter, ws_response_type_e::NewUpdate));
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
  queue_.erase(queue_.begin());
  if (!queue_.empty()) {
    websock_stream_.async_write(
        net::buffer(*queue_.front()),
        [self = shared_from_this()](beast::error_code ec, std::size_t const) {
          self->on_data_written(ec);
        });
  }
}

std::string websocket_updates::error(ws_error_type_e error_type,
                                     ws_request_type_e request_type) {
  json::object_t error_object;
  error_object["status"] = static_cast<uint32_t>(ws_response_status_e::Error);
  error_object["type"] = static_cast<uint32_t>(error_type);
  error_object["request"] = static_cast<uint32_t>(request_type);
  return json(error_object).dump();
}

void websocket_updates::send_message(std::string_view const message) {
  queue_.push_back(std::make_unique<std::string>(
      std::string(message.data(), message.size())));
  if (queue_.size() > 1)
    return;
  websock_stream_.async_write(
      net::buffer(*queue_.front()),
      [self = shared_from_this()](beast::error_code ec, std::size_t const) {
        self->on_data_written(ec);
      });
}
} // namespace wudi_server
