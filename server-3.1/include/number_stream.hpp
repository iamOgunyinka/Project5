#pragma once

#include <fstream>
#include <mutex>
#include <vector>

namespace woody_server {

struct empty_container_exception_t : public std::runtime_error {
  empty_container_exception_t() : std::runtime_error("") {}
};

class number_stream_t {
public:
  explicit number_stream_t(std::ifstream &file_stream);
  std::string get() noexcept(false);
  bool empty();
  bool isOpen();
  void close();
  decltype(std::declval<std::ifstream>().rdbuf()) dumpS();
  std::vector<std::string> &dump();
  void append(std::string const &);

private:
  std::ifstream &m_inputStream;
  std::vector<std::string> m_temporaries;
  std::mutex m_mutex;
  bool m_closed = false;
};
} // namespace woody_server
