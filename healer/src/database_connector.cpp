#include "database_connector.hpp"
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace wudi_server {

using utilities::atomic_task_t;
using namespace fmt::v6::literals;

void log_sql_error(otl_exception const &exception) {
  spdlog::error("SQLError code: {}", exception.code);
  spdlog::error("SQLError stmt: {}", exception.stm_text);
  spdlog::error("SQLError state: {}", exception.sqlstate);
  spdlog::error("SQLError msg: {}", exception.msg);
}

db_config_t parse_database_file(std::string const &filename,
                                std::string const &config_name) {
  using utilities::split_string_view;

  std::ifstream in_file{filename};
  if (!in_file)
    return {};
  std::string line{};
  bool found = false;
  db_config_t db_config{};
  while (std::getline(in_file, line)) {
    if (line.size() < 1)
      continue;
    if (line[0] == '#' && line[1] == '~') { // new config
      if (found)
        return db_config;
      found = config_name == line.c_str() + 2;
      continue;
    } else if (found) {
      auto name_pair = split_string_view(line, ":");
      if (name_pair.size() != 2)
        continue;

      std::string_view value = name_pair[1];

      if (name_pair[0] == "username")
        db_config.username = value;
      else if (name_pair[0] == "password")
        db_config.password = value;
      else if (name_pair[0] == "db_dns")
        db_config.db_dns = value;
    }
  }
  return db_config;
}

std::shared_ptr<database_connector_t>
database_connector_t::s_get_db_connector() {
  static std::shared_ptr<database_connector_t> db_connector{};
  if (!db_connector) {
    otl_connect::otl_initialize(1);
    db_connector = std::make_unique<database_connector_t>();
  }
  return db_connector;
}

database_connector_t &
database_connector_t::username(std::string const &username) {
  db_config.username = boost::trim_copy(username);
  return *this;
}

database_connector_t &
database_connector_t::password(std::string const &password) {
  db_config.password = boost::trim_copy(password);
  return *this;
}

database_connector_t &
database_connector_t::database_name(std::string const &db_name) {
  db_config.db_dns = boost::trim_copy(db_name);
  return *this;
}

bool database_connector_t::connect() {
  if (db_config.db_dns.empty() || db_config.password.empty() ||
      db_config.username.empty()) {
    throw std::runtime_error{"configuration incomplete"};
  }
  if (is_running)
    return is_running;

  std::string const login_str{"{}/{}@{}"_format(
      db_config.username, db_config.password, db_config.db_dns)};
  try {
    this->otl_connector_.rlogon(login_str.c_str());
    is_running = true;
    return is_running;
  } catch (otl_exception const &exception) {
    log_sql_error(exception);
    return is_running;
  }
}

bool database_connector_t::change_task_status(uint32_t const task_id,
                                              uint32_t const processed,
                                              uint32_t const ip_used,
                                              utilities::task_status_e status) {
  std::string const sql_statement =
      "UPDATE tb_tasks SET status={}, processed={}, ip_used={} WHERE "
      "id={}"_format(status, processed, ip_used, task_id);
  try {
    std::lock_guard<std::mutex> lock_g{db_mutex_};
    otl_cursor::direct_exec(otl_connector_, sql_statement.c_str(),
                            otl_exception::enabled);
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

bool database_connector_t::get_stopped_tasks(
    std::vector<int> const &tasks, std::vector<atomic_task_t> &stopped_tasks) {
  std::string const sql_statement =
      "SELECT id, website_id, processed, total_numbers, input_filename, "
      "ok_file, not_ok_file, unknown_file, ok2_file, ok_count, not_ok_count, "
      "unknown_count, per_ip, ip_used FROM tb_tasks WHERE id IN ({})"_format(
          utilities::intlist_to_string(tasks));
  try {
    std::lock_guard<std::mutex> lock_g{db_mutex_};

    otl_stream db_stream(1'000, sql_statement.c_str(), otl_connector_);
    atomic_task_t stopped_task{};
    while (db_stream >> stopped_task.task_id >> stopped_task.website_id >>
           stopped_task.processed >> stopped_task.total >>
           stopped_task.input_filename >> stopped_task.ok_filename >>
           stopped_task.not_ok_filename >> stopped_task.unknown_filename >>
           stopped_task.ok2_filename >> stopped_task.ok_count >>
           stopped_task.not_ok_count >> stopped_task.unknown_count >>
           stopped_task.scans_per_ip >> stopped_task.ip_used) {
      stopped_task.type_ = stopped_task.stopped;
      stopped_tasks.push_back(stopped_task);
    }
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

} // namespace wudi_server
