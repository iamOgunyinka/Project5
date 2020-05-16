#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <ctime>
#include <memory>
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <vector>

namespace wudi_server {

using nlohmann::json;

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ip = net::ip;

using tcp = ip::tcp;

net::io_context &get_network_context();

enum class ProxyProperty { ProxyActive, ProxyBlocked, ProxyUnresponsive };

struct custom_endpoint {
  tcp::endpoint endpoint_{};
  int number_scanned{};
  std::string username_{};
  std::string password_{};
  ProxyProperty property{ProxyProperty::ProxyActive};
  custom_endpoint(tcp::endpoint &&ep) : endpoint_(std::move(ep)) {}
  operator tcp::endpoint() const { return endpoint_; }

  void swap(custom_endpoint &);
  auto &username() { return username_; }
  auto &password() { return password_; }
};

using endpoint_ptr = std::shared_ptr<custom_endpoint>;

void swap(custom_endpoint &a, custom_endpoint &b);

class proxy_base {

protected:
  net::io_context &context_;
  int const max_ip_;
  int extracted_{};
  std::mutex mutex_{};
  std::size_t count_{};
  std::atomic_bool has_error_ = false;
  std::vector<endpoint_ptr> endpoints_{};

protected:
  virtual void get_more_proxies();

public:
  using Property = ProxyProperty;
  using value_type = endpoint_ptr;
  proxy_base(net::io_context &, int max_ip);
  virtual ~proxy_base() {}
  endpoint_ptr next_endpoint();
};

struct socks5_proxy final : proxy_base {
  socks5_proxy(net::io_context &, int);
  ~socks5_proxy() {}
};

struct uri {
  uri(std::string const &url_s);
  std::string path() const;
  std::string host() const;
  std::string target() const;
  std::string protocol() const;

private:
  void parse(std::string const &);
  std::string host_;
  std::string path_;
  std::string protocol_;
  std::string query_;
};
using proxy_provider_t = proxy_base;
} // namespace wudi_server

namespace wudi_server {
uri::uri(const std::string &url_s) { parse(url_s); }

std::string uri::target() const { return path_ + "?" + query_; }

std::string uri::protocol() const { return protocol_; }

std::string uri::path() const { return path_; }

std::string uri::host() const { return host_; }

void uri::parse(const std::string &url_s) {
  std::string const prot_end{"://"};
  std::string::const_iterator prot_i =
      std::search(url_s.begin(), url_s.end(), prot_end.begin(), prot_end.end());
  protocol_.reserve(
      static_cast<std::size_t>(std::distance(url_s.cbegin(), prot_i)));
  std::transform(url_s.begin(), prot_i, std::back_inserter(protocol_),
                 [](int c) { return std::tolower(c); });
  if (prot_i == url_s.end()) {
    prot_i = url_s.begin();
  } else {
    std::advance(prot_i, prot_end.length());
  }
  std::string::const_iterator path_i = std::find(prot_i, url_s.end(), '/');
  host_.reserve(static_cast<std::size_t>(std::distance(prot_i, path_i)));
  std::transform(prot_i, path_i, std::back_inserter(host_),
                 [](int c) { return std::tolower(c); });
  std::string::const_iterator query_i = std::find(path_i, url_s.end(), '?');
  path_.assign(path_i, query_i);
  if (query_i != url_s.end())
    ++query_i;
  query_.assign(query_i, url_s.end());
}

proxy_base::proxy_base(net::io_context &context, int const max_ip)
    : context_{context}, max_ip_{max_ip} {}

void split_ips(std::vector<std::string> &out, std::string const &str) {
  std::size_t i = 0;
  std::size_t last_read_index = i;

  while (i < str.size()) {
    if (str[i] == '\n' || str[i] == '\\') {
      out.emplace_back(
          std::string(str.cbegin() + last_read_index, str.cbegin() + i));
      i = last_read_index = str.find_first_of("0123456789.", i);
      if (i == std::string::npos)
        break;
    }
    ++i;
  }
  if (last_read_index != i) {
    out.emplace_back(
        std::string(str.cbegin() + last_read_index, str.cbegin() + i));
  }
}

std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delim) {
  std::size_t const delim_length = std::strlen(delim);
  std::size_t from_pos{};
  std::size_t index{str.find(delim, from_pos)};
  if (index == std::string::npos)
    return {str};
  std::vector<boost::string_view> result{};
  while (index != std::string::npos) {
    result.emplace_back(str.data() + from_pos, index - from_pos);
    from_pos = index + delim_length;
    index = str.find(delim, from_pos);
  }
  if (from_pos < str.length())
    result.emplace_back(str.data() + from_pos, str.size() - from_pos);
  return result;
}

void proxy_base::get_more_proxies() {
  beast::tcp_stream http_tcp_stream(net::make_strand(context_));

  endpoints_.clear();
  net::ip::tcp::resolver resolver{context_};
  std::vector<custom_endpoint> new_eps{};
  try {
    auto resolves = resolver.resolve("api.wandoudl.com", "http");

    http::request<http::empty_body> http_request{};
    http_tcp_stream.connect(resolves);
    http_request.method(http::verb::get);
    http_request.target(
        R"(/api/ip?app_key=19ead3cb7a5d477f5d480d850f18de94&pack=211389&num=1&xy=3&type=1&lb=\n&mr=1&)");
    http_request.version(11);
    http_request.set(http::field::host, "api.wandoudl.com");
    http_request.set(http::field::user_agent, "Boost Beast 1.70");
    http::write(http_tcp_stream, http_request);
    beast::flat_buffer buffer{};
    http::response<http::string_body> server_response{};
    http::read(http_tcp_stream, buffer, server_response);
    beast::error_code ec{};
    http_tcp_stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    auto &response_body = server_response.body();
    if (server_response.result_int() != 200 || ec) {
      has_error_ = true;
      return spdlog::error("server was kind enough to say: {}", response_body);
    }
    std::vector<std::string> ips;
    split_ips(ips, response_body);
    if (ips.empty() || response_body.find('{') != std::string::npos) {
      has_error_ = true;
      return spdlog::error("IPs empty: {}", response_body);
    }
    spdlog::info("Grabbed {} proxies", ips.size());
    for (auto const &line : ips) {
      if (line.empty())
        continue;
      std::istringstream ss{line};
      std::string ip_address{};
      std::string username{};
      std::string password{};
      ss >> ip_address >> username >> password;
      auto ip_port = split_string_view(ip_address, ":");
      if (ip_port.size() < 2)
        continue;
      ec = {};
      try {
        auto endpoint = net::ip::tcp::endpoint(
            net::ip::make_address(ip_port[0].to_string()),
            std::stoi(ip_port[1].to_string()));
        new_eps.emplace_back(custom_endpoint(std::move(endpoint)));
      } catch (std::exception const &except) {
        has_error_ = true;
        spdlog::error("server says: {}", response_body);
        return spdlog::error("[get_more_proxies] {}", except.what());
      }
    }

  } catch (std::exception const &e) {
    has_error_ = true;
    return spdlog::error("safe_proxy exception: {}", e.what());
  }

  for (auto const &ep : new_eps) {
    endpoints_.push_back(std::make_shared<custom_endpoint>(ep));
  }

  ++extracted_;
}

endpoint_ptr proxy_base::next_endpoint() {
  if (has_error_ || endpoints_.empty())
    return nullptr;
  if (count_ >= endpoints_.size()) {
    count_ = 0;
    while (count_ < endpoints_.size()) {
      if (endpoints_[count_]->property == ProxyProperty::ProxyActive) {
        return endpoints_[count_++];
      }
      ++count_;
    }
  } else {
    return endpoints_[count_++];
  }
  endpoints_.erase(std::remove_if(endpoints_.begin(), endpoints_.end(),
                                  [](auto const &ep) {
                                    return ep->property !=
                                           ProxyProperty::ProxyActive;
                                  }),
                   endpoints_.end());
  if (extracted_ == this->max_ip_)
    return nullptr;
  count_ = 0;
  get_more_proxies();
  if (has_error_ || endpoints_.empty()) {
    int const max_retries = 3;
    int n = 0;
    auto const sleep_time = std::chrono::seconds(10);
    std::size_t const prev_size = endpoints_.size();
    std::this_thread::sleep_for(sleep_time);
    if (endpoints_.size() != prev_size)
      return next_endpoint();
    spdlog::info("Entering while loop");
    while (n < max_retries) {
      ++n;
      get_more_proxies();
      if (has_error_ || endpoints_.empty()) {
        has_error_ = false;
        std::this_thread::sleep_for(sleep_time);
        if (prev_size != endpoints_.size())
          break;
        continue;
      }
      break;
    }
  }
  return next_endpoint();
}

socks5_proxy::socks5_proxy(net::io_context &io, int n) : proxy_base(io, n) {
  get_more_proxies();
}

void custom_endpoint::swap(custom_endpoint &other) {
  std::swap(other.endpoint_, this->endpoint_);
  std::swap(other.property, this->property);
}

void swap(custom_endpoint &a, custom_endpoint &b) { a.swap(b); }

} // namespace wudi_server
