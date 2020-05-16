#include "auto_home_socks5_sock.hpp"
#include "safe_proxy.hpp"
#include <boost/asio/ssl/context.hpp>
#include <iostream>
#include <thread>

using namespace wudi_server;

void run_tests(int const max_proxy_count, std::ifstream &in_file,
               boost::asio::ssl::context &ssl_context,
               boost::asio::io_context &io_) {
  std::map<std::string, int> map_result{};
  auto proxy = std::make_unique<socks5_proxy>(io_, max_proxy_count);
  number_stream_t number_stream{in_file};
  auto socket = std::make_shared<auto_home_socks5_socket_t<socks5_proxy>>(
      ssl_context, io_, *proxy, number_stream, 0);
  int processed = 1;
  socket->signal().connect([&](std::string const &str) {
    if (!str.empty()) {
      spdlog::info("Processed {}", processed++);
      ++map_result[str];
    }
  });
  socket->start_connect();
  io_.run();

  std::ofstream out_file{"./result_log.txt"};
  auto j_result = json(map_result).dump();
  if (!out_file) {
    std::cout << j_result << std::endl;
  } else {
    out_file << j_result << std::endl;
  }
  spdlog::info("Done");
}

int main(int argc, char *argv[]) {
  std::string const filename =
      "D:\\Visual Studio Projects\\wudi-server\\x64\\Debug\\10k.txt";
  int const proxy_count = 20;
  std::ifstream in_file(filename);
  if (!in_file) {
    spdlog::error("File does not exist");
    return -1;
  }

  boost::asio::ssl::context ssl_context(
      boost::asio::ssl::context::tlsv11_client);
  ssl_context.set_default_verify_paths();
  ssl_context.set_verify_mode(boost::asio::ssl::verify_none);

  boost::asio::io_context io_context{};
  run_tests(proxy_count, in_file, ssl_context, io_context);
  return EXIT_SUCCESS;
}
