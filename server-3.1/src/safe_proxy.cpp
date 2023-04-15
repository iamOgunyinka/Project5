#include "safe_proxy.hpp"
#include "custom_timed_socket.hpp"
#include "http_uri.hpp"
#include "random.hpp"
#include "string_utils.hpp"

#include <boost/asio/strand.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace woody_server {
using nlohmann::json;

promise_container_t &global_proxy_repo_t::getPromiseContainer() {
  static promise_container_t container{};
  return container;
}

[[noreturn]] void
global_proxy_repo_t::backgroundProxyFetcher(net::io_context &ioContext) {
  using timed_socket_t = custom_timed_socket_t<http_result_t>;
  auto &promises = getPromiseContainer();
  int const timeoutSec = 15;
  std::time_t lastFetchTime = 0;

  while (true) {
    auto infoPosted{promises.get()};
    {
      std::time_t const currentTime = std::time(nullptr);
      auto const timeDifference = currentTime - lastFetchTime;
      auto const intervalSecs = utilities::proxyFetchInterval();
      if (timeDifference < intervalSecs) {
        std::this_thread::sleep_for(
            std::chrono::seconds(intervalSecs - timeDifference));
      }
    }
    std::promise<http_result_t> httpResultPromise{};
    auto resultFuture = httpResultPromise.get_future();

    auto customSocket = std::make_unique<timed_socket_t>(
        ioContext, infoPosted.url, timeoutSec, std::move(httpResultPromise));
    customSocket->start();
    try {
      auto const status = resultFuture.wait_for(std::chrono::seconds(60));

      if (status == std::future_status::timeout) {
        customSocket.reset();
        lastFetchTime = std::time(nullptr);
        throw std::runtime_error{"connection timed out"};
      }
      auto const result = resultFuture.get();
      lastFetchTime = std::time(nullptr);
      infoPosted.promise.set_value(result);
    } catch (...) {
      infoPosted.promise.set_exception(std::current_exception());
    }
  }
}

proxy_base_t::proxy_base_t(proxy_base_params_t &params,
                           std::string const &filename)
    : m_proxyParam(params) {
  params.filename = filename;
  params.proxyInfoMap[params.threadID] = {};
}

proxy_base_t::~proxy_base_t() {
  m_proxyParam.proxyInfoMap.erase(m_proxyParam.threadID);
}

extraction_data_t proxy_base_t::getRemainCount() {
  beast::tcp_stream tcpStream(net::make_strand(m_proxyParam.m_ioContext));
  auto const countUrl = http_uri_t{m_proxyParam.m_config.countTarget};
  tcp::resolver resolver{m_proxyParam.m_ioContext};

  try {
    auto resolves = resolver.resolve(countUrl.host(), countUrl.protocol());
    tcpStream.connect(resolves);
    beast::http::request<http::empty_body> httpRequest{};
    httpRequest.method(http::verb::get);
    httpRequest.target(countUrl.target());
    httpRequest.version(11);
    httpRequest.set(http::field::host, countUrl.host());
    httpRequest.set(http::field::user_agent, utilities::getRandomUserAgent());
    http::write(tcpStream, httpRequest);
    beast::flat_buffer buffer{};
    http::response<http::string_body> serverResponse{};
    http::read(tcpStream, buffer, serverResponse);

    if (serverResponse.result_int() != 200)
      return {};
    tcpStream.cancel();
    auto &responseBody = serverResponse.body();
    responseBody.erase(
        std::remove(responseBody.begin(), responseBody.end(), '\n'),
        responseBody.end());
    auto getTimeT = [](std::string const &timeOpened) -> std::time_t {
      std::tm tm{};
      std::memset(&tm, 0, sizeof(tm));
      std::istringstream ss(timeOpened);
      ss >> std::get_time(&tm, "%Y-%m-%d%H:%M:%S");
      return std::mktime(&tm);
    };

    auto jsonResult = json::parse(responseBody).get<json::object_t>();
    if (jsonResult["code"].get<json::number_integer_t>() != 200)
      return {};
    auto const dataList = jsonResult["data"].get<json::array_t>();
    std::vector<extraction_data_t> proxyExtractInfo{};
    proxyExtractInfo.reserve(dataList.size());

    for (auto const &data_item : dataList) {
      auto dataObject = data_item.get<json::object_t>();
      auto const expireTime = dataObject["expire_time"].get<json::string_t>();

      extraction_data_t data{};
      data.expireTime = getTimeT(expireTime);
      data.isAvailable = data_item["is_available"].get<json::boolean_t>();
      // for unknown and most likely insane reasons, the server returns some
      // data as both integers and strings
      try {
        data.connectRemain = static_cast<int>(
            data_item["remain_connect"].get<json::number_integer_t>());
      } catch (std::exception const &) {
        data.connectRemain =
            std::stoi(data_item["remain_connect"].get<json::string_t>());
      }
      try {
        data.extractRemain = static_cast<int>(
            data_item["remain_extract"].get<json::number_integer_t>());
      } catch (std::exception const &) {
        data.extractRemain =
            std::stoi(data_item["remain_extract"].get<json::string_t>());
      }
      try {
        data.productRemain =
            std::stoi(data_item["remain"].get<json::string_t>());
      } catch (std::exception const &) {
        data.productRemain =
            static_cast<int>(data_item["remain"].get<json::number_integer_t>());
      }
      proxyExtractInfo.push_back(data);
    }
    if (proxyExtractInfo.empty() || !proxyExtractInfo.back().isAvailable) {
      return {};
    }
    return proxyExtractInfo.back();
  } catch (std::exception const &e) {
    spdlog::error("[get_remain_count] {}", e.what());
    return {};
  }
}

void splitIps(std::vector<std::string> &out, std::string const &str) {
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

void proxy_base_t::getMoreProxies() {

  auto &post_office = global_proxy_repo_t::getPromiseContainer();
  std::vector<custom_endpoint_t> new_eps{};

  try {
    if (!m_confirmCount || m_currentExtractedData.extractRemain > 0) {

      posted_data_t letter{m_proxyParam.m_config.proxyTarget, {}};
      auto future = letter.promise.get_future();
      post_office.append(std::move(letter));

      // wait for 5 minutes max
      auto const wait_status = future.wait_for(std::chrono::minutes(5));
      if (wait_status == std::future_status::timeout) {
        m_hasError = true;
        return;
      }

      auto const result = future.get();
      auto const status_code = result.statusCode;
      auto &response_body = result.responseBody;

      if (status_code != 200) {
        m_hasError = true;
        return spdlog::error("server was kind enough to say: {}",
                             response_body);
      }
      std::vector<std::string> ips;
      splitIps(ips, response_body);
      if (ips.empty() || response_body.find('{') != std::string::npos) {
        m_hasError = true;
        return spdlog::error("IPs isEmpty: {}", response_body);
      }
      new_eps.reserve(m_proxyParam.m_config.fetchOnce);
      spdlog::info("Grabbed {} proxies", ips.size());
      for (auto const &line : ips) {
        if (line.empty()) {
          continue;
        }
        std::istringstream ss{line};
        std::string ip_address{};
        std::string username{};
        std::string password{};
        ss >> ip_address >> username >> password;
        auto ip_port = utilities::splitStringView(ip_address, ":");
        if (ip_port.size() < 2)
          continue;
        try {
          auto endpoint = net::ip::tcp::endpoint(
              net::ip::make_address(ip_port[0].to_string()),
              std::stoi(ip_port[1].to_string()));
          new_eps.emplace_back(std::move(endpoint), username, password);
        } catch (std::exception const &except) {
          m_hasError = true;
          spdlog::error("server says: {}", response_body);
          return spdlog::error("[getMoreProxies] {}", except.what());
        }
      }
    }
  } catch (std::exception const &e) {
    m_hasError = true;
    return spdlog::error("safe_proxy exception: {}", e.what());
  }

  if (m_proxyParam.m_config.shareProxy && !m_proxyParam.m_signal.empty()) {
    shared_data_t shared_data{};
    shared_data.endpoints = std::move(new_eps);
    shared_data.proxyType = type();
    shared_data.threadID = m_proxyParam.threadID;
    shared_data.webID = m_proxyParam.webID;
    shared_data.sharedWebIDs.insert(m_proxyParam.webID);
    m_proxyParam.m_signal(shared_data);

    if (m_endpoints.size() >= MaxCapacity) {
      m_endpoints.removeFirstN(shared_data.endpoints.size());
    }
    m_proxyParam.proxyInfoMap[m_proxyParam.threadID].proxyCount +=
        shared_data.endpoints.size();
    m_endpoints.append(shared_data.endpoints);
    m_proxiesUsed += shared_data.endpoints.size();
  } else {
    if (m_endpoints.size() >= MaxCapacity) {
      m_endpoints.removeFirstN(new_eps.size());
    }
    m_proxyParam.proxyInfoMap[m_proxyParam.threadID].proxyCount +=
        new_eps.size();
    m_endpoints.append(new_eps);
    m_proxiesUsed += new_eps.size();
  }
}

void proxy_base_t::addMore(shared_data_t const &shared_data) {
  bool const can_share = m_proxyParam.threadID != shared_data.threadID &&
                         m_proxyParam.webID != shared_data.webID &&
                         (type() == shared_data.proxyType);
  if (!can_share || m_endpoints.size() >= MaxCapacity)
    return;
  if (shared_data.sharedWebIDs.find(m_proxyParam.webID) ==
      shared_data.sharedWebIDs.cend()) {
    auto iter = m_proxyParam.proxyInfoMap.cend();
    for (auto find_iter = m_proxyParam.proxyInfoMap.cbegin();
         find_iter != m_proxyParam.proxyInfoMap.cend(); ++find_iter) {
      if (find_iter->second.webID != m_proxyParam.webID)
        continue;

      if (iter == m_proxyParam.proxyInfoMap.cend() ||
          iter->second.proxyCount < find_iter->second.proxyCount) {
        iter = find_iter;
      }
    }
    if (iter == m_proxyParam.proxyInfoMap.cend())
      return;
    m_endpoints.append(shared_data.endpoints);
    m_proxyParam.proxyInfoMap[m_proxyParam.threadID].proxyCount +=
        shared_data.endpoints.size();
    m_proxiesUsed += shared_data.endpoints.size();
    shared_data.sharedWebIDs.insert(m_proxyParam.webID);
  }
}

void proxy_base_t::saveProxiesToFile() {
  std::unique_ptr<std::ofstream> out_file_ptr{nullptr};
  if (std::filesystem::exists(m_proxyParam.filename)) {
    out_file_ptr = std::make_unique<std::ofstream>(
        m_proxyParam.filename, std::ios::app | std::ios::out);
  } else {
    out_file_ptr = std::make_unique<std::ofstream>(m_proxyParam.filename);
  }
  if (!out_file_ptr)
    return;
  std::set<std::string> unique_set{};
  m_endpoints.forEach([&](endpoint_ptr_t const &proxy) {
    try {
      auto const ep = boost::lexical_cast<std::string>(proxy->endpoint);
      if (unique_set.find(ep) == unique_set.end()) {
        unique_set.insert(ep);
        (*out_file_ptr) << ep << " " << proxy->getUsername() << " "
                        << proxy->getPassword() << "\n";
      }
    } catch (std::exception const &e) {
      spdlog::error("Exception in \"save_proxy\": {}", e.what());
    }
  });
  out_file_ptr->close();
}

void proxy_base_t::loadProxyFile() {
  std::filesystem::path const http_filename_path{m_proxyParam.filename};
  if (!std::filesystem::exists(http_filename_path)) {
    return getMoreProxies();
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
    return getMoreProxies();
  }
  std::string line{};
  std::vector<std::string> ip_port{};

  while (std::getline(proxy_file, line)) {
    ip_port.clear();
    utilities::trimString(line);
    if (line.empty())
      continue;
    std::istringstream ss(line);
    std::string temp_ip{};
    std::string username{};
    std::string password{};
    ss >> temp_ip >> username >> password;
    utilities::splitStringInto(ip_port, temp_ip, ":");
    if (ip_port.size() < 2)
      continue;
    try {
      auto endpoint = net::ip::tcp::endpoint(net::ip::make_address(ip_port[0]),
                                             std::stoi(ip_port[1]));
      if (m_endpoints.size() > MaxReadAllowed) {
        m_endpoints.removeFirstN(1);
      }
      m_endpoints.append(
          custom_endpoint_t(std::move(endpoint), username, password));
    } catch (std::exception const &e) {
      spdlog::error("Error while converting( {} ), {}", line, e.what());
    }
  }
}

endpoint_ptr_t proxy_base_t::nextEndpoint() {
  if (m_hasError || m_endpoints.empty())
    return nullptr;
  if (m_count >= m_endpoints.size()) {
    m_count = 0;
    while (m_count < m_endpoints.size()) {
      if (m_endpoints[m_count]->property == proxy_property_e::ProxyActive) {
        return m_endpoints[m_count++];
      }
      ++m_count;
    }
  } else {
    while (m_count < m_endpoints.size()) {
      if (m_endpoints[m_count]->property == proxy_property_e::ProxyActive) {
        return m_endpoints[m_count++];
      }
      ++m_count;
    }
    return m_endpoints.back();
  }

  m_endpoints.removeIf([](auto const &ep) {
    return ep->property != proxy_property_e::ProxyActive &&
           ep->property != proxy_property_e::ProxyToldToWait;
  });
  m_count = 0;
  if (!m_endpoints.empty()) {
    auto const current_time = std::time(nullptr);
    bool has_usable = false;
    std::size_t usable_index = 0;
    m_endpoints.forEach([&](endpoint_ptr_t &e) {
      if (e->property == proxy_property_e::ProxyToldToWait &&
          (e->timeLastUsed + (600)) <= current_time) {
        e->property = proxy_property_e::ProxyActive;
        has_usable = true;
        if (m_count == 0)
          m_count = usable_index;
      }
      ++usable_index;
    });
    if (m_count != 0)
      return m_endpoints[m_count++];
  }
  getMoreProxies();
  if (m_hasError || m_endpoints.empty()) {
    int const max_retries = 5;
    int n = 0;
    auto const sleep_time =
        std::chrono::seconds(utilities::proxyFetchInterval());
    std::size_t const prev_size = m_endpoints.size();
    std::this_thread::sleep_for(sleep_time);
    if (m_endpoints.size() != prev_size)
      return nextEndpoint();
    while (n < max_retries) {
      ++n;
      getMoreProxies();
      if (m_hasError || m_endpoints.empty()) {
        m_hasError = false;
        std::this_thread::sleep_for(sleep_time);
        if (prev_size != m_endpoints.size())
          break;
        continue;
      }
      break;
    }
  }
  return nextEndpoint();
}

proxy_type_e proxy_base_t::type() const {
  return m_proxyParam.m_config.proxyProtocol;
}

socks5_proxy_t::socks5_proxy_t(proxy_base_params_t &param)
    : proxy_base_t(param, "./socks5_proxy_servers.txt") {
  loadProxyFile();
}

http_proxy_t::http_proxy_t(proxy_base_params_t &param)
    : proxy_base_t(param, "./http_proxy_servers.txt") {
  loadProxyFile();
}

net::io_context &get_network_context() {
  static boost::asio::io_context context{};
  return context;
}

void custom_endpoint_t::swap(custom_endpoint_t &other) {
  std::swap(other.endpoint, this->endpoint);
  std::swap(other.property, this->property);
}

void swap(custom_endpoint_t &a, custom_endpoint_t &b) { a.swap(b); }

std::optional<proxy_configuration_t> readProxyConfiguration() {
  std::ifstream in_file{"./proxy_config.json"};
  if (!in_file)
    return std::nullopt;
  std::optional<proxy_configuration_t> proxy_config{};
  proxy_config.emplace();
  try {
    json json_file;
    in_file >> json_file;
    auto root_object = json_file.get<json::object_t>();
    auto proxy_field = root_object["proxy"].get<json::object_t>();
    auto available_protocols =
        proxy_field["#available_protocols"].get<json::array_t>();

    proxy_config->softwareVersion = static_cast<int>(
        root_object["client_version"].get<json::number_integer_t>());
    std::size_t const highest_index = available_protocols.size();
    std::size_t const protocol_index =
        proxy_field["protocol"].get<json::number_integer_t>();
    proxy_config->proxyTarget = proxy_field["target"].get<json::string_t>();
    proxy_config->countTarget =
        proxy_field["count_target"].get<json::string_t>();
    proxy_config->proxyUsername = proxy_field["username"].get<json::string_t>();
    proxy_config->proxyPassword = proxy_field["password"].get<json::string_t>();
    proxy_config->shareProxy = proxy_field["share"].get<json::boolean_t>();
    proxy_config->maxSocket = static_cast<int>(
        proxy_field["socket_count"].get<json::number_integer_t>());
    proxy_config->fetchOnce = static_cast<int>(
        proxy_field["per_fetch"].get<json::number_integer_t>());
    proxy_config->fetchInterval = static_cast<int>(
        proxy_field["fetch_interval"].get<json::number_integer_t>());
    if (protocol_index >= highest_index)
      return std::nullopt;
    switch (protocol_index) {
    case 0:
      proxy_config->proxyProtocol = proxy_type_e::socks5;
      break;
    case 1:
      proxy_config->proxyProtocol = proxy_type_e::http_https_proxy;
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
} // namespace woody_server
