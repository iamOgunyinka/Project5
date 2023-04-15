#include "task_data.hpp"
#include <sstream>

namespace woody_server {
namespace utilities {
threadsafe_list_t<atomic_task_t> &getScheduledTasks() {
  static threadsafe_list_t<atomic_task_t> tasks{};
  return tasks;
}

response_queue_map_t &getResponseQueue() {
  static response_queue_map_t task_result;
  return task_result;
}

std::string integerListToString(std::vector<atomic_task_t> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i) {
    ss << vec[i].task_id << ", ";
  }
  ss << vec.back().task_id;
  return ss.str();
}
} // namespace utilities

bool operator<(internal_task_result_t const &task_1,
               internal_task_result_t const &task_2) {
  return std::tie(task_1.task_id, task_1.website_id) <
         std::tie(task_2.task_id, task_2.website_id);
}
} // namespace woody_server
