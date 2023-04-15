#include "worker.hpp"
#include "backgroundworker.hpp"
#include "database_connector.hpp"
#include "file_utils.hpp"
#include "number_stream.hpp"
#include "random.hpp"
#include <boost/asio/ssl/context.hpp>
#include <spdlog/spdlog.h>

namespace woody_server {

void on_task_ran(task_status_e status, atomic_task_t &,
                 std::shared_ptr<database_connector_t> &db_connector,
                 background_worker_t *worker_ptr) {
  switch (status) {
  case task_status_e::Stopped:
  case task_status_e::AutoStopped:
    return run_stopped_op(db_connector, *worker_ptr);
  case task_status_e::Completed:
    return run_completion_op(db_connector, *worker_ptr);
  case task_status_e::Erred:
    return run_error_occurred_op(db_connector, *worker_ptr);
  default:
    return;
  }
}

std::unique_ptr<background_worker_t>
start_new_task(atomic_task_t &scheduled_task, net::ssl::context &ssl_context) {
  using utilities::integerListToString;

  auto db_connector = database_connector_t::s_get_db_connector();
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

  auto &response_queue = utilities::getResponseQueue();

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
  using utilities::normalizePaths;

  normalizePaths(scheduled_task.input_filename);
  normalizePaths(scheduled_task.ok_filename);
  normalizePaths(scheduled_task.ok2_filename);
  normalizePaths(scheduled_task.not_ok_filename);
  normalizePaths(scheduled_task.unknown_filename);

  auto db_connector = database_connector_t::s_get_db_connector();
  std::optional<website_result_t> website =
      db_connector->get_website(scheduled_task.website_id);

  if (!website) {
    spdlog::error("website address is isEmpty");
    return {};
  }

  if (!std::filesystem::exists(scheduled_task.input_filename)) {
    spdlog::error("file does not exist anymore");
    return {};
  }
  std::shared_ptr<internal_task_result_t> task_result{};
  auto &response_queue = utilities::getResponseQueue();
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
resume_unstarted_task(atomic_task_t &scheduled_task,
                      net::ssl::context &ssl_context) {
  auto string_to_intlist = [](std::string const &str, char const *delim = ",") {
    std::vector<uint32_t> list{};
    auto split = utilities::splitStringView(str, delim);
    list.reserve(split.size());
    for (auto const &s : split)
      list.push_back(std::stoul(utilities::trimCopy(s.to_string())));
    return list;
  };

  auto db_connector = database_connector_t::s_get_db_connector();
  std::optional<website_result_t> website =
      db_connector->get_website(scheduled_task.website_id);
  std::vector<upload_result_t> numbers = db_connector->get_uploads(
      string_to_intlist(scheduled_task.input_filename));
  if (!website || numbers.empty()) {
    spdlog::error("No such website or numbers is isEmpty");
    return {};
  }
  scheduled_task.input_filename.clear();
  scheduled_task.not_ok_filename.clear();
  scheduled_task.ok_filename.clear();
  scheduled_task.ok2_filename.clear();
  scheduled_task.unknown_filename.clear();
  scheduled_task.website_address.clear();
  auto &response_queue = utilities::getResponseQueue();

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
  auto &scheduled_tasks = utilities::getScheduledTasks();

  auto on_error = [db_connector](atomic_task_t &scheduled_task) {
    using utilities::replaceSpecialChars;
    if (scheduled_task.task_type != task_type_e::fresh) {
      replaceSpecialChars(scheduled_task.input_filename);
      replaceSpecialChars(scheduled_task.not_ok_filename);
      replaceSpecialChars(scheduled_task.ok_filename);
      replaceSpecialChars(scheduled_task.ok2_filename);
      replaceSpecialChars(scheduled_task.unknown_filename);
      db_connector->save_stopped_task(scheduled_task);
    }
    db_connector->change_task_status(
        scheduled_task.task_id, scheduled_task.processed,
        scheduled_task.ip_used, task_status_e::Erred);
  };

  while (!stopped) {
    atomic_task_t scheduled_task{scheduled_tasks.get()};
    std::unique_ptr<background_worker_t> worker{};
    if (scheduled_task.task_type == task_type_e::fresh) {
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
          scheduled_task.task_id, scheduled_task.processed,
          scheduled_task.ip_used, task_status_e::Ongoing);
      auto on_post_execution = [&scheduled_task, &db_connector,
                                worker_ptr =
                                    worker.get()](task_status_e status) {
        on_task_ran(status, scheduled_task, db_connector, worker_ptr);
      };

      worker->proxyCallbackSignal(r.newEndpointSignal());
      worker->proxyInfoMap(r.getThreadProxyInfo());
      on_post_execution(worker->run());

      auto task_result_ptr = worker->taskResult();

      try {
        if (task_result_ptr) {
          task_result_ptr->ok2_file.close();
          task_result_ptr->not_ok_file.close();
          task_result_ptr->ok_file.close();
          task_result_ptr->unknown_file.close();
          worker->numberStream()->close();
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

  using utilities::getRandomInteger;
  using utilities::getRandomString;

  auto task_result_ptr = bg_worker.taskResult();
  bool const status_changed = db_connector->change_task_status(
      task_result_ptr->task_id, task_result_ptr->total_numbers,
      task_result_ptr->ip_used, task_result_ptr->operation_status);
  if (status_changed) {
    if (std::filesystem::exists(bg_worker.filename())) {
      if (bg_worker.numberStream()->isOpen()) {
        bg_worker.numberStream()->close();
      }
      std::error_code ec{};
      std::filesystem::remove(bg_worker.filename(), ec);
    }
    spdlog::info("Saved completed task successfully");
  } else {
    std::ofstream out_file{"./erred_saving.txt", std::ios::out | std::ios::app};
    if (out_file) {
      auto const random_string = getRandomString(getRandomInteger());
      std::string const dump = random_string + ".txt";
      std::ofstream out_file_stream{dump};
      if (out_file_stream) {
        out_file_stream << bg_worker.numberStream()->dumpS();
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
  using utilities::replaceSpecialChars;

  auto task_result_ptr = bg_worker.taskResult();
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

  replaceSpecialChars(erred_task.ok_filename);
  replaceSpecialChars(erred_task.ok2_filename);
  replaceSpecialChars(erred_task.not_ok_filename);
  replaceSpecialChars(erred_task.unknown_filename);
  replaceSpecialChars(erred_task.input_filename);
  db_connector->add_erred_task(erred_task);
}

void run_stopped_op(std::shared_ptr<database_connector_t> &db_connector,
                    background_worker_t &bg_worker) {
  using utilities::getRandomInteger;
  using utilities::getRandomString;

  auto task_result_ptr = bg_worker.taskResult();
  if (task_result_ptr->savingState()) {
    std::string random_name = getRandomString(getRandomInteger()) + ".txt";
    std::filesystem::path filename =
        "." / std::filesystem::path("stopped_files") / random_name;
    while (std::filesystem::exists(filename)) {
      random_name = getRandomString(getRandomInteger()) + ".txt";
      filename = "." / std::filesystem::path("stopped_files") / random_name;
    }
    if (utilities::createFileDirectory(filename) &&
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
  auto task_result_ptr = bg_worker.taskResult();
  atomic_task_t stopped_task{};
  stopped_task.task_type = task_type_e::stopped;
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

  using utilities::replaceSpecialChars;

  replaceSpecialChars(stopped_task.not_ok_filename);
  replaceSpecialChars(stopped_task.ok_filename);
  replaceSpecialChars(stopped_task.ok2_filename);
  replaceSpecialChars(stopped_task.unknown_filename);
  replaceSpecialChars(stopped_task.input_filename);

  std::ofstream out_file(filename);
  if (!out_file) {
    spdlog::error("Unable to save file to persistent storage");
    return false;
  }
  auto &numberStream = bg_worker.numberStream();
  out_file << numberStream->dumpS();
  for (auto const &number : numberStream->dump()) {
    if (!number.empty())
      out_file << number << "\n";
  }
  out_file.close();
  if (numberStream->isOpen()) {
    numberStream->close();
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
} // namespace woody_server
