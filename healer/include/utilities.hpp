#pragma once

#include <string>
#include <vector>

namespace wudi_server {
namespace utilities {
enum class task_status_e {
  NotStarted,
  Ongoing,
  Stopped,
  Erred,
  Completed,
  AutoStopped
};

struct atomic_task_t {
  enum task_type { stopped, fresh, completed };

  int type_ = task_type::fresh;
  uint32_t task_id{};
  uint32_t scans_per_ip{};
  uint32_t ip_used{};
  uint32_t website_id{};
  uint32_t processed{};
  uint32_t total{};
  uint32_t ok_count{};
  uint32_t not_ok_count{};
  uint32_t unknown_count{};
  std::string input_filename{};
  std::string ok_filename{};
  std::string ok2_filename{};
  std::string not_ok_filename{};
  std::string unknown_filename{};
  std::string website_address{};
  std::vector<uint32_t> number_ids{};
};

void normalize_paths(std::string &str);
std::string intlist_to_string(std::vector<int> const &vec);
std::vector<std::string_view> split_string_view(std::string_view const &str,
                                                char const *delimeter);
} // namespace utilities
} // namespace wudi_server
