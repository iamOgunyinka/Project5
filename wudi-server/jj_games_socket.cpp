#include "jj_games_socket.hpp"
#include "utilities.hpp"
#include <boost/lexical_cast.hpp>
#include <gzip/decompress.hpp>
#include <nlohmann/json.hpp>

namespace wudi_server {

std::map<boost::string_view, boost::string_view>
parse_headers(std::string_view const &str) {
  std::map<boost::string_view, boost::string_view> header_map{};
  std::vector<boost::string_view> headers = utilities::split_string_view(
      boost::string_view(str.data(), str.size()), "\\r\\n");
  // first row is always a: { HTTP_VERB PATH HTTP/1.1 } combination, called
  // `request line`
  if (headers.size() < 2)
    return {};

  for (auto iter = headers.begin() + 1; iter != headers.end(); ++iter) {
    auto header_key_value =
        utilities::split_string_view(*iter, ": "); // make copies of header info
    if (header_key_value.size() != 2)
      continue;
    header_map[header_key_value[0]] = header_key_value[1];
  }
  return header_map;
}

std::string get_proxy_string(std::optional<tcp::endpoint> const &ep) {
  if (!ep)
    return {};
  return boost::lexical_cast<std::string>(*ep);
}

std::string get_current_time() {
  std::time_t current_time{std::time(nullptr)};
  return std::to_string(current_time);
}

void jj_games_single_interface::create_jj_games_interface() {
  auto &connect_info = connection_;
  std::string const url =
      "https://a4.srv.jj.cn/reg/check_loginname.php?regtype=2&t=" +
      get_current_time() + "&n=1&loginname=" + phone_number;
  connect_info->headers.append("sec-fetch-site: same-origin");
  connect_info->headers.append("sec-fetch-mode: cors");
  connect_info->headers.append("Accept-Language: en-US,en;q=0.5 --compressed");
  connect_info->headers.append(
      "Referer: https://www.jj.cn/reg/reg.html?type=phone");
  connect_info->headers.append("Accept-Encoding: gzip, deflate, br");
  auto curl_handle = connect_info->easy_interface;

  std::string const proxy_address =
      "http://" + get_proxy_string(proxy_provider_.endpoint(proxy_index_));
  std::string const user_agent = utilities::get_random_agent();
  curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_HTTPPROXYTUNNEL, 1L);
  curl_easy_setopt(curl_handle, CURLOPT_PROXY, proxy_address.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_PROXYHEADER,
                   static_cast<curl_slist *>(connect_info->headers));
  curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_writer);
  curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA,
                   static_cast<void *>(&connect_info->header_buffer));
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, user_agent.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br");
  curl_easy_setopt(curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2);
  curl_easy_setopt(curl_handle, CURLOPT_REFERER,
                   "https://www.jj.cn/reg/reg.html?type=phone");
  curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER,
                   static_cast<curl_slist *>(connect_info->headers));
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, data_writer);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA,
                   static_cast<void *>(&connect_info->body_buffer));
  curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER,
                   &connect_info->error_buffer);
  curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS,
                   utilities::TimeoutMilliseconds * 2);
  curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L);
}

std::size_t data_writer(char *buffer, std::size_t, std::size_t nmemb,
                        void *user_data) {
  std::vector<char> *data = static_cast<std::vector<char> *>(user_data);
  std::copy(buffer, buffer + nmemb, std::back_inserter(*data));
  return nmemb;
}

std::size_t header_writer(char *buffer, std::size_t, std::size_t nmemb,
                          void *user_data) {
  std::vector<char> *data = static_cast<std::vector<char> *>(user_data);
  std::transform(buffer, buffer + nmemb, std::back_inserter(*data),
                 [](char const ch) { return std::tolower(ch); });
  return nmemb;
}

jj_games_single_interface::jj_games_single_interface(
    bool &stopped, proxy_provider_t &proxy_provider,
    utilities::number_stream_t &numbers)
    : proxy_provider_{proxy_provider}, numbers_{numbers}, stopped_{stopped} {}

void jj_games_single_interface::start_connect() { initialize_async_sockets(); }

void jj_games_single_interface::initialize_async_sockets() {
  connection_ = std::make_unique<single_connect_info_t>();
  while (!stopped_) {
    try {
      send_next();
    } catch (std::exception const &) {
      break;
    }
  }
}

void jj_games_single_interface::perform_action() {
  if (proxy_index_ == std::numeric_limits<std::size_t>::max() || stopped_) {
    if (stopped_)
      numbers_.push_back(phone_number);
    throw std::runtime_error("could not get proxy");
  }
  create_jj_games_interface();
  connection_->body_buffer.clear();
  connection_->header_buffer.clear();
  connection_->headers = {};
  process_result(curl_easy_perform(connection_->easy_interface));
}

void jj_games_single_interface::send_next() {
  if (stopped_ || !connection_->easy_interface)
    throw std::runtime_error("Stopped");

  phone_number = numbers_.get();
  choose_next_proxy();
  perform_action();
}

void jj_games_single_interface::current_proxy_assign_prop(ProxyProperty p) {
  proxy_provider_.assign_property(proxy_index_, p);
}

void jj_games_single_interface::choose_next_proxy() {
  if (proxy_index_ = proxy_provider_.next_endpoint();
      proxy_index_ == std::numeric_limits<std::size_t>::max()) {
    spdlog::error("error getting next endpoint");
    numbers_.push_back(phone_number);
    phone_number.clear();
    signal_(utilities::search_result_type_e::RequestStop, phone_number);
  }
}

void jj_games_single_interface::process_result(CURLcode const code) {
  if (code != CURLE_OK) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
    perform_action();
    return;
  }
  auto const &result_buffer = connection_->body_buffer;
  auto const headers = parse_headers(connection_->header_buffer.data());
  auto const content_encoding_iter = headers.find("content-encoding");
  auto const content_length_iter = headers.find("content-length");

  std::size_t content_length = result_buffer.size();

  if (content_length_iter != std::cend(headers)) {
    content_length = std::stoul(std::string(content_length_iter->second));
  }
  std::string response_body{};
  if (content_encoding_iter != std::cend(headers)) {
    boost::string_view const encoding = content_encoding_iter->second;
    if (encoding.find("gzip") != boost::string_view::npos) {
      try {
        response_body = gzip::decompress(result_buffer.data(), content_length);
      } catch (std::exception const &) {
        response_body = std::string(result_buffer.data(), content_length);
      }
    } else {
      response_body = std::string(result_buffer.cbegin(), result_buffer.cend());
    }
  } else {
    response_body = std::string(result_buffer.cbegin(), result_buffer.cend());
  }
  using utilities::search_result_type_e;
  std::size_t opening_brace_index = response_body.find_first_of('{');
  std::size_t closing_brace_index = response_body.find_last_of('}');
  if (opening_brace_index == std::string::npos ||
      closing_brace_index == std::string::npos) {
    spdlog::error(response_body);
    signal_(search_result_type_e::Unknown, phone_number);
    return;
  }
  std::string const body_temp(
      response_body.begin() + static_cast<int>(opening_brace_index),
      response_body.begin() + static_cast<int>(closing_brace_index + 1));
  try {
    // badly formed JSON response, server's fault -> there's nothing we can do
    // about that
    json json_response = json::parse(body_temp);
    json::object_t object = json_response.get<json::object_t>();
    bool const status = object["REV"].get<json::boolean_t>();
    if (status) {
      signal_(search_result_type_e::NotRegistered, phone_number);
    } else {
      static std::string const already_registered{
          "%E8%AF%A5%E6%89%8B%E6%9C%BA%E5%8F%B7%E5%B7%B2%E6%B3%A8%E5%86%8C%EF%"
          "BC%8C%E8%AF%B7%E6%9B%B4%E6%8D%A2"};
      std::string const server_message = object["MSG"].get<json::string_t>();
      if (server_message.find(already_registered) != std::string::npos) {
        signal_(search_result_type_e::Registered, phone_number);
      } else {
        spdlog::warn(response_body);
        signal_(search_result_type_e::Unknown, phone_number);
      }
    }
  } catch (std::exception const &) {
  }
}
} // namespace wudi_server
