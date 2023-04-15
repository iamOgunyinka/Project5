#include "backgroundworker.hpp"
#include "database_connector.hpp"
#include "enumerations.hpp"
#include "file_utils.hpp"
#include "number_stream.hpp"
#include "random.hpp"
#include "sockets_instantiator.hpp"
#include "sockets_interface.hpp"
#include <filesystem>
#include <memory>
#include <random>
#include <spdlog/spdlog.h>
#include <utility>

namespace woody_server {

background_worker_t::background_worker_t(
    website_result_t &&website, std::vector<upload_result_t> &&uploads,
    std::shared_ptr<internal_task_result_t> taskResult,
    net::ssl::context &sslContext)
    : m_sslContext{sslContext}, m_websiteInfo{std::move(website)},
      m_uploadsInfo{std::move(uploads)}, m_taskResultPtr{std::move(taskResult)},
      m_websiteType{supported_websites_e::Unknown} {}

background_worker_t::background_worker_t(
    atomic_task_t oldTask, std::shared_ptr<internal_task_result_t> taskResult,
    net::ssl::context &sslContext)
    : m_sslContext(sslContext), m_websiteInfo(), m_uploadsInfo(),
      m_taskResultPtr(std::move(taskResult)),
      m_websiteType{supported_websites_e::Unknown},
      m_atomicTask(std::move(oldTask)) {}

background_worker_t::~background_worker_t() {
  m_signalConnector.disconnect();
  m_sockets.clear();
  m_proxyProvider.reset();

  delete m_proxyParameters;
  m_proxyParameters = nullptr;
  m_ioContext.reset();
  spdlog::info("Closing all files");
}

task_status_e background_worker_t::setWebsiteType() {
  if (m_websiteType == supported_websites_e::Unknown) {
    m_websiteType = getSupportedSite(m_websiteInfo.address);
    if (m_websiteType == supported_websites_e::Unknown) {
      spdlog::error("Type not found");
      return task_status_e::Erred;
    }
  }
  return task_status_e::Ongoing;
}

task_status_e background_worker_t::setupProxyProvider() {
  // we delayed construction of safe_proxy/io_context until now
  m_proxyConfig = readProxyConfiguration();
  if (!m_proxyConfig) {
    return task_status_e::Erred;
  }

  auto const thread_id = std::this_thread::get_id();
  auto const web_id = m_taskResultPtr->website_id;
  m_ioContext.emplace();
  m_proxyParameters = new proxy_base_params_t{*m_ioContext,   *m_newProxySignal,
                                              *m_proxyConfig, *m_proxyInfoMap,
                                              thread_id,      web_id};

  switch (m_proxyConfig->proxyProtocol) {
  case proxy_type_e::http_https_proxy:
    m_proxyProvider = std::make_unique<http_proxy_t>(*m_proxyParameters);
    break;
  case proxy_type_e::socks5:
    m_proxyProvider = std::make_unique<socks5_proxy_t>(*m_proxyParameters);
    break;
  default:
    return task_status_e::Erred;
  }

  // when this signal is emitted, all workers subscribe to it so they
  // can have copies of the new proxies obtained
  if (m_proxyConfig->shareProxy) {
    m_signalConnector = m_newProxySignal->connect([this](auto &&...args) {
      m_proxyProvider->addMore(std::forward<decltype(args)>(args)...);
    });
  }
  m_proxyProvider->totalUsed((int)m_taskResultPtr->ip_used);
  m_proxyConfig->maxSocket = std::max<int>(1, m_proxyConfig->maxSocket);

  return task_status_e::Ongoing;
}

task_status_e background_worker_t::startOperations() {
  bool &is_stopped = m_taskResultPtr->stopped();
  auto const proxy_type = m_proxyProvider->type();
  auto const per_ip = m_taskResultPtr->scans_per_ip;
  is_stopped = false;

  m_sockets.reserve(static_cast<std::size_t>(m_proxyConfig->maxSocket));
  for (int i = 0; i != m_proxyConfig->maxSocket; ++i) {
    auto c_socket = socket_instantiator_t::get_socket(
        m_websiteType, m_sslContext, proxy_type, is_stopped, *m_ioContext,
        *m_proxyProvider, *m_numberStream, per_ip);
    if (!c_socket) {
      return task_status_e::AutoStopped;
    }
    m_sockets.push_back(std::move(c_socket));
  }
  auto callback = [=](search_result_type_e type, std::string_view number) {
    onDataResultObtained(type, number);
  };
  for (auto &socket : m_sockets) {
    socket->signal().connect(callback);
    socket->startConnect();
  }

  m_ioContext->run();

  if (m_taskResultPtr->operation_status == task_status_e::Ongoing) {
    if (m_numberStream->empty()) {
      m_taskResultPtr->operation_status = task_status_e::Completed;
    } else {
      // this will hardly ever happen, if it does, put it in a stop state
      m_taskResultPtr->stop();
      m_taskResultPtr->operation_status = task_status_e::Stopped;
    }
  }
  m_taskResultPtr->ip_used =
      static_cast<uint32_t>(m_proxyProvider->totalUsed());
  return m_taskResultPtr->operation_status;
}

task_status_e background_worker_t::runNumberCrawler() {
  if (!openOutputFiles()) {
    spdlog::error("OpenOutputFiles failed");
    if (std::filesystem::exists(m_inputFilename)) {
      if (m_inputFile.is_open())
        m_inputFile.close();
      std::filesystem::remove(m_inputFilename);
    }
    spdlog::error("unable to open output files");
    return task_status_e::Erred;
  }

  m_taskResultPtr->operation_status = task_status_e::Ongoing;
  if (auto status = setWebsiteType(); status == task_status_e::Erred) {
    return status;
  }

  m_dbConnector = database_connector_t::s_get_db_connector();
  if (!m_dbConnector->set_input_files(
          m_inputFilename, m_taskResultPtr->ok_filename.string(),
          m_taskResultPtr->ok2_filename.string(),
          m_taskResultPtr->not_ok_filename.string(),
          m_taskResultPtr->unknown_filename.string(),
          m_taskResultPtr->task_id)) {
    spdlog::error("Could not set input files");
    return task_status_e::Erred;
  }

  if (auto status = setupProxyProvider(); status == task_status_e::Erred) {
    return status;
  }
  return startOperations();
}

void background_worker_t::onDataResultObtained(search_result_type_e type,
                                               std::string_view number) {
  auto &processed = m_taskResultPtr->processed;
  ++processed;
  switch (type) {
  case search_result_type_e::NotRegistered:
    ++m_taskResultPtr->ok_count;
    m_taskResultPtr->ok_file << number << "\n";
    m_taskResultPtr->ok_file.flush();
    break;
  case search_result_type_e::Registered:
    ++m_taskResultPtr->not_ok_count;
    m_taskResultPtr->not_ok_file << number << "\n";
    m_taskResultPtr->not_ok_file.flush();
    break;
  case search_result_type_e::Unknown:
    ++m_taskResultPtr->unknown_count;
    m_taskResultPtr->unknown_file << number << "\n";
    m_taskResultPtr->unknown_file.flush();
    break;
  case search_result_type_e::RequestStop:
    --processed; // `processed` will *always* be greater than 0
    m_taskResultPtr->stop();
    m_taskResultPtr->operation_status = task_status_e::AutoStopped;
    return;
  }
  uint32_t const mod =
      (m_proxyConfig->maxSocket < 25) ? 25 : m_proxyConfig->maxSocket;
  bool const signallable = (processed % mod) == 0;
  if (signallable) {
    m_dbConnector->update_task_progress(*m_taskResultPtr,
                                        m_proxyProvider->totalUsed());
  }
  if (processed > 10 && ((processed - 10) > m_taskResultPtr->total_numbers)) {
    // if we get here, there's a problem
    m_taskResultPtr->stop();
    m_taskResultPtr->operation_status = task_status_e::AutoStopped;
  }
}

bool background_worker_t::openOutputFiles() {
  using utilities::createFileDirectory;

  std::filesystem::path parent_directory{std::filesystem::current_path()};
  auto const abs_path{std::filesystem::absolute(parent_directory) / "over" /
                      m_websiteInfo.alias};
  std::string current_date{}, current_time{};
  auto const current_time_t = std::time(nullptr);
  if (std::size_t const count =
          utilities::unixTimeToString(current_date, current_time_t, "%Y_%m_%d");
      count != std::string::npos) {
    current_date.resize(count);
  } else {
    // this is called if and only if we could not do the proper formatting
    current_date = std::to_string(current_time_t);
  }
  if (std::size_t const count =
          utilities::unixTimeToString(current_time, current_time_t, "%H_%M_%S");
      count != std::string::npos) {
    current_time.resize(count);
  } else {
    current_time = std::to_string(current_time_t);
  }

  if (m_taskResultPtr->not_ok_filename.string().empty()) {
    auto const suffix{std::filesystem::path{current_date} /
                      (current_time + ".txt")};
    m_taskResultPtr->not_ok_filename = abs_path / "not_ok" / suffix;
    m_taskResultPtr->ok_filename = abs_path / "ok" / suffix;
    m_taskResultPtr->ok2_filename = abs_path / "ok2" / suffix;
    m_taskResultPtr->unknown_filename = abs_path / "unknown" / suffix;
  }

  if (!(createFileDirectory(m_taskResultPtr->not_ok_filename) &&
        createFileDirectory(m_taskResultPtr->ok_filename) &&
        createFileDirectory(m_taskResultPtr->ok2_filename) &&
        createFileDirectory(m_taskResultPtr->unknown_filename))) {
    // create some error messages and fire out
    return false;
  }
  m_taskResultPtr->not_ok_file.open(m_taskResultPtr->not_ok_filename,
                                    std::ios::out | std::ios::app);
  m_taskResultPtr->ok_file.open(m_taskResultPtr->ok_filename,
                                std::ios::out | std::ios::app);
  m_taskResultPtr->ok2_file.open(m_taskResultPtr->ok2_filename,
                                 std::ios::out | std::ios::app);
  m_taskResultPtr->unknown_file.open(m_taskResultPtr->unknown_filename,
                                     std::ios::out | std::ios::app);

  return m_taskResultPtr->not_ok_file.is_open() &&
         m_taskResultPtr->ok_file.is_open() &&
         m_taskResultPtr->ok2_file.is_open() &&
         m_taskResultPtr->unknown_file.is_open();
}

task_status_e background_worker_t::continueOldTask() {
  auto &task = m_atomicTask.value();
  m_websiteType = getSupportedSite(task.website_address);
  if (m_websiteType == supported_websites_e::Unknown) {
    spdlog::error("Type not found");
    return task_status_e::Erred;
  }

  m_inputFilename = task.input_filename;
  if (m_taskResultPtr->total_numbers == 0) {
    if (m_atomicTask->total == 0) {
      using utilities::getFileContent;
      using utilities::isValidMobileNumber;

      m_taskResultPtr->total_numbers = 0;
      getFileContent<std::string>(m_inputFilename, isValidMobileNumber,
                                  [this](std::string_view) mutable {
                                    ++m_taskResultPtr->total_numbers;
                                  });
    } else {
      m_taskResultPtr->total_numbers = m_atomicTask->total;
    }
  }
  if (m_taskResultPtr->total_numbers == 0) {
    spdlog::error("Total numbers = 0");
    return task_status_e::Erred;
  }
  m_inputFile.open(m_inputFilename, std::ios::in);
  if (!m_inputFile) {
    if (std::filesystem::exists(m_inputFilename)) {
      std::filesystem::remove(m_inputFilename);
    }
    m_taskResultPtr->operation_status = task_status_e::Erred;
    spdlog::error("Could not open input file: {}", m_inputFilename);
    return task_status_e::Erred;
  }
  m_numberStream = std::make_unique<number_stream_t>(m_inputFile);
  return runNumberCrawler();
}

task_status_e background_worker_t::runNewTask() {
  m_inputFilename =
      "." + utilities::getRandomString(utilities::getRandomInteger()) + ".txt";
  std::ofstream out_file{m_inputFilename};
  if (!out_file) {
    spdlog::error("Could not open out_file");
    m_inputFilename.clear();
    m_taskResultPtr->operation_status = task_status_e::Erred;
    return m_taskResultPtr->operation_status;
  }
  for (auto &index : m_uploadsInfo) {
    spdlog::info("name on disk: {}", index.name_on_disk);
    std::ifstream in_file{index.name_on_disk};
    if (!in_file)
      continue;
    out_file << in_file.rdbuf();
  }
  out_file.close();
  {
    using utilities::getFileContent;
    using utilities::isValidMobileNumber;

    m_taskResultPtr->total_numbers = 0;
    getFileContent<std::string>(
        m_inputFilename, isValidMobileNumber,
        [this](std::string_view) mutable { ++m_taskResultPtr->total_numbers; });
    if (m_taskResultPtr->total_numbers == 0)
      return task_status_e::Erred;
  }
  m_inputFile.open(m_inputFilename, std::ios::in);
  if (!m_inputFile) {
    if (std::filesystem::exists(m_inputFilename)) {
      std::filesystem::remove(m_inputFilename);
    }
    spdlog::error("Could not open inputFile");
    return task_status_e::Erred;
  }
  m_numberStream = std::make_unique<number_stream_t>(m_inputFile);
  return runNumberCrawler();
}

task_status_e background_worker_t::run() {
  if (m_websiteInfo.id != 0)
    return runNewTask();
  return continueOldTask();
}

void background_worker_t::proxyCallbackSignal(new_proxy_signal_t *signal) {
  m_newProxySignal = signal;
}

void background_worker_t::proxyInfoMap(proxy_info_map_t *proxyMap) {
  m_proxyInfoMap = proxyMap;
}

supported_websites_e getSupportedSite(std::string const &web_address) {
  if (web_address.find("lazada") != std::string::npos) {
    return supported_websites_e::LacazaPhillipines;
  }

  return supported_websites_e::Unknown;
}

time_data_t getTimeData() {
  static std::random_device rd{};
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<> dis(0.0, 1.0);
  uint64_t const current_time = std::time(nullptr) * 1'000;
  auto const random_number =
      static_cast<std::size_t>(std::round(1e3 * dis(gen)));
  auto const callback_number =
      static_cast<std::size_t>(current_time + random_number);
  return time_data_t{current_time, callback_number};
}

} // namespace woody_server
