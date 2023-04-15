#pragma once

#include <array>
#include <boost/signals2/signal.hpp>
#include <cstdint>

#include "enumerations.hpp"

namespace boost::asio {
class io_context;
} // namespace boost::asio

namespace net = boost::asio;

namespace woody_server {

struct time_data_t {
  uint64_t currentTime{};
  uint64_t callbackNumber{};
};

time_data_t getTimeData();

template <typename type, typename source> type readByte(source &p) {
  type ret = 0;
  for (std::size_t i = 0; i < sizeof(type); i++)
    ret = (ret << 8) | (static_cast<unsigned char>(*p++));
  return ret;
}

template <typename type, typename target> void writeByte(type v, target &p) {
  for (auto i = (int)sizeof(type) - 1; i >= 0; i--, p++)
    *p = static_cast<unsigned char>((v >> (i * 8)) & 0xff);
}

template <size_t N>
bool statusInCodes(std::size_t const code,
                   std::array<std::size_t, N> const &codes) {
  for (auto const &stat_code : codes)
    if (code == stat_code)
      return true;
  return false;
}

class proxy_base_t;
class number_stream_t;

class socket_interface_t {
protected:
  boost::signals2::signal<void(search_result_type_e, std::string_view)>
      m_signal{};
  net::io_context &m_ioContext;
  proxy_base_t &m_proxyProvider;
  number_stream_t &m_mobileNumbers;
  bool &m_appStopped;
  int const m_scansPerIP;

public:
  socket_interface_t(net::io_context &ioContext, proxy_base_t &proxyProviders,
                     number_stream_t &mobileNumbers, bool &appStopped,
                     int const scansPerIP)
      : m_ioContext(ioContext), m_proxyProvider(proxyProviders),
        m_mobileNumbers(mobileNumbers), m_appStopped(appStopped),
        m_scansPerIP(scansPerIP) {}
  virtual ~socket_interface_t() = default;
  virtual void startConnect() = 0;
  auto &signal() { return m_signal; }
};

} // namespace woody_server
