#pragma once
#include <fstream>
#include <mutex>
#include <vector>

namespace wudi_server {

struct empty_container_exception_t : public std::runtime_error {
  empty_container_exception_t() : std::runtime_error("") {}
};

class number_stream_t {
public:
  number_stream_t(std::ifstream &file_stream);
  std::string get() noexcept(false);
  bool empty();
  bool is_open();
  void close();
  decltype(std::declval<std::ifstream>().rdbuf()) dump_s();
  std::vector<std::string> &dump();
  void push_back(std::string const &);

private:
  std::ifstream &input_stream;
  std::vector<std::string> temporaries_;
  std::mutex mutex_;
  bool closed_ = false;
};
} // namespace wudi_server
