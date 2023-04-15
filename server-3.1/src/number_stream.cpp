#include "number_stream.hpp"
#include <string>

namespace woody_server {
number_stream_t::number_stream_t(std::ifstream &file_stream)
    : m_inputStream{file_stream}, m_mutex{} {}

std::string number_stream_t::get() noexcept(false) {
  if (m_closed)
    throw empty_container_exception_t{};
  std::string number{};
  std::lock_guard<std::mutex> lock_g{m_mutex};
  if (!m_temporaries.empty()) {
    std::string temp = m_temporaries.front();
    m_temporaries.erase(m_temporaries.begin());
    return temp;
  }
  while (std::getline(m_inputStream, number)) {
    if (number.empty())
      continue;
    return number;
  }
  throw empty_container_exception_t{};
}

void number_stream_t::close() {
  m_inputStream.close();
  m_closed = true;
}

bool number_stream_t::isOpen() { return m_inputStream.is_open(); }

bool number_stream_t::empty() {
  return m_closed || !m_inputStream || m_inputStream.eof();
}

decltype(std::declval<std::ifstream>().rdbuf()) number_stream_t::dumpS() {
  return m_inputStream.rdbuf();
}

std::vector<std::string> &number_stream_t::dump() { return m_temporaries; }

void number_stream_t::append(std::string const &str) {
  m_temporaries.push_back(str);
}

} // namespace woody_server
