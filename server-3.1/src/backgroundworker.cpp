#include "backgroundworker.hpp"
#include "database_connector.hpp"
#include "number_stream.hpp"
#include "sockets_instantiator.hpp"
#include "sockets_interface.hpp"
#include <filesystem>
#include <random>

namespace wudi_server {
using utilities::atomic_task_t;
using utilities::internal_task_result_t;
using utilities::task_status_e;

background_worker_t::background_worker_t(
    website_result_t &&website, std::vector<upload_result_t> &&uploads,
    std::shared_ptr<internal_task_result_t> task_result,
    net::ssl::context &ssl_context)
    : ssl_context_{ssl_context}, website_info_{std::move(website)},
      uploads_info_{std::move(uploads)}, task_result_ptr_{task_result},
      website_type_{website_type_e::Unknown} {}

background_worker_t::background_worker_t(
    atomic_task_t old_task, std::shared_ptr<internal_task_result_t> task_result,
    net::ssl::context &ssl_context)
    : ssl_context_{ssl_context}, website_info_{}, uploads_info_{},
      task_result_ptr_{task_result}, website_type_{website_type_e::Unknown},
      atomic_task_{std::move(old_task)} {}

background_worker_t::~background_worker_t() {
  signal_connector_.disconnect();
  sockets_.clear();
  proxy_provider_.reset();

  delete proxy_parameters_;
  proxy_parameters_ = nullptr;
  io_context_.reset();
  spdlog::info("Closing all files");
}

task_status_e background_worker_t::set_website_type() {
  if (website_type_ == website_type_e::Unknown) {
    website_type_ = get_website_type(website_info_.address);
    if (website_type_ == website_type_e::Unknown) {
      spdlog::error("Type not found");
      return task_status_e::Erred;
    }
  }
  return task_status_e::Ongoing;
}

task_status_e background_worker_t::setup_proxy_provider() {
  // we delayed construction of safe_proxy/io_context until now
  proxy_config_ = read_proxy_configuration();
  if (!proxy_config_) {
    return task_status_e::Erred;
  }

  auto const thread_id = std::this_thread::get_id();
  auto const web_id = task_result_ptr_->website_id;
  io_context_.emplace();
  proxy_parameters_ = new proxy_base_params_t{
      *io_context_,     *new_proxy_signal_, *proxy_config_,
      *proxy_info_map_, thread_id,          web_id};

  switch (proxy_config_->proxy_protocol) {
  case proxy_type_e::http_https_proxy:
    proxy_provider_.reset(new http_proxy_t(*proxy_parameters_));
    break;
  case proxy_type_e::socks5:
    proxy_provider_.reset(new socks5_proxy_t(*proxy_parameters_));
    break;
  default:
    return task_status_e::Erred;
  }

  // when this signal is emitted, all workers subscribe to it so they
  // can have copies of the new proxies obtained
  if (proxy_config_->share_proxy) {
    signal_connector_ = new_proxy_signal_->connect([this](auto &&... args) {
      proxy_provider_->add_more(std::forward<decltype(args)>(args)...);
    });
  }
  proxy_provider_->total_used(task_result_ptr_->ip_used);
  proxy_config_->max_socket = std::max<int>(1, proxy_config_->max_socket);

  return task_status_e::Ongoing;
}

task_status_e background_worker_t::start_operations() {
  bool &is_stopped = task_result_ptr_->stopped();
  auto const proxy_type = proxy_provider_->type();
  int const per_ip = task_result_ptr_->scans_per_ip;
  is_stopped = false;

  sockets_.reserve(static_cast<std::size_t>(proxy_config_->max_socket));
  for (int i = 0; i != proxy_config_->max_socket; ++i) {
    auto c_socket = socket_instantiator::get_socket(
        website_type_, ssl_context_, proxy_type, is_stopped, *io_context_,
        *proxy_provider_, *number_stream_, per_ip);
    if (!c_socket) {
      return task_status_e::AutoStopped;
    }
    sockets_.push_back(std::move(c_socket));
  }
  auto callback = [=](search_result_type_e type, std::string_view number) {
    on_data_result_obtained(type, number);
  };
  for (auto &socket : sockets_) {
    socket->signal().connect(callback);
    socket->start_connect();
  }

  io_context_->run();

  if (task_result_ptr_->operation_status == task_status_e::Ongoing) {
    if (number_stream_->empty()) {
      task_result_ptr_->operation_status = task_status_e::Completed;
    } else {
      // this will hardly ever happen, if it does, put it in a stop state
      task_result_ptr_->stop();
      task_result_ptr_->operation_status = task_status_e::Stopped;
    }
  }
  task_result_ptr_->ip_used =
      static_cast<uint32_t>(proxy_provider_->total_used());
  return task_result_ptr_->operation_status;
}

task_status_e background_worker_t::run_number_crawler() {
  if (!open_output_files()) {
    spdlog::error("OpenOutputFiles failed");
    if (std::filesystem::exists(input_filename)) {
      if (input_file.is_open())
        input_file.close();
      std::filesystem::remove(input_filename);
    }
    spdlog::error("unable to open output files");
    return task_status_e::Erred;
  }

  task_result_ptr_->operation_status = task_status_e::Ongoing;
  if (auto status = set_website_type(); status == task_status_e::Erred) {
    return status;
  }

  db_connector = database_connector_t::s_get_db_connector();
  if (!db_connector->set_input_files(
          input_filename, task_result_ptr_->ok_filename.string(),
          task_result_ptr_->ok2_filename.string(),
          task_result_ptr_->not_ok_filename.string(),
          task_result_ptr_->unknown_filename.string(),
          task_result_ptr_->task_id)) {
    spdlog::error("Could not set input files");
    return task_status_e::Erred;
  }

  if (auto status = setup_proxy_provider(); status == task_status_e::Erred) {
    return status;
  }
  return start_operations();
}

void background_worker_t::on_data_result_obtained(search_result_type_e type,
                                                  std::string_view number) {
  auto &processed = task_result_ptr_->processed;
  ++processed;
  switch (type) {
  case search_result_type_e::NotRegistered:
    ++task_result_ptr_->ok_count;
    task_result_ptr_->ok_file << number << "\n";
    task_result_ptr_->ok_file.flush();
    break;
  case search_result_type_e::Registered2:
    ++task_result_ptr_->ok_count;
    task_result_ptr_->ok2_file << number << "\n";
    task_result_ptr_->ok2_file.flush();
    break;
  case search_result_type_e::Registered:
    ++task_result_ptr_->not_ok_count;
    task_result_ptr_->not_ok_file << number << "\n";
    task_result_ptr_->not_ok_file.flush();
    break;
  case search_result_type_e::Unknown:
    ++task_result_ptr_->unknown_count;
    task_result_ptr_->unknown_file << number << "\n";
    task_result_ptr_->unknown_file.flush();
    break;
  case search_result_type_e::RequestStop:
    --processed; // `processed` will *always* be greater than 0
    task_result_ptr_->stop();
    task_result_ptr_->operation_status = task_status_e::AutoStopped;
    return;
  }
  uint32_t const mod =
      (proxy_config_->max_socket < 25) ? 25 : proxy_config_->max_socket;
  bool const signallable = (processed % mod) == 0;
  if (signallable) {
    db_connector->update_task_progress(*task_result_ptr_,
                                       proxy_provider_->total_used());
  }
  if (processed > 10 && ((processed - 10) > task_result_ptr_->total_numbers)) {
    // if we get here, there's a problem
    task_result_ptr_->stop();
    task_result_ptr_->operation_status = task_status_e::AutoStopped;
  }
}

bool background_worker_t::open_output_files() {
  using utilities::create_file_directory;

  std::filesystem::path parent_directory{std::filesystem::current_path()};
  auto const abs_path{std::filesystem::absolute(parent_directory) / "over" /
                      website_info_.alias};
  std::string current_date{}, current_time{};
  auto const current_time_t = std::time(nullptr);
  if (std::size_t const count =
          utilities::timet_to_string(current_date, current_time_t, "%Y_%m_%d");
      count != std::string::npos) {
    current_date.resize(count);
  } else {
    // this is called if and only if we could not do the proper formatting
    current_date = std::to_string(current_time_t);
  }
  if (std::size_t const count =
          utilities::timet_to_string(current_time, current_time_t, "%H_%M_%S");
      count != std::string::npos) {
    current_time.resize(count);
  } else {
    current_time = std::to_string(current_time_t);
  }

  if (task_result_ptr_->not_ok_filename.string().empty()) {
    auto const suffix{std::filesystem::path{current_date} /
                      (current_time + ".txt")};
    task_result_ptr_->not_ok_filename = abs_path / "not_ok" / suffix;
    task_result_ptr_->ok_filename = abs_path / "ok" / suffix;
    task_result_ptr_->ok2_filename = abs_path / "ok2" / suffix;
    task_result_ptr_->unknown_filename = abs_path / "unknown" / suffix;
  }

  if (!(create_file_directory(task_result_ptr_->not_ok_filename) &&
        create_file_directory(task_result_ptr_->ok_filename) &&
        create_file_directory(task_result_ptr_->ok2_filename) &&
        create_file_directory(task_result_ptr_->unknown_filename))) {
    // create some error messages and fire out
    return false;
  }
  task_result_ptr_->not_ok_file.open(task_result_ptr_->not_ok_filename,
                                     std::ios::out | std::ios::app);
  task_result_ptr_->ok_file.open(task_result_ptr_->ok_filename,
                                 std::ios::out | std::ios::app);
  task_result_ptr_->ok2_file.open(task_result_ptr_->ok2_filename,
                                  std::ios::out | std::ios::app);
  task_result_ptr_->unknown_file.open(task_result_ptr_->unknown_filename,
                                      std::ios::out | std::ios::app);

  return task_result_ptr_->not_ok_file.is_open() &&
         task_result_ptr_->ok_file.is_open() &&
         task_result_ptr_->ok2_file.is_open() &&
         task_result_ptr_->unknown_file.is_open();
}

task_status_e background_worker_t::continue_old_task() {
  auto &task = atomic_task_.value();
  website_type_ = get_website_type(task.website_address);
  if (website_type_ == website_type_e::Unknown) {
    spdlog::error("Type not found");
    return task_status_e::Erred;
  }
  input_filename = task.input_filename;
  if (task_result_ptr_->total_numbers == 0) {
    if (atomic_task_->total == 0) {
      using utilities::get_file_content;
      using utilities::is_valid_number;

      task_result_ptr_->total_numbers = 0;
      get_file_content<std::string>(input_filename, is_valid_number,
                                    [this](std::string_view) mutable {
                                      ++task_result_ptr_->total_numbers;
                                    });
    } else {
      task_result_ptr_->total_numbers = atomic_task_->total;
    }
  }
  if (task_result_ptr_->total_numbers == 0) {
    spdlog::error("Total numbers = 0");
    return task_status_e::Erred;
  }
  input_file.open(input_filename, std::ios::in);
  if (!input_file) {
    if (std::filesystem::exists(input_filename)) {
      std::filesystem::remove(input_filename);
    }
    task_result_ptr_->operation_status = task_status_e::Erred;
    spdlog::error("Could not open input file: {}", input_filename);
    return task_status_e::Erred;
  }
  number_stream_ = std::make_unique<number_stream_t>(input_file);
  return run_number_crawler();
}

task_status_e background_worker_t::run_new_task() {
  input_filename =
      "." + utilities::get_random_string(utilities::get_random_integer()) +
      ".txt";
  std::ofstream out_file{input_filename};
  if (!out_file) {
    spdlog::error("Could not open out_file");
    input_filename.clear();
    task_result_ptr_->operation_status = task_status_e::Erred;
    return task_result_ptr_->operation_status;
  }
  for (std::size_t index = 0; index != uploads_info_.size(); ++index) {
    spdlog::info("name on disk: {}", uploads_info_[index].name_on_disk);
    std::ifstream in_file{uploads_info_[index].name_on_disk};
    if (!in_file)
      continue;
    out_file << in_file.rdbuf();
  }
  out_file.close();
  {
    using utilities::get_file_content;
    using utilities::is_valid_number;

    task_result_ptr_->total_numbers = 0;
    get_file_content<std::string>(input_filename, is_valid_number,
                                  [this](std::string_view) mutable {
                                    ++task_result_ptr_->total_numbers;
                                  });
    if (task_result_ptr_->total_numbers == 0)
      return task_status_e::Erred;
  }
  input_file.open(input_filename, std::ios::in);
  if (!input_file) {
    if (std::filesystem::exists(input_filename)) {
      std::filesystem::remove(input_filename);
    }
    spdlog::error("Could not open input_file");
    return task_status_e::Erred;
  }
  number_stream_ = std::make_unique<number_stream_t>(input_file);

  return run_number_crawler();
}

task_status_e background_worker_t::run() {
  if (website_info_.id != 0) {
    return run_new_task();
  }
  return continue_old_task();
}

void background_worker_t::proxy_callback_signal(NewProxySignal *signal) {
  new_proxy_signal_ = signal;
}

void background_worker_t::proxy_info_map(proxy_info_map_t *proxy_map) {
  proxy_info_map_ = proxy_map;
}

website_type_e get_website_type(std::string const &web_address) {
  if (web_address.find("jjgames") != std::string::npos) {
    return website_type_e::JJGames;
  } else if (web_address.find("autohome") != std::string::npos) {
    return website_type_e::AutoHomeRegister;
  } else if (web_address.find("ppsports") != std::string::npos) {
    return website_type_e::PPSports;
  } else if (web_address.find("qunar") != std::string::npos) {
    return website_type_e::Qunar;
  } else if (web_address.find("wines") != std::string::npos) {
    return website_type_e::Wines;
  } else if (web_address.find("xpuji") != std::string::npos) {
    return website_type_e::Xpuji;
  } else if (web_address.find("pcauto") != std::string::npos) {
    return website_type_e::PcAuto;
  } else if (web_address.find("lbm.") != std::string::npos) {
    return website_type_e::LisboaMacau;
  } else if (web_address.find("chm.") != std::string::npos) {
    return website_type_e::ChineseMacau;
  } else if (web_address.find("grandl") != std::string::npos) {
    return website_type_e::GrandLisboa;
  } else if (web_address.find("suncity") != std::string::npos) {
    return website_type_e::SunCity;
  } else if (web_address.find("baccarat") != std::string::npos) {
    return website_type_e::MacauBaccarat;
  } else if (web_address.find("vns") != std::string::npos) {
    return website_type_e::VNS;
  } else if (web_address.find("lottery") != std::string::npos) {
    return website_type_e::Lottery81;
  } else if (web_address.find("vip5") != std::string::npos) {
    return website_type_e::Vip5;
  } else if (web_address.find("zed3") != std::string::npos) {
    return website_type_e::Zed3;
  } else if (web_address.find("devil") != std::string::npos) {
    return website_type_e::DevilsHorn;
  } else if (web_address.find("fourty") != std::string::npos) {
    return website_type_e::FourtyFour;
  } else if (web_address.find("js3") != std::string::npos) {
    return website_type_e::JSThree;
  } else if (web_address.find("sugar") != std::string::npos) {
    return website_type_e::SugarRaise;
  } else if (web_address.find("tiger") != std::string::npos) {
    return website_type_e::TigerFortress;
  } else if (web_address.find("lebo") != std::string::npos) {
    return website_type_e::Lebo;
  } else if (web_address.find("fafa77") != std::string::npos) {
    return website_type_e::Fafa77;
  }

  return website_type_e::Unknown;
}

time_data_t get_time_data() {
  static std::random_device rd{};
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<> dis(0.0, 1.0);
  uint64_t const current_time = std::time(nullptr) * 1'000;
  std::size_t const random_number =
      static_cast<std::size_t>(std::round(1e3 * dis(gen)));
  std::uint64_t const callback_number =
      static_cast<std::size_t>(current_time + random_number);
  return time_data_t{current_time, callback_number};
}

} // namespace wudi_server
