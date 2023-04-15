#pragma once

#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "string_utils.hpp"
#include "task_data.hpp"
#include "upload_data.hpp"

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

namespace woody_server {

otl_stream &operator>>(otl_stream &, task_result_t &item);
otl_stream &operator>>(otl_stream &, upload_result_t &);
otl_stream &operator>>(otl_stream &, website_result_t &);

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
  bool save_unstarted_task(atomic_task_t const &);

  bool get_completed_tasks(std::vector<uint32_t> const &,
                           std::vector<atomic_task_t> &);
  bool get_stopped_tasks(std::vector<uint32_t> const &tasks,
                         std::vector<atomic_task_t> &);
  bool remove_filtered_tasks(boost::string_view const,
                             std::vector<uint32_t> const &);
  bool remove_uploads(std::vector<boost::string_view> const &ids = {});
  std::vector<website_result_t> get_websites(std::vector<uint32_t> const &ids);
  std::optional<website_result_t> get_website(uint32_t const id);
  bool add_website(std::string_view const address,
                   std::string_view const alias);
  bool add_task(scheduled_task_t &task);
  bool save_stopped_task(atomic_task_t const &);
  bool change_task_status(uint32_t const task_id, uint32_t const processed,
                          uint32_t const ip_used, task_status_e const);
  bool update_task_progress(internal_task_result_t const &, std::size_t);
  bool add_erred_task(atomic_task_t &);
  void delete_stopped_tasks(std::vector<uint32_t> const &task_ids);
  std::vector<task_result_t>
  get_all_tasks(boost::string_view,
                std::vector<boost::string_view> const & = {});
  std::pair<int, int> get_login_role(std::string_view const,
                                     std::string_view const);
  bool add_upload(upload_request_t const &upload_request);
  bool set_input_files(std::string input_filename, std::string ok_filename,
                       std::string ok2_filename, std::string not_ok_filename,
                       std::string unknown_filename, uint32_t const task_id);

  template <typename T>
  std::vector<upload_result_t> get_uploads(std::vector<T> const &ids,
                                           bool const include_deleted = true) {
    std::string sql_statement{};
    if (ids.empty()) {
      sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                      "name_on_disk, status FROM tb_uploads";
      if (!include_deleted) {
        sql_statement += " WHERE status=0";
      }
    } else {
      if constexpr (std::is_same_v<T, boost::string_view>) {
        sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                        "name_on_disk, status FROM tb_uploads WHERE id IN (" +
                        utilities::stringListToString(ids) + ")";
        if (!include_deleted) {
          sql_statement += " AND status=0";
        }
      } else {
        sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                        "name_on_disk, status FROM tb_uploads WHERE id IN (" +
                        utilities::integerListToString(ids) + ")";
        if (!include_deleted) {
          sql_statement += " AND status=0";
        }
      }
    }
    std::vector<upload_result_t> result{};
    try {
      otl_stream db_stream(1'000'000, sql_statement.c_str(), otl_connector_);
      upload_result_t item{};
      while (db_stream >> item)
        result.push_back(std::move(item));
    } catch (otl_exception const &e) {
      log_sql_error(e);
    }
    return result;
  }
};

[[nodiscard]] db_config_t parse_database_file(std::string const &filename,
                                              std::string const &config_name);

} // namespace woody_server
