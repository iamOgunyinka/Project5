#ifndef BACKGROUNDWORKER_HPP
#define BACKGROUNDWORKER_HPP

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/signals2.hpp>
#include <fstream>
#include <optional>
#include <vector>

namespace wudi_server {
using utilities::upload_result_t;
using utilities::website_result_t;
namespace asio = boost::asio;
namespace http = boost::beast::http;

enum class website_type { Unknown, AutoHomeRegister, JJGames };

class background_worker_t {
public:
  background_worker_t(
      website_result_t &&, std::vector<upload_result_t> &&,
      std::shared_ptr<utilities::atomic_task_result_t> task_result,
      net::io_context &);
  background_worker_t(
      utilities::atomic_task_t old_task,
      std::shared_ptr<utilities::atomic_task_result_t> task_result,
      net::io_context &);
  ~background_worker_t();
  utilities::task_status_e run();
  website_type type() const { return type_; }
  auto &number_stream() { return number_stream_; }
  auto task_result() { return task_result_ptr_; }
  std::string filename() { return input_filename; }

private:
  bool open_output_files();
  void on_data_result_obtained(utilities::search_result_type_e,
                               std::string_view);
  utilities::task_status_e run_new_task();
  utilities::task_status_e run_number_crawler();
  utilities::task_status_e continue_old_task();

private:
  net::io_context &context_;
  safe_proxy safe_proxy_;
  website_type type_;
  std::shared_ptr<utilities::number_stream_t> number_stream_;
  website_result_t website_info_;
  std::vector<upload_result_t> uploads_info_;
  std::shared_ptr<utilities::atomic_task_result_t> task_result_ptr_;
  std::shared_ptr<database_connector_t> db_connector;
  std::string input_filename{};
  std::ifstream input_file;
  std::optional<utilities::atomic_task_t> atomic_task_;
  std::vector<std::shared_ptr<void>> sockets_{};
};
} // namespace wudi_server

#endif // BACKGROUNDWORKER_HPP
