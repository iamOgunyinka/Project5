#include "database_connector.hpp"

namespace wudi_server {
using utilities::atomic_task_t;
using utilities::task_result_t;

otl_stream &operator>>(otl_stream &os, task_result_t &item) {
  return os >> item.id >> item.total_numbers >> item.task_status >>
         item.scheduled_date >> item.website_ids >> item.data_ids >>
         item.progress;
}

otl_stream &operator>>(otl_stream &os, utilities::upload_result_t &item) {
  return os >> item.upload_id >> item.filename >> item.total_numbers >>
         item.upload_date >> item.name_on_disk;
}

otl_stream &operator>>(otl_stream &os, utilities::website_result_t &web) {
  return os >> web.id >> web.address >> web.alias;
}

otl_stream &operator>>(otl_stream &db_stream, atomic_task_t &task) {
  db_stream >> task.task_id >> task.website_id >> task.processed >> task.total;
  if (task.type_ == atomic_task_t::task_type::fresh) {
    auto &new_task = std::get<atomic_task_t::fresh_task>(task.task);
    return db_stream >> new_task.website_id;
  } else {
    auto &old_task = std::get<atomic_task_t::stopped_task>(task.task);
    return db_stream >> old_task.input_filename >> old_task.website_address >>
           old_task.ok_filename >> old_task.not_ok_filename >>
           old_task.unknown_filename;
  }
}

void log_sql_error(otl_exception const &exception) {
  spdlog::error("SQLError code: {}", exception.code);
  spdlog::error("SQLError stmt: {}", exception.stm_text);
  spdlog::error("SQLError state: {}", exception.sqlstate);
  spdlog::error("SQLError msg: {}", exception.msg);
}

db_config_t parse_database_file(std::string const &filename,
                                std::string const &config_name) {
  using utilities::split_string_view;
  using utilities::view_to_string;

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

      std::string value = view_to_string(name_pair[1]);

      if (name_pair[0] == "username")
        db_config.username = std::move(value);
      else if (name_pair[0] == "password")
        db_config.password = std::move(value);
      else if (name_pair[0] == "db_dns")
        db_config.db_dns = std::move(value);
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
  db_config.username = username;
  return *this;
}

database_connector_t &
database_connector_t::password(std::string const &password) {
  db_config.password = password;
  return *this;
}

database_connector_t &
database_connector_t::database_name(std::string const &db_name) {
  db_config.db_dns = db_name;
  return *this;
}

void database_connector_t::keep_sql_server_busy() {
  spdlog::info("keeping SQL server busy");
  std::thread sql_thread{[this] {
    while (true) {
      try {
        auto dir = otl_cursor::direct_exec(otl_connector_, "select 1", true);
        spdlog::info("OTL Busy server says: {}", dir);
      } catch (otl_exception const &exception) {
        log_sql_error(exception);
        otl_connector_.logoff();
        otl_connector_.rlogon("{}/{}@{}"_format(db_config.username,
                                                db_config.password,
                                                db_config.db_dns)
                                  .c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }
      std::this_thread::sleep_for(std::chrono::minutes(15));
    }
  }};
  sql_thread.detach();
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
  spdlog::info(login_str);
  try {
    this->otl_connector_.rlogon(login_str.c_str());
    keep_sql_server_busy();
    is_running = true;
    return is_running;
  } catch (otl_exception const &exception) {
    log_sql_error(exception);
    return is_running;
  }
}

std::pair<int, int>
database_connector_t::get_login_role(std::string_view const username,
                                     std::string_view const password) {
  std::string const sql_statement{
      "select id, role from tb_users where "
      "username = '{}' and password = '{}'"_format(username, password)};
  std::pair<int, int> id_role_pair = {-1, -1};
  try {
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      otl_stream db_stream(5, sql_statement.c_str(), otl_connector_);
      db_stream >> id_role_pair.first >> id_role_pair.second;
    }
  } catch (otl_exception const &e) {
    log_sql_error(e);
  }
  return id_role_pair;
}

bool database_connector_t::add_upload(
    utilities::upload_request_t const &upload_request) {
  using utilities::bv2sv;
  std::string const sql_statement{
      "insert into tb_uploads (uploader_id, filename, upload_date, "
      "total_numbers, name_on_disk ) VALUES( {}, \"{}\", \"{}\", {}, \"{}\" )"_format(
          bv2sv(upload_request.uploader_id),
          bv2sv(upload_request.upload_filename),
          bv2sv(upload_request.upload_date), upload_request.total_numbers,
          bv2sv(upload_request.name_on_disk))};
  try {
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      otl_cursor::direct_exec(otl_connector_, sql_statement.c_str(),
                              otl_exception::enabled);
    }
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

bool database_connector_t::add_task(utilities::scheduled_task_t &task) {
  std::string time_str{};
  if (std::size_t const count =
          utilities::timet_to_string(time_str, task.scheduled_dt);
      count != std::string::npos) {
    time_str.resize(count);
  } else {
    time_str = std::to_string(task.scheduled_dt);
  }
  using utilities::intlist_to_string;
  std::string sql_statement{
      "INSERT INTO tb_tasks (scheduler_id, date_scheduled, websites, uploads, "
      "progress, total_numbers, status)"
      "VALUES( {}, \"{}\", \"{}\", \"{}\", 0, {}, {} )"_format(
          task.scheduler_id, time_str, intlist_to_string(task.website_ids),
          intlist_to_string(task.number_ids), task.total_numbers,
          static_cast<int>(utilities::task_status_e::NotStarted))};
  spdlog::info(sql_statement);
  try {
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      otl_cursor::direct_exec(otl_connector_, sql_statement.c_str(),
                              otl_exception::enabled);
      otl_stream stream(1, "SELECT MAX(id) FROM tb_tasks", otl_connector_);
      stream >> task.task_id;
    }
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

bool database_connector_t::change_task_status(uint32_t task_id,
                                              uint32_t processed,
                                              utilities::task_status_e status) {
  std::string const sql_statement =
      "UPDATE tb_tasks SET status = {}, progress = {} WHERE id = {}"_format(
          static_cast<uint32_t>(status), processed, task_id);
  try {
    std::lock_guard<std::mutex> lock_g{db_mutex_};
    int const status = otl_cursor::direct_exec(
        otl_connector_, sql_statement.c_str(), otl_exception::enabled);
    return status > 0;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

std::vector<utilities::task_result_t>
database_connector_t::get_all_tasks(boost::string_view user_id) {
  std::string const sql_statement =
      "SELECT id, total_numbers, status,"
      "date_scheduled, websites, uploads, progress FROM "
      "tb_tasks WHERE scheduler_id="
      "{}"_format(user_id.to_string());
  std::vector<utilities::task_result_t> result{};
  try {
    otl_stream db_stream(1'000, sql_statement.c_str(), otl_connector_);
    utilities::task_result_t item{};
    while (db_stream >> item) {
      result.push_back(std::move(item));
    }
  } catch (otl_exception const &e) {
    log_sql_error(e);
  }
  return result;
}

bool database_connector_t::save_stopped_task(
    atomic_task_t const &stopped_task) {
  auto &task = std::get<atomic_task_t::stopped_task>(stopped_task.task);
  std::string const sql_statement =
      "INSERT INTO tb_stopped_tasks(task_id, website_id, filename,"
      "total_numbers, processed, website_address, ok_filename, "
      "not_ok_filename, unknown_filename) VALUES({}, {}, \"{}\", {}, {},"
      "\"{}\", \"{}\", \"{}\", \"{}\")"_format(
          stopped_task.task_id, stopped_task.website_id, task.input_filename,
          stopped_task.total, stopped_task.processed, task.website_address,
          task.ok_filename, task.not_ok_filename, task.unknown_filename);
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
    std::vector<uint32_t> const &tasks,
    std::vector<atomic_task_t> &stopped_tasks) {
  std::string const sql_statement =
      "SELECT task_id, website_id, processed, total_numbers, filename, "
      "website_address, ok_filename, not_ok_filename, unknown_filename FROM "
      "tb_stopped_tasks WHERE task_id IN ({})"_format(
          utilities::intlist_to_string(tasks));
  try {
    otl_stream db_stream(1'000, sql_statement.c_str(), otl_connector_);
    atomic_task_t stopped_task{};
    stopped_task.type_ = atomic_task_t::task_type::stopped;
    stopped_task.task.emplace<atomic_task_t::stopped_task>();
    while (db_stream >> stopped_task) {
      stopped_tasks.push_back(stopped_task);
    }
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

bool database_connector_t::remove_stopped_tasks(
    std::vector<uint32_t> const &tasks) {
  std::string const sql_statement =
      "DELETE FROM tb_stopped_tasks WHERE task_id IN ({})"_format(
          utilities::intlist_to_string(tasks));
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

bool database_connector_t::remove_filtered_tasks(
    boost::string_view const user_id, std::vector<uint32_t> const &ids) {
  std::string const sql_statement =
      "DELETE FROM tb_tasks WHERE scheduler_id="
      "{} AND id in ({})"_format(user_id.to_string(),
                                 utilities::intlist_to_string(ids));
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

bool database_connector_t::remove_completed_tasks(
    std::vector<uint32_t> const &tasks) {
  std::string const sql_statement =
      "DELETE FROM tb_tasks WHERE task_id IN ({}) AND status={}"_format(
          utilities::intlist_to_string(tasks),
          utilities::task_status_e::Completed);
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

bool database_connector_t::remove_stopped_tasks(
    std::vector<atomic_task_t> const &task_ids) {
  if (task_ids.empty())
    return false;
  std::string const sql_statement =
      "DELETE FROM tb_stopped_tasks WHERE task_id IN ({})"_format(
          utilities::intlist_to_string(task_ids));
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
bool database_connector_t::remove_uploads(
    std::vector<boost::string_view> const &ids) {
  using utilities::svector_to_string;
  std::string sql_statement;
  if (!ids.empty()) {
    sql_statement = "DELETE FROM tb_uploads WHERE id in ({})"_format(
        svector_to_string(ids));
  } else {
    sql_statement = "ALTER TABLE tb_uploads AUTO_INCREMENT = 1";
  }
  try {
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      otl_cursor::direct_exec(otl_connector_, sql_statement.c_str(),
                              otl_exception::enabled);
    }
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

std::vector<utilities::website_result_t>
database_connector_t::get_websites(std::vector<uint32_t> const &ids) {
  std::string sql_statement{};
  using utilities::intlist_to_string;

  if (ids.empty()) {
    sql_statement = "SELECT id, address, nickname FROM tb_websites";
  } else {
    sql_statement =
        "SELECT id, address, nickname FROM tb_websites WHERE ID in ({})"_format(
            intlist_to_string(ids));
  }
  std::vector<utilities::website_result_t> results{};
  try {
    otl_stream db_stream(1'000, sql_statement.c_str(), otl_connector_);
    utilities::website_result_t website_info{};
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      while (db_stream >> website_info) {
        results.push_back(std::move(website_info));
      }
    }
  } catch (otl_exception const &e) {
    log_sql_error(e);
  }
  return results;
}

std::optional<utilities::website_result_t>
database_connector_t::get_website(uint32_t const id) {
  std::string sql_statement{
      "SELECT id, address, nickname FROM tb_websites WHERE ID={}"_format(id)};
  using utilities::intlist_to_string;

  try {
    otl_stream db_stream(5, sql_statement.c_str(), otl_connector_);
    utilities::website_result_t website_info{};
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      db_stream >> website_info;
    }
    return website_info;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return std::nullopt;
  }
}

bool database_connector_t::add_website(std::string_view const address,
                                       std::string_view const alias) {
  std::string sql_statement =
      "INSERT INTO tb_websites (nickname, address) VALUES (\"{}\", "
      "\"{}\")"_format(alias, address);
  try {
    {
      std::lock_guard<std::mutex> lock_g{db_mutex_};
      otl_cursor::direct_exec(otl_connector_, sql_statement.c_str(),
                              otl_exception::enabled);
    }
    return true;
  } catch (otl_exception const &e) {
    log_sql_error(e);
    return false;
  }
}

} // namespace wudi_server
