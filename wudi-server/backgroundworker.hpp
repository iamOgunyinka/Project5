#ifndef BACKGROUNDWORKER_HPP
#define BACKGROUNDWORKER_HPP

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/asio/ssl/context.hpp>
#include <boost/signals2.hpp>
#include <fstream>
#include <optional>
#include <vector>

namespace wudi_server {
class background_worker_t;
using utilities::task_status_e;
using utilities::upload_result_t;
using utilities::website_result_t;

namespace asio = boost::asio;
namespace http = boost::beast::http;

enum class website_type {
  Unknown,
  AutoHomeRegister,
  JJGames,
  PPSports,
  WatchHome
};

class background_worker_t {
public:
  background_worker_t(
      website_result_t &&, std::vector<upload_result_t> &&,
      std::shared_ptr<utilities::internal_task_result_t> task_result,
      net::ssl::context &);
  background_worker_t(
      utilities::atomic_task_t old_task,
      std::shared_ptr<utilities::internal_task_result_t> task_result,
      net::ssl::context &);
  ~background_worker_t();

  task_status_e run();
  website_type type() const { return type_; }
  auto &number_stream() { return number_stream_; }
  auto task_result() { return task_result_ptr_; }
  std::string filename() { return input_filename; }
  auto &sockets() { return sockets_; }

private:
  bool open_output_files();
  void on_data_result_obtained(utilities::search_result_type_e,
                               std::string_view);
  utilities::task_status_e run_new_task();
  utilities::task_status_e run_number_crawler();
  utilities::task_status_e continue_old_task();

private:
  net::ssl::context &ssl_context_;

  std::unique_ptr<proxy_provider_t> proxy_provider_;
  website_type type_;
  std::shared_ptr<utilities::number_stream_t> number_stream_;
  website_result_t website_info_;
  std::vector<upload_result_t> uploads_info_;
  std::shared_ptr<utilities::internal_task_result_t> task_result_ptr_;
  std::shared_ptr<database_connector_t> db_connector;
  std::string input_filename{};
  std::ifstream input_file;
  std::optional<utilities::atomic_task_t> atomic_task_;
  std::optional<net::io_context> io_context_;
  std::vector<std::shared_ptr<void>> sockets_{};
};
} // namespace wudi_server

#endif // BACKGROUNDWORKER_HPP
