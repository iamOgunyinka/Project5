#ifndef BACKGROUNDWORKER_HPP
#define BACKGROUNDWORKER_HPP

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/signals2.hpp>
#include <fstream>
#include <map>
#include <vector>
#include <optional>

namespace wudi_server {
using utilities::UploadResult;
using utilities::WebsiteResult;
namespace asio = boost::asio;
namespace http = boost::beast::http;

enum class website_type { Unknown, AutoHomeRegister, JJGames };

class BackgroundWorker {
  static std::string const http_proxy_filename;

public:
  BackgroundWorker(WebsiteResult &&, std::vector<UploadResult> &&,
                   std::shared_ptr<utilities::AtomicTaskResult> task_result,
                   net::io_context &);
  BackgroundWorker(utilities::AtomicTask old_task,
                   std::shared_ptr<utilities::AtomicTaskResult> task_result,
                   net::io_context &);
  ~BackgroundWorker();
  void run();

private:
  bool save_status_to_persistence();
  bool open_output_files();
  void on_data_result_obtained(utilities::SearchResultType, std::string_view);
  void run_new_task();
  void run_number_crawler();
  void continue_old_task();
private:
  net::io_context &context_;
  safe_proxy safe_proxy_;
  int current_progress_{};
  website_type type_;
  std::shared_ptr<utilities::number_stream> number_stream_;
  WebsiteResult website_info_;
  std::vector<UploadResult> uploads_info_;
  std::list<std::shared_ptr<void>> sockets_{};
  std::shared_ptr<utilities::AtomicTaskResult> task_result_ptr_;
  std::string input_filename{};
  std::ifstream input_file;
  std::optional<utilities::AtomicTask> atomic_task_;
};
} // namespace wudi_server

#endif // BACKGROUNDWORKER_HPP
