#include "session.hpp"
#include "database_connector.hpp"
#include "file_utils.hpp"
#include "random.hpp"
#include "upload_data.hpp"

#include <fstream>
#include <gzip/decompress.hpp>
#include <spdlog/spdlog.h>
#include <zip_file.hpp>

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

extern int WOODY_SOFTWARE_VERSION;

namespace woody_server {
using nlohmann::json;
using namespace fmt::v6::literals;

enum constant_e { RequestBodySize = 1024 * 1024 * 50 };

std::filesystem::path const download_path =
    std::filesystem::current_path() / "downloads" / "zip_files";

rule_t::rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback)
    : num_verbs_{verbs.size()}, route_callback_{std::move(callback)} {
  if (verbs.size() > 3)
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

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(std::string const &target) {
  auto iter = endpoints.find(target);
  if (iter == endpoints.end())
    return std::nullopt;
  return iter;
}

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(std::string(target.data(), target.size()));
}

session_t::session_t(asio::io_context &io, asio::ip::tcp::socket &&socket)
    : io_context_{io}, m_tcpStream{std::move(socket)} {
  add_endpoint_interfaces();
}

void session_t::add_endpoint_interfaces() {
  using http::verb;

  endpoint_apis_.add_endpoint(
      "/", {verb::get},
      [=](string_request_t const &request, url_query const &optional_query) {
        index_page_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/login", {verb::get, verb::post},
      [=](string_request_t const &request, url_query const &optional_query) {
        login_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/upload", {verb::post, verb::delete_, verb::get},
      [=](string_request_t const &request, url_query const &optional_query) {
        upload_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/website", {verb::post, verb::get, verb::post},
      [=](string_request_t const &request, url_query const &optional_query) {
        website_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/schedule_task", {verb::post, verb::get, verb::delete_},
      [=](string_request_t const &request, url_query const &optional_query) {
        schedule_task_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/task", {verb::post, verb::get, verb::delete_},
      [=](string_request_t const &request, url_query const &optional_query) {
        schedule_task_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/download", {verb::post},
      [=](string_request_t const &request, url_query const &optional_query) {
        download_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/stop", {verb::post},
      [=](string_request_t const &request, url_query const &optional_query) {
        stop_tasks_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/start", {verb::post},
      [=](auto const &request, auto const &optional_query) {
        restart_tasks_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint(
      "/remove", {verb::post},
      [=](string_request_t const &request, url_query const &optional_query) {
        remove_tasks_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint("/get_file", {verb::get},
                              [this](auto const &req, auto const &query) {
                                get_file_handler(req, query);
                              });

  endpoint_apis_.add_endpoint(
      "/get_config", {verb::get, verb::post},
      [=](string_request_t const &request, url_query const &optional_query) {
        proxy_config_handler(request, optional_query);
      });

  endpoint_apis_.add_endpoint("/upgrade", {verb::get, verb::post},
                              [=](auto const &req, auto const &query) {
                                return software_update_handler(req, query);
                              });
}

void session_t::run() { http_read_data(); }

void session_t::http_read_data() {
  buffer_.clear();
  empty_body_parser_.emplace();
  empty_body_parser_->body_limit(RequestBodySize);
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::minutes(5));
  http::async_read_header(m_tcpStream, buffer_, *empty_body_parser_,
                          beast::bind_front_handler(&session_t::on_header_read,
                                                    shared_from_this()));
}

void session_t::on_header_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream)
    return shutdown_socket();
  if (ec) {
    return error_handler(
        server_error(ec.message(), session_error_e::ServerError, {}), true);
  } else {
    content_type_ = empty_body_parser_->get()[http::field::content_type];
    if (content_type_ == "application/json") {
      client_request_ =
          std::make_unique<http::request_parser<http::string_body>>(
              std::move(*empty_body_parser_));
      http::async_read(m_tcpStream, buffer_, *client_request_,
                       beast::bind_front_handler(&session_t::on_data_read,
                                                 shared_from_this()));
    } else if (content_type_ == "application/gzip") {
      dynamic_body_parser =
          std::make_unique<dynamic_request>(std::move(*empty_body_parser_));
      dynamic_body_parser->body_limit(RequestBodySize);
      http::async_read(m_tcpStream, buffer_, *dynamic_body_parser,
                       beast::bind_front_handler(&session_t::binary_data_read,
                                                 shared_from_this()));
    } else {
      return error_handler(bad_request(
          "contact your admin for proper request format", string_request_t{}));
    }
  }
}

void session_t::handle_requests(string_request_t const &request) {
  std::string const request_target{utilities::decodeUrl(request.target())};
  if (request_target.empty())
    return index_page_handler(request, {});

  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utilities::splitStringView(request_target_view, "?");
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

void session_t::binary_data_read(beast::error_code ec,
                                 std::size_t bytes_transferred) {
  if (ec) {
    spdlog::error(ec.message());
    return error_handler(
        bad_request("invalid content-type", string_request_t{}));
  }
  auto &request = dynamic_body_parser->get();
  handle_requests(request);
}

void session_t::on_data_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream) { // end of connection
    return shutdown_socket();
  } else if (ec == http::error::body_limit) {
    return error_handler(server_error(ec.message(),
                                      session_error_e::ServerError,
                                      string_request_t{}),
                         true);
  } else if (ec) {
    return error_handler(server_error(ec.message(),
                                      session_error_e::ServerError,
                                      string_request_t{}),
                         true);
  } else {
    handle_requests(client_request_->get());
  }
}

bool session_t::is_closed() {
  return !beast::get_lowest_layer(m_tcpStream).socket().is_open();
}

void session_t::shutdown_socket() {
  beast::error_code ec{};
  beast::get_lowest_layer(m_tcpStream)
      .socket()
      .shutdown(asio::socket_base::shutdown_send, ec);
  ec = {};
  beast::get_lowest_layer(m_tcpStream).socket().close(ec);
  beast::get_lowest_layer(m_tcpStream).close();
}

void session_t::error_handler(string_response_t &&response, bool close_socket) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  resp_ = resp;
  if (!close_socket) {
    http::async_write(m_tcpStream, *resp,
                      beast::bind_front_handler(&session_t::on_data_written,
                                                shared_from_this()));
  } else {
    http::async_write(
        m_tcpStream, *resp,
        [self = shared_from_this()](auto const err_c, std::size_t const) {
          self->shutdown_socket();
        });
  }
}

void session_t::on_data_written(beast::error_code ec,
                                std::size_t const bytes_written) {
  if (ec) {
    return spdlog::error(ec.message());
  }
  resp_ = nullptr;
  http_read_data();
}

void session_t::login_handler(string_request_t const &request,
                              url_query const &optional_query) {
  if (request.method() == http::verb::get) {
    return error_handler(bad_request("POST username && password", request));
  }

  try {
    std::string const software_version = request["X-Version-Number"];
    if (software_version.empty()) {
      return error_handler(upgrade_required(request));
    }
    int const version_number = std::stoi(software_version);
    if (version_number < WOODY_SOFTWARE_VERSION) {
      return error_handler(upgrade_required(request));
    }
  } catch (std::exception const &) {
    return error_handler(bad_request("incorrect software version", request));
  }

  // respond to POST request
  try {
    json json_body = json::parse(request.body());
    auto login_info = json_body.get<json::object_t>();
    auto username = login_info["username"].get<json::string_t>();
    auto password = login_info["password"].get<json::string_t>();
    auto [id, role] =
        database_connector_t::s_get_db_connector()->get_login_role(username,
                                                                   password);
    if (id == -1) {
      spdlog::error("[login.POST] {} {} is invalid", username, password);
      return error_handler(get_error("invalid username or password",
                                     session_error_e::Unauthorized,
                                     http::status::unauthorized, request));
    }
    return send_response(successful_login(id, role, request));
  } catch (std::exception const &exception) {
    spdlog::error(exception.what());
    return error_handler(bad_request("json object not valid", request));
  }
}

void session_t::index_page_handler(string_request_t const &request,
                                   url_query const &) {
  return error_handler(
      get_error("login", session_error_e::NoError, http::status::ok, request));
}

void session_t::upload_handler(string_request_t const &request,
                               url_query const &optional_query) {
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
    boost::string_view filename_view = parser["filename"];
    auto total_iter = optional_query.find("total");
    auto uploader_iter = optional_query.find("uploader");
    auto time_iter = optional_query.find("time");
    if (filename_view.empty() || utilities::anyOf(optional_query, total_iter,
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
                                        session_error_e::ServerError, request));
    try {
      if (is_zipped) {
        counter = std::stol(total_iter->second.to_string());
        out_file << gzip::decompress(const_cast<char const *>(body.data()),
                                     body.size());
      } else {
        counter = 0;
        json json_root =
            json::parse(std::string_view(body.data(), body.size()));
        auto list_of_numbers = json_root.get<json::array_t>();
        for (auto const &number : list_of_numbers) {
          out_file << number.get<json::string_t>() << "\n";
          ++counter;
        }
      }
      out_file.close();
      std::string time_str{};
      std::size_t const t{std::stoul(time_iter->second.to_string())};
      if (std::size_t const count = utilities::unixTimeToString(time_str, t);
          count > 0) {
        time_str.resize(count);
      } else {
        time_str = time_iter->second.to_string();
      }
      upload_request_t upload_request{
          filename_view, file_path, uploader_iter->second,
          boost::string_view(time_str.data(), time_str.size()), counter};
      if (!db_connector->add_upload(upload_request)) {
        std::error_code ec{};
        std::filesystem::remove(file_path, ec);
        return error_handler(server_error("unable to save file to database",
                                          session_error_e::ServerError,
                                          request));
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
      ids = utilities::splitStringView(id_iter->second, "|");
    }
    // GET method
    if (method == http::verb::get) {
      auto const with_deleted_iter = optional_query.find("with_deleted");
      if (with_deleted_iter == optional_query.cend()) {
        return error_handler(bad_request(
            "Deletion not specified, use an updated client", request));
      }
      bool use_deleted = std::stoi(with_deleted_iter->second.to_string()) == 1;
      json json_result = db_connector->get_uploads(ids, use_deleted);
      return send_response(json_success(json_result, request));
    } else {
      // a DELETE request
      std::vector<upload_result_t> uploads;
      if (!ids.empty() && ids[0] == "all") { // remove all
        if (!db_connector->remove_uploads({})) {
          return error_handler(server_error("unable to delete any record",
                                            session_error_e::ServerError,
                                            request));
        }
      } else {
        uploads = db_connector->get_uploads(ids);
        if (!db_connector->remove_uploads(ids)) {
          return error_handler(server_error("unable to delete specified IDs",
                                            session_error_e::ServerError,
                                            request));
        }
      }
      /*
      for (auto const &upload : uploads) {
        if (std::filesystem::exists(upload.name_on_disk)) {
          std::filesystem::remove(upload.name_on_disk);
        }
      }
      */
      return send_response(success("ok", request));
    }
  }
}

std::filesystem::path session_t::copy_file_n(
    std::filesystem::path const &source, std::filesystem::path const &temp_dest,
    std::string const &filename, json::number_integer_t const user_from,
    json::number_integer_t const user_to) {
  std::error_code ec{};
  if (user_from == 0) {
    std::filesystem::path dest = temp_dest / filename;
    if (std::filesystem::copy_file(source, dest, ec)) {
      return dest;
    }
    return {};
  }
  if (!std::filesystem::exists(source, ec))
    return {};
  std::size_t counter{1};
  std::filesystem::path temp_file{temp_dest / "{}"_format(filename)};
  while (std::filesystem::exists(temp_file, ec)) {
    temp_file = temp_dest / "{}_{}"_format(counter, filename);
    ++counter;
  }
  std::ifstream in_file{source};
  std::ofstream out_file{temp_file};
  if (!(in_file && out_file))
    return {};
  json::number_integer_t current = 1;
  std::string line{};
  while (std::getline(in_file, line)) {
    if (current >= user_from && current <= user_to) {
      out_file << line << "\n";
    }
    if (current > user_to)
      break;
    ++current;
  }
  in_file.close();
  out_file.close();
  return temp_file;
}

void session_t::download_handler(string_request_t const &request,
                                 url_query const &optional_query) {
  // we only handle POST(application/json) requests here, not any other.
  if (content_type_ != "application/json") {
    return error_handler(bad_request("invalid content-type", request));
  }
  try {
    json json_root = json::parse(request.body());
    auto task_object = json_root.get<json::object_t>();
    auto const user_task_ids = task_object["tasks"].get<json::array_t>();
    auto const user_website_ids = task_object["websites"].get<json::array_t>();
    auto const user_download_types = task_object["type"].get<json::array_t>();
    json::number_integer_t user_from = 0;
    json::number_integer_t user_to = 0;
    if (auto iter = task_object.find("from"); iter != task_object.end()) {
      user_from = iter->second.get<json::number_integer_t>();
      user_to = task_object["to"].get<json::number_integer_t>();
    }
    auto db_connector = database_connector_t::s_get_db_connector();
    std::vector<atomic_task_t> tasks{};
    std::vector<uint32_t> task_ids{};
    std::vector<uint32_t> website_ids{};
    task_ids.reserve(user_task_ids.size());
    for (auto const &user_task_id : user_task_ids) {
      task_ids.push_back(
          static_cast<uint32_t>(user_task_id.get<json::number_integer_t>()));
    }
    for (auto const &user_website_id : user_website_ids) {
      website_ids.push_back(
          static_cast<uint32_t>(user_website_id.get<json::number_integer_t>()));
    }
    if (!(db_connector->get_stopped_tasks(task_ids, tasks) &&
          db_connector->get_completed_tasks(task_ids, tasks))) {
      return error_handler(
          server_error("failed", session_error_e::ServerError, request));
    }
    bool needs_ok = false, needs_not_ok = false, needs_unknown = false;
    for (const auto &user_download_type : user_download_types) {
      std::string const str = user_download_type.get<json::string_t>();
      if (str == "ok")
        needs_ok = true;
      else if (str == "not_ok")
        needs_not_ok = true;
      else if (str == "others")
        needs_unknown = true;
    }
    std::error_code ec{};
    std::filesystem::path const temp_path =
        std::filesystem::temp_directory_path(ec);
    if (ec) {
      spdlog::error("Unable to get temp path: {}", ec.message());
      return error_handler(server_error("unable to get temp path",
                                        session_error_e::ServerError, request));
    }
    ec = {};
    std::vector<std::filesystem::path> paths{};
    for (auto &stopped_task : tasks) {
      if (std::find(website_ids.cbegin(), website_ids.cend(),
                    stopped_task.website_id) == website_ids.cend()) {
        continue;
      }
      using utilities::normalizePaths;
      normalizePaths(stopped_task.not_ok_filename);
      normalizePaths(stopped_task.ok_filename);
      normalizePaths(stopped_task.ok2_filename);
      normalizePaths(stopped_task.unknown_filename);
      if (needs_ok) { // provides numbers that are OK.
        std::string const ok_filename = "task_{}_website_{}_ok.txt"_format(
            stopped_task.task_id, stopped_task.website_id);
        std::string const ok2_filename = "task_{}_website_{}_ok2.txt"_format(
            stopped_task.task_id, stopped_task.website_id);

        paths.emplace_back(copy_file_n(stopped_task.ok_filename, temp_path,
                                       ok_filename, user_from, user_to));
        paths.emplace_back(copy_file_n(stopped_task.ok2_filename, temp_path,
                                       ok2_filename, user_from, user_to));
      }
      if (needs_not_ok) {
        std::string const filename = "task_{}_website_{}_not_ok.txt"_format(
            stopped_task.task_id, stopped_task.website_id);
        paths.emplace_back(copy_file_n(stopped_task.not_ok_filename, temp_path,
                                       filename, user_from, user_to));
      }
      if (needs_unknown) {
        std::string const filename = "task_{}_website_{}_unknown.txt"_format(
            stopped_task.task_id, stopped_task.website_id);
        paths.emplace_back(copy_file_n(stopped_task.unknown_filename, temp_path,
                                       filename, user_from, user_to));
      }
    }
    // remove all isEmpty files from the `paths`
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [](std::filesystem::path const &path) {
                                 return path.string().empty() ||
                                        std::filesystem::file_size(path) == 0;
                               }),
                paths.end());
    if (paths.empty())
      return send_response(success("isEmpty", request));
    std::filesystem::path const czip_file_path =
        temp_path / "{}.zip"_format(std::time(nullptr));
    miniz_cpp::zip_file zip_file{};
    for (auto const &path : paths) {
      zip_file.write(path.string());
      std::error_code errorCode{};
      std::filesystem::remove(path, errorCode);
    }
    zip_file.save(czip_file_path.string());
    if (!std::filesystem::exists(download_path)) {
      std::error_code errorCode{};
      std::filesystem::create_directories(download_path, errorCode);
    }
    ec = {};
    std::filesystem::copy(czip_file_path, download_path, ec);
    if (ec) {
      spdlog::error("Unable to copy zip file from temp to download path: {}",
                    ec.message());
      return error_handler(server_error("unable to copy zip",
                                        session_error_e::ServerError, request));
    }
    std::filesystem::remove(czip_file_path);
    json::object_t json_url;
    json_url["filename"] = czip_file_path.filename().string();
    return send_response(json_success(json_url, request));
  } catch (std::exception const &e) {
    spdlog::error("exception in `download_handler`: {}", e.what());
    return error_handler(
        bad_request("unable to process JSON request", request));
  }
}

void session_t::remove_tasks_handler(string_request_t const &req,
                                     url_query const &optional_query) {
  if (content_type_ != "application/json")
    return error_handler(bad_request("invalid content-type", req));
  try {
    auto user_id_iter = optional_query.find("id");
    if (user_id_iter == optional_query.cend())
      return error_handler(bad_request("user id is missing", req));
    json json_root = json::parse(req.body());
    auto task_type_object = json_root.get<json::object_t>();
    if (task_type_object.empty()) {
      return error_handler(bad_request("isEmpty task list", req));
    }
    auto const user_specified_tasks =
        task_type_object["id"].get<json::array_t>();
    std::vector<uint32_t> tasks{};
    tasks.reserve(user_specified_tasks.size());
    for (auto const &task : user_specified_tasks) {
      tasks.emplace_back(
          static_cast<uint32_t>(task.get<json::number_integer_t>()));
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

void session_t::proxy_config_handler(string_request_t const &request,
                                     url_query const &query) {
  char const *const json_content_type = "application/json";
  if (content_type_ != json_content_type) {
    return error_handler(bad_request("invalid content-type", request));
  }
  if (request.method() == http::verb::get) {
    std::filesystem::path const file_path = "./proxy_config.json";
    return send_file(file_path, json_content_type, request, [] {});
  }

  try {
    {
      // if this throws, the JSON file is not valid.
      auto root = json::parse(request.body()).get<json::object_t>();
      auto proxy_object = root["proxy"].get<json::object_t>();
      int const interval_between_fetch = static_cast<int>(
          proxy_object["fetch_interval"].get<json::number_integer_t>());
      int const software_version = static_cast<int>(
          root["client_version"].get<json::number_integer_t>());
      auto &current_interval = utilities::proxyFetchInterval();
      if (current_interval != interval_between_fetch) {
        current_interval = interval_between_fetch;
      }
      if (software_version < WOODY_SOFTWARE_VERSION) {
        return error_handler(upgrade_required(request));
      }
    }
    std::ofstream out_file{"./proxy_config.json",
                           std::ios::out | std::ios::trunc};
    if (!out_file) {
      return error_handler(server_error("unable to open config file for write",
                                        session_error_e::ServerError, request));
    }
    out_file << request.body() << "\n";
  } catch (std::exception const &e) {
    spdlog::error("[get_config] {}", e.what());
    return error_handler(bad_request("unable to save config file", request));
  }
  return send_response(success("ok", request));
}

void session_t::delete_stopped_tasks_impl(
    std::vector<uint32_t> const &user_stopped_tasks) {
  using utilities::removeFile;
  auto db_connector = database_connector_t::s_get_db_connector();
  std::vector<atomic_task_t> tasks{};
  db_connector->get_stopped_tasks(user_stopped_tasks, tasks);
  for (auto &stopped_task : tasks) {
    removeFile(stopped_task.input_filename);
    removeFile(stopped_task.not_ok_filename);
    removeFile(stopped_task.ok_filename);
    removeFile(stopped_task.ok2_filename);
    removeFile(stopped_task.unknown_filename);
  }
  return db_connector->delete_stopped_tasks(user_stopped_tasks);
}

void session_t::delete_other_tasks_impl(
    boost::string_view const user_id, std::vector<uint32_t> const &all_tasks) {
  auto db_connector = database_connector_t::s_get_db_connector();
  db_connector->remove_filtered_tasks(user_id, all_tasks);
  auto &response_queue = utilities::getResponseQueue();
  for (auto const &task : all_tasks) {
    response_queue.erase(task);
  }
}

std::vector<uint32_t>
session_t::stop_running_tasks_impl(std::vector<uint32_t> const &task_id_list,
                                   bool const saving_state) {
  auto &running_tasks = utilities::getResponseQueue();
  std::vector<uint32_t> stopped_tasks{};
  stopped_tasks.reserve(task_id_list.size());
  for (std::size_t index = 0; index != task_id_list.size(); ++index) {
    auto const &task_id = task_id_list[index];
    if (auto iter = running_tasks.equal_range(task_id);
        iter.first != running_tasks.cend()) {
      stopped_tasks.push_back(task_id);
      for (auto beg = iter.first; beg != iter.second; ++beg) {
        if (beg->second->operation_status == task_status_e::Ongoing) {
          beg->second->savingState() = saving_state;
          beg->second->stop();
        }
      }
    }
  }
  return stopped_tasks;
}

void session_t::get_file_handler(string_request_t const &request,
                                 url_query const &optional_query) {
  if (content_type_ != "application/json") {
    return error_handler(bad_request("invalid content-type", request));
  }
  auto iter = optional_query.find("filename");
  if (iter == optional_query.cend()) {
    return error_handler(bad_request("key parameter missing", request));
  }
  std::filesystem::path const file_path =
      download_path / iter->second.to_string();
  char const *const content_type =
      "application/zip, application/octet-stream, "
      "application/x-zip-compressed, multipart/x-zip";
  auto callback = [=] {
    std::error_code temp_ec{};
    std::filesystem::remove(file_path, temp_ec);
  };
  return send_file(file_path, content_type, request, callback);
}

void session_t::software_update_handler(string_request_t const &request,
                                        url_query const &optional_query) {
  if (content_type_ != "application/json") {
    return error_handler(bad_request("invalid content-type", request));
  }
  char const *const content_type =
      "application/zip, application/octet-stream, "
      "application/x-zip-compressed, multipart/x-zip";
  if (!std::filesystem::exists(download_path)) {
    std::error_code ec{};
    std::filesystem::create_directories(download_path, ec);
    if (ec) {
      return error_handler(server_error("server encountered an error",
                                        session_error_e::RequiresUpdate,
                                        request));
    }
  }
  std::filesystem::path const file_path = download_path / "woody_latest.zip";
  return send_file(file_path, content_type, request, [] {});
}

void session_t::stop_tasks_handler(string_request_t const &request,
                                   url_query const &optional_query) {
  if (content_type_ != "application/json")
    return error_handler(bad_request("invalid content-type", request));
  try {
    json json_root = json::parse(request.body());
    auto const task_id_list = json_root.get<json::array_t>();
    if (task_id_list.empty()) {
      return error_handler(bad_request("isEmpty task list", request));
    }
    std::vector<uint32_t> tasks{};
    tasks.reserve(task_id_list.size());
    for (auto const &task : task_id_list) {
      tasks.emplace_back(task.get<json::number_integer_t>());
    }
    auto &queued_task = utilities::getScheduledTasks();
    auto interesting_tasks = queued_task.removeValue(
        tasks, [](atomic_task_t &task, std::vector<uint32_t> const &ids) {
          for (auto const &id : ids) {
            if (task.task_id == id)
              return true;
          }
          return false;
        });
    auto db_connector = database_connector_t::s_get_db_connector();
    for (auto const &task : interesting_tasks) {
      atomic_task_t unstarted_task{};
      unstarted_task.task_type = task_type_e::fresh;
      unstarted_task.task_id = task.task_id;
      unstarted_task.website_id = task.website_id;
      unstarted_task.processed = 0;
      unstarted_task.total = task.total;
      unstarted_task.not_ok_filename = unstarted_task.ok_filename =
          unstarted_task.ok2_filename = unstarted_task.unknown_filename =
              "{free}";

      unstarted_task.input_filename =
          utilities::integerListToString(unstarted_task.number_ids);
      db_connector->save_unstarted_task(unstarted_task);
    }
    return send_response(json_success(stop_running_tasks_impl(tasks), request));
  } catch (std::exception const &e) {
    spdlog::error("exception in stop_tasks: {}", e.what());
    return error_handler(bad_request("unable to stop tasks", request));
  }
}

void session_t::restart_tasks_handler(string_request_t const &request,
                                      url_query const &optional_query) {
  if (content_type_ != "application/json")
    return error_handler(bad_request("invalid content-type", request));
  try {
    json json_root = json::parse(request.body());
    auto user_task_list = json_root.get<json::array_t>();
    if (user_task_list.empty()) {
      return error_handler(bad_request("isEmpty task list", request));
    }
    std::vector<uint32_t> task_list{};
    task_list.reserve(user_task_list.size());
    for (auto const &user_task_id : user_task_list) {
      task_list.push_back(
          static_cast<uint32_t>(user_task_id.get<json::number_integer_t>()));
    }
    using utilities::restartTasks;

    if (auto stopped_tasks = restartTasks(task_list); !stopped_tasks.empty()) {
      return send_response(json_success(stopped_tasks, request));
    }
    return error_handler(server_error("not able to restart tasks",
                                      session_error_e::ServerError, request));
  } catch (std::exception const &e) {
    spdlog::error("exception in restart_tasks: {}", e.what());
    return error_handler(bad_request("unable to restarts tasks", request));
  }
}

void session_t::schedule_task_handler(string_request_t const &request,
                                      url_query const &optional_query) {
  using http::verb;
  using utilities::getScheduledTasks;

  auto const method = request.method();
  if (method == verb::post) {
    if (content_type_ != "application/json") {
      return error_handler(bad_request("invalid content-type", request));
    }
    std::vector<uint32_t> task_ids{};
    try {
      json json_root = json::parse(request.body());
      auto task_object = json_root.get<json::object_t>();
      auto const websites_ids = task_object["websites"].get<json::array_t>();
      auto const number_ids = task_object["numbers"].get<json::array_t>();
      auto const total = task_object["total"].get<json::number_integer_t>();
      auto const per_ip = task_object["per_ip"].get<json::number_integer_t>();
      auto &tasks{getScheduledTasks()};

      scheduled_task_t task{};
      task.total_numbers = static_cast<uint32_t>(total);
      task.scans_per_ip = static_cast<uint32_t>(per_ip);
      task.scheduled_dt =
          static_cast<int>(task_object["date"].get<json::number_integer_t>());
      task.scheduler_id = static_cast<int>(
          task_object["scheduler"].get<json::number_integer_t>());
      for (auto const &number_id : number_ids) {
        task.number_ids.push_back(
            static_cast<int>(number_id.get<json::number_integer_t>()));
      }
      for (auto const &website_id : websites_ids) {
        task.website_id =
            static_cast<uint32_t>(website_id.get<json::number_integer_t>());
        if (!database_connector_t::s_get_db_connector()->add_task(task)) {
          return error_handler(server_error("unable to schedule task",
                                            session_error_e::ServerError,
                                            request));
        }
        // an hack to make sure DB don't reject duplicate insertion
        ++task.scheduled_dt;

        atomic_task_t atom_task;
        atom_task.task_type = task_type_e::fresh;
        atom_task.task_id = task.task_id;
        atom_task.total = task.total_numbers;
        atom_task.website_id = task.website_id;
        atom_task.number_ids = task.number_ids;
        atom_task.scans_per_ip = task.scans_per_ip;
        task_ids.push_back(task.task_id);
        tasks.append(std::move(atom_task));
      }

      json::object_t obj;
      obj["ids"] = task_ids;
      json j = obj;
      return send_response(json_success(j, request));
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return error_handler(bad_request("json object is not parsable", request));
    }
  } else if (method == http::verb::delete_) {
    return error_handler(server_error("not implemented yet",
                                      session_error_e::ServerError, request));
  } else {
    auto const user_id_iter = optional_query.find("user_id");
    auto const task_ids_iter = optional_query.find("task_id");
    try {
      if (user_id_iter == optional_query.cend()) {
        return error_handler(bad_request("uploader id unspecified", request));
      }
      std::vector<boost::string_view> task_ids{};
      if (task_ids_iter != optional_query.cend()) {
        task_ids = utilities::splitStringView(task_ids_iter->second, "|");
      }
      auto const user_id = user_id_iter->second;
      auto db_connector = database_connector_t::s_get_db_connector();
      return send_response(json_success(
          db_connector->get_all_tasks(user_id, task_ids), request));
    } catch (std::exception const &e) {
      spdlog::error("Get tasks exception: {}", e.what());
      return error_handler(bad_request("user id missing", request));
    }
  }
}

void session_t::website_handler(string_request_t const &request,
                                url_query const &) {
  auto db_connector = database_connector_t::s_get_db_connector();
  if (request.method() == http::verb::get) {
    json j = db_connector->get_websites({});
    return send_response(json_success(j, request));
  }
  if (content_type_ != "application/json") {
    return error_handler(bad_request("expects a JSON body", request));
  }
  try {
    json json_root = json::parse(request.body());
    auto obj = json_root.get<json::object_t>();
    auto address = obj["address"].get<json::string_t>();
    auto alias = obj["alias"].get<json::string_t>();
    if (!db_connector->add_website(address, alias)) {
      spdlog::error("[website_handler] could not add website");
      return error_handler(server_error("could not add website",
                                        session_error_e::ServerError, request));
    }
    return send_response(success("ok", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request("cannot parse body", request));
  }
}

string_response_t session_t::not_found(string_request_t const &request) {
  return get_error("url not found", session_error_e::ResourceNotFound,
                   http::status::not_found, request);
}

string_response_t session_t::upgrade_required(string_request_t const &request) {
  return get_error("you need to upgrade your client software",
                   session_error_e::ResourceNotFound,
                   http::status::upgrade_required, request);
}

string_response_t session_t::server_error(std::string const &message,
                                          session_error_e type,
                                          string_request_t const &request) {
  return get_error(message, type, http::status::internal_server_error, request);
}

string_response_t session_t::bad_request(std::string const &message,
                                         string_request_t const &request) {
  return get_error(message, session_error_e::BadRequest,
                   http::status::bad_request, request);
}

string_response_t session_t::method_not_allowed(string_request_t const &req) {
  return get_error("method not allowed", session_error_e::MethodNotAllowed,
                   http::status::method_not_allowed, req);
}

string_response_t session_t::successful_login(int const id, int const role,
                                              string_request_t const &req) {
  json::object_t result_obj;
  result_obj["status"] = session_error_e::NoError;
  result_obj["message"] = "success";
  result_obj["id"] = id;
  result_obj["role"] = role;
  json result = result_obj;

  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response_t session_t::get_error(std::string const &error_message,
                                       session_error_e type,
                                       http::status status,
                                       string_request_t const &req) {
  json::object_t result_obj;
  result_obj["status"] = type;
  result_obj["message"] = error_message;
  json result = result_obj;

  string_response_t response{status, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response_t session_t::json_success(json const &body,
                                          string_request_t const &req) {
  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

string_response_t session_t::success(char const *message,
                                     string_request_t const &req) {
  json::object_t result_obj;
  result_obj["status"] = session_error_e::NoError;
  result_obj["message"] = message;
  json result{result_obj};

  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "application/json");
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

void session_t::send_response(string_response_t &&response) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  resp_ = resp;
  http::async_write(m_tcpStream, *resp,
                    beast::bind_front_handler(&session_t::on_data_written,
                                              shared_from_this()));
}

url_query
session_t::split_optional_queries(boost::string_view const &optional_query) {
  url_query result{};
  if (!optional_query.empty()) {
    auto queries = utilities::splitStringView(optional_query, "&");
    for (auto const &q : queries) {
      auto split = utilities::splitStringView(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace(split[0], split[1]);
    }
  }
  return result;
}

namespace utilities {
std::vector<atomic_task_t> restartTasks(std::vector<uint32_t> const &task_ids) {
  auto db_connector = database_connector_t::s_get_db_connector();
  std::vector<atomic_task_t> stopped_tasks{};
  auto &task_queue = utilities::getScheduledTasks();
  if (db_connector->get_stopped_tasks(task_ids, stopped_tasks)) {
    for (auto &stopped_task : stopped_tasks) {
      task_queue.append(std::move(stopped_task));
    }
    return stopped_tasks;
  }
  return {};
}
} // namespace utilities
} // namespace woody_server

std::vector<uint32_t> operator+(std::vector<uint32_t> const &a,
                                std::vector<uint32_t> const &b) {
  std::vector<uint32_t> c(std::cbegin(a), std::cend(a));
  c.reserve(a.size() + b.size());
  for (auto const &temp : b) {
    c.push_back(temp);
  }
  return c;
}
