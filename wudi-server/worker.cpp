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
    std::shared_ptr<background_worker_t> worker{};
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
      worker->run();
      // if we are here, we are done.
    } else {
      db_connector->save_stopped_task(scheduled_task);
    }
  }
}

} // namespace wudi_server
