#include "fields_alloc.hpp"
#include "json.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

using string_request = beast::http::request<beast::http::string_body>;
using string_response = beast::http::response<beast::http::string_body>;
using nlohmann::json;

class server : public std::enable_shared_from_this<server> {
  net::io_context &io_context_;
  tcp::acceptor tcp_acceptor_;
  tcp::endpoint tcp_endpoint_;
  bool is_open_ = false;

public:
  server(net::io_context &io_, std::string const &ip_address,
         uint16_t const port)
      : io_context_{io_}, tcp_acceptor_{net::make_strand(io_)},
        tcp_endpoint_{net::ip::make_address(ip_address), port} {
    beast::error_code ec{};
    tcp_acceptor_.open(tcp_endpoint_.protocol(), ec);
    if (ec) {
      std::cerr << "Unable to open acceptor because: " << ec.message()
                << std::endl;
      return;
    }
    tcp_acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      std::cerr << "Unable to set option because: " << ec.message()
                << std::endl;
      return;
    }
    tcp_acceptor_.bind(tcp_endpoint_, ec);
    if (ec) {
      std::cerr << "Unable to bind address because: " << ec.message()
                << std::endl;
      return;
    }
    ec = {};
    tcp_acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      std::cerr << "Unable to listen to listen on that port because: "
                << ec.message() << std::endl;
      return;
    }
    is_open_ = true;
  }
  void run();
  void on_connection_accepted(beast::error_code, tcp::socket socket);
};

void server::run() {
  if (!is_open_)
    return;
  tcp_acceptor_.async_accept(
      net::make_strand(io_context_),
      std::bind(&server::on_connection_accepted, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

struct ad_info {
  enum ad_type { banner, full_screen };

  int type = banner;
  std::string url{};
  std::string image_filename{};
};

class session : public std::enable_shared_from_this<session> {
  static std::vector<ad_info> advertisement_list;
  using crequest_parser = http::request_parser<http::empty_body>;
  using alloc_t = wudi_server::fields_alloc<char>;

  net::io_context &context_;
  beast::tcp_stream tcp_stream_;
  beast::flat_buffer read_buffer_;
  std::optional<crequest_parser> empty_body_parser_;
  std::shared_ptr<void> resp_;
  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      file_response_;
  alloc_t alloc_{8192};
  // The file-based response serializer.
  std::optional<
      http::response_serializer<http::file_body, http::basic_fields<alloc_t>>>
      file_serializer_;
  void serve_file(char const *filename, char const *mime_type);

public:
  session(net::io_context &io, tcp::socket &&socket)
      : context_{io}, tcp_stream_{std::move(socket)} {}
  void run();
  void on_data_read(beast::error_code, std::size_t);
  void send_response(string_response &&res);
  std::string get_shangai_time();
  void on_data_written(beast::error_code ec, std::size_t);
  string_response make_response(std::string const &);
  string_response make_html(std::string const &);
  string_response make_json(json const &);
  string_response not_found();
  void get_logs_handler();
  void get_ads_handler(beast::string_view);
  void load_advertisements();
  void get_file_handler(boost::string_view);
};

std::vector<ad_info> session::advertisement_list;

void session::on_data_written(beast::error_code ec, std::size_t const) {
  if (ec == beast::http::error::end_of_stream) {
    beast::error_code ec{};
    beast::get_lowest_layer(tcp_stream_)
        .socket()
        .shutdown(net::socket_base::shutdown_both, ec);
    return;
  } else if (ec) {
    std::cerr << ec.message() << std::endl;
    return;
  }
  run();
}
void to_json(json &j, ad_info const &info) {
  j = json{{"type", info.type},
           {"url", info.url},
           {"ad", "/data" + info.image_filename}};
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

using url_query = std::map<boost::string_view, boost::string_view>;

url_query split_optional_queries(boost::string_view const &optional_query) {
  url_query result{};
  if (!optional_query.empty()) {
    auto queries = split_string_view(optional_query, "&");
    for (auto const &q : queries) {
      auto split = split_string_view(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace(split[0], split[1]);
    }
  }
  return result;
}

void session::get_ads_handler(beast::string_view target) {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(0, 9);
  std::cout << "ere\n";
  if (advertisement_list.empty())
    load_advertisements();
  std::cout << "tere\n";
  auto split = split_string_view(target, "?");
  if (split[0] == target) {
    return send_response(make_json(advertisement_list[uid(gen)]));
  }
  std::cout << "There\n";
  boost::string_view const query_string = split.size() > 1 ? split[1] : "";
  auto url_query_{split_optional_queries(query_string)};
  auto iter = url_query_.find("type");
  if (iter == url_query_.end()) {
    return send_response(make_json(advertisement_list[uid(gen)]));
  }
  int type = 0;
  try {
    type = std::stoi(iter->second.to_string());
  } catch (std::exception const &) {
    type = 0;
  }
  auto ad = advertisement_list[uid(gen)];
  while (ad.type != type) {
    ad = advertisement_list[uid(gen)];
  }
  return send_response(make_json(ad));
}

void session::load_advertisements() {
  static char const *filename = "/root/woody/Project5/wudi-worker/data.json";
  if (!advertisement_list.empty())
    return;
  if (!std::filesystem::exists(filename)) {
    throw std::runtime_error("Ad file does not exist");
  }
  auto const file_size = std::filesystem::file_size(filename);
  std::vector<char> data_buffer{};
  data_buffer.resize(file_size);
  {
    std::ifstream in_file(filename);
    if (!in_file)
      throw std::runtime_error("Cannot open ad file");
    in_file.read(data_buffer.data(), file_size);
  }
  try {
    json::array_t item_list = json::parse(data_buffer.data());
    for (auto const &item : item_list) {
      json::object_t object = item.get<json::object_t>();
      ad_info info{};
      info.image_filename = object["image"].get<json::string_t>();
      info.type = object["type"].get<json::number_integer_t>();
      info.url = object["url"].get<json::string_t>();
      advertisement_list.push_back(info);
    }
  } catch (std::exception const &e) {
    std::cerr << e.what() << "\n";
    throw;
  }
}

void session::serve_file(char const *file_path, char const *mime_type) {
  beast::error_code ec{};
  http::file_body::value_type file;
  file.open(file_path, beast::file_mode::read, ec);
  file_response_.emplace(std::piecewise_construct, std::make_tuple(),
                         std::make_tuple(alloc_));
  file_response_->result(http::status::ok);
  file_response_->keep_alive(false);
  file_response_->set(http::field::server, "wudi-server");
  file_response_->set(http::field::content_type, mime_type);
  file_response_->body() = std::move(file);
  file_response_->prepare_payload();
  file_serializer_.emplace(*file_response_);
  http::async_write(tcp_stream_, *file_serializer_,
                    [self = shared_from_this()](
                        beast::error_code ec, std::size_t const size_written) {
                      self->file_serializer_.reset();
                      self->file_response_.reset();
                      self->on_data_written(ec, size_written);
                    });
}
void session::get_logs_handler() {
  std::filesystem::path const file_path =
      "/root/woody/Project5/wudi-worker/stdout.log";
  serve_file(file_path.string().c_str(), "text/plain");
}

string_response session::make_html(std::string const &str) {
  string_response response{beast::http::status::ok, string_request{}.version()};
  response.set(beast::http::field::content_type, "text/html");
  response.keep_alive(false);
  response.body() =
      "<html><title>Hello</title><head><body>" + str + "</body></head></html>";
  response.prepare_payload();
  return response;
}

string_response session::make_json(json const &j) {
  string_response response{beast::http::status::ok, string_request{}.version()};
  response.set(beast::http::field::content_type, "application/json");
  response.keep_alive(false);
  response.body() = j.dump();
  response.prepare_payload();
  return response;
}

string_response session::not_found() {
  string_response response{beast::http::status::not_found,
                           string_request{}.version()};
  response.set(beast::http::field::content_type, "application/txt");
  response.keep_alive(false);
  response.body() = "Object not found";
  response.prepare_payload();
  return response;
}

string_response session::make_response(std::string const &str) {
  string_response response{beast::http::status::ok, string_request{}.version()};
  response.set(beast::http::field::content_type, "application/txt");
  response.keep_alive(false);
  response.body() = str;
  response.prepare_payload();
  return response;
}

void session::run() {
  read_buffer_.consume(read_buffer_.size());
  read_buffer_.clear();
  empty_body_parser_.emplace();
  beast::get_lowest_layer(tcp_stream_).expires_after(std::chrono::seconds(5));
  beast::http::async_read(tcp_stream_, read_buffer_, *empty_body_parser_,
                          std::bind(&session::on_data_read, shared_from_this(),
                                    std::placeholders::_1,
                                    std::placeholders::_2));
}

void session::send_response(string_response &&response) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  beast::http::async_write(
      tcp_stream_, *resp,
      beast::bind_front_handler(&session::on_data_written, shared_from_this()));
}

std::string session::get_shangai_time() {
  net::ip::tcp::resolver resolver{context_};
  beast::tcp_stream http_tcp_stream(net::make_strand(context_));
  beast::http::request<beast::http::empty_body> http_request_;

  try {
    auto resolves = resolver.resolve("worldtimeapi.org", "http");
    beast::tcp_stream http_tcp_stream(net::make_strand(context_));
    http_tcp_stream.connect(resolves);
    beast::http::request<http::empty_body> http_request{};
    http_request.method(http::verb::get);
    http_request.target(R"(/api/timezone/Asia/Shanghai)");
    http_request.version(11);
    http_request.set(http::field::host, "worldtimeapi.org:80");
    http_request.set(http::field::user_agent,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:74.0) "
                     "Gecko/20100101 Firefox/74.0");
    http::write(http_tcp_stream, http_request);
    beast::flat_buffer buffer{};
    http::response<http::string_body> server_response{};
    http::read(http_tcp_stream, buffer, server_response);
    beast::error_code ec{};
    if (server_response.result_int() != 200)
      return {};
    http_tcp_stream.cancel();
    auto &response_body = server_response.body();
    auto r = json::parse(response_body).get<json::object_t>();
    return r["datetime"].get<json::string_t>();
  } catch (std::exception const &e) {
    std::cerr << e.what() << "\n";
    return {};
  }
}

void session::get_file_handler(boost::string_view target) {
  char const *data_temp = "/data/";
  boost::string_view::size_type found_index = target.find(data_temp);
  auto file_name = target.substr(found_index + strlen(data_temp));
  std::filesystem::path path = "/root/woody/Project5/wudi-worker" /
                               std::filesystem::path(file_name.to_string());
  if (!std::filesystem::exists(path)) {
    return send_response(not_found());
  }
  return serve_file(path.string().c_str(), "image/png");
}

void session::on_data_read(beast::error_code const ec, std::size_t const) {
  if (ec == beast::http::error::end_of_stream) {
    beast::error_code ec{};
    beast::get_lowest_layer(tcp_stream_)
        .socket()
        .shutdown(net::socket_base::shutdown_both, ec);
    return;
  } else if (ec) {
    std::cerr << ec.message() << std::endl;
    return;
  }
  http::request<http::empty_body> r = empty_body_parser_->get();
  std::string remote_ep =
      tcp_stream_.socket().remote_endpoint().address().to_string();
  std::cout << remote_ep << " => " << r[http::field::user_agent].to_string()
            << std::endl;
  if (r.target() == "/time") {
    return send_response(make_response(get_shangai_time()));
  } else if (r.target() == "/tony") {
    return get_logs_handler();
  } else if (r.target().find("/ads") != beast::string_view::npos) {
    return get_ads_handler(r.target());
  } else if (r.target().starts_with("/data/img")) {
    return get_file_handler(r.target());
  }
  return send_response(make_response(remote_ep));
}

void server::on_connection_accepted(beast::error_code const ec,
                                    tcp::socket socket) {
  if (ec) {
    std::cerr << "On connection accepted: " << ec.message() << std::endl;
  } else {
    std::make_shared<session>(io_context_, std::move(socket))->run();
  }
  run();
}

int main(int argc, char **argv) {
  if (argc != 3)
    return -1;
  std::string const ip_address{argv[1]};
  uint16_t ip_port = std::stoi(argv[2]);

  net::io_context context{};
  std::make_shared<server>(context, ip_address, ip_port)->run();
  context.run();

  return 0;
}
