#include "safe_proxy.hpp"
#include "custom_timed_socket.hpp"
#include "utilities.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

extern int FETCH_INTERVAL;

namespace wudi_server {
enum constants_e { max_read_allowed = 300, max_capacity = 5'000 };

using promise_container = utilities::threadsafe_cv_container<posted_data_t>;

promise_container &get_promise_container() {
  static promise_container container{};
  return container;
}

void global_proxy_repo_t::background_proxy_fetcher(
    net::io_context &io_context) {
  using timed_socket = custom_timed_socket_t<http_result_t>;
  auto &promises = get_promise_container();
  int const connect_timeout_sec = 15;
  int const read_timeout_sec = 15;
  std::time_t last_fetch_time = 0;

  while (true) {
    auto info_posted{promises.get()};
    {
      std::time_t const current_time = std::time(nullptr);
      auto const time_difference = current_time - last_fetch_time;
      if (time_difference < FETCH_INTERVAL) {
        std::this_thread::sleep_for(
            std::chrono::seconds(FETCH_INTERVAL - time_difference));
      }
    }
    std::promise<http_result_t> http_result_promise{};
    auto result_future = http_result_promise.get_future();

    timed_socket custom_socket(io_context, info_posted.url, connect_timeout_sec,
                               read_timeout_sec,
                               std::move(http_result_promise));
    custom_socket.start();
    try {
      auto const status = result_future.wait_for(std::chrono::seconds(30));

      if (status == std::future_status::timeout) {
        last_fetch_time = std::time(nullptr);
        throw std::runtime_error{"connection timed out"};
      }
      auto const result = result_future.get();
      last_fetch_time = std::time(nullptr);
      info_posted.promise.set_value(result);
    } catch (...) {
      info_posted.promise.set_exception(std::current_exception());
    }
  }
}

proxy_base::proxy_base(proxy_base_params &params, std::string const &filename)
    : param_{params} {
  params.filename = filename;
}

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

void proxy_base::get_more_proxies() {

  auto &promises = get_promise_container();
  std::vector<custom_endpoint> new_eps{};

  try {
    if (!confirm_count_ || current_extracted_data_.extract_remain > 0) {

      posted_data_t post_office{param_.config_.proxy_target, {}};
      auto future = post_office.promise.get_future();
      promises.push_back(std::move(post_office));

      // wait for 2 minutes, 30 seconds max
      auto const wait_status = future.wait_for(std::chrono::seconds(150));
      if (wait_status == std::future_status::timeout) {
        has_error_ = true;
        return;
      }

      auto const result = future.get();
      int const status_code = result.status_code;
      auto &response_body = result.response_body;

      if (status_code != 200) {
        has_error_ = true;
        return spdlog::error("server was kind enough to say: {}",
                             result.response_body);
      }
      std::vector<std::string> ips;
      split_ips(ips, response_body);
      if (ips.empty() || response_body.find('{') != std::string::npos) {
        has_error_ = true;
        return spdlog::error("IPs empty: {}", response_body);
      }
      new_eps.reserve(param_.config_.fetch_once);
      spdlog::info("Grabbed {} proxies", ips.size());
      for (auto const &line : ips) {
        if (line.empty())
          continue;
        std::istringstream ss{line};
        std::string ip_address{};
        std::string username{};
        std::string password{};
        ss >> ip_address >> username >> password;
        auto ip_port = utilities::split_string_view(ip_address, ":");
        if (ip_port.size() < 2)
          continue;
        boost::system::error_code ec{};
        try {
          auto endpoint = net::ip::tcp::endpoint(
              net::ip::make_address(ip_port[0].to_string()),
              std::stoi(ip_port[1].to_string()));
          new_eps.emplace_back(
              custom_endpoint(std::move(endpoint), username, password));
        } catch (std::exception const &except) {
          has_error_ = true;
          spdlog::error("server says: {}", response_body);
          return spdlog::error("[get_more_proxies] {}", except.what());
        }
      }
    }
  } catch (std::exception const &e) {
    has_error_ = true;
    return spdlog::error("safe_proxy exception: {}", e.what());
  }

  if (param_.config_.share_proxy && !param_.signal_.empty()) {
    shared_data_t shared_data{};
    shared_data.eps = std::move(new_eps);
    shared_data.proxy_type = type();
    shared_data.thread_id = param_.thread_id;
    shared_data.web_id = param_.web_id;
    shared_data.shared_web_ids.insert(param_.web_id);
    param_.signal_(shared_data);

    if (endpoints_.size() >= max_capacity) {
      endpoints_.remove_first_n(shared_data.eps.size());
    }
    endpoints_.push_back(shared_data.eps);
    proxies_used_ += shared_data.eps.size();
  } else {
    if (endpoints_.size() >= max_capacity) {
      endpoints_.remove_first_n(new_eps.size());
    }
    endpoints_.push_back(new_eps);
    proxies_used_ += new_eps.size();
  }
}

void proxy_base::add_more(shared_data_t const &shared_data) {
  bool const can_share = param_.thread_id != shared_data.thread_id &&
                         param_.web_id != shared_data.web_id &&
                         (type() == shared_data.proxy_type);
  if (!can_share || endpoints_.size() >= max_capacity)
    return;
  endpoints_.push_back(shared_data.eps);
  if (shared_data.shared_web_ids.find(param_.web_id) ==
      shared_data.shared_web_ids.cend()) {
    proxies_used_ += shared_data.eps.size();
    shared_data.shared_web_ids.insert(param_.web_id);
  }
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
    while (count_ < endpoints_.size()) {
      if (endpoints_[count_]->property == ProxyProperty::ProxyActive) {
        return endpoints_[count_++];
      }
      ++count_;
    }
    return endpoints_.back();
  }

  endpoints_.remove_if([](auto const &ep) {
    return ep->property != ProxyProperty::ProxyActive &&
           ep->property != ProxyProperty::ProxyToldToWait;
  });
  count_ = 0;
  if (!endpoints_.empty()) {
    auto const current_time = std::time(nullptr);
    bool has_usable = false;
    std::size_t usable_index = 0;
    endpoints_.for_each([&](auto &e) {
      if (e->property == ProxyProperty::ProxyToldToWait &&
          (e->time_last_used + (600)) <= current_time) {
        e->property = Property::ProxyActive;
        has_usable = true;
        if (count_ == 0)
          count_ = usable_index;
      }
      ++usable_index;
    });
    if (count_ != 0)
      return endpoints_[count_++];
  }
  get_more_proxies();
  if (has_error_ || endpoints_.empty()) {
    int const max_retries = 5;
    int n = 0;
    auto const sleep_time = std::chrono::seconds(FETCH_INTERVAL);
    std::size_t const prev_size = endpoints_.size();
    std::this_thread::sleep_for(sleep_time);
    if (endpoints_.size() != prev_size)
      return next_endpoint();
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

proxy_type_e proxy_base::type() const { return param_.config_.proxy_protocol; }

socks5_proxy::socks5_proxy(proxy_base_params &param)
    : proxy_base(param, "./socks5_proxy_servers.txt") {}

http_proxy::http_proxy(proxy_base_params &param)
    : proxy_base(param, "./http_proxy_servers.txt") {}

net::io_context &get_network_context() {
  static boost::asio::io_context context{};
  return context;
}

void custom_endpoint::swap(custom_endpoint &other) {
  std::swap(other.endpoint_, this->endpoint_);
  std::swap(other.property, this->property);
}

void swap(custom_endpoint &a, custom_endpoint &b) { a.swap(b); }

std::unique_ptr<proxy_configuration_t> read_proxy_configuration() {
  using nlohmann::json;
  std::ifstream in_file{"./proxy_config.json"};
  if (!in_file)
    return nullptr;
  auto proxy_config = std::make_unique<proxy_configuration_t>();
  try {
    json json_file;
    in_file >> json_file;
    json::object_t root_object = json_file.get<json::object_t>();
    auto proxy_field = root_object["proxy"].get<json::object_t>();
    auto available_protocols =
        proxy_field["#available_protocols"].get<json::array_t>();

    proxy_config->software_version =
        root_object["client_version"].get<json::number_integer_t>();
    std::size_t const highest_index = available_protocols.size();
    std::size_t const protocol_index =
        proxy_field["protocol"].get<json::number_integer_t>();
    proxy_config->proxy_target = proxy_field["target"].get<json::string_t>();
    proxy_config->count_target =
        proxy_field["count_target"].get<json::string_t>();
    proxy_config->proxy_username =
        proxy_field["username"].get<json::string_t>();
    proxy_config->proxy_password =
        proxy_field["password"].get<json::string_t>();
    proxy_config->share_proxy = proxy_field["share"].get<json::boolean_t>();
    proxy_config->max_socket =
        proxy_field["socket_count"].get<json::number_integer_t>();
    proxy_config->fetch_once =
        proxy_field["per_fetch"].get<json::number_integer_t>();
    proxy_config->fetch_interval =
        proxy_field["fetch_interval"].get<json::number_integer_t>();
    if (protocol_index >= highest_index)
      return nullptr;
    switch (protocol_index) {
    case 0:
      proxy_config->proxy_protocol = proxy_type_e::socks5;
      break;
    case 1:
      proxy_config->proxy_protocol = proxy_type_e::http_https_proxy;
      break;
    default:
      spdlog::error("unknown proxy procotol specified");
      return nullptr;
    }
  } catch (std::exception const &e) {
    spdlog::error("[read_proxy_configuration] {}", e.what());
    return nullptr;
  }
  return proxy_config;
}
} // namespace wudi_server
