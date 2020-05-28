#pragma once
#include <array>
#include <ctime>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>

namespace wudi_server {

struct time_data_t {
  uint64_t current_time{};
  uint64_t callback_number{};
};

namespace utilities {
std::string get_random_agent();
std::string md5(std::string const &);
time_data_t get_time_data();
} // namespace utilities

using nlohmann::json;

enum constants_e {
  MaxRetries = 2,
  SleepTimeoutSec = 5,
  LenUserAgents = 14,
  TimeoutMilliseconds = 3'000
};

enum class search_result_type_e {
  Registered = 0xA,
  NotRegistered = 0xB,
  Unknown = 0XC,
  RequestStop = 0xD,
  Registered2 = 0xE // only for PPSports
};

struct empty_container_exception_t : public std::runtime_error {
  empty_container_exception_t() : std::runtime_error("") {}
};

class number_stream_t {
public:
  number_stream_t(std::ifstream &file_stream) : input_stream{file_stream} {}

  std::string get() noexcept(false) {
    if (closed_)
      throw empty_container_exception_t{};
    std::string number, temp{};
    if (!temporaries_.empty()) {
      std::string temp = temporaries_.front();
      temporaries_.erase(temporaries_.begin());
      return temp;
    }
    while (std::getline(input_stream, temp)) {
      if (temp.empty())
        continue;
      return number;
    }
    throw empty_container_exception_t{};
  }

  void close() {
    input_stream.close();
    closed_ = true;
  }

  bool is_open() { return input_stream.is_open(); }

  bool empty() { return closed_ || !input_stream || input_stream.eof(); }

  decltype(std::declval<std::ifstream>().rdbuf()) dump_s() {
    return input_stream.rdbuf();
  }

  std::vector<std::string> &dump() { return temporaries_; }

  void push_back(std::string const &str) { temporaries_.push_back(str); }

private:
  std::ifstream &input_stream;
  std::vector<std::string> temporaries_;
  bool closed_ = false;
};

template <typename type, typename source> type read_byte(source &p) {
  type ret = 0;
  for (std::size_t i = 0; i < sizeof(type); i++)
    ret = (ret << 8) | (static_cast<unsigned char>(*p++));
  return ret;
}
template <typename type, typename target> void write_byte(type v, target &p) {
  for (auto i = (int)sizeof(type) - 1; i >= 0; i--, p++)
    *p = static_cast<unsigned char>((v >> (i * 8)) & 0xff);
}
} // namespace wudi_server
