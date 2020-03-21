#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>

namespace wudi_server {
std::string const safe_proxy::proxy_generator_address{
    "http://api.wandoudl.com/api/ip"};
std::string const safe_proxy::http_proxy_filename{"./http_proxy_servers.txt"};
std::string const safe_proxy::https_proxy_filename{"./https_proxy_servers.txt"};

safe_proxy::safe_proxy(net::io_context &context) : context_{context} {
  load_proxy_file();
}

extraction_data safe_proxy::get_remain_count(
    net::ip::basic_resolver_results<net::ip::tcp> &resolves) {
  utilities::uri uri_{proxy_generator_address};

  beast::tcp_stream http_tcp_stream(net::make_strand(context_));

  try {
    http_tcp_stream.connect(resolves);
    beast::http::request<http::empty_body> http_request{};
    http_request.method(http::verb::get);
    http_request.target(
        R"(/api/product/list?app_key=86adb80a7af9ee8d31bf765dd02e1431)");
    http_request.version(11);
    http_request.set(http::field::host, "api.wandoudl.com:80");
    http_request.set(http::field::user_agent, utilities::get_random_agent());
    http::write(http_tcp_stream, http_request);
    beast::flat_buffer buffer{};
    http::response<http::string_body> server_response{};
    http::read(http_tcp_stream, buffer, server_response);
    beast::error_code ec{};
    if (server_response.result_int() != 200)
      return {};
    http_tcp_stream.cancel();
    auto &response_body = server_response.body();
    response_body.erase(
        std::remove(response_body.begin(), response_body.end(), '\n'),
        response_body.end());
    auto get_time_t = [](std::string const &time_opened) -> std::time_t {
      std::tm tm{};
      std::memset(&tm, 0, sizeof(tm));
      std::istringstream ss(time_opened);
      ss >> std::get_time(&tm, "%Y-%m-%d%H:%M:%S");
      return std::mktime(&tm);
    };

    json::object_t json_result =
        json::parse(response_body).get<json::object_t>();
    if (json_result["code"].get<json::number_integer_t>() != 200)
      return {};
    json::array_t const data_list = json_result["data"].get<json::array_t>();
    std::vector<extraction_data> proxy_extract_info{};
    proxy_extract_info.reserve(data_list.size());

    for (auto const &data_item : data_list) {
      json::object_t data_object = data_item.get<json::object_t>();
      std::string const expire_time =
          data_object["expire_time"].get<json::string_t>();

      extraction_data data{};
      data.expire_time = get_time_t(expire_time);
      data.is_available = data_item["is_available"].get<json::boolean_t>();
      try {
        data.connect_remain =
            data_item["remain_connect"].get<json::number_integer_t>();
      } catch (std::exception const &) {
        data.connect_remain =
            std::stoi(data_item["remain_connect"].get<json::string_t>());
      }
      try {
        data.extract_remain =
            data_item["remain_extract"].get<json::number_integer_t>();
      } catch (std::exception const &) {
        data.extract_remain =
            std::stoi(data_item["remain_extract"].get<json::string_t>());
      }
      try {
        data.product_remain =
            std::stoi(data_item["remain"].get<json::string_t>());
      } catch (std::exception const &) {
        data.product_remain = data_item["remain"].get<json::number_integer_t>();
      }
      proxy_extract_info.push_back(data);
    }
    proxy_extract_info.erase(
        std::remove_if(proxy_extract_info.begin(), proxy_extract_info.end(),
                       [](extraction_data const &data) {
                         return !data.is_available ||
                                data.expire_time < std::time(nullptr);
                       }),
        proxy_extract_info.end());
    if (proxy_extract_info.empty())
      return {};
    return proxy_extract_info.back();
  } catch (std::exception const &e) {
    spdlog::error("[get_remain_count] {}", e.what());
    return {};
  }
}

void safe_proxy::get_more_proxies() {
  utilities::uri uri_{proxy_generator_address};
  net::ip::tcp::resolver resolver{context_};

  std::lock_guard<std::mutex> lock_g{mutex_};

  beast::tcp_stream http_tcp_stream(net::make_strand(context_));
  http::request<http::empty_body> http_request_;

  try {
    auto resolves = resolver.resolve(uri_.host(), "http");
    current_extracted_data_ = get_remain_count(resolves);
    http_tcp_stream.connect(resolves);
    if (!current_extracted_data_.is_available) {
      has_error = true;
      return;
    }
    if (current_extracted_data_.extract_remain > 0) {
      http_request_ = {};
      http_request_.method(http::verb::get);
      http_request_.target(
          R"(/api/ip?app_key=86adb80a7af9ee8d31bf765dd02e1431&pack=210115&num=100&xy=1&type=1&lb=\n&mr=1)");
      http_request_.version(11);
      http_request_.set(http::field::host, uri_.host() + ":80");
      http_request_.set(http::field::user_agent, utilities::get_random_agent());
      http::write(http_tcp_stream, http_request_);
      beast::flat_buffer buffer{};
      http::response<http::string_body> server_response{};
      http::read(http_tcp_stream, buffer, server_response);
      beast::error_code ec{};
      http_tcp_stream.socket().shutdown(tcp::socket::shutdown_both, ec);
      if (server_response.result_int() != 200 || ec) {
        has_error = true;
        return spdlog::error("Error obtaining proxy servers from server");
      }
      auto &response_body = server_response.body();
      std::vector<std::string> ips;
      boost::split(ips, response_body,
                   [](char const ch) { return ch == '\n'; });
      if (ips.empty()) {
        has_error = true;
        return spdlog::error("IPs empty");
      }
      spdlog::info("Grabbed {} proxies", ips.size());
      int const max_allowed = 1'000;
      if (endpoints_.size() >= max_allowed) {
        endpoints_.erase(endpoints_.begin(),
                         endpoints_.begin() +
                             (max_allowed - endpoints_.size()));
      }
      for (auto &line : ips) {
        if (line.empty())
          continue;

        auto ip_port = utilities::split_string_view(line, ":");
        if (ip_port.size() < 2)
          continue;
        ec = {};
        try {
          auto endpoint = net::ip::tcp::endpoint(
              net::ip::make_address(ip_port[0].to_string()),
              std::stoi(ip_port[1].to_string()));
          endpoints_.emplace_back(std::make_shared<custom_endpoint>(
              std::move(endpoint), ProxyProperty::ProxyActive));
        } catch (std::exception const &except) {
          spdlog::error("[get_more_proxies 60] {}", except.what());
        }
      }
      has_error = ips.empty();
    }
  } catch (std::exception const &e) {
    spdlog::error("safe_proxy exception: {}", e.what());
    has_error = true;
  }
  if (current_extracted_data_.connect_remain > 0) {
    for (auto &ep : endpoints_) {
      if (ep->property != ProxyProperty::ProxyBlocked) {
        ep->property = ProxyProperty::ProxyActive;
      }
    }
    save_proxies_to_file();
  } else {
    endpoints_.clear();
  }
  if (has_error)
    spdlog::error("Error occurred while getting more proxies");
}

void safe_proxy::save_proxies_to_file() {
  std::unique_ptr<std::ofstream> out_file_ptr{nullptr};
  if (std::filesystem::exists(http_proxy_filename)) {
    out_file_ptr = std::make_unique<std::ofstream>(
        http_proxy_filename, std::ios::app | std::ios::out);
  } else {
    out_file_ptr = std::make_unique<std::ofstream>(http_proxy_filename);
  }
  if (!out_file_ptr)
    return;
  try {
    out_file_ptr->seekp(std::ios::beg);
  } catch (std::exception const &) {
  }
  for (auto const &proxy : this->endpoints_) {
    (*out_file_ptr) << boost::lexical_cast<std::string>(proxy->endpoint)
                    << "\n";
  }
  out_file_ptr->close();
}

void safe_proxy::load_proxy_file() {
  std::filesystem::path const http_filename_path{http_proxy_filename};
  if (!std::filesystem::exists(http_filename_path)) {
    get_more_proxies();
    if (!endpoints_.empty()) {
      save_proxies_to_file();
    }
    return;
  }
  std::ifstream proxy_file{http_filename_path};
  if (!proxy_file) {
    get_more_proxies();
    return save_proxies_to_file();
  }
  std::string line{};
  std::vector<std::string> ip_port{};
  int count = 0;
  while (std::getline(proxy_file, line) && count < 1000) {
    ip_port.clear();
    boost::trim(line);
    if (line.empty())
      continue;
    boost::split(ip_port, line, [](auto ch) { return ch == ':'; });
    if (ip_port.size() < 2)
      continue;
    beast::error_code ec{};
    try {
      auto endpoint = net::ip::tcp::endpoint(net::ip::make_address(ip_port[0]),
                                             std::stoi(ip_port[1]));
      endpoints_.emplace_back(std::make_shared<custom_endpoint>(
          std::move(endpoint), ProxyProperty::ProxyActive));
      ++count;
    } catch (std::exception const &e) {
      spdlog::error("Error while converting( {} ), {}", line, e.what());
    }
  }
}

std::optional<endpoint_ptr> safe_proxy::next_endpoint() {
  {
    std::lock_guard<std::mutex> lock_g{mutex_};
    if (has_error || endpoints_.empty())
      return std::nullopt;
    if (count_ >= endpoints_.size()) {
      count_ = 0;
      while (count_ < endpoints_.size()) {
        if (endpoints_[count_]->property == ProxyProperty::ProxyActive) {
          return endpoints_[count_++];
        }
        count_++;
      }
    } else {
      return endpoints_[count_++];
    }
  }
  get_more_proxies();
  return next_endpoint();
}

net::io_context &get_network_context() {
  static boost::asio::io_context context{};
  return context;
}

void safe_proxy::clear() {
  std::lock_guard<std::mutex> lock_g{mutex_};
  endpoints_.clear();
}

void safe_proxy::push_back(custom_endpoint ep) {
  std::lock_guard<std::mutex> lock_g{mutex_};
  endpoints_.emplace_back(std::make_shared<custom_endpoint>(std::move(ep)));
}

void custom_endpoint::swap(custom_endpoint &other) {
  std::swap(other.endpoint, this->endpoint);
  std::swap(other.property, this->property);
}

void swap(custom_endpoint &a, custom_endpoint &b) { a.swap(b); }
} // namespace wudi_server
