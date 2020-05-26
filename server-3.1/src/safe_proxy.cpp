#include "safe_proxy.hpp"
#include "custom_timed_socket.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace wudi_server {
enum constants_e { max_read_allowed = 300, max_capacity = 5'000 };

promise_container &global_proxy_repo_t::get_promise_container() {
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
    auto info_posted = std::move(promises.get());
    {
      std::time_t const current_time = std::time(nullptr);
      auto const time_difference = current_time - last_fetch_time;
      auto const interval_secs = utilities::proxy_fetch_interval();
      if (time_difference < interval_secs) {
        std::this_thread::sleep_for(
            std::chrono::seconds(interval_secs - time_difference));
      }
    }
    std::promise<http_result_t> http_result_promise{};
    auto result_future = http_result_promise.get_future();

    std::optional<timed_socket> custom_socket{
        std::in_place,       io_context,       info_posted.url,
        connect_timeout_sec, read_timeout_sec, std::move(http_result_promise)};
    custom_socket->start();
    try {
      auto const status = result_future.wait_for(std::chrono::seconds(30));

      if (status == std::future_status::timeout) {
        custom_socket.reset();
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

extraction_data proxy_base::get_remain_count() {
  beast::tcp_stream http_tcp_stream(net::make_strand(param_.io_));
  auto const count_url_ = utilities::uri{param_.config_.count_target};
  net::ip::tcp::resolver resolver{param_.io_};

  try {
    auto resolves = resolver.resolve(count_url_.host(), count_url_.protocol());
    http_tcp_stream.connect(resolves);
    beast::http::request<http::empty_body> http_request{};
    http_request.method(http::verb::get);
    http_request.target(count_url_.target());
    http_request.version(11);
    http_request.set(http::field::host, count_url_.host());
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

  auto &promises = global_proxy_repo_t::get_promise_container();
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
    for (auto const &ep : shared_data.eps) {
      endpoints_.push_back(std::make_shared<custom_endpoint>(ep));
    }
    proxies_used_ += shared_data.eps.size();
  } else {
    if (endpoints_.size() >= max_capacity) {
      endpoints_.remove_first_n(new_eps.size());
    }
    for (auto const &ep : new_eps) {
      endpoints_.push_back(std::make_shared<custom_endpoint>(ep));
    }
    proxies_used_ += new_eps.size();
  }
  // save_proxies_to_file();
}

void proxy_base::add_more(shared_data_t const &shared_data) {
  bool const can_share = param_.thread_id != shared_data.thread_id &&
                         param_.web_id != shared_data.web_id &&
                         (type() == shared_data.proxy_type);
  if (!can_share)
    return;
  bool has_enough = false;
  if (endpoints_.size() >= max_capacity) {
    endpoints_.remove_if([](endpoint_ptr const &ep) {
      return ep->property != ProxyProperty::ProxyActive;
    });
    int const num_to_remove =
        max_capacity - (endpoints_.size() + shared_data.eps.size());
    if (num_to_remove < 0) {
      endpoints_.remove_first_n(std::abs(num_to_remove));
      has_enough = true;
    }
  }
  for (auto const &ep : shared_data.eps) {
    endpoints_.push_back(std::make_shared<custom_endpoint>(ep));
  }
  if (!has_enough && (shared_data.shared_web_ids.find(param_.web_id) ==
                      shared_data.shared_web_ids.cend())) {
    proxies_used_ += shared_data.eps.size();
    shared_data.shared_web_ids.insert(param_.web_id);
  }
}

void proxy_base::save_proxies_to_file() {
  std::unique_ptr<std::ofstream> out_file_ptr{nullptr};
  if (std::filesystem::exists(param_.filename)) {
    out_file_ptr = std::make_unique<std::ofstream>(
        param_.filename, std::ios::app | std::ios::out);
  } else {
    out_file_ptr = std::make_unique<std::ofstream>(param_.filename);
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
  std::filesystem::path const http_filename_path{param_.filename};
  if (!std::filesystem::exists(http_filename_path)) {
    return get_more_proxies();
  }
  std::error_code ec{};
  auto const last_write_time =
      std::filesystem::last_write_time(http_filename_path, ec);
  auto const since_epoch = last_write_time.time_since_epoch();
  auto const lwt_time =
      std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
  auto const current_time = std::chrono::duration_cast<std::chrono::seconds>(
      decltype(last_write_time)::clock::now().time_since_epoch());
  bool const past_an_hour = current_time > (lwt_time + std::chrono::hours(1));
  if (past_an_hour) {
    ec = {};
    std::filesystem::remove(http_filename_path, ec);
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
      if (endpoints_.size() > max_read_allowed) {
        endpoints_.remove_first_n(1);
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

  endpoints_.remove_if([](auto const &ep) {
    return ep->property != ProxyProperty::ProxyActive;
  });
  count_ = 0;
  if (!endpoints_.empty()) {
    endpoints_.for_each([](auto &e) { e->property = Property::ProxyActive; });
    return endpoints_[count_++];
  }
  get_more_proxies();
  if (has_error_ || endpoints_.empty()) {
    int const max_retries = 5;
    int n = 0;
    auto const sleep_time =
        std::chrono::seconds(utilities::proxy_fetch_interval());
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
    : proxy_base(param, "./socks5_proxy_servers.txt") {
  load_proxy_file();
}

http_proxy::http_proxy(proxy_base_params &param)
    : proxy_base(param, "./http_proxy_servers.txt") {
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

std::optional<proxy_configuration_t> read_proxy_configuration() {
  std::ifstream in_file{"./proxy_config.json"};
  if (!in_file)
    return std::nullopt;
  std::optional<proxy_configuration_t> proxy_config{};
  proxy_config.emplace();
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
      return std::nullopt;
    switch (protocol_index) {
    case 0:
      proxy_config->proxy_protocol = proxy_type_e::socks5;
      break;
    case 1:
      proxy_config->proxy_protocol = proxy_type_e::http_https_proxy;
      break;
    default:
      spdlog::error("unknown proxy procotol specified");
      return std::nullopt;
    }
  } catch (std::exception const &e) {
    spdlog::error("[read_proxy_configuration] {}", e.what());
    return std::nullopt;
  }
  return proxy_config;
}
} // namespace wudi_server
