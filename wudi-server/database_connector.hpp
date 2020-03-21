#pragma once

#include "utilities.hpp"
#include <memory>
#include <thread>
#include <vector>

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
using utilities::task_result_t;

otl_stream &operator>>(otl_stream &, task_result_t &item);
otl_stream &operator>>(otl_stream &, utilities::upload_result_t &);
otl_stream &operator>>(otl_stream &, utilities::website_result_t &);
otl_stream &operator>>(otl_stream &, utilities::atomic_task_t &);

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

private:
  void keep_sql_server_busy();

public:
  static std::shared_ptr<database_connector_t> s_get_db_connector();
  database_connector_t &username(std::string const &username);
  database_connector_t &password(std::string const &password);
  database_connector_t &database_name(std::string const &db_name);
  bool connect();

public:
  bool save_stopped_task(utilities::atomic_task_t const &);
  bool get_completed_tasks(std::vector<uint32_t> const &,
                           std::vector<utilities::atomic_task_t> &);
  bool get_stopped_tasks(std::vector<uint32_t> const &tasks,
                         std::vector<utilities::atomic_task_t> &);
  bool remove_stopped_tasks(std::vector<utilities::atomic_task_t> const &tasks);
  bool remove_stopped_tasks(std::vector<uint32_t> const &tasks);
  bool remove_completed_tasks(std::vector<uint32_t> const &tasks);
  bool remove_filtered_tasks(boost::string_view const,
                             std::vector<uint32_t> const &);
  bool remove_uploads(std::vector<boost::string_view> const &ids = {});
  std::vector<utilities::website_result_t>
  get_websites(std::vector<uint32_t> const &ids);
  std::optional<utilities::website_result_t> get_website(uint32_t const id);
  bool add_website(std::string_view const address,
                   std::string_view const alias);
  bool add_task(utilities::scheduled_task_t &task);
  bool change_task_status(uint32_t const task_id, uint32_t const processed,
                          utilities::task_status_e const);
  bool add_completed_task(utilities::atomic_task_t &task);
  bool add_erred_task(utilities::atomic_task_t &);
  // only as a fail safe
  bool get_stopped_tasks_from_tasks(std::vector<uint32_t> const &tasks,
                                    std::vector<utilities::atomic_task_t> &);
  std::vector<utilities::task_result_t>
  get_all_tasks(boost::string_view,
                std::vector<boost::string_view> const & = {});
  std::pair<int, int> get_login_role(std::string_view const,
                                     std::string_view const);
  bool add_upload(utilities::upload_request_t const &upload_request);

  template <typename T>
  std::vector<utilities::upload_result_t>
  get_uploads(std::vector<T> const &ids) {
    std::string sql_statement{};
    if (ids.empty()) {
      sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                      "name_on_disk FROM tb_uploads";
    } else {
      if constexpr (std::is_same_v<T, boost::string_view>) {
        sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                        "name_on_disk FROM tb_uploads WHERE id IN "
                        "({})"_format(utilities::svector_to_string(ids));
      } else {
        sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                        "name_on_disk FROM tb_uploads WHERE id IN "
                        "({})"_format(utilities::intlist_to_string(ids));
      }
    }
    std::vector<utilities::upload_result_t> result{};
    try {
      otl_stream db_stream(1'000'000, sql_statement.c_str(), otl_connector_);
      utilities::upload_result_t item{};
      while (db_stream >> item) {
        result.push_back(std::move(item));
      }
    } catch (otl_exception const &e) {
      log_sql_error(e);
    }
    return result;
  }
};

[[nodiscard]] db_config_t parse_database_file(std::string const &filename,
                                              std::string const &config_name);

} // namespace wudi_server
