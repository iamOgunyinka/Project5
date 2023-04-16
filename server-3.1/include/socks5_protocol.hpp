#pragma once

enum { SOCKS_VERSION_5 = 5 };

enum {
  SOCKS5_AUTH_NONE = 0x00,
  SOCKS5_AUTH = 0x02,
  SOCKS5_AUTH_UNACCEPTABLE = 0xFF
};

enum { SOCKS_CMD_CONNECT = 0x01, SOCKS_CMD_BIND = 0x02 };

enum {
  SOCKS5_ATYP_IPV4 = 0x01,
  SOCKS5_ATYP_DOMAINNAME = 0x03,
  SOCKS5_ATYP_IPV6 = 0x04
};

enum {
  SOCKS5_SUCCEEDED = 0x00,
  SOCKS5_GENERAL_SOCKS_SERVER_FAILURE,
  SOCKS5_CONNECTION_NOT_ALLOWED_BY_RULESET,
  SOCKS5_NETWORK_UNREACHABLE,
  SOCKS5_CONNECTION_REFUSED,
  SOCKS5_TTL_EXPIRED,
  SOCKS5_COMMAND_NOT_SUPPORTED,
  SOCKS5_ADDRESS_TYPE_NOT_SUPPORTED,
  SOCKS5_UNASSIGNED
};