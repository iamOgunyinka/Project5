#ifndef BACKGROUNDWORKER_HPP
#define BACKGROUNDWORKER_HPP

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <map>
#include <vector>

namespace wudi_server {
using utilities::UploadResult;
using utilities::WebsiteResult;
namespace asio = boost::asio;
namespace http = boost::beast::http;

enum class website_type { AutoHomeRegister };

class BackgroundWorker {
  static std::map<std::string, website_type> website_maps;
  static std::string const http_proxy_filename;

public:
  BackgroundWorker(std::vector<WebsiteResult> &&, std::vector<UploadResult> &&,
                   net::io_context &);
  void run();

private:
  void on_data_result_obtained(utilities::SearchResultType, std::string_view);
  void make_mapper();
  void run_number_crawler(std::size_t &);
  void make_proxy_providers();

  std::vector<WebsiteResult> const websites_info_;
  std::vector<UploadResult> const uploads_info_;
  net::io_context &context_;
  std::unique_ptr<utilities::threadsafe_vector<std::string>> web_uploads_ptr_{};
  std::map<std::string, std::shared_ptr<safe_proxy>> safe_proxies_;
  std::size_t counter_;
  std::size_t curr_website_number_counter_{};
  unsigned long long total_numbers_{};
  unsigned long long current_count_{};
};
} // namespace wudi_server

#endif // BACKGROUNDWORKER_HPP
