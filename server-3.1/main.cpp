#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "database_connector.hpp"
#include "random.hpp"
#include "safe_proxy.hpp"
#include "server.hpp"
#include "worker.hpp"
#include <CLI11/CLI11.hpp>
#include <boost/asio/ssl/context.hpp>
#include <thread>

enum constant_e { WorkerThreadCount = 15 };

int WOODY_SOFTWARE_VERSION = 316;

using woody_server::global_proxy_repo_t;

int main(int argc, char *argv[]) {
  CLI::App cli_parser{"Woody an asynchronous web server for scanning websites"};
  woody_server::command_line_interface_t args{};
  auto const thread_count = std::thread::hardware_concurrency();

  cli_parser.add_option("-p", args.port, "port to bind server to", true);
  cli_parser.add_option("-a", args.ip_address, "IP address to use", true);
  cli_parser.add_option("-d", args.database_config_filename,
                        "Database config filename", true);
  cli_parser.add_option("-y", args.launch_type,
                        "Launch type(production, development)", true);
  CLI11_PARSE(cli_parser, argc, argv);

  auto database_connector{
      woody_server::database_connector_t::s_get_db_connector()};

  auto db_config = woody_server::parse_database_file(
      args.database_config_filename, args.launch_type);
  if (!db_config) {
    std::cerr << "Unable to get database configuration values\n";
    return EXIT_FAILURE;
  }

  global_proxy_repo_t global_proxy_provider{};
  boost::asio::ssl::context ssl_context(
      boost::asio::ssl::context::tlsv12_client);
  ssl_context.set_default_verify_paths();
  ssl_context.set_verify_mode(boost::asio::ssl::verify_none);
  {
    auto proxy_config = woody_server::readProxyConfiguration();
    if (!proxy_config) {
      std::cerr << "Unable to read proxy configuration file\n";
      return -1;
    }
    woody_server::utilities::proxyFetchInterval() = proxy_config->fetchInterval;
    WOODY_SOFTWARE_VERSION = proxy_config->softwareVersion;
  }
  woody_server::asio::io_context io_context{static_cast<int>(thread_count)};
  std::atomic_bool stop = false;
  {
    using woody_server::background_task_executor;
    auto thread_callback = [&] {
      background_task_executor(stop, ssl_context, global_proxy_provider);
    };
    for (int i = 0; i != WorkerThreadCount; ++i) {
      std::thread t{thread_callback};
      t.detach();
    }

    std::thread safe_proxy_executor{global_proxy_repo_t::backgroundProxyFetcher,
                                    std::ref(io_context)};
    safe_proxy_executor.detach();
  }
  database_connector->username(db_config.username)
      .password(db_config.password)
      .database_name(db_config.db_dns);
  if (!database_connector->connect()) {
    stop = true;
    return EXIT_FAILURE;
  }
  auto server_instance =
      std::make_shared<woody_server::server_t>(io_context, args);
  server_instance->run();

  std::vector<std::thread> threads{};
  threads.reserve(thread_count);
  for (std::size_t counter = 0; counter < thread_count; ++counter) {
    threads.emplace_back([&] { io_context.run(); });
  }
  io_context.run();
  stop = true;
  return EXIT_SUCCESS;
}
