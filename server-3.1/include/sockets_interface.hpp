#pragma once
#include <array>
#include <boost/signals2.hpp>
#include <nlohmann/json.hpp>

namespace wudi_server {
using nlohmann::json;

struct time_data_t {
  uint64_t current_time{};
  uint64_t callback_number{};
};

time_data_t get_time_data();

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

template <std::size_t N>
bool status_in_codes(std::size_t const code,
                     std::array<std::size_t, N> const &codes) {
  for (auto const &stat_code : codes)
    if (code == stat_code)
      return true;
  return false;
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

enum class website_type_e {
  Unknown,
  AutoHomeRegister,
  JJGames,
  PPSports,
  Qunar,
  Wines,
  Xpuji,
  PcAuto,
  LisboaMacau,
  ChineseMacau,
  MacauBaccarat,
  SunCity,
  GrandLisboa,
  VNS,
  Lottery81,
  Vip5,
  DevilsHorn,
  FourtyFour,
  Zed3,
  JSThree,
  SugarRaise,
  TigerFortress,
  Lebo
};

} // namespace wudi_server
