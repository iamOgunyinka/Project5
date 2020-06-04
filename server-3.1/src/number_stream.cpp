#include "number_stream.hpp"
#include <string>

namespace wudi_server {
number_stream_t::number_stream_t(std::ifstream &file_stream)
    : input_stream{file_stream}, mutex_{} {}

std::string number_stream_t::get() noexcept(false) {
  if (closed_)
    throw empty_container_exception_t{};
  std::string number{};
  std::lock_guard<std::mutex> lock_g{mutex_};
  if (!temporaries_.empty()) {
    std::string temp = temporaries_.front();
    temporaries_.erase(temporaries_.begin());
    return temp;
  }
  while (std::getline(input_stream, number)) {
    if (number.empty())
      continue;
    return number;
  }
  throw empty_container_exception_t{};
}

void number_stream_t::close() {
  input_stream.close();
  closed_ = true;
}

bool number_stream_t::is_open() { return input_stream.is_open(); }

bool number_stream_t::empty() {
  return closed_ || !input_stream || input_stream.eof();
}

decltype(std::declval<std::ifstream>().rdbuf()) number_stream_t::dump_s() {
  return input_stream.rdbuf();
}

std::vector<std::string> &number_stream_t::dump() { return temporaries_; }

void number_stream_t::push_back(std::string const &str) {
  temporaries_.push_back(str);
}

} // namespace wudi_server
