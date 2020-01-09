#include "server.hpp"
#include <CLI11/CLI11.hpp>
#include <atomic>
#include <spdlog/spdlog.h>
#include <thread>

int main(int argc, char *argv[]) {
  CLI::App cli_parser{"Wu-di: an asynchronous web server for Farasha trading"};
  wudi_server::command_line_interface args{};
  int const thread_count = std::thread::hardware_concurrency();
  args.thread_count = thread_count;

  cli_parser.add_option("-p", args.port, "port to bind server to", true);
  cli_parser.add_option("-a", args.ip_address, "IP address to use", true);
  cli_parser.add_option("-t", args.thread_count, "Number of threads to use",
                        true);
  cli_parser.add_option("-d", args.database_config_filename,
                        "Database config filename", true);
  cli_parser.add_option("--tL", args.scheduled_snapshot,
                        "Scheduled task snapshot");
  cli_parser.add_option("-y", args.launch_type,
                        "Launch type(production, development)", false);
  CLI11_PARSE(cli_parser, argc, argv);

  auto database_connector{wudi_server::DatabaseConnector::GetDBConnector()};
  auto db_config = wudi_server::utilities::parse_database_file(
      args.database_config_filename, args.launch_type);
  if (!db_config) {
    std::cerr << "Unable to get database configuration values\n";
    return EXIT_FAILURE;
  }

  std::atomic_bool stop = false;
  std::mutex task_mutex{};
  {
    using wudi_server::utilities::background_task_executor;
    using wudi_server::utilities::read_task_file;
    using namespace wudi_server::utilities;

    read_task_file(args.scheduled_snapshot);

    for (int i = 0; i != WorkerThreadCount; ++i) {
      std::thread t{background_task_executor, std::ref(stop),
                    std::ref(task_mutex), std::ref(database_connector)};
      t.detach();
    }
  }
  database_connector->username(db_config.username)
      .password(db_config.password)
      .database_name(db_config.db_dns);
  if (!database_connector->connect()) {
    stop = true;
    return EXIT_FAILURE;
  }
  wudi_server::asio::io_context context{thread_count};
  auto server_instance =
      std::make_shared<wudi_server::server>(context, args, database_connector);
  server_instance->run();

  std::vector<std::thread> threads{};
  threads.reserve(args.thread_count);
  for (std::size_t counter = 0; counter < args.thread_count; ++counter) {
    threads.emplace_back([&] { context.run(); });
  }
  context.run();
  stop = true;
  return EXIT_SUCCESS;
}
