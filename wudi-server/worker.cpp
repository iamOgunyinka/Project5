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
  spdlog::info(intlist_to_string(task.number_ids));
  std::vector<upload_result_t> numbers =
      db_connector->get_uploads(task.number_ids);
  if (!website || numbers.empty()) {
    spdlog::error("No such website or numbers is empty");
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
  auto normalize_paths = [](std::string &str) {
    for (std::string::size_type i = 0; i != str.size(); ++i) {
      if (str[i] == '#')
        str[i] = '\\';
    }
  };
  using utilities::task_status_e;

  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  atomic_task_t::stopped_task &task =
      std::get<atomic_task_t::stopped_task>(scheduled_task.task);

  normalize_paths(task.input_filename);
  normalize_paths(task.ok_filename);
  normalize_paths(task.not_ok_filename);
  normalize_paths(task.unknown_filename);

  if (task.website_address.empty() ||
      !std::filesystem::exists(task.input_filename)) {
    spdlog::error("website address is empty or file does not exist anymore");
    return {};
  }
  std::shared_ptr<atomic_task_result_t> task_result{};
  auto &response_queue = utilities::get_response_queue();
  auto &task_counter = utilities::get_task_counter();
  auto iter = response_queue.equal_range(scheduled_task.task_id);
  if (iter.first == response_queue.cend()) {
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
    for (auto first = iter.first; first != iter.second; ++first) {
      if (first->second->website_id == scheduled_task.website_id) {
        task_result = first->second;
        break;
      }
    }
  }
  return std::make_unique<background_worker_t>(
      std::move(scheduled_task), task_result, get_network_context());
}

void background_task_executor(
    std::atomic_bool &stopped, std::mutex &mutex,
    std::shared_ptr<database_connector_t> &db_connector) {

  using utilities::task_status_e;

  auto &scheduled_tasks = utilities::get_scheduled_tasks();
  while (!stopped) {
    auto scheduled_task = std::move(scheduled_tasks.get());
    auto &task_counter = utilities::get_task_counter();
    std::shared_ptr<background_worker_t> worker{};
    if (scheduled_task.type_ == atomic_task_t::task_type::fresh) {
      worker = start_new_task(scheduled_task);
    } else {
      worker = continue_recent_task(scheduled_task);
    }
    if (worker) {
      task_counter.insert(scheduled_task.task_id, [=](uint32_t task_id) {
        db_connector->change_task_status(
            task_id, worker->task_result()->processed, task_status_e::Ongoing);
      });
      worker->run();
      task_counter.remove(scheduled_task.task_id, [=](uint32_t const task_id) {
        db_connector->change_task_status(
            task_id, worker->task_result()->processed,
            worker->task_result()->operation_status);
      });
      // if we are here, we are done.
    }
  }
}

} // namespace wudi_server
