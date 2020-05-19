#include "worker.hpp"
#include "backgroundworker.hpp"
#include "database_connector.hpp"
#include "utilities.hpp"
#include <boost/algorithm/string.hpp>

namespace wudi_server {
using utilities::atomic_task_t;
using utilities::internal_task_result_t;

utilities::threadsafe_container<uint32_t, std::vector<uint32_t>> &
get_stopped_tasks() {
  static utilities::threadsafe_container<uint32_t, std::vector<uint32_t>>
      stopped_tasks{};
  return stopped_tasks;
}

void on_task_ran(utilities::task_status_e status,
                 utilities::atomic_task_t &scheduled_task,
                 std::shared_ptr<database_connector_t> &db_connector,
                 background_worker_t *worker_ptr) {
  switch (status) {
  case utilities::task_status_e::Stopped:
    return run_stopped_op(db_connector, *worker_ptr);
  case utilities::task_status_e::Completed:
    return run_completion_op(db_connector, *worker_ptr);
  case utilities::task_status_e::Erred:
    return run_error_occurred_op(db_connector, *worker_ptr);
  case utilities::task_status_e::AutoStopped:
    run_stopped_op(db_connector, *worker_ptr);
    return get_stopped_tasks().push_back(scheduled_task.task_id);
  }
}

std::unique_ptr<background_worker_t>
start_new_task(atomic_task_t &scheduled_task, net::ssl::context &ssl_context) {
  using utilities::intlist_to_string;
  using utilities::task_status_e;

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  std::optional<website_result_t> website =
      db_connector->get_website(scheduled_task.website_id);
  std::vector<upload_result_t> numbers =
      db_connector->get_uploads(scheduled_task.number_ids);
  if (!website) {
    spdlog::error("No such website: {}", scheduled_task.website_id);
    return {};
  }
  if (numbers.empty()) {
    spdlog::error("No numbers obtained");
    return {};
  }

  auto &response_queue = utilities::get_response_queue();

  auto task_result = std::make_shared<internal_task_result_t>();
  task_result->task_id = scheduled_task.task_id;
  task_result->website_id = scheduled_task.website_id;
  task_result->scans_per_ip = scheduled_task.scans_per_ip;
  scheduled_task.website_address = website->address;
  response_queue.emplace(scheduled_task.task_id, task_result);
  return std::make_unique<background_worker_t>(
      std::move(*website), std::move(numbers), task_result, ssl_context);
}

std::unique_ptr<background_worker_t>
continue_recent_task(atomic_task_t &scheduled_task,
                     net::ssl::context &ssl_context) {
  using utilities::normalize_paths;
  using utilities::task_status_e;

  normalize_paths(scheduled_task.input_filename);
  normalize_paths(scheduled_task.ok_filename);
  normalize_paths(scheduled_task.ok2_filename);
  normalize_paths(scheduled_task.not_ok_filename);
  normalize_paths(scheduled_task.unknown_filename);

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  std::optional<website_result_t> website =
      db_connector->get_website(scheduled_task.website_id);

  if (!website) {
    spdlog::error("website address is empty");
    return {};
  }

  if (!std::filesystem::exists(scheduled_task.input_filename)) {
    spdlog::error("file does not exist anymore");
    return {};
  }
  std::shared_ptr<internal_task_result_t> task_result{};
  auto &response_queue = utilities::get_response_queue();
  auto iter = response_queue.find(scheduled_task.task_id);
  if (iter == response_queue.cend()) {
    task_result = std::make_shared<internal_task_result_t>();
    task_result->task_id = scheduled_task.task_id;
    task_result->website_id = scheduled_task.website_id;
    task_result->processed = scheduled_task.processed;
    task_result->total_numbers = scheduled_task.total;
    task_result->ok_count = scheduled_task.ok_count;
    task_result->not_ok_count = scheduled_task.not_ok_count;
    task_result->unknown_count = scheduled_task.unknown_count;
    task_result->not_ok_filename = scheduled_task.not_ok_filename;
    task_result->ok_filename = scheduled_task.ok_filename;
    task_result->ok2_filename = scheduled_task.ok2_filename;
    task_result->unknown_filename = scheduled_task.unknown_filename;
    task_result->scans_per_ip = scheduled_task.scans_per_ip;
    task_result->ip_used = scheduled_task.ip_used;
    scheduled_task.website_address = website->address;
    response_queue.emplace(scheduled_task.task_id, task_result);
  } else {
    task_result = iter->second;
    scheduled_task.website_address = website->address;
  }
  return std::make_unique<background_worker_t>(std::move(scheduled_task),
                                               task_result, ssl_context);
}

std::unique_ptr<background_worker_t>
resume_unstarted_task(utilities::atomic_task_t &scheduled_task,
                      net::ssl::context &ssl_context) {
  auto string_to_intlist = [](std::string const &str, char const *delim = ",") {
    std::vector<uint32_t> list{};
    auto split = utilities::split_string_view(str, delim);
    for (auto const &s : split) {
      list.push_back(std::stoul(boost::trim_copy(s.to_string())));
    }
    return list;
  };

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  std::optional<website_result_t> website =
      db_connector->get_website(scheduled_task.website_id);
  std::vector<upload_result_t> numbers = db_connector->get_uploads(
      string_to_intlist(scheduled_task.input_filename));
  if (!website || numbers.empty()) {
    spdlog::error("No such website or numbers is empty");
    return {};
  }
  scheduled_task.input_filename.clear();
  scheduled_task.not_ok_filename.clear();
  scheduled_task.ok_filename.clear();
  scheduled_task.ok2_filename.clear();
  scheduled_task.unknown_filename.clear();
  scheduled_task.website_address.clear();
  auto &response_queue = utilities::get_response_queue();

  auto task_result = std::make_shared<internal_task_result_t>();
  task_result->task_id = scheduled_task.task_id;
  task_result->website_id = scheduled_task.website_id;
  task_result->scans_per_ip = scheduled_task.scans_per_ip;
  response_queue.emplace(scheduled_task.task_id, task_result);
  return std::make_unique<background_worker_t>(
      std::move(*website), std::move(numbers), task_result, ssl_context);
}

void background_task_executor(std::atomic_bool &stopped,
                              boost::asio::ssl::context &ssl_context,
                              global_proxy_repo_t &r) {

  auto db_connector = database_connector_t::s_get_db_connector();
  auto &scheduled_tasks = utilities::get_scheduled_tasks();

  auto on_error = [db_connector](atomic_task_t &scheduled_task) {
    using utilities::replace_special_chars;
    if (scheduled_task.type_ != atomic_task_t::task_type::fresh) {
      replace_special_chars(scheduled_task.input_filename);
      replace_special_chars(scheduled_task.not_ok_filename);
      replace_special_chars(scheduled_task.ok_filename);
      replace_special_chars(scheduled_task.ok2_filename);
      replace_special_chars(scheduled_task.unknown_filename);
      db_connector->save_stopped_task(scheduled_task);
    }
    db_connector->change_task_status(
        scheduled_task.task_id, scheduled_task.processed,
        scheduled_task.ip_used, utilities::task_status_e::Erred);
  };

  while (!stopped) {
    auto scheduled_task = std::move(scheduled_tasks.get());
    std::unique_ptr<background_worker_t> worker{};
    if (scheduled_task.type_ == atomic_task_t::task_type::fresh) {
      worker = start_new_task(scheduled_task, ssl_context);
    } else {
      if (scheduled_task.ok_filename == "{free}") {
        worker = resume_unstarted_task(scheduled_task, ssl_context);
      } else {
        worker = continue_recent_task(scheduled_task, ssl_context);
      }
    }
    if (worker) {
      db_connector->change_task_status(
          scheduled_task.task_id, worker->task_result()->processed,
          scheduled_task.ip_used, utilities::task_status_e::Ongoing);
      auto handle = [&scheduled_task, &db_connector, worker_ptr = worker.get()](
                        utilities::task_status_e status) {
        on_task_ran(status, scheduled_task, db_connector, worker_ptr);
      };
      worker->proxy_callback_signal(r.new_ep_signal());
      handle(worker->run());
      auto task_result_ptr = worker->task_result();

      try {
        if (task_result_ptr) {
          task_result_ptr->ok2_file.close();
          task_result_ptr->not_ok_file.close();
          task_result_ptr->ok_file.close();
          task_result_ptr->unknown_file.close();
          worker->number_stream()->close();
        }
        // if we are here, we are done.
      } catch (std::exception const &e) {
        spdlog::error("Exception while running worker: {}", e.what());
        on_error(scheduled_task);
      }
    } else {
      spdlog::error("worker failed");
      on_error(scheduled_task);
    }
  }
}

void run_completion_op(std::shared_ptr<database_connector_t> &db_connector,
                       background_worker_t &bg_worker) {

  using utilities::get_random_integer;
  using utilities::get_random_string;

  auto task_result_ptr = bg_worker.task_result();
  bool const status_changed = db_connector->change_task_status(
      task_result_ptr->task_id, task_result_ptr->total_numbers,
      task_result_ptr->ip_used, task_result_ptr->operation_status);
  if (status_changed) {
    if (std::filesystem::exists(bg_worker.filename())) {
      if (bg_worker.number_stream()->is_open()) {
        bg_worker.number_stream()->close();
      }
      std::error_code ec{};
      std::filesystem::remove(bg_worker.filename(), ec);
    }
    spdlog::info("Saved completed task successfully");
  } else {
    std::ofstream out_file{"./erred_saving.txt", std::ios::out | std::ios::app};
    if (out_file) {
      std::string const dump =
          "{}.txt"_format(get_random_string(get_random_integer()));
      std::ofstream out_file_stream{dump};
      if (out_file_stream) {
        out_file_stream << bg_worker.number_stream()->dump_s();
        out_file_stream.close();
      }
      out_file << "ID: " << task_result_ptr->task_id
               << ", OK: " << task_result_ptr->ok_filename.string()
               << ", OK2: " << task_result_ptr->ok2_filename.string()
               << ", NOT_OK: " << task_result_ptr->not_ok_filename.string()
               << ", Unknown: " << task_result_ptr->unknown_filename.string()
               << ", WEB_ID: " << task_result_ptr->website_id
               << ", DUMP: " << dump << "\n\n";
    }
    spdlog::error("Unable to save completed tasks");
  }
  spdlog::info("Done processing task -> {}:{}", task_result_ptr->task_id,
               task_result_ptr->website_id);
}

void run_error_occurred_op(std::shared_ptr<database_connector_t> &db_connector,
                           background_worker_t &bg_worker) {
  using utilities::replace_special_chars;
  using utilities::task_status_e;

  auto task_result_ptr = bg_worker.task_result();
  task_result_ptr->operation_status = task_status_e::Erred;
  atomic_task_t erred_task{};
  erred_task.not_ok_filename = task_result_ptr->not_ok_filename.string();
  erred_task.ok_filename = task_result_ptr->ok_filename.string();
  erred_task.ok2_filename = task_result_ptr->ok2_filename.string();
  erred_task.unknown_filename = task_result_ptr->unknown_filename.string();
  erred_task.input_filename = bg_worker.filename();
  erred_task.task_id = task_result_ptr->task_id;
  erred_task.website_id = task_result_ptr->website_id;
  erred_task.processed = task_result_ptr->processed;

  replace_special_chars(erred_task.ok_filename);
  replace_special_chars(erred_task.ok2_filename);
  replace_special_chars(erred_task.not_ok_filename);
  replace_special_chars(erred_task.unknown_filename);
  replace_special_chars(erred_task.input_filename);
  db_connector->add_erred_task(erred_task);
}

void run_stopped_op(std::shared_ptr<database_connector_t> &db_connector,
                    background_worker_t &bg_worker) {
  using utilities::get_random_integer;
  using utilities::get_random_string;

  auto task_result_ptr = bg_worker.task_result();
  if (task_result_ptr->saving_state()) {
    std::filesystem::path filename =
        "." / std::filesystem::path("stopped_files") /
        "{}.txt"_format(get_random_string(get_random_integer()));
    while (std::filesystem::exists(filename)) {
      filename = "." / std::filesystem::path("stopped_files") /
                 "{}.txt"_format(get_random_string(get_random_integer()));
    }
    if (utilities::create_file_directory(filename) &&
        save_status_to_persistent_storage(filename.string(), bg_worker,
                                          db_connector)) {
      spdlog::info("saved task -> {}:{} to persistent storage",
                   task_result_ptr->task_id, task_result_ptr->website_id);
    } else {
      std::error_code ec{};
      if (std::filesystem::exists(filename, ec)) {
        std::filesystem::remove(filename, ec);
      }
      spdlog::error("unable to save task-> {}:{} to persistent storage",
                    task_result_ptr->task_id, task_result_ptr->website_id);
    }
  }
}

bool save_status_to_persistent_storage(
    std::string const &filename, background_worker_t &bg_worker,
    std::shared_ptr<database_connector_t> db_connector) {
  using utilities::atomic_task_t;
  auto task_result_ptr = bg_worker.task_result();
  atomic_task_t stopped_task{};
  stopped_task.type_ = atomic_task_t::task_type::stopped;
  stopped_task.processed = task_result_ptr->processed;
  stopped_task.total = task_result_ptr->total_numbers;
  stopped_task.task_id = task_result_ptr->task_id;
  stopped_task.website_id = task_result_ptr->website_id;
  stopped_task.not_ok_count = task_result_ptr->not_ok_count;
  stopped_task.ok_count = task_result_ptr->ok_count;
  stopped_task.unknown_count = task_result_ptr->unknown_count;
  stopped_task.input_filename = filename;
  stopped_task.not_ok_filename = task_result_ptr->not_ok_filename.string();
  stopped_task.ok_filename = task_result_ptr->ok_filename.string();
  stopped_task.ok2_filename = task_result_ptr->ok2_filename.string();
  stopped_task.unknown_filename = task_result_ptr->unknown_filename.string();
  using utilities::replace_special_chars;

  replace_special_chars(stopped_task.not_ok_filename);
  replace_special_chars(stopped_task.ok_filename);
  replace_special_chars(stopped_task.ok2_filename);
  replace_special_chars(stopped_task.unknown_filename);
  replace_special_chars(stopped_task.input_filename);

  std::ofstream out_file(filename);
  if (!out_file) {
    spdlog::error("Unable to save file to persistent storage");
    return false;
  }
  auto &number_stream = bg_worker.number_stream();
  out_file << number_stream->dump_s();
  for (auto const &number : number_stream->dump()) {
    if (!number.empty())
      out_file << number << "\n";
  }
  out_file.close();
  if (number_stream->is_open()) {
    number_stream->close();
  }
  std::error_code ec{};
  std::filesystem::remove(bg_worker.filename(), ec);
  if (ec) {
    spdlog::error("Unable to remove file because: {}", ec.message());
  }
  return db_connector->save_stopped_task(stopped_task) &&
         db_connector->change_task_status(
             stopped_task.task_id, task_result_ptr->processed,
             task_result_ptr->ip_used, task_result_ptr->operation_status);
}

std::string get_shangai_time(net::io_context &context) {
  net::ip::tcp::resolver resolver{context};
  beast::tcp_stream http_tcp_stream(net::make_strand(context));
  beast::http::request<beast::http::empty_body> http_request_;

  try {
    auto resolves = resolver.resolve("worldtimeapi.org", "http");
    beast::tcp_stream http_tcp_stream(net::make_strand(context));
    http_tcp_stream.connect(resolves);
    beast::http::request<http::empty_body> http_request{};
    http_request.method(http::verb::get);
    http_request.target(R"(/api/timezone/Asia/Shanghai)");
    http_request.version(11);
    http_request.set(http::field::host, "worldtimeapi.org:80");
    http_request.set(http::field::user_agent,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:74.0) "
                     "Gecko/20100101 Firefox/74.0");
    http::write(http_tcp_stream, http_request);
    beast::flat_buffer buffer{};
    http::response<http::string_body> server_response{};
    http::read(http_tcp_stream, buffer, server_response);
    beast::error_code ec{};
    if (server_response.result_int() != 200) {
      spdlog::error("Server returned code: {}", server_response.result_int());
      return {};
    }
    http_tcp_stream.cancel();
    auto &response_body = server_response.body();
    auto r = json::parse(response_body).get<json::object_t>();
    return r["datetime"].get<json::string_t>();
  } catch (std::exception const &e) {
    spdlog::error("[get_shangai_time] {}", e.what());
    return {};
  }
}
} // namespace wudi_server
