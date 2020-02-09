#include "session.hpp"
#include "websocket_updates.hpp"
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <gzip/decompress.hpp>
#include <spdlog/spdlog.h>

namespace wudi_server {
using namespace fmt::v6::literals;

void session::http_read_data() {
  buffer_.clear();
  empty_body_parser_.emplace();
  empty_body_parser_->body_limit(utilities::FiveMegabytes);
  beast::get_lowest_layer(tcp_stream_)
      .expires_after(std::chrono::minutes(args_.timeout_mins));
  http::async_read_header(
      tcp_stream_, buffer_, *empty_body_parser_,
      beast::bind_front_handler(&session::on_header_read, shared_from_this()));
}

void session::on_header_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream)
    return shutdown_socket();
  if (ec) {
    spdlog::info(ec.message());
    return error_handler(
        server_error(ec.message(), ErrorType::ServerError, string_request{}),
        true);
  } else {
    if (websocket::is_upgrade(empty_body_parser_->get())) {
      std::make_shared<websocket_updates>(tcp_stream_.release_socket())
          ->run(empty_body_parser_->release());
      return;
    }
    content_type_ = empty_body_parser_->get()[http::field::content_type];
    if (content_type_ == "application/json") {
      client_request_ =
          std::make_unique<http::request_parser<http::string_body>>(
              std::move(*empty_body_parser_));
      http::async_read(tcp_stream_, buffer_, *client_request_,
                       beast::bind_front_handler(&session::on_data_read,
                                                 shared_from_this()));
    } else if (content_type_ == "application/gzip") {
      dynamic_body_parser =
          std::make_unique<dynamic_request>(std::move(*empty_body_parser_));
      dynamic_body_parser->body_limit(utilities::FiveMegabytes);
      http::async_read(tcp_stream_, buffer_, *dynamic_body_parser,
                       beast::bind_front_handler(&session::binary_data_read,
                                                 shared_from_this()));
    } else {
      return error_handler(bad_request(
          "contact your admin for proper request format", string_request{}));
    }
  }
}

void session::binary_data_read(beast::error_code ec,
                               std::size_t bytes_transferred) {
  if (ec) {
    spdlog::error(ec.message());
    return error_handler(bad_request("invalid content-type", string_request{}));
  }
  auto &request = dynamic_body_parser->get();
  handle_requests(request);
}

void session::on_data_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream) { // end of connection
    return shutdown_socket();
  } else if (ec == http::error::body_limit) {
    return error_handler(
        server_error(ec.message(), ErrorType::ServerError, string_request{}),
        true);
  } else if (ec) {
    fputs(ec.message().c_str(), stderr);
    return error_handler(
        server_error(ec.message(), ErrorType::ServerError, string_request{}),
        true);
  } else {
    handle_requests(client_request_->get());
  }
}

void session::shutdown_socket() {
  beast::error_code ec{};
  beast::get_lowest_layer(tcp_stream_)
      .socket()
      .shutdown(asio::socket_base::shutdown_send, ec);
  beast::get_lowest_layer(tcp_stream_).close();
}

void session::error_handler(string_response &&response, bool close_socket) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  if (!close_socket) {
    http::async_write(tcp_stream_, *resp,
                      beast::bind_front_handler(&session::on_data_written,
                                                shared_from_this()));
  } else {
    http::async_write(
        tcp_stream_, *resp,
        [self = shared_from_this()](auto const err_c, std::size_t const) {
          self->shutdown_socket();
        });
  }
}

void session::on_data_written(beast::error_code ec,
                              std::size_t const bytes_written) {
  if (ec) {
    spdlog::error(ec.message());
    return;
  }
  resp_ = nullptr;
  http_read_data();
}

void session::login_handler(string_request const &request,
                            std::string_view const &optional_query) {
  spdlog::info("[login_handler] {}", request.target());
  if (request.method() == http::verb::get) {
    return error_handler(bad_request("POST username && password", request));
  }
  // respond to POST request
  try {
    json json_body = json::parse(request.body());
    json::object_t login_info = json_body.get<json::object_t>();
    auto username = login_info["username"].get<json::string_t>();
    auto password = login_info["password"].get<json::string_t>();
    auto [id, role] = db_connector->get_login_role(username, password);
    if (id == -1) {
      spdlog::error("[login.POST] {} {} is invalid", username, password);
      return error_handler(get_error("invalid username or password",
                                     ErrorType::Unauthorized,
                                     http::status::unauthorized, request));
    }
    return send_response(successful_login(id, role, request));
  } catch (std::exception const &exception) {
    spdlog::error(exception.what());
    return error_handler(bad_request("json object not valid", request));
  }
}

void session::index_page_handler(string_request const &request,
                                 std::string_view const &optional_query) {
  spdlog::info("[index_page_handler] {}", request.target());
  return error_handler(
      get_error("login", ErrorType::NoError, http::status::ok, request));
}

void session::upload_handler(string_request const &request,
                             std::string_view const &optional_query) {
  spdlog::info("[/upload_handler] {}", request.target());
  auto const query_pairs{split_optional_queries(optional_query)};
  static std::string uploads_directory{"uploads/"};
  auto const method = request.method();
  auto const find_query_key = [](auto &query_pairs,
                                 boost::string_view const &key) {
    return std::find_if(
        query_pairs.cbegin(), query_pairs.cend(),
        [=](string_view_pair const &str) { return str.first == key; });
  };

  if (method == http::verb::post) {
    bool const is_zipped = content_type_ == "application/gzip";
    auto &parser =
        is_zipped ? dynamic_body_parser->get() : client_request_->get();

    if (!std::filesystem::exists(uploads_directory)) {
      std::error_code ec{};
      std::filesystem::create_directory(uploads_directory, ec);
    }
    spdlog::info("[/upload_handler(type)] -> {}", content_type_);
    boost::string_view filename_view = parser["filename"];
    auto total_iter = find_query_key(query_pairs, "total");
    auto uploader_iter = find_query_key(query_pairs, "uploader");
    auto time_iter = find_query_key(query_pairs, "time");
    if (filename_view.empty() ||
        utilities::any_of(query_pairs, total_iter, uploader_iter, time_iter)) {
      return error_handler(bad_request("key parameters is missing", request));
    }

    auto &body = request.body();
    std::string_view temp_filename_view(filename_view.data(),
                                        filename_view.size());
    std::string file_path{
        "{}{}.txt"_format(uploads_directory, temp_filename_view)};
    std::size_t counter{1};
    while (std::filesystem::exists(file_path)) {
      file_path = "{}{}_{}.txt"_format(uploads_directory, temp_filename_view,
                                       counter++);
    }
    std::ofstream out_file{file_path};
    if (!out_file)
      return error_handler(
          server_error("unable to save file", ErrorType::ServerError, request));
    try {
      if (is_zipped) {
        counter = std::stol(total_iter->second.to_string());
        out_file << gzip::decompress(const_cast<char const *>(body.data()),
                                     body.size());
      } else {
        counter = 0;
        json json_root =
            json::parse(std::string_view(body.data(), body.size()));
        json::array_t list_of_numbers = json_root.get<json::array_t>();
        for (auto const &number : list_of_numbers) {
          out_file << number.get<json::string_t>() << "\n";
          ++counter;
        }
      }
      out_file.close();
      std::string time_str{};
      std::size_t const t{std::stoul(time_iter->second.to_string())};
      if (int const count = utilities::timet_to_string(time_str, t);
          count > 0) {
        time_str.resize(count);
      } else {
        time_str = time_iter->second.to_string();
      }
      utilities::UploadRequest upload_request{
          filename_view, file_path, uploader_iter->second,
          boost::string_view(time_str.data(), time_str.size()), counter};
      if (!db_connector->add_upload(upload_request)) {
        std::error_code ec{};
        std::filesystem::remove(file_path, ec);
        return error_handler(server_error("unable to save file to database",
                                          ErrorType::ServerError, request));
      }
      return send_response(success("ok", request));
    } catch (std::exception const &e) {
      if (out_file)
        out_file.close();
      spdlog::error(e.what());
      return error_handler(bad_request("unable to process file", request));
    }
  } else {
    std::vector<boost::string_view> ids{};
    auto const id_iter = find_query_key(query_pairs, "id");
    if (id_iter != query_pairs.cend()) {
      ids = utilities::split_string_view(id_iter->second, "|");
    }
    if (method == http::verb::get) { // GET method
      json json_result = db_connector->get_uploads(ids);
      return send_response(json_success(json_result, request));
    } else {
      // a DELETE request
      std::vector<utilities::UploadResult> uploads;
      if (!ids.empty() && ids[0] == "all") { // remove all
        uploads = db_connector->get_uploads(std::vector<boost::string_view>{});
        if (!db_connector->remove_websites({})) {
          return error_handler(server_error("unable to delete any record",
                                            ErrorType::ServerError, request));
        }
      } else {
        uploads = db_connector->get_uploads(ids);
        if (!db_connector->remove_websites(ids)) {
          return error_handler(server_error("unable to delete specified IDs",
                                            ErrorType::ServerError, request));
        }
      }
      for (auto const &upload : uploads) {
        if (std::filesystem::exists(upload.name_on_disk)) {
          std::filesystem::remove(upload.name_on_disk);
        }
      }
      return send_response(success("ok", request));
    }
  }
}

void session::handle_requests(string_request const &request) {
  std::string const request_target{utilities::decode_url(request.target())};
  auto method = request.method();
  if (request_target.empty())
    return index_page_handler(request, "");
  auto split = utilities::split_string_view(request_target, "?");
  if (auto iter = endpoint_apis_.get_rules(split[0]); iter.has_value()) {
    auto iter_end =
        iter.value()->second.verbs_.cbegin() + iter.value()->second.num_verbs_;
    auto found_iter =
        std::find(iter.value()->second.verbs_.cbegin(), iter_end, method);
    if (found_iter == iter_end) {
      return error_handler(method_not_allowed(request));
    }
    std::string_view query =
        split.size() > 1 ? std::string_view(split[1].data(), split[1].size())
                         : "";
    return iter.value()->second.route_callback_(request, query);
  } else {
    return error_handler(not_found(request));
  }
}

session::session(asio::ip::tcp::socket &&socket,
                 command_line_interface const &args,
                 std::shared_ptr<DatabaseConnector> db)
    : tcp_stream_{std::move(socket)}, args_{args}, db_connector{db} {
  add_endpoint_interfaces();
}

void session::add_endpoint_interfaces() {
  using http::verb;
  endpoint_apis_.add_endpoint(
      "/", {verb::get},
      std::bind(&session::index_page_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/login", {verb::get, verb::post},
      std::bind(&session::login_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/upload", {verb::post, verb::delete_, verb::get},
      std::bind(&session::upload_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/website", {verb::post, verb::get, verb::post},
      std::bind(&session::website_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/schedule_task", {verb::post, verb::get, verb::delete_},
      std::bind(&session::schedule_task_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/task", {verb::post, verb::get, verb::delete_},
      std::bind(&session::schedule_task_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

void session::schedule_task_handler(string_request const &request,
                                    std::string_view const &query) {
  using http::verb;
  using wudi_server::utilities::get_scheduled_tasks;

  auto const method = request.method();
  if (method == verb::post) {
    if (content_type_ != "application/json") {
      return error_handler(bad_request("invalid content-type", request));
    }
    try {
      json json_root = json::parse(request.body());
      json::object_t task_object = json_root.get<json::object_t>();
      json::array_t websites_ids = task_object["websites"].get<json::array_t>();
      json::array_t number_ids = task_object["numbers"].get<json::array_t>();
      utilities::ScheduledTask task{};
      task.scheduled_dt =
          static_cast<int>(task_object["date"].get<json::number_integer_t>());
      task.scheduler_id = static_cast<int>(
          task_object["scheduler"].get<json::number_integer_t>());

      for (auto const &number_id : number_ids) {
        task.number_ids.push_back(
            static_cast<int>(number_id.get<json::number_integer_t>()));
      }
      for (auto const &website_id : websites_ids) {
        task.website_ids.push_back(
            static_cast<int>(website_id.get<json::number_integer_t>()));
      }
      if (!db_connector->add_task(task)) {
        return error_handler(server_error("unable to schedule task",
                                          ErrorType::ServerError, request));
      }
      spdlog::info("Added new task{}", task.website_ids.size() == 1 ? "s" : "");
      auto &tasks{get_scheduled_tasks()};

      // split the main task into sub-tasks, based on the number of websites
      for (auto const &website_id : task.website_ids) {
        utilities::AtomicTask atom_task{task.task_id};
        atom_task.website_id = website_id;
        atom_task.number_ids = task.number_ids;
        tasks.push_back(std::move(atom_task));
      }
      json::object_t obj;
      obj["id"] = task.task_id;
      json j = obj;
      return send_response(json_success(j, request));
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return error_handler(bad_request("json object is not parsable", request));
    }
  } else if (method == http::verb::delete_) {
    return error_handler(
        server_error("not implemented yet", ErrorType::ServerError, request));
  } else {
    return error_handler(
        bad_request("only accessible through websocket", request));
  }
}

void session::website_handler(string_request const &request,
                              std::string_view const &query) {
  spdlog::info("[website_handler] {}", request.target());
  if (request.method() == http::verb::get) {
    json j = db_connector->get_websites({});
    return send_response(json_success(j, request));
  }
  if (content_type_ != "application/json") {
    spdlog::error("[website.POST] Wrong content-type: {}", content_type_);
    return error_handler(bad_request("expects a JSON body", request));
  }
  try {
    json json_root = json::parse(request.body());
    json::object_t obj = json_root.get<json::object_t>();
    auto address = obj["address"].get<json::string_t>();
    auto alias = obj["alias"].get<json::string_t>();
    if (!db_connector->add_website(address, alias)) {
      spdlog::error("[website_handler] could not add website");
      return error_handler(server_error("could not add website",
                                        ErrorType::ServerError, request));
    }
    return send_response(success("ok", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("cannot parse body", request));
  }
}

void session::run() { http_read_data(); }

string_response session::not_found(string_request const &request) {
  return get_error("url not found", ErrorType::ResourceNotFound,
                   http::status::not_found, request);
}

string_response session::server_error(std::string const &message,
                                      ErrorType type,
                                      string_request const &request) {
  return get_error(message, type, http::status::internal_server_error, request);
}

string_response session::bad_request(std::string const &message,
                                     string_request const &request) {
  return get_error(message, ErrorType::BadRequest, http::status::bad_request,
                   request);
}

string_response session::method_not_allowed(string_request const &req) {
  return get_error("method not allowed", ErrorType::MethodNotAllowed,
                   http::status::method_not_allowed, req);
}

string_response session::successful_login(int const id, int const role,
                                          string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = ErrorType::NoError;
  result_obj["message"] = "success";
  result_obj["id"] = id;
  result_obj["role"] = role;
  json result = result_obj;

  string_response response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response session::get_error(std::string const &error_message,
                                   utilities::ErrorType type,
                                   http::status status,
                                   string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = type;
  result_obj["message"] = error_message;
  json result = result_obj;

  string_response response{status, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response session::json_success(json const &body,
                                      string_request const &req) {
  string_response response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

string_response session::success(char const *message,
                                 string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = ErrorType::NoError;
  result_obj["message"] = message;
  json result{result_obj};

  string_response response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

void session::send_response(string_response &&response) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  http::async_write(
      tcp_stream_, *resp,
      beast::bind_front_handler(&session::on_data_written, shared_from_this()));
}

string_view_pair_list
session::split_optional_queries(std::string_view const &optional_query) {
  string_view_pair_list result{};
  if (!optional_query.empty()) {
    boost::string_view query{optional_query.data(), optional_query.size()};
    auto queries = utilities::split_string_view(query, "&");
    for (auto const &q : queries) {
      auto split = utilities::split_string_view(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace_back(split[0], split[1]);
    }
  }
  return result;
}
} // namespace wudi_server
