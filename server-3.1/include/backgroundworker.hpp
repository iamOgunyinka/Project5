#pragma once

#include "safe_proxy.hpp"
#include "task_data.hpp"
#include "upload_data.hpp"

#include <fstream>
#include <optional>

namespace boost::asio::ssl {
class context;
} // namespace boost::asio::ssl

namespace woody_server {

class number_stream_t;
class socket_interface_t;
enum class website_type_e;
enum class search_result_type_e;
struct database_connector_t;

namespace net = boost::asio;

class background_worker_t {
public:
  background_worker_t(website_result_t &&, std::vector<upload_result_t> &&,
                      std::shared_ptr<internal_task_result_t> task_result,
                      net::ssl::context &);
  background_worker_t(atomic_task_t old_task,
                      std::shared_ptr<internal_task_result_t> task_result,
                      net::ssl::context &);
  ~background_worker_t();

  void proxyCallbackSignal(new_proxy_signal_t *);
  void proxyInfoMap(proxy_info_map_t *);
  task_status_e run();
  auto &numberStream() { return m_numberStream; }
  auto taskResult() { return m_taskResultPtr; }
  std::string filename() const { return m_inputFilename; }

private:
  void onDataResultObtained(search_result_type_e, std::string_view);
  bool openOutputFiles();
  task_status_e setWebsiteType();
  task_status_e setupProxyProvider();
  task_status_e startOperations();
  task_status_e runNewTask();
  task_status_e runNumberCrawler();
  task_status_e continueOldTask();

private:
  net::ssl::context &m_sslContext;
  std::unique_ptr<proxy_base_t> m_proxyProvider = nullptr;
  supported_websites_e m_websiteType;
  std::shared_ptr<number_stream_t> m_numberStream = nullptr;
  website_result_t m_websiteInfo;
  std::vector<upload_result_t> m_uploadsInfo;
  std::shared_ptr<internal_task_result_t> m_taskResultPtr = nullptr;
  std::shared_ptr<database_connector_t> m_dbConnector = nullptr;
  std::string m_inputFilename{};
  std::ifstream m_inputFile;
  std::optional<atomic_task_t> m_atomicTask = std::nullopt;
  std::optional<net::io_context> m_ioContext = std::nullopt;
  std::vector<std::unique_ptr<socket_interface_t>> m_sockets;
  new_proxy_signal_t *m_newProxySignal = nullptr;
  proxy_info_map_t *m_proxyInfoMap = nullptr;
  proxy_base_params_t *m_proxyParameters{nullptr};
  boost::signals2::connection m_signalConnector;
  std::optional<proxy_configuration_t> m_proxyConfig = std::nullopt;
};

supported_websites_e getSupportedSite(std::string const &webAddress);

} // namespace woody_server
