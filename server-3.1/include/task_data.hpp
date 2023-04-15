#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "container.hpp"
#include "enumerations.hpp"

namespace woody_server {
struct scheduled_task_t {
  uint32_t task_id{};
  uint32_t scans_per_ip{};
  uint32_t progress{};
  uint32_t scheduler_id{};
  uint32_t scheduled_dt{};
  uint32_t total_numbers{};
  uint32_t website_id{};
  std::vector<uint32_t> number_ids{};
};

struct task_result_t {
  int task_status{};
  uint32_t id{};
  uint32_t total{};
  uint32_t ok{};
  uint32_t not_ok{};
  uint32_t unknown{};
  uint32_t processed{};
  uint32_t website_id{};
  uint32_t scans_per_ip{};
  uint32_t ip_used{};
  std::string data_ids{};
  std::string scheduled_date{};
};

struct atomic_task_t {
  task_type_e task_type = task_type_e::fresh;
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

class internal_task_result_t {
  bool m_stopped = false;
  bool m_save_state = true;

public:
  task_status_e operation_status{task_status_e::NotStarted};
  uint32_t task_id{};
  uint32_t website_id{};
  uint32_t ok_count{};
  uint32_t not_ok_count{};
  uint32_t unknown_count{};
  uint32_t processed{};
  uint32_t total_numbers{};
  uint32_t scans_per_ip{};
  uint32_t ip_used{};

  std::filesystem::path ok_filename;
  std::filesystem::path ok2_filename;
  std::filesystem::path not_ok_filename;
  std::filesystem::path unknown_filename;
  std::ofstream ok_file;
  std::ofstream ok2_file;
  std::ofstream not_ok_file;
  std::ofstream unknown_file;

  bool &stopped() { return m_stopped; }
  bool &savingState() { return m_save_state; }
  void stop() {
    m_stopped = true;
    operation_status = task_status_e::Stopped;
  }
};

bool operator<(internal_task_result_t const &task_1,
               internal_task_result_t const &task_2);

namespace utilities {
using response_queue_map_t =
    std::map<uint32_t, std::shared_ptr<internal_task_result_t>>;

std::string integerListToString(std::vector<atomic_task_t> const &vec);
response_queue_map_t &getResponseQueue();
threadsafe_list_t<atomic_task_t> &getScheduledTasks();

// defined in session.cpp (bottom of the file)
std::vector<atomic_task_t> restartTasks(std::vector<uint32_t> const &task_ids);
} // namespace utilities
} // namespace woody_server
