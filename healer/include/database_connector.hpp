#pragma once

#include "utilities.hpp"
#include <memory>
#include <mutex>

#define OTL_BIG_INT long long
#define OTL_ODBC_MYSQL
#define OTL_STL
#ifdef _WIN32
#define OTL_ODBC_WINDOWS
#else
#define OTL_ODBC_UNIX
#endif
#define OTL_SAFE_EXCEPTION_ON
#include <otlv4/otlv4.h>

namespace wudi_server {

using utilities::atomic_task_t;

void log_sql_error(otl_exception const &exception);

struct db_config_t {
  std::string username;
  std::string password;
  std::string db_dns;

  operator bool() {
    return !(username.empty() && password.empty() && db_dns.empty());
  }
};

struct database_connector_t {
  db_config_t db_config;
  otl_connect otl_connector_;
  std::mutex db_mutex_;
  bool is_running = false;

public:
  static std::shared_ptr<database_connector_t> s_get_db_connector();
  database_connector_t &username(std::string const &username);
  database_connector_t &password(std::string const &password);
  database_connector_t &database_name(std::string const &db_name);
  bool connect();

public:
  bool change_task_status(uint32_t const task_id, uint32_t const processed,
                          uint32_t const ip_used,
                          utilities::task_status_e const);
  bool get_stopped_tasks(std::vector<int> const &,
                         std::vector<atomic_task_t> &);
};

[[nodiscard]] db_config_t parse_database_file(std::string const &filename,
                                              std::string const &config_name);

} // namespace wudi_server
