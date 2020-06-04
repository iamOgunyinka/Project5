#pragma once
#include <boost/signals2.hpp>
#include <string_view>

namespace wudi_server {

enum class search_result_type_e {
  Registered = 0xA,
  NotRegistered = 0xB,
  Unknown = 0XC,
  RequestStop = 0xD,
  Registered2 = 0xE // only for PPSports
};

enum socket_constants_e {
  max_retries = 2,
  SleepTimeoutSec = 5,
  LenUserAgents = 14,
  timeout_millisecs = 3'000
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

class sockets_interface {
protected:
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;

public:
  sockets_interface() : signal_{} {}
  virtual ~sockets_interface() {}
  virtual void start_connect() = 0;
  auto &signal() { return signal_; }
};
} // namespace wudi_server
