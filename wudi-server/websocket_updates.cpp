#include "websocket_updates.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace wudi_server {
void websocket_updates::on_websocket_accepted(beast::error_code const ec) {
  if (!ec)
    return read_websock_data();
  return on_error_occurred(ec);
}
void websocket_updates::run(
    beast::http::request<beast::http::empty_body> &&request) {
  websock_stream_.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::server));
  websock_stream_.set_option(
      websocket::stream_base::decorator([](websocket::response_type &response) {
        response.set(beast::http::field::server, "wudi-websocket-server");
      }));
  websock_stream_.async_accept(request, [this](beast::error_code const ec) {
    on_websocket_accepted(ec);
  });
}

void websocket_updates::on_error_occurred(beast::error_code const ec) {
  spdlog::error("WebSocket error[on_error_occurred]: {}", ec.message());
  // perform other error handling actions
}

void websocket_updates::read_websock_data() {
  read_buffer_ = {};
  websock_stream_.async_read(
      read_buffer_, std::bind(&websocket_updates::on_data_read, this,
                              std::placeholders::_1, std::placeholders::_2));
}

void websocket_updates::on_data_read(beast::error_code const ec,
                                     std::size_t const) {
  if (ec == websocket::error::closed) {
    spdlog::info("A websocket session has been ended");
    return;
  }
  if (ec) {
    spdlog::error("websocket error: {}", ec.message());
    // send back some kind of error message
    // websock_stream_.async_write_some();
    return;
  }
  websock_stream_.text(websock_stream_.got_text());
  interpret_message(read_buffer_.data());
}

void websocket_updates::interpret_message(
    beast::flat_buffer::const_buffers_type const &buffer_data) {
  boost::string_view const data_view(
      static_cast<char const *>(buffer_data.data()), buffer_data.size());
  try {
    json j = json::parse(data_view);
    json::object_t request = j.get<json::object_t>();
    std::string const type = request["type"].get<json::string_t>();
    json::number_integer_t const user_id =
        request["id"].get<json::number_integer_t>();

  } catch (std::exception const &e) {
    spdlog::error("websocket[interpret_message]: {}", e.what());
    json::object_t error_message;
    error_message["type"] = "error";
    error_message["what"] = "unable to interpret the requst sent";
    do_write(json{error_message}.dump());
  }
}
void websocket_updates::do_write(std::string const &message) {}
} // namespace wudi_server
