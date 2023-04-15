#pragma once

namespace woody_server {
enum class task_status_e : int {
  NotStarted,
  Ongoing,
  Stopped,
  Erred,
  Completed,
  AutoStopped
};

enum class task_type_e : int {
  stopped,
  fresh,
  completed,
};

enum class search_result_type_e {
  Registered = 0xA,
  NotRegistered = 0xB,
  Unknown = 0XC,
  RequestStop = 0xD,
};

enum class supported_websites_e {
  Unknown,
  LacazaPhillipines,
};

enum class proxy_property_e {
  ProxyActive,
  ProxyBlocked,
  ProxyMaxedOut,
  ProxyToldToWait,
  ProxyUnresponsive
};

enum class proxy_type_e : int { socks5 = 0, http_https_proxy = 1 };

enum constants_e {
  // used in socket connections
  MaxRetries = 2,
  SleepTimeoutSec = 5,
  MaxLenUserAgents = 14,
  MaxPasswordLen = 14,
  TimeoutMilliseconds = 5'000,
  MaxReadAllowed = 300,
  MaxCapacity = 5'000,
};

enum class request_type_e {
  GetRequest,
  PostRequest,
};

enum class session_error_e {
  NoError,
  ResourceNotFound,
  RequiresUpdate,
  BadRequest,
  ServerError,
  MethodNotAllowed,
  Unauthorized
};
} // namespace woody_server
