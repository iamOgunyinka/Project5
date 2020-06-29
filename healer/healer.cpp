#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif // _WIN32

#include "database_connector.hpp"
#include <CLI11/CLI11.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/process/io.hpp>
#include <boost/process/system.hpp>
#include <filesystem>

using namespace wudi_server;

int count_lines(std::string const &filename) {
  std::ifstream ifile(filename);
  if (!ifile)
    return 0;
  std::string line{};
  line.reserve(15);
  int line_count{};
  while (std::getline(ifile, line)) {
    ++line_count;
  }
  return line_count;
}

template <typename... Args> auto count_file_lines(Args &&... filenames) {
  return (... + count_lines(filenames));
}

namespace bp = boost::process;

void fix_database_problem(std::vector<int> const &task_ids) {
  auto database_connector =
      wudi_server::database_connector_t::s_get_db_connector();
  std::vector<atomic_task_t> stopped_tasks{};
  auto tasks = database_connector->get_stopped_tasks(task_ids, stopped_tasks);
  if (!tasks) {
    std::cerr << "There was an error getting all specified tasks\n";
    return;
  }
  for (auto &task : stopped_tasks) {
    utilities::normalize_paths(task.input_filename);
    utilities::normalize_paths(task.not_ok_filename);
    utilities::normalize_paths(task.ok2_filename);
    utilities::normalize_paths(task.ok_filename);
    utilities::normalize_paths(task.unknown_filename);

    for (auto const &filename : {task.not_ok_filename, task.ok_filename,
                                 task.ok2_filename, task.unknown_filename}) {
      std::string const command = "sort -u \"" + filename + "\"";
      std::string out_filename = filename + "_";
      try {
        bp::system(command, bp::std_out > out_filename);
      } catch (std::exception const &e) {
        std::cerr << e.what() << std::endl;
        return;
      }
      std::error_code ec{};
      std::filesystem::remove(filename, ec);
      if (ec) {
        std::cerr << ec.message() << std::endl;
        continue;
      }
      ec = {};
      std::filesystem::rename(out_filename, filename, ec);
      if (ec) {
        std::cerr << ec.message() << std::endl;
        continue;
      }
    }

    auto processed = count_file_lines(task.not_ok_filename, task.ok2_filename,
                                      task.ok_filename, task.unknown_filename);
    std::cout << "Total processed for task " << task.task_id << " = "
              << processed << "\n";
    auto const needed = task.total - processed;
    std::string const command =
        "tail -n " + std::to_string(needed) + " " + task.input_filename;
    std::string const output_filename = task.input_filename + "_";
    try {
      bp::system(command, bp::std_out > output_filename);
    } catch (std::exception const &e) {
      std::cerr << e.what() << std::endl;
      return;
    }
    std::error_code ec{};
    std::filesystem::remove(task.input_filename, ec);
    if (ec) {
      std::cerr << ec.message() << std::endl;
      continue;
    }
    ec = {};
    std::filesystem::rename(output_filename, task.input_filename, ec);
    if (ec) {
      std::cerr << ec.message() << std::endl;
      continue;
    }
    database_connector->change_task_status(task.task_id, processed,
                                           task.ip_used,
                                           utilities::task_status_e::Stopped);
  }
}

int main(int argc, char *argv[]) {
  CLI::App cli_parser{
      "header: a utility program for healing corrupted tasks in woody server"};
  std::string db_config_filename{"/root/woody/scripts/config/database.ini"};
  std::string launch_type{"development"};
  std::string task_ids{};
  cli_parser.add_option("-d", db_config_filename, "Database config filename",
                        true);
  cli_parser.add_option("-l", launch_type, "Launch type(default: development)",
                        true);
  cli_parser.add_option("-t", task_ids, "Comma-separated or range task ids",
                        false);
  CLI11_PARSE(cli_parser, argc, argv);

  auto database_connector{
      wudi_server::database_connector_t::s_get_db_connector()};
  boost::trim(task_ids);
  if (task_ids.empty()) {
    std::cerr << "Task ID is empty\n";
    return -1;
  }
  auto db_config =
      wudi_server::parse_database_file(db_config_filename, launch_type);
  std::cout << db_config_filename << " " << launch_type << std::endl;
  if (!db_config) {
    std::cerr << "Unable to get database configuration values\n";
    return EXIT_FAILURE;
  }

  database_connector->username(db_config.username)
      .password(db_config.password)
      .database_name(db_config.db_dns);
  if (!database_connector->connect()) {
    return EXIT_FAILURE;
  }

  std::vector<int> ids{};
  if (task_ids.find(',') != std::string::npos) {
    auto result = wudi_server::utilities::split_string_view(task_ids, ",");
    for (auto const &r : result) {
      ids.push_back(std::stoi(boost::trim_copy(std::string(r))));
    }
  } else if (task_ids.find('-') != std::string::npos) {
    auto result = wudi_server::utilities::split_string_view(task_ids, "-");
    if (result.size() != 2) {
      std::cerr << "Improper formed range sequence\n";
      return -1;
    }
    auto start = std::stoi(boost::trim_copy(std::string(result[0])));
    auto end = std::stoi(boost::trim_copy(std::string(result[1])));
    if (end > start) {
      for (int i = start; i <= end; ++i) {
        ids.push_back(i);
      }
    } else {
      for (int i = end; i >= start; --i) {
        ids.push_back(i);
      }
    }
  } else {
    ids.push_back(std::stoi(task_ids));
  }
  if (ids.empty()) {
    std::cerr << "No task is specified. Exiting...\n";
    return 0;
  }
  fix_database_problem(ids);
  return EXIT_SUCCESS;
}
