#include "session.hpp"
#include "database_connector.hpp"
#include "websocket_updates.hpp"
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <gzip/decompress.hpp>
#include <spdlog/spdlog.h>

namespace wudi_server {

using namespace fmt::v6::literals;

rule_t::rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback)
    : num_verbs_{verbs.size()}, route_callback_{std::move(callback)} {
  if (verbs.size() > 5)
    throw std::runtime_error{"maximum number of verbs is 5"};
  for (int i = 0; i != verbs.size(); ++i) {
    verbs_[i] = *(verbs.begin() + i);
  }
}

void endpoint_t::add_endpoint(std::string const &route,
                              std::initializer_list<http::verb> verbs,
                              callback_t &&callback) {
  if (route.empty() || route[0] != '/')
    throw std::runtime_error{"A valid route starts with a /"};
  endpoints.emplace(route, rule_t{std::move(verbs), std::move(callback)});
}

std::optional<endpoint_t::iterator>
endpoint_t::get_rules(std::string const &target) {
  auto iter = endpoints.find(target);
  if (iter == endpoints.end())
    return std::nullopt;
  return iter;
}

std::optional<endpoint_t::iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(std::string(target.data(), target.size()));
}

void session::http_read_data() {
  buffer_.clear();
  empty_body_parser_.emplace();
  empty_body_parser_->body_limit(utilities::FiveMegabytes);
  beast::get_lowest_layer(tcp_stream_)
      .expires_after(std::chrono::minutes(utilities::MaxRetries));
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
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  } else {
    if (websocket::is_upgrade(empty_body_parser_->get())) {
      auto ws = std::make_shared<websocket_updates>(
          io_context_, tcp_stream_.release_socket());
      websockets_.push_back(ws);
      ws->run(empty_body_parser_->release());
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
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
        true);
  } else if (ec) {
    fputs(ec.message().c_str(), stderr);
    return error_handler(
        server_error(ec.message(), error_type_e::ServerError, string_request{}),
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
    return spdlog::error(ec.message());
  }
  resp_ = nullptr;
  http_read_data();
}

void session::login_handler(string_request const &request,
                            url_query const &optional_query) {
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
    auto [id, role] =
        database_connector_t::s_get_db_connector()->get_login_role(username,
                                                                   password);
    if (id == -1) {
      spdlog::error("[login.POST] {} {} is invalid", username, password);
      return error_handler(get_error("invalid username or password",
                                     error_type_e::Unauthorized,
                                     http::status::unauthorized, request));
    }
    return send_response(successful_login(id, role, request));
  } catch (std::exception const &exception) {
    spdlog::error(exception.what());
    return error_handler(bad_request("json object not valid", request));
  }
}

void session::index_page_handler(string_request const &request,
                                 url_query const &) {
  spdlog::info("[index_page_handler] {}", request.target());
  return error_handler(
      get_error("login", error_type_e::NoError, http::status::ok, request));
}

void session::upload_handler(string_request const &request,
                             url_query const &optional_query) {
  spdlog::info("[/upload_handler] {}", request.target());
  static std::string uploads_directory{"uploads/"};
  auto const method = request.method();
  auto db_connector = database_connector_t::s_get_db_connector();

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
    auto total_iter = optional_query.find("total");
    auto uploader_iter = optional_query.find("uploader");
    auto time_iter = optional_query.find("time");
    if (filename_view.empty() || utilities::any_of(optional_query, total_iter,
                                                   uploader_iter, time_iter)) {
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
      return error_handler(server_error("unable to save file",
                                        error_type_e::ServerError, request));
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
      utilities::upload_request_t upload_request{
          filename_view, file_path, uploader_iter->second,
          boost::string_view(time_str.data(), time_str.size()), counter};
      if (!db_connector->add_upload(upload_request)) {
        std::error_code ec{};
        std::filesystem::remove(file_path, ec);
        return error_handler(server_error("unable to save file to database",
                                          error_type_e::ServerError, request));
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
    auto const id_iter = optional_query.find("id");
    if (id_iter != optional_query.cend()) {
      using utilities::split_string_view;
      ids = split_string_view(id_iter->second, "|");
    }
    // GET method
    if (method == http::verb::get) {
      json json_result = db_connector->get_uploads(ids);
      return send_response(json_success(json_result, request));
    } else {
      // a DELETE request
      std::vector<utilities::upload_result_t> uploads;
      if (!ids.empty() && ids[0] == "all") { // remove all
        uploads = db_connector->get_uploads(std::vector<boost::string_view>{});
        if (!db_connector->remove_uploads({})) {
          return error_handler(server_error("unable to delete any record",
                                            error_type_e::ServerError,
                                            request));
        }
      } else {
        uploads = db_connector->get_uploads(ids);
        if (!db_connector->remove_uploads(ids)) {
          return error_handler(server_error("unable to delete specified IDs",
                                            error_type_e::ServerError,
                                            request));
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
  if (request_target.empty())
    return index_page_handler(request, {});
  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utilities::split_string_view(request_target_view, "?");
  if (auto iter = endpoint_apis_.get_rules(split[0]); iter.has_value()) {
    auto iter_end =
        iter.value()->second.verbs_.cbegin() + iter.value()->second.num_verbs_;
    auto found_iter =
        std::find(iter.value()->second.verbs_.cbegin(), iter_end, method);
    if (found_iter == iter_end) {
      return error_handler(method_not_allowed(request));
    }
    boost::string_view const query_string = split.size() > 1 ? split[1] : "";
    auto url_query_{split_optional_queries(query_string)};
    return iter.value()->second.route_callback_(request, url_query_);
  } else {
    return error_handler(not_found(request));
  }
}

session::session(net::io_context &io, asio::ip::tcp::socket &&socket)
    : io_context_{io}, tcp_stream_{std::move(socket)} {
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
  endpoint_apis_.add_endpoint(
      "/download", {verb::post},
      std::bind(&session::download_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/stop", {verb::post},
      std::bind(&session::stop_tasks_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/start", {verb::post},
      std::bind(&session::restart_tasks_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
  endpoint_apis_.add_endpoint(
      "/remove", {verb::post},
      std::bind(&session::remove_tasks_handler, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

void session::download_handler(string_request const &request,
                               url_query const &optional_query) {
  // we only handle POST(application/json) requests here, not any other.
  if (content_type_ != "application/json") {
    return error_handler(bad_request("invalid content-type", request));
  }
  try {
    json json_root = json::parse(request.body());
    json::array_t task_object = json_root.get<json::array_t>();

  } catch (std::exception const &e) {
    spdlog::error("exception in `download_handler`: {}", e.what());
    return error_handler(
        bad_request("unable to process JSON request", request));
  }
}

void session::remove_tasks_handler(string_request const &req,
                                   url_query const &optional_query) {
  if (content_type_ != "application/json")
    return error_handler(bad_request("invalid content-type", req));
  try {
    using utilities::task_status_e;
    auto user_id_iter = optional_query.find("id");
    if (user_id_iter == optional_query.cend())
      return error_handler(bad_request("user id is missing", req));
    json json_root = json::parse(req.body());
    json::object_t task_type_object = json_root.get<json::object_t>();
    if (task_type_object.empty()) {
      return error_handler(bad_request("empty task list", req));
    }
    json::array_t const user_specified_tasks =
        task_type_object["id"].get<json::array_t>();
    std::vector<uint32_t> tasks{};
    tasks.reserve(user_specified_tasks.size());
    for (auto const &task : user_specified_tasks) {
      tasks.emplace_back(task.get<json::number_integer_t>());
    }
    [[maybe_unused]] auto stopped_tasks = stop_running_tasks_impl(tasks, false);
    delete_stopped_tasks_impl(tasks);
    delete_other_tasks_impl(user_id_iter->second, tasks);
    return send_response(json_success(tasks, req));
  } catch (std::exception const &e) {
    spdlog::error("exception in remove_tasks_handler: {}", e.what());
    return error_handler(bad_request("unable to remove tasks", req));
  }
}

void session::delete_stopped_tasks_impl(
    std::vector<uint32_t> const &user_stopped_tasks) {
  using utilities::atomic_task_t;
  using utilities::remove_file;
  auto db_connector = database_connector_t::s_get_db_connector();
  std::vector<atomic_task_t> tasks{};
  db_connector->get_stopped_tasks(user_stopped_tasks, tasks);
  for (auto &task : tasks) {
    auto &stopped_task = std::get<atomic_task_t::stopped_task>(task.task);
    remove_file(stopped_task.input_filename);
    remove_file(stopped_task.not_ok_filename);
    remove_file(stopped_task.ok_filename);
    remove_file(stopped_task.unknown_filename);
  }
  db_connector->remove_stopped_tasks(user_stopped_tasks);
  return;
}

void session::delete_other_tasks_impl(boost::string_view const user_id,
                                      std::vector<uint32_t> const &all_tasks) {
  auto db_connector = database_connector_t::s_get_db_connector();
  db_connector->remove_filtered_tasks(user_id, all_tasks);
  auto &response_queue = utilities::get_response_queue();
  for (auto const &task : all_tasks) {
    response_queue.erase(task);
  }
}

std::vector<uint32_t>
session::stop_running_tasks_impl(std::vector<uint32_t> const &task_id_list,
                                 bool save_state) {
  using utilities::task_status_e;

  auto &running_tasks = utilities::get_response_queue();
  std::vector<uint32_t> stopped_tasks{};
  stopped_tasks.reserve(task_id_list.size());
  for (std::size_t index = 0; index != task_id_list.size(); ++index) {
    auto const &task_id = task_id_list[index];
    spdlog::info("Trying to stop: {}", task_id);
    if (auto iter = running_tasks.equal_range(task_id);
        iter.first != running_tasks.cend()) {
      stopped_tasks.push_back(task_id);
      for (auto beg = iter.first; beg != iter.second; ++beg) {
        if (beg->second->operation_status == task_status_e::Ongoing) {
          beg->second->save_state() = save_state;
          beg->second->stop();
        }
      }
    }
  }
  return stopped_tasks;
}

void session::stop_tasks_handler(string_request const &request,
                                 url_query const &optional_query) {
  if (content_type_ != "application/json")
    return error_handler(bad_request("invalid content-type", request));
  try {
    using utilities::atomic_task_t;
    using utilities::task_status_e;
    json json_root = json::parse(request.body());
    json::array_t const task_id_list = json_root.get<json::array_t>();
    if (task_id_list.empty()) {
      return error_handler(bad_request("empty task list", request));
    };
    std::vector<uint32_t> tasks{};
    tasks.reserve(task_id_list.size());
    for (auto const &task : task_id_list) {
      tasks.emplace_back(task.get<json::number_integer_t>());
    }
    auto &queued_task = utilities::get_scheduled_tasks();
    auto interesting_tasks = queued_task.remove_task(
        tasks, [](atomic_task_t &task, std::vector<uint32_t> const &ids) {
          for (auto const &id : ids)
            if (task.task_id == id)
              return true;
          return false;
        });
    auto db_connector = database_connector_t::s_get_db_connector();
    for (auto const &task : interesting_tasks) {
      atomic_task_t stopped_task{};
      stopped_task.type_ = atomic_task_t::task_type::stopped;
      stopped_task.task_id = task.task_id;
      stopped_task.website_id = task.website_id;
      stopped_task.processed = 0;
      stopped_task.total = task.total;
      auto &st_task = stopped_task.task.emplace<atomic_task_t::stopped_task>();
      st_task.not_ok_filename = st_task.ok_filename = st_task.unknown_filename =
          st_task.website_address = "{free}";

      auto const &fresh_task = std::get<atomic_task_t::fresh_task>(task.task);
      st_task.input_filename =
          utilities::intlist_to_string(fresh_task.number_ids);
      db_connector->save_stopped_task(stopped_task);
    }
    return send_response(json_success(stop_running_tasks_impl(tasks), request));
  } catch (std::exception const &e) {
    spdlog::error("exception in stop_tasks: {}", e.what());
    return error_handler(bad_request("unable to stop tasks", request));
  }
}

void session::restart_tasks_handler(string_request const &request,
                                    url_query const &optional_query) {
  if (content_type_ != "application/json")
    return error_handler(bad_request("invalid content-type", request));
  try {
    using utilities::task_status_e;
    json json_root = json::parse(request.body());
    json::array_t user_task_list = json_root.get<json::array_t>();
    if (user_task_list.empty()) {
      return error_handler(bad_request("empty task list", request));
    }
    std::vector<uint32_t> task_list{};
    task_list.reserve(user_task_list.size());
    for (auto const &user_task_id : user_task_list) {
      task_list.push_back(user_task_id.get<json::number_integer_t>());
    }
    auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
    std::vector<utilities::atomic_task_t> stopped_tasks{};
    if (db_connector->get_stopped_tasks(task_list, stopped_tasks) &&
        db_connector->remove_stopped_tasks(stopped_tasks)) {
      auto &task_queue = utilities::get_scheduled_tasks();
      spdlog::info("Stopped tasks: {}", stopped_tasks.size());
      for (auto &stopped_task : stopped_tasks) {
        task_queue.push_back(std::move(stopped_task));
      }
      return send_response(json_success(stopped_tasks, request));
    }
    return error_handler(server_error("not able to restart tasks",
                                      error_type_e::ServerError, request));
  } catch (std::exception const &e) {
    spdlog::error("exception in restart_tasks: {}", e.what());
    return error_handler(bad_request("unable to restarts tasks", request));
  }
}

void session::schedule_task_handler(string_request const &request,
                                    url_query const &optional_query) {
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
      json::number_integer_t total =
          task_object["total"].get<json::number_integer_t>();
      utilities::scheduled_task_t task{};
      task.total_numbers = total;
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
      if (!database_connector_t::s_get_db_connector()->add_task(task)) {
        return error_handler(server_error("unable to schedule task",
                                          error_type_e::ServerError, request));
      }
      spdlog::info("Added new task{}", task.website_ids.size() <= 1 ? "" : "s");
      auto &tasks{get_scheduled_tasks()};

      // split the main task into sub-tasks, based on the number of websites
      using utilities::atomic_task_t;
      for (auto const &website_id : task.website_ids) {
        atomic_task_t atom_task;
        atom_task.type_ = utilities::atomic_task_t::task_type::fresh;
        atom_task.task_id = task.task_id;
        atom_task.total = task.total_numbers;
        auto &new_atomic_task =
            atom_task.task.emplace<atomic_task_t::fresh_task>();
        new_atomic_task.website_id = website_id;
        new_atomic_task.number_ids = task.number_ids;
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
    return error_handler(server_error("not implemented yet",
                                      error_type_e::ServerError, request));
  } else {
    auto const id_iter = optional_query.find("id");
    try {
      if (id_iter == optional_query.cend()) {
        return error_handler(bad_request("uploader id unspecified", request));
      }
      auto const user_id = id_iter->second;
      auto db_connector =
          wudi_server::database_connector_t::s_get_db_connector();
      return send_response(
          json_success(db_connector->get_all_tasks(user_id), request));
    } catch (std::exception const &e) {
      spdlog::error("Get tasks exception: {}", e.what());
      return error_handler(bad_request("user id missing", request));
    }
  }
}

void session::website_handler(string_request const &request,
                              url_query const &) {
  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
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
                                        error_type_e::ServerError, request));
    }
    return send_response(success("ok", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("cannot parse body", request));
  }
}

void session::run() { http_read_data(); }

string_response session::not_found(string_request const &request) {
  return get_error("url not found", error_type_e::ResourceNotFound,
                   http::status::not_found, request);
}

string_response session::server_error(std::string const &message,
                                      error_type_e type,
                                      string_request const &request) {
  return get_error(message, type, http::status::internal_server_error, request);
}

string_response session::bad_request(std::string const &message,
                                     string_request const &request) {
  return get_error(message, error_type_e::BadRequest, http::status::bad_request,
                   request);
}

string_response session::method_not_allowed(string_request const &req) {
  return get_error("method not allowed", error_type_e::MethodNotAllowed,
                   http::status::method_not_allowed, req);
}

string_response session::successful_login(int const id, int const role,
                                          string_request const &req) {
  json::object_t result_obj;
  result_obj["status"] = error_type_e::NoError;
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
                                   error_type_e type, http::status status,
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
  result_obj["status"] = error_type_e::NoError;
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

url_query
session::split_optional_queries(boost::string_view const &optional_query) {
  url_query result{};
  if (!optional_query.empty()) {
    auto queries = utilities::split_string_view(optional_query, "&");
    for (auto const &q : queries) {
      auto split = utilities::split_string_view(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace(split[0], split[1]);
    }
  }
  return result;
}
} // namespace wudi_server

std::vector<uint32_t> operator+(std::vector<uint32_t> const &a,
                                std::vector<uint32_t> const &b) {
  std::vector<uint32_t> c(std::cbegin(a), std::cend(a));
  for (auto const &temp : b)
    c.push_back(temp);
  return c;
}
