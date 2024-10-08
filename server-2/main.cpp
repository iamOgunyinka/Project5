#include "database_connector.hpp"
#include "server.hpp"
#include "worker.hpp"
#include <CLI11/CLI11.hpp>
#include <atomic>
#include <spdlog/spdlog.h>
#include <thread>

enum constant_e { WorkerThreadCount = 15 };

int main(int argc, char *argv[]) {
  CLI::App cli_parser{
      "Wu-di: an asynchronous web server for Kiaowa Trading LLC"};
  wudi_server::command_line_interface args{};
  auto const thread_count = std::thread::hardware_concurrency();
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
                        "Launch type(production, development)", true);
  CLI11_PARSE(cli_parser, argc, argv);

  auto database_connector{
      wudi_server::database_connector_t::s_get_db_connector()};
  auto db_config = wudi_server::parse_database_file(
      args.database_config_filename, args.launch_type);
  if (!db_config) {
    std::cerr << "Unable to get database configuration values\n";
    return EXIT_FAILURE;
  }

  std::atomic_bool stop = false;
  std::mutex task_mutex{};

  wudi_server::asio::io_context io_context{static_cast<int>(thread_count)};
  {
    using wudi_server::background_task_executor;
    using namespace wudi_server::utilities;

    for (int i = 0; i != WorkerThreadCount; ++i) {
      std::thread t{background_task_executor, std::ref(stop),
                    std::ref(task_mutex)};
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
  auto server_instance =
      std::make_shared<wudi_server::server>(io_context, args);
  server_instance->run();

  std::vector<std::thread> threads{};
  threads.reserve(args.thread_count);
  for (std::size_t counter = 0; counter < args.thread_count; ++counter) {
    threads.emplace_back([&] { io_context.run(); });
  }
  io_context.run();
  stop = true;
  return EXIT_SUCCESS;
}
