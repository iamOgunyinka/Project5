#include "jj_games_socket.hpp"
#include "utilities.hpp"
#include <gzip/decompress.hpp>
#include <nlohmann/json.hpp>

namespace wudi_server {
jj_games_socket::jj_games_socket(net::io_context &io,
                                 safe_proxy &proxy_provider,
                                 utilities::number_stream &numbers)
    : context_{io}, proxy_provider_{proxy_provider}, timer_{io},
      numbers_{numbers}, thread_data_{} {}

void jj_games_socket::start_connect() {
  using custom_curl::multi_socket_callback;
  using custom_curl::multi_socket_timer_callback;

  if (!thread_data_)
    return;

  curl_multi_setopt(thread_data_.multi_interface, CURLMOPT_SOCKETFUNCTION,
                    multi_socket_callback);
  curl_multi_setopt(thread_data_.multi_interface, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(thread_data_.multi_interface, CURLMOPT_TIMERFUNCTION,
                    multi_socket_timer_callback);
  curl_multi_setopt(thread_data_.multi_interface, CURLMOPT_TIMERDATA, this);
  initialize_async_sockets();
}

void jj_games_socket::remove_tcp_socket(int *action_ptr) {
  if (action_ptr) {
    fd_list.erase(fd_list.find(action_ptr));
    delete action_ptr;
  }
}

void jj_games_socket::initialize_async_sockets() {
  int const max_connections = 1;
  connections.clear();
  connections.reserve(
      max_connections); // very important! We can't afford to reallocate
  for (std::size_t i = 0; i != max_connections; ++i) {
    auto proxy = proxy_provider_.next_endpoint().value();
    ConnectInfo *connect_info{new ConnectInfo{context_, socket_map}};
    if (!connect_info->easy_interface) {
      delete connect_info;
      continue;
    }
    auto connected = custom_curl::create_jj_games_interface(
        connect_info, thread_data_, proxy, numbers_.get());

    if (!connected)
      continue;
    connections.emplace_back(connect_info);
  }
}

boost::asio::deadline_timer &jj_games_socket::timer() { return timer_; }

CURLM *jj_games_socket::curlm_async_interface() {
  return thread_data_.multi_interface;
}

void jj_games_socket::simple_timer_callback(
    boost::system::error_code const ec) {
  if (!ec) {
    CURLMcode return_code =
        curl_multi_socket_action(curlm_async_interface(), CURL_SOCKET_TIMEOUT,
                                 0, &thread_data_.still_running);
    if (return_code != CURLM_OK)
      return;
    check_multi_info();
  }
}

void jj_games_socket::add_tcp_socket(curl_socket_t scket, CURL *easy_handle,
                                     int what) {
  int *fd = new int{};
  fd_list.insert(fd);
  set_tcp_socket(fd, scket, easy_handle, what, 0);
  curl_multi_assign(curlm_async_interface(), scket, fd);
}

void jj_games_socket::try_different_proxy(ConnectInfo *connect_info) {
  auto easy_handle = connect_info->easy_interface;
  curl_multi_remove_handle(curlm_async_interface(), easy_handle);
  if (is_stopped()) {
    curl_easy_cleanup(connect_info->easy_interface);
    connect_info->easy_interface = nullptr;
    return;
  }
  select_proxy(connect_info);
  prepare_next_data(connect_info, connect_info->phone_number);
}

void jj_games_socket::process_result(CURLcode code,
                                     ConnectInfo *connection_info) {
  if (code != CURLE_OK) {
    current_proxy_assign_prop(wudi_server::ProxyProperty::ProxyUnresponsive,
                              connection_info->proxy);
    try_different_proxy(connection_info);
    return;
  }
  auto const &result_buffer = connection_info->body_buffer;
  auto const headers =
      custom_curl::parse_headers(connection_info->header_buffer.data());
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
  using utilities::SearchResultType;
  std::size_t opening_brace_index = response_body.find_first_of('{');
  std::size_t closing_brace_index = response_body.find_last_of('}');
  if (opening_brace_index == std::string::npos ||
      closing_brace_index == std::string::npos) {
    spdlog::error(response_body);
    emit signal_(SearchResultType::Unknown, connection_info->phone_number);
    send_next(connection_info);
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
      emit signal_(SearchResultType::NotRegistered,
                   connection_info->phone_number);
    } else {
      static std::string const already_registered{
          "%E8%AF%A5%E6%89%8B%E6%9C%BA%E5%8F%B7%E5%B7%B2%E6%B3%A8%E5%86%8C%EF%"
          "BC%8C%E8%AF%B7%E6%9B%B4%E6%8D%A2"};
      std::string const server_message = object["MSG"].get<json::string_t>();
      if (server_message.find(already_registered) != std::string::npos) {
        emit signal_(SearchResultType::Registered,
                     connection_info->phone_number);
      } else {
        spdlog::warn(response_body);
        emit signal_(SearchResultType::Unknown, connection_info->phone_number);
      }
    }
  } catch (std::exception const &) {
  }
  send_next(connection_info);
}

void jj_games_socket::select_proxy(ConnectInfo *connect_info) {
  if (is_stopped() && connect_info->easy_interface) {
    curl_multi_remove_handle(curlm_async_interface(),
                             connect_info->easy_interface);
    curl_easy_cleanup(connect_info->easy_interface);
    connect_info->easy_interface = nullptr;
    return;
  }

  if (auto proxy = proxy_provider_.next_endpoint(); proxy.has_value()) {
    connect_info->proxy = proxy.value();
  } else {
    connect_info->proxy = nullptr;
    emit signal_(utilities::SearchResultType::Unknown,
                 connect_info->phone_number);
  }
}

bool jj_games_socket::is_stopped() const { return false; }

void jj_games_socket::current_proxy_assign_prop(ProxyProperty property,
                                                endpoint_ptr ep) {
  if (ep)
    ep->property = property;
}

void jj_games_socket::send_next(void *connection_info) {
  ConnectInfo *connect_info_ptr = static_cast<ConnectInfo *>(connection_info);
  connect_info_ptr->headers = {};
  connect_info_ptr->body_buffer = {};
  connect_info_ptr->error_buffer = {};
  connect_info_ptr->header_buffer = {};
  if (is_stopped()) {
    if (connect_info_ptr->easy_interface) {
      curl_multi_remove_handle(curlm_async_interface(),
                               connect_info_ptr->easy_interface);
      curl_easy_cleanup(connect_info_ptr->easy_interface);
      connect_info_ptr->easy_interface = nullptr;
      return;
    }
  }

  auto easy_handle = connect_info_ptr->easy_interface;
  curl_multi_remove_handle(curlm_async_interface(), easy_handle);
  select_proxy(connect_info_ptr);
  try {
    prepare_next_data(connect_info_ptr, numbers_.get());
  } catch (utilities::empty_container_exception const &) {
  }
}

void jj_games_socket::prepare_next_data(ConnectInfo *connect_info_ptr,
                                        std::string const &number) {
  if (is_stopped()) {
    if (connect_info_ptr->easy_interface) {
      curl_multi_remove_handle(curlm_async_interface(),
                               connect_info_ptr->easy_interface);
      curl_easy_cleanup(connect_info_ptr->easy_interface);
      connect_info_ptr->easy_interface = nullptr;
      return;
    }
  }
  auto easy_handle = connect_info_ptr->easy_interface;
  curl_easy_reset(easy_handle);
  connect_info_ptr->header_buffer.clear();
  connect_info_ptr->body_buffer.clear();
  connect_info_ptr->headers = {};
  connect_info_ptr->error_buffer = {};
  if (!custom_curl::create_jj_games_interface(
          connect_info_ptr, thread_data_, connect_info_ptr->proxy, number)) {
    return;
  }
  curl_multi_add_handle(curlm_async_interface(), easy_handle);
}

void jj_games_socket::on_socket_event_occurred(
    boost::system::error_code const ec, curl_socket_t const scket, int action,
    int *fd) {
  if (!socket_map.contains(scket))
    return;
  if (*fd == action || *fd == CURL_POLL_INOUT) {
    if (ec) {
      action = CURL_CSELECT_ERR;
    }
    CURLMcode rc = curl_multi_socket_action(
        curlm_async_interface(), scket, action, &thread_data_.still_running);
    if (rc != CURLM_OK) {
      return;
    }
    check_multi_info();
    if (thread_data_.still_running <= 0) {
      timer_.cancel();
    }
    if (auto tcp_socket = socket_map.value(scket);
        tcp_socket.has_value() && !ec &&
        (*fd == action || *fd == CURL_POLL_INOUT)) {
      if (action == CURL_POLL_IN) {
        (*tcp_socket)
            ->async_wait(boost::asio::socket_base::wait_read,
                         std::bind(&jj_games_socket::on_socket_event_occurred,
                                   this, std::placeholders::_1, scket, action,
                                   fd));
      }
      if (action == CURL_POLL_OUT) {
        (*tcp_socket)
            ->async_wait(boost::asio::socket_base::wait_write,
                         std::bind(&jj_games_socket::on_socket_event_occurred,
                                   this, std::placeholders::_1, scket, action,
                                   fd));
      }
    }
    if (thread_data_.still_running <= 0 && !numbers_.empty()) {
      initialize_async_sockets();
      simple_timer_callback({});
    }
  }
}

jj_games_socket::~jj_games_socket() {
  for (auto &connection : connections) {
    if (connection)
      delete connection;
  }
  for (auto &fd : fd_list) {
    delete fd;
  }
  fd_list.clear();
  connections.clear();
  socket_map.clear();
}

void jj_games_socket::check_multi_info() {
  CURLMsg *msg{};
  int msgs_left{};
  ConnectInfo *connection_info{nullptr};
  if (is_stopped())
    return;
  while ((msg = curl_multi_info_read(curlm_async_interface(), &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURL *easy_handle = msg->easy_handle;
      CURLcode code = msg->data.result;
      curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &connection_info);
      process_result(code, connection_info);
    }
  }
}

void jj_games_socket::set_tcp_socket(int *fd, curl_socket_t scket, CURL *,
                                     int const new_action,
                                     int const old_action) {
  auto optional_socket = socket_map.value(scket);
  if (!optional_socket || is_stopped())
    return;
  auto &tcp_socket = optional_socket.value();
  *fd = new_action;
  if (new_action == CURL_POLL_IN) {
    if (old_action != CURL_POLL_IN && old_action != CURL_POLL_INOUT) {
      tcp_socket->async_wait(boost::asio::socket_base::wait_read,
                             [=](boost::system::error_code ec) {
                               on_socket_event_occurred(ec, scket, new_action,
                                                        fd);
                             });
    }
  } else if (new_action == CURL_POLL_OUT) {
    if (old_action != CURL_POLL_OUT && old_action != CURL_POLL_INOUT) {
      tcp_socket->async_wait(boost::asio::socket_base::wait_write,
                             [=](boost::system::error_code ec) {
                               on_socket_event_occurred(ec, scket, new_action,
                                                        fd);
                             });
    }
  } else if (new_action == CURL_POLL_INOUT) {
    if (old_action != CURL_POLL_IN && old_action != CURL_POLL_INOUT) {
      tcp_socket->async_wait(boost::asio::socket_base::wait_read,
                             [=](boost::system::error_code ec) {
                               on_socket_event_occurred(ec, scket, new_action,
                                                        fd);
                             });
    }
    if (old_action != CURL_POLL_OUT && old_action != CURL_POLL_INOUT) {
      tcp_socket->async_wait(boost::asio::socket_base::wait_write,
                             [=](boost::system::error_code ec) {
                               on_socket_event_occurred(ec, scket, new_action,
                                                        fd);
                             });
    }
  }
}

} // namespace wudi_server

namespace custom_curl {
std::map<boost::string_view, boost::string_view>
parse_headers(std::string_view const &str) {
  std::map<boost::string_view, boost::string_view> header_map{};
  std::vector<boost::string_view> headers =
      wudi_server::utilities::split_string_view(
          boost::string_view(str.data(), str.size()), "\r\n");
  // first row is always a: { HTTP_VERB PATH HTTP/1.1 } combination, called
  // `request line`
  if (headers.size() < 2)
    return {};

  for (auto iter = headers.begin() + 1; iter != headers.end(); ++iter) {
    auto header_key_value = wudi_server::utilities::split_string_view(
        *iter, ": "); // make copies of header info
    if (header_key_value.size() != 2)
      continue;
    header_map[header_key_value[0]] = header_key_value[1];
  }
  return header_map;
}

std::string
get_proxy_string(std::optional<wudi_server::endpoint_ptr> const &ep) {
  if (!ep.value())
    return {};
  auto proxy_address = (*ep)->endpoint.address().to_string();
  auto proxy_port = std::to_string((*ep)->endpoint.port());
  return proxy_address + ":" + proxy_port;
}

std::string get_current_time() {
  std::time_t current_time{std::time(nullptr)};
  return std::to_string(current_time);
}

bool create_jj_games_interface(ConnectInfo *connect_info,
                               CurlThreadData &glob_data,
                               wudi_server::endpoint_ptr proxy,
                               std::string const &number) {
  connect_info->url =
      "https://a4.srv.jj.cn/reg/check_loginname.php?regtype=2&t=" +
      get_current_time() + "&n=1&loginname=";
  connect_info->proxy = proxy;
  connect_info->phone_number = number;
  connect_info->headers.append("sec-fetch-site: same-origin");
  connect_info->headers.append("sec-fetch-mode: cors");
  connect_info->headers.append("Accept-Language: en-US,en;q=0.5 --compressed");
  connect_info->headers.append(
      "Referer: https://www.jj.cn/reg/reg.html?type=phone");
  connect_info->headers.append("Accept-Encoding: gzip, deflate, br");
  auto curl_handle = connect_info->easy_interface;

  std::string const proxy_address =
      "http://" + get_proxy_string(connect_info->proxy);
  std::string const user_agent = wudi_server::utilities::get_random_agent();
  curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl_handle, CURLOPT_URL, connect_info->url.c_str());
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
  curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, connect_info);
  curl_easy_setopt(curl_handle, CURLOPT_OPENSOCKETFUNCTION, open_tcp_socket);
  curl_easy_setopt(curl_handle, CURLOPT_OPENSOCKETDATA, connect_info);
  curl_easy_setopt(curl_handle, CURLOPT_CLOSESOCKETFUNCTION, close_tcp_socket);
  curl_easy_setopt(curl_handle, CURLOPT_CLOSESOCKETDATA, connect_info);
  curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS,
                   wudi_server::utilities::TimeoutMilliseconds * 2);
  curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

  auto ec = curl_multi_add_handle(glob_data.multi_interface, curl_handle);
  return ec == CURLM_OK;
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

curl_socket_t open_tcp_socket(void *userp, curlsocktype purpose,
                              curl_sockaddr *address) {
  ConnectInfo *connect_info = reinterpret_cast<ConnectInfo *>(userp);
  if (purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET) {
    auto tcp_socket = std::make_shared<tcp::socket>(connect_info->context);
    boost::system::error_code ec{};
    tcp_socket->open(tcp::v4(), ec);
    if (ec)
      return CURL_SOCKET_BAD;
    curl_socket_t native_socket = tcp_socket->native_handle();
    connect_info->map_.insert(native_socket, tcp_socket);
    return native_socket;
  }
  return CURL_SOCKET_BAD;
}

int close_tcp_socket(void *userp, curl_socket_t item) {
  ConnectInfo *connect_info = reinterpret_cast<ConnectInfo *>(userp);
  return connect_info->map_.remove(item) ? 0 : -1;
}

int multi_socket_callback(CURL *easy_handle, curl_socket_t socket, int what,
                          void *userp, void *socketp) {
  using wudi_server::jj_games_socket;

  jj_games_socket *object_ptr = reinterpret_cast<jj_games_socket *>(userp);
  if (object_ptr->is_stopped())
    return -1;

  int *action_ptr = static_cast<int *>(socketp);
  if (what == CURL_POLL_REMOVE) {
    object_ptr->remove_tcp_socket(action_ptr);
  } else {
    if (!action_ptr) {
      object_ptr->add_tcp_socket(socket, easy_handle, what);
    } else {
      object_ptr->set_tcp_socket(action_ptr, socket, easy_handle, what,
                                 *action_ptr);
    }
  }
  return 0;
}

int multi_socket_timer_callback(CURLM *, long timeout_ms, void *user_data) {
  using wudi_server::jj_games_socket;
  jj_games_socket *object_ptr = reinterpret_cast<jj_games_socket *>(user_data);
  auto &timer = object_ptr->timer();
  timer.cancel();

  if (object_ptr->is_stopped())
    return -1;
  if (timeout_ms > 0) {
    timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
    timer.async_wait(std::bind(&jj_games_socket::simple_timer_callback,
                               object_ptr, std::placeholders::_1));
  } else if (timeout_ms == 0) {
    object_ptr->simple_timer_callback({});
  }
  return 0;
}
} // namespace custom_curl
