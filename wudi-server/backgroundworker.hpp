#ifndef BACKGROUNDWORKER_HPP
#define BACKGROUNDWORKER_HPP

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/signals2.hpp>
#include <fstream>
#include <map>
#include <vector>

namespace wudi_server {
using utilities::UploadResult;
using utilities::WebsiteResult;
namespace asio = boost::asio;
namespace http = boost::beast::http;

enum class website_type { AutoHomeRegister, JJGames };

class BackgroundWorker {
  static std::string const http_proxy_filename;

public:
  BackgroundWorker(WebsiteResult &&, std::vector<UploadResult> &&,
                   std::shared_ptr<utilities::AtomicTaskResult> task_result,
                   net::io_context &);
  void run();

private:
  bool open_output_files();
  void on_data_result_obtained(utilities::SearchResultType, std::string_view);
  void make_mapper();
  void run_number_crawler();

private:
  net::io_context &context_;
  safe_proxy safe_proxy_;
  int current_progress_{};
  std::shared_ptr<utilities::number_stream> number_stream_;
  WebsiteResult const website_info_;
  std::vector<UploadResult> const uploads_info_;
  std::shared_ptr<utilities::AtomicTaskResult> task_result_ptr_;
  std::string input_filename{};
  std::ifstream input_file;
};
} // namespace wudi_server

#endif // BACKGROUNDWORKER_HPP
