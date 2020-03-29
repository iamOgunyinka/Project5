#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>

namespace wudi_server {
std::string const global_proxy_provider::proxy_generator_address{
    "http://api.wandoudl.com/api/ip"};
std::string const global_proxy_provider::http_proxy_filename{
    "./http_proxy_servers.txt"};

global_proxy_provider::global_proxy_provider(net::io_context &context)
    : context_{context} {
  load_proxy_file();
}

extraction_data global_proxy_provider::remain_count(
    net::io_context &context,
    net::ip::basic_resolver_results<net::ip::tcp> &resolves) {

  beast::tcp_stream http_tcp_stream(net::make_strand(context));
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
      // for unknown and most likely insane reasons, the server returns some
      // data as both integers and strings
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
    auto const current_time = std::time(nullptr);
    proxy_extract_info.erase(
        std::remove_if(proxy_extract_info.begin(), proxy_extract_info.end(),
                       [&current_time](extraction_data const &data) {
                         return !data.is_available ||
                                data.expire_time < current_time;
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

std::size_t
global_proxy_provider::get_more_proxies(std::size_t const current_count) {
  std::lock_guard<std::mutex> lock_g{mutex_};

  if (current_count < endpoints_.size())
    return endpoints_.size() - current_count;

  utilities::uri uri_{proxy_generator_address};
  net::ip::tcp::resolver resolver{context_};
  beast::tcp_stream http_tcp_stream(net::make_strand(context_));
  http::request<http::empty_body> http_request_;

  try {
    auto resolves = resolver.resolve(uri_.host(), "http");
    current_extracted_data_ = remain_count(context_, resolves);
    http_tcp_stream.connect(resolves);
    if (!current_extracted_data_.is_available) {
      return std::numeric_limits<std::size_t>::max();
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
        spdlog::error("Error obtaining proxy servers from server");
        return std::numeric_limits<std::size_t>::max();
      }
      auto &response_body = server_response.body();
      std::vector<std::string> ips;
      boost::split(ips, response_body,
                   [](char const ch) { return ch == '\n'; });
      if (ips.empty()) {
        spdlog::error("IPs empty");
        return std::numeric_limits<std::size_t>::max();
      }
      spdlog::info("Grabbed {} proxies", ips.size());
      if (endpoints_.size() >= max_endpoints_allowed) {
        endpoints_.erase(endpoints_.begin(),
                         endpoints_.begin() +
                             (max_endpoints_allowed - endpoints_.size()));
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
          endpoints_.emplace_back(std::move(endpoint));
        } catch (std::exception const &except) {
          spdlog::error("[get_more_proxies] {}", except.what());
        }
      }
    }
  } catch (std::exception const &e) {
    spdlog::error("safe_proxy exception: {}", e.what());
    return std::numeric_limits<std::size_t>::max();
  }
  save_proxies_to_file();
  // index to the start of the newly acquired endpoints
  return endpoints_.size() - 100;
}

void global_proxy_provider::save_proxies_to_file() {
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
    (*out_file_ptr) << boost::lexical_cast<std::string>(proxy) << "\n";
  }
  out_file_ptr->close();
}

void global_proxy_provider::load_proxy_file() {
  std::filesystem::path const http_filename_path{http_proxy_filename};
  if (!std::filesystem::exists(http_filename_path)) {
    get_more_proxies(0);
    return;
  }
  std::ifstream proxy_file{http_filename_path};
  if (!proxy_file) {
    get_more_proxies(0);
    return;
  }
  std::vector<std::string> ip_port{};
  std::string line{};

  endpoints_.clear();
  endpoints_.reserve(max_endpoints_allowed);

  while (std::getline(proxy_file, line)) {
    boost::trim(line);
    if (line.empty())
      continue;
    ip_port.clear();
    boost::split(ip_port, line, [](auto ch) { return ch == ':'; });
    if (ip_port.size() < 2)
      continue;
    beast::error_code ec{};
    try {
      auto endpoint = net::ip::tcp::endpoint(net::ip::make_address(ip_port[0]),
                                             std::stoi(ip_port[1]));
      endpoints_.emplace_back(std::move(endpoint));
      // we need the last `max_allowed` endpoints
      if (endpoints_.size() >= max_endpoints_allowed) {
        endpoints_.erase(endpoints_.begin());
      }
    } catch (std::exception const &e) {
      spdlog::error("Error while converting( {} ), {}", line, e.what());
    }
  }
}

global_proxy_provider &global_proxy_provider::get_global_proxy_provider() {
  static global_proxy_provider proxy_provider(get_network_context());
  return proxy_provider;
}

tcp::endpoint &global_proxy_provider::endpoint_at(std::size_t index) {
  std::lock_guard<std::mutex> lock_g{mutex_};
  return endpoints_[index];
}

std::size_t proxy_provider_t::next_endpoint() {
  for (std::size_t index = 0; index != information_list_.size(); ++index) {
    auto &ep = information_list_[index];
    if (ep.property == ProxyProperty::ProxyActive &&
        ep.use_count < max_ip_use) {
      ++ep.use_count;
      return index;
    }
  }
  if (!exhausted_daily_dose) {
    std::size_t const new_index =
        proxy_provider_.get_more_proxies(information_list_.size());
    if (new_index == std::numeric_limits<std::size_t>::max()) {
      exhausted_daily_dose = true;
      information_list_.erase(
          std::remove_if(information_list_.begin(), information_list_.end(),
                         [](endpoint_info const &info) {
                           return info.property == ProxyProperty::ProxyBlocked;
                         }),
          information_list_.end());
      for (auto &info : information_list_) {
        info.property = ProxyProperty::ProxyActive;
        info.use_count = 0;
      }
      // new_index == std::numeric_limits<std::size_t>::max();
      if (information_list_.empty())
        return new_index;
      return 0;
    }
    return new_index;
  }
  information_list_.erase(
      std::remove_if(information_list_.begin(), information_list_.end(),
                     [](endpoint_info const &info) {
                       return info.property == ProxyProperty::ProxyBlocked &&
                              info.use_count >= max_ip_use;
                     }),
      information_list_.end());
  for (auto &info : information_list_) {
    info.property = ProxyProperty::ProxyActive;
    info.use_count = 0;
  }
  if (information_list_.empty())
    return std::numeric_limits<std::size_t>::max();
  return 0;
}

tcp::endpoint &proxy_provider_t::endpoint(std::size_t const index) {
  return proxy_provider_.endpoint_at(index);
}

void proxy_provider_t::assign_property(std::size_t const index,
                                       ProxyProperty prop) {
  information_list_[index].property = prop;
}

proxy_provider_t::proxy_provider_t(global_proxy_provider &gsp)
    : proxy_provider_{gsp}, information_list_(gsp.count(), endpoint_info{}) {}

net::io_context &get_network_context() {
  static boost::asio::io_context context{};
  return context;
}
} // namespace wudi_server
