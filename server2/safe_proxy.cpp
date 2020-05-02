#include "safe_proxy.hpp"

#include <spdlog/spdlog.h>

#include "utilities.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace wudi_server {

enum constants_e { max_allowed = 5'000, minutes_allowed = 60 * 2 };

proxy_base::proxy_base(net::io_context &context, NewProxySignal &proxy_signal,
                       proxy_configuration_t &proxy_config, std::thread::id id,
                       std::uint32_t web_id, std::string const &filename)
    : context_{context},
      broadcast_proxy_signal_(proxy_signal), proxy_config_{proxy_config},
      this_thread_id_{id}, website_id_{web_id}, filename_{filename} {}

extraction_data proxy_base::get_remain_count(
    net::ip::basic_resolver_results<net::ip::tcp> &resolves) {
  beast::tcp_stream http_tcp_stream(net::make_strand(context_));
  try {
    http_tcp_stream.connect(resolves);
    beast::http::request<http::empty_body> http_request{};
    http_request.method(http::verb::get);
    http_request.target(proxy_config_.count_target);
    http_request.version(11);
    http_request.set(http::field::host,
                     utilities::uri{proxy_config_.hostname}.host());
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
    if (proxy_extract_info.empty() || !proxy_extract_info.back().is_available) {
      return {};
    }
    return proxy_extract_info.back();
  } catch (std::exception const &e) {
    spdlog::error("[get_remain_count] {}", e.what());
    return {};
  }
}

void proxy_base::get_more_proxies() {
  if (last_fetch_time_ == 0) {
    last_fetch_time_ = std::time(nullptr);
  } else {
    std::time_t const current_time = std::time(nullptr);
    auto const time_difference = current_time - last_fetch_time_;
    if (time_difference < minutes_allowed) {
      auto const ep_size = endpoints_.size();
      std::this_thread::sleep_for(
          std::chrono::seconds(minutes_allowed - time_difference));
      last_fetch_time_ += minutes_allowed;
      // while this is sleeping, has a new thread fetched new EPs?
      if (endpoints_.size() != ep_size)
        return;
    } else {
      last_fetch_time_ = current_time;
    }
  }
  utilities::uri uri_{proxy_config_.hostname};

  beast::tcp_stream http_tcp_stream(net::make_strand(context_));
  http::request<http::empty_body> http_request_;
  net::ip::tcp::resolver resolver{context_};

  std::vector<custom_endpoint> new_eps{};
  new_eps.reserve(proxy_config_.fetch_once);
  std::lock_guard<std::mutex> lock_g{mutex_};
  try {
    auto resolves = resolver.resolve(uri_.host(), uri_.protocol());
    if (confirm_count_) {
      current_extracted_data_ = get_remain_count(resolves);
      if (!current_extracted_data_.is_available) {
        has_error_ = true;
        return;
      }
    }
    if (!confirm_count_ || current_extracted_data_.extract_remain > 0) {
      http_tcp_stream.connect(resolves);
      http_request_ = {};
      http_request_.method(http::verb::get);
      http_request_.target(proxy_config_.proxy_target);
      http_request_.version(11);
      http_request_.set(http::field::host, uri_.host());
      http_request_.set(http::field::user_agent, utilities::get_random_agent());
      http::write(http_tcp_stream, http_request_);
      beast::flat_buffer buffer{};
      http::response<http::string_body> server_response{};
      http::read(http_tcp_stream, buffer, server_response);
      beast::error_code ec{};
      http_tcp_stream.socket().shutdown(tcp::socket::shutdown_both, ec);
      auto &response_body = server_response.body();
      if (server_response.result_int() != 200 || ec) {
        has_error_ = true;
        spdlog::error("server was kind enough to say: {}", response_body);
        return spdlog::error("Error obtaining proxy servers from server");
      }
      std::vector<std::string> ips;
      boost::split(ips, response_body,
                   [](char const ch) { return ch == '\n'; });
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
        auto ip_port = utilities::split_string_view(ip_address, ":");
        if (ip_port.size() < 2)
          continue;
        ec = {};
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
      has_error_ = ips.empty();
    }
  } catch (std::exception const &e) {
    spdlog::error("safe_proxy exception: {}", e.what());
    has_error_ = true;
  }
  if (has_error_) {
    return spdlog::error("Error occurred while getting more proxies");
  }
  broadcast_proxy_signal_(this_thread_id_, website_id_, type(), new_eps);
  if (endpoints_.size() >= max_allowed) {
    endpoints_.remove(new_eps.size());
  }
  using iter_t = std::vector<endpoint_ptr>::iterator;
  for (auto const &ep : new_eps) {
    endpoints_.push_back(std::make_shared<custom_endpoint>(ep));
  }
  save_proxies_to_file();
}

void proxy_base::add_more(std::thread::id const thread_id,
                          std::uint32_t const web_id, proxy_type_e proxy_type,
                          std::vector<custom_endpoint> const &new_endpoints) {
  bool const compatible = thread_id != this_thread_id_ &&
                          website_id_ != web_id && (type() == proxy_type);
  if (!compatible)
    return;
  if (endpoints_.size() >= max_allowed) {
    endpoints_.remove(new_endpoints.size());
  }
  for (auto const &ep : new_endpoints) {
    endpoints_.push_back(std::make_shared<custom_endpoint>(ep));
  }
}

void proxy_base::save_proxies_to_file() {
  std::unique_ptr<std::ofstream> out_file_ptr{nullptr};
  if (std::filesystem::exists(filename_)) {
    out_file_ptr = std::make_unique<std::ofstream>(
        filename_, std::ios::app | std::ios::out);
  } else {
    out_file_ptr = std::make_unique<std::ofstream>(filename_);
  }
  if (!out_file_ptr)
    return;
  try {
    out_file_ptr->seekp(std::ios::beg);
  } catch (std::exception const &) {
  }
  std::set<std::string> unique_set{};
  endpoints_.for_each([&](auto const &proxy) {
    try {
      std::string const ep = boost::lexical_cast<std::string>(proxy->endpoint_);
      if (unique_set.find(ep) == unique_set.end()) {
        unique_set.insert(ep);
        (*out_file_ptr) << ep << " " << proxy->username() << " "
                        << proxy->password() << "\n";
      }
    } catch (std::exception const &e) {
      spdlog::error("Exception in \"save_proxy\": {}", e.what());
    }
  });
  out_file_ptr->close();
}

void proxy_base::load_proxy_file() {
  std::filesystem::path const http_filename_path{filename_};
  if (!std::filesystem::exists(http_filename_path)) {
    return get_more_proxies();
  }
  std::ifstream proxy_file{http_filename_path};
  if (!proxy_file) {
    return get_more_proxies();
  }
  std::string line{};
  std::vector<std::string> ip_port{};

  while (std::getline(proxy_file, line)) {
    ip_port.clear();
    boost::trim(line);
    if (line.empty())
      continue;
    std::istringstream ss(line);
    std::string temp_ip{};
    std::string username{};
    std::string password{};
    ss >> temp_ip >> username >> password;
    boost::split(ip_port, temp_ip, [](auto ch) { return ch == ':'; });
    if (ip_port.size() < 2)
      continue;
    beast::error_code ec{};
    try {
      auto endpoint = net::ip::tcp::endpoint(net::ip::make_address(ip_port[0]),
                                             std::stoi(ip_port[1]));
      if (endpoints_.size() > max_allowed) {
        endpoints_.remove(1);
      }
      endpoints_.push_back(std::make_shared<custom_endpoint>(
          std::move(endpoint), username, password));
    } catch (std::exception const &e) {
      spdlog::error("Error while converting( {} ), {}", line, e.what());
    }
  }
}

endpoint_ptr proxy_base::next_endpoint() {
  if (has_error_ || endpoints_.empty())
    return nullptr;
  {
    std::lock_guard<std::mutex> lock_g{mutex_};
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
  }
  get_more_proxies();
  endpoints_.remove_if([](auto const &ep) {
    return ep->property == ProxyProperty::ProxyBlocked;
  });
  count_ = 0;
  return next_endpoint();
}

proxy_type_e proxy_base::type() const { return proxy_config_.proxy_protocol; }

socks5_proxy::socks5_proxy(net::io_context &io, NewProxySignal &proxy_signal,
                           proxy_configuration_t &proxy_config,
                           std::thread::id id, std::uint32_t web_id)
    : proxy_base(io, proxy_signal, proxy_config, id, web_id,
                 "./socks5_proxy_servers.txt") {
  load_proxy_file();
}

http_proxy::http_proxy(net::io_context &context, NewProxySignal &proxy_signal,
                       proxy_configuration_t &proxy_config,
                       std::thread::id thread_id, std::uint32_t web_id)
    : proxy_base(context, proxy_signal, proxy_config, thread_id, web_id,
                 "./http_proxy_servers.txt") {
  load_proxy_file();
}

net::io_context &get_network_context() {
  static boost::asio::io_context context{};
  return context;
}

void custom_endpoint::swap(custom_endpoint &other) {
  std::swap(other.endpoint_, this->endpoint_);
  std::swap(other.property, this->property);
}

void swap(custom_endpoint &a, custom_endpoint &b) { a.swap(b); }
} // namespace wudi_server
