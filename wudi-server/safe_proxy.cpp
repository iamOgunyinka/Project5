#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>

namespace wudi_server {
std::string const safe_proxy::proxy_generator_address{
    "http://api.wandoudl.com/api/ip?app_key="
    "d9c96cbb82ce73721589f1d63125690e&pack="
    "208774&num=150&xy=1&type=1&lb=\\n&mr=1&"};
std::string const safe_proxy::http_proxy_filename{"./http_proxy_servers.txt"};
std::string const safe_proxy::https_proxy_filename{"./https_proxy_servers.txt"};

safe_proxy::safe_proxy(net::io_context &context)
    : context_{context}, http_tcp_stream_{net::make_strand(context)} {
  load_proxy_file();
}

void safe_proxy::get_more_proxies() {
  is_free = false;
  utilities::uri uri_{proxy_generator_address};
  net::ip::tcp::resolver resolver{context_};

  std::lock_guard<std::mutex> lock_g{ mutex_ };
  try {
    auto resolves = resolver.resolve(uri_.host(), "http");
    http_tcp_stream_.connect(resolves);
    http_request_ =
        http::request<http::empty_body>(http::verb::get, uri_.path(), 11);
    http_request_.set(http::field::host, uri_.host());
    http_request_.set(http::field::user_agent, utilities::get_random_agent());

    http::write(http_tcp_stream_, http_request_);
    beast::flat_buffer buffer{};
    http::response<http::string_body> server_response{};
    http::read(http_tcp_stream_, buffer, server_response);
    beast::error_code ec{};
    http_tcp_stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
    if (server_response.result_int() != 200 || ec ) {
      is_free = true;
      has_error = true;
      return spdlog::error("Error obtaining proxy servers from server");
    }
    std::vector<std::string> ips{};
    boost::split(ips, server_response.body(),
                 [](auto ch) { return ch == '\n'; });
    if (ips.empty()) {
      is_free = true;
      has_error = true;
      return;
    }
    std::vector<std::string> ip_port{};
    for (auto &line : ips) {
      boost::trim(line);
      if (line.empty())
        continue;
      boost::split(ip_port, line, [](auto ch) { return ch == ':'; });
      if (ip_port.size() < 2)
        continue;
      beast::error_code ec{};
      try {
        auto endpoint = net::ip::tcp::endpoint(
            net::ip::make_address(ip_port[0]), std::stoi(ip_port[1]));
        endpoints_.emplace_back(std::make_shared<custom_endpoint>(
            std::move(endpoint), ProxyProperty::ProxyActive));
      } catch (std::exception const &except) {
        spdlog::error("[get_more_proxies 60] {}", except.what());
      }
    }
    has_error = ips.empty();
    if (has_error)
      spdlog::error("Error occurred while getting more proxies");
  } catch (std::exception const &) {
    has_error = true;
  }
  is_free = true;
}

void safe_proxy::save_proxies_to_file() {
  std::set<std::string> proxy_set{};
  if (std::filesystem::exists(http_proxy_filename)) {
    std::ifstream file{http_proxy_filename};
    if (file) {
      std::string line{};
      while (std::getline(file, line)) {
        boost::trim(line);
        if (line.empty() || proxy_set.find(line) != proxy_set.cend())
          continue;
        proxy_set.insert(line);
      }
    }
  }
  for (auto const &proxy : this->endpoints_) {
    proxy_set.insert(proxy->endpoint.address().to_string() + ":" +
                     std::to_string(proxy->endpoint.port()));
  }

  std::ofstream file{http_proxy_filename};
  if (file) {
    for (auto const &proxy : proxy_set) {
      file << proxy << "\n";
      file.close();
    }
  }
}

void safe_proxy::load_proxy_file() {
  std::filesystem::path const http_filename_path{http_proxy_filename};
  if (!std::filesystem::exists(http_filename_path)) {
    return get_more_proxies();
  }
  std::ifstream proxy_file{http_filename_path};
  if (!proxy_file)
    return get_more_proxies();
  std::string line{};
  std::vector<std::string> ip_port{};
  while (std::getline(proxy_file, line)) {
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
          return endpoints_[count_];
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
