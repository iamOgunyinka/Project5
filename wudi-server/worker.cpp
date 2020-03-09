#include "worker.hpp"
#include "backgroundworker.hpp"
#include "database_connector.hpp"
#include "utilities.hpp"

namespace wudi_server {
using utilities::atomic_task_result_t;
using utilities::atomic_task_t;

std::unique_ptr<background_worker_t>
start_new_task(atomic_task_t &scheduled_task) {
  using utilities::intlist_to_string;
  using utilities::task_status_e;

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  auto &task = std::get<atomic_task_t::fresh_task>(scheduled_task.task);
  std::optional<website_result_t> website =
      db_connector->get_website(task.website_id);
  std::vector<upload_result_t> numbers =
      db_connector->get_uploads(task.number_ids);
  if (!website) {
    spdlog::error("No such website: {}", task.website_id);
    return {};
  }
  if (numbers.empty()) {
    spdlog::error("No numbers obtained");
    return {};
  }

  auto &response_queue = utilities::get_response_queue();

  auto task_result = std::make_shared<atomic_task_result_t>();
  task_result->task_id = scheduled_task.task_id;
  task_result->website_id = task.website_id;
  response_queue.emplace(scheduled_task.task_id, task_result);
  return std::make_unique<background_worker_t>(std::move(*website),
                                               std::move(numbers), task_result,
                                               get_network_context());
}

std::unique_ptr<background_worker_t>
continue_recent_task(atomic_task_t &scheduled_task) {
  using utilities::normalize_paths;
  using utilities::task_status_e;

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  atomic_task_t::stopped_task &task =
      std::get<atomic_task_t::stopped_task>(scheduled_task.task);

  normalize_paths(task.input_filename);
  normalize_paths(task.ok_filename);
  normalize_paths(task.not_ok_filename);
  normalize_paths(task.unknown_filename);

  if (task.website_address.empty()) {
    spdlog::error("website address is empty");
    return {};
  }
  if (!std::filesystem::exists(task.input_filename)) {
    spdlog::error("file does not exist anymore");
    return {};
  }
  std::shared_ptr<atomic_task_result_t> task_result{};
  auto &response_queue = utilities::get_response_queue();
  auto iter = response_queue.find(scheduled_task.task_id);
  if (iter == response_queue.cend()) {
    task_result = std::make_shared<atomic_task_result_t>();
    task_result->task_id = scheduled_task.task_id;
    task_result->website_id = scheduled_task.website_id;
    task_result->processed = scheduled_task.processed;
    task_result->total_numbers = scheduled_task.total;
    task_result->not_ok_filename = task.not_ok_filename;
    task_result->ok_filename = task.ok_filename;
    task_result->unknown_filename = task.unknown_filename;
    response_queue.emplace(scheduled_task.task_id, task_result);
  } else {
    task_result = iter->second;
  }
  return std::make_unique<background_worker_t>(
      std::move(scheduled_task), task_result, get_network_context());
}

std::unique_ptr<background_worker_t>
resume_unstarted_task(utilities::atomic_task_t &scheduled_task) {
  auto string_to_intlist = [](std::string const &str, char const *delim = ",") {
    std::vector<uint32_t> list{};
    auto split = utilities::split_string_view(str, delim);
    for (auto const &s : split) {
      list.push_back(std::stoul(boost::trim_copy(s.to_string())));
    }
    return list;
  };

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  auto &task = std::get<atomic_task_t::stopped_task>(scheduled_task.task);
  std::optional<website_result_t> website =
      db_connector->get_website(scheduled_task.website_id);
  std::vector<upload_result_t> numbers =
      db_connector->get_uploads(string_to_intlist(task.input_filename));
  if (!website || numbers.empty()) {
    spdlog::error("No such website or numbers is empty");
    return {};
  }
  task.input_filename.clear();
  task.not_ok_filename.clear();
  task.ok_filename.clear();
  task.unknown_filename.clear();
  task.website_address.clear();
  auto &response_queue = utilities::get_response_queue();

  auto task_result = std::make_shared<atomic_task_result_t>();
  task_result->task_id = scheduled_task.task_id;
  task_result->website_id = scheduled_task.website_id;
  response_queue.emplace(scheduled_task.task_id, task_result);
  return std::make_unique<background_worker_t>(std::move(*website),
                                               std::move(numbers), task_result,
                                               get_network_context());
}

void background_task_executor(
    std::atomic_bool &stopped, std::mutex &mutex,
    std::shared_ptr<database_connector_t> &db_connector) {

  auto &scheduled_tasks = utilities::get_scheduled_tasks();
  while (!stopped) {
    auto scheduled_task = std::move(scheduled_tasks.get());
    std::unique_ptr<background_worker_t> worker{};
    if (scheduled_task.type_ == atomic_task_t::task_type::fresh) {
      worker = start_new_task(scheduled_task);
    } else {
      auto &task = std::get<atomic_task_t::stopped_task>(scheduled_task.task);
      if (task.website_address == "{free}") {
        worker = resume_unstarted_task(scheduled_task);
      } else {
        worker = continue_recent_task(scheduled_task);
      }
    }
    if (worker) {
      db_connector->change_task_status(scheduled_task.task_id,
                                       worker->task_result()->processed,
                                       utilities::task_status_e::Ongoing);
      switch (worker->run()) {
      case utilities::task_status_e::Stopped:
        run_stopped_op(db_connector, *worker);
        break;
      case utilities::task_status_e::Completed:
        run_completion_op(db_connector, *worker);
        break;
      case utilities::task_status_e::Erred:
        run_error_occurred_op(db_connector, *worker);
        break;
      }
      auto task_result_ptr = worker->task_result();
      task_result_ptr->not_ok_file.close();
      task_result_ptr->ok_file.close();
      task_result_ptr->unknown_file.close();
      worker->number_stream()->close();
      task_result_ptr.reset();
      // if we are here, we are done.
    } else {
      using utilities::replace_special_chars;
      if (scheduled_task.type_ != atomic_task_t::task_type::fresh) {
        atomic_task_t::stopped_task &save_tasks =
            std::get<atomic_task_t::stopped_task>(scheduled_task.task);
        replace_special_chars(save_tasks.input_filename);
        replace_special_chars(save_tasks.not_ok_filename);
        replace_special_chars(save_tasks.ok_filename);
        replace_special_chars(save_tasks.unknown_filename);
        db_connector->save_stopped_task(scheduled_task);
      }
      db_connector->change_task_status(scheduled_task.task_id,
                                       scheduled_task.processed,
                                       utilities::task_status_e::Erred);
    }
  }
}

void run_completion_op(std::shared_ptr<database_connector_t> &db_connector,
                       background_worker_t &bg_worker) {

  using utilities::get_random_integer;
  using utilities::get_random_string;

  auto task_result_ptr = bg_worker.task_result();
  task_result_ptr->operation_status = utilities::task_status_e::Completed;
  atomic_task_t completed_task{};
  auto &task = completed_task.task.emplace<atomic_task_t::stopped_task>();
  task.not_ok_filename = task_result_ptr->not_ok_filename.string();
  task.ok_filename = task_result_ptr->ok_filename.string();
  task.unknown_filename = task_result_ptr->unknown_filename.string();
  completed_task.task_id = task_result_ptr->task_id;
  completed_task.website_id = task_result_ptr->website_id;
  using utilities::replace_special_chars;

  replace_special_chars(task.ok_filename);
  replace_special_chars(task.not_ok_filename);
  replace_special_chars(task.unknown_filename);

  if (db_connector->change_task_status(task_result_ptr->task_id,
                                       task_result_ptr->total_numbers,
                                       task_result_ptr->operation_status) &&
      db_connector->add_completed_task(completed_task)) {
    if (std::filesystem::exists(bg_worker.filename())) {
      if (bg_worker.number_stream()->is_open()) {
        bg_worker.number_stream()->close();
      }
      std::filesystem::remove(bg_worker.filename());
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
  auto task_result_ptr = bg_worker.task_result();
  db_connector->change_task_status(task_result_ptr->task_id,
                                   task_result_ptr->processed,
                                   utilities::task_status_e::Erred);
}

void run_stopped_op(std::shared_ptr<database_connector_t> &db_connector,
                    background_worker_t &bg_worker) {

  using utilities::get_random_integer;
  using utilities::get_random_string;
  using utilities::task_status_e;

  auto task_result_ptr = bg_worker.task_result();
  bg_worker.task_result()->operation_status = task_status_e::Stopped;
  if (task_result_ptr->save_state()) {
    std::filesystem::path filename =
        "." / std::filesystem::path("stopped_files") /
        "{}.txt"_format(get_random_string(get_random_integer()));
    while (std::filesystem::exists(filename)) {
      filename = "." / std::filesystem::path("stopped_files") /
                 "{}.txt"_format(get_random_string(get_random_integer()));
    }
    if (utilities::create_file_directory(filename) &&
        save_status_to_persistence(filename.string(), bg_worker,
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

bool save_status_to_persistence(
    std::string const &filename, background_worker_t &bg_worker,
    std::shared_ptr<database_connector_t> db_connector) {
  auto website_address = [](website_type const type) {
    switch (type) { // we can easily add more websites in the future
    case website_type::AutoHomeRegister:
      return "autohome";
    case website_type::JJGames:
      return "jjgames";
    }
    return "";
  };

  using utilities::atomic_task_t;
  auto task_result_ptr = bg_worker.task_result();
  atomic_task_t stopped_task{};
  stopped_task.type_ = atomic_task_t::task_type::stopped;
  stopped_task.task.emplace<atomic_task_t::stopped_task>();
  stopped_task.processed = task_result_ptr->processed;
  stopped_task.total = task_result_ptr->total_numbers;
  stopped_task.task_id = task_result_ptr->task_id;
  stopped_task.website_id = task_result_ptr->website_id;

  auto &task =
      std::get<utilities::atomic_task_t::stopped_task>(stopped_task.task);
  task.input_filename = filename;

  task.not_ok_filename = task_result_ptr->not_ok_filename.string();
  task.ok_filename = task_result_ptr->ok_filename.string();
  task.unknown_filename = task_result_ptr->unknown_filename.string();
  using utilities::replace_special_chars;

  replace_special_chars(task.not_ok_filename);
  replace_special_chars(task.ok_filename);
  replace_special_chars(task.unknown_filename);
  replace_special_chars(task.input_filename);

  task.website_address = website_address(bg_worker.type());

  std::ofstream out_file(filename);
  if (!out_file) {
    spdlog::error("Unable to save file to hard disk");
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
         db_connector->change_task_status(stopped_task.task_id,
                                          task_result_ptr->processed,
                                          task_result_ptr->operation_status);
}
} // namespace wudi_server
