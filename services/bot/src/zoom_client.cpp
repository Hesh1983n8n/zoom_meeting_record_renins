#include "zoom_client.h"

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <cctype>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <locale>
#include <regex>
#include <type_traits>
#include <vector>

#include "meeting_url.h"
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

namespace {
std::string StripWhitespace(const std::string& value) {
  return std::regex_replace(value, std::regex(R"(\s+)"), "");
}

template <typename T>
struct ZStringConverter;

template <>
struct ZStringConverter<char> {
  static std::basic_string<char> Convert(const std::string& value) {
    return value;
  }
};

template <>
struct ZStringConverter<wchar_t> {
  static std::basic_string<wchar_t> Convert(const std::string& value) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(value);
  }
};

static std::basic_string<zchar_t> ToZString(const std::string& value) {
  return ZStringConverter<zchar_t>::Convert(value);
}

std::string SdkErrorToString(ZOOMSDK::SDKError err) {
  switch (err) {
    case ZOOMSDK::SDKERR_SUCCESS:
      return "SDKERR_SUCCESS";
    case ZOOMSDK::SDKERR_NO_IMPL:
      return "SDKERR_NO_IMPL";
    case ZOOMSDK::SDKERR_WRONG_USAGE:
      return "SDKERR_WRONG_USAGE";
    case ZOOMSDK::SDKERR_INVALID_PARAMETER:
      return "SDKERR_INVALID_PARAMETER";
    case ZOOMSDK::SDKERR_MODULE_LOAD_FAILED:
      return "SDKERR_MODULE_LOAD_FAILED";
    case ZOOMSDK::SDKERR_MEMORY_FAILED:
      return "SDKERR_MEMORY_FAILED";
    case ZOOMSDK::SDKERR_SERVICE_FAILED:
      return "SDKERR_SERVICE_FAILED";
    case ZOOMSDK::SDKERR_UNINITIALIZE:
      return "SDKERR_UNINITIALIZE";
    case ZOOMSDK::SDKERR_UNAUTHENTICATION:
      return "SDKERR_UNAUTHENTICATION";
    case ZOOMSDK::SDKERR_INTERNAL_ERROR:
      return "SDKERR_INTERNAL_ERROR";
    default:
      return "SDKERR_UNKNOWN";
  }
}

std::string ToLowerCopy(const std::string& value) {
  std::string lowered = value;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

std::string ResolveHomeDir() {
  const char* home = std::getenv("HOME");
  if (home && *home) {
    return home;
  }
  return "/data";
}

void LogHomeContents(const std::string& home_dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::cout << "[auth] HOME=" << home_dir << std::endl;
  if (!fs::exists(home_dir, ec)) {
    std::cout << "[auth] HOME does not exist" << std::endl;
    return;
  }
  for (const auto& entry :
       fs::directory_iterator(home_dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      std::cout << "[auth] HOME listing failed: " << ec.message() << std::endl;
      return;
    }
    std::string type = "other";
    if (entry.is_directory(ec)) {
      type = "dir";
    } else if (entry.is_regular_file(ec)) {
      type = "file";
    } else if (entry.is_symlink(ec)) {
      type = "symlink";
    }
    std::cout << "[auth] HOME entry (" << type << "): " << entry.path().filename().string()
              << std::endl;
  }
  if (ec) {
    std::cout << "[auth] HOME listing failed: " << ec.message() << std::endl;
  }
}

void MaybeResetZoomProfileOnAuthFailure(const std::string& home_dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const std::vector<std::string> dir_whitelist = {".zoom", "ZoomSDK", "zoomsdk"};
  const std::vector<std::string> file_exts = {".db", ".sqlite", ".sqlite3"};

  std::cout << "[auth] attempting zoom profile cleanup in " << home_dir << std::endl;
  if (!fs::exists(home_dir, ec)) {
    std::cout << "[auth] cleanup skipped: HOME does not exist" << std::endl;
    return;
  }

  for (const auto& entry :
       fs::directory_iterator(home_dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      std::cout << "[auth] cleanup listing failed: " << ec.message() << std::endl;
      return;
    }
    const fs::path path = entry.path();
    const std::string name = path.filename().string();
    if (entry.is_directory(ec) &&
        std::find(dir_whitelist.begin(), dir_whitelist.end(), name) != dir_whitelist.end()) {
      std::error_code remove_ec;
      fs::remove_all(path, remove_ec);
      if (remove_ec) {
        std::cout << "[auth] failed to remove directory " << name << ": " << remove_ec.message()
                  << std::endl;
      } else {
        std::cout << "[auth] removed directory " << name << std::endl;
      }
      continue;
    }

    if (entry.is_regular_file(ec)) {
      const std::string lowered = ToLowerCopy(name);
      const std::string extension = ToLowerCopy(path.extension().string());
      const bool ext_allowed =
          std::find(file_exts.begin(), file_exts.end(), extension) != file_exts.end();
      const bool name_allowed = lowered.find("zoom") != std::string::npos;
      if (ext_allowed && name_allowed) {
        std::error_code remove_ec;
        fs::remove(path, remove_ec);
        if (remove_ec) {
          std::cout << "[auth] failed to remove file " << name << ": " << remove_ec.message()
                    << std::endl;
        } else {
          std::cout << "[auth] removed file " << name << std::endl;
        }
      }
    }
  }
  if (ec) {
    std::cout << "[auth] cleanup listing failed: " << ec.message() << std::endl;
  }
}

}  // namespace

class ZoomClient::AuthEventHandler : public ZOOMSDK::IAuthServiceEvent {
 public:
  explicit AuthEventHandler(ZoomClient* owner) : owner_(owner) {}

  void onAuthenticationReturn(ZOOMSDK::AuthResult ret) override {
    std::lock_guard<std::mutex> lock(owner_->mutex_);
    owner_->last_auth_code_ = static_cast<int>(ret);
    owner_->auth_ok_ = (ret == ZOOMSDK::AUTHRET_SUCCESS);
    owner_->authed_ = owner_->auth_ok_;
    owner_->auth_done_ = true;
    std::cout << "[auth] callback result=" << static_cast<int>(ret) << std::endl;
    owner_->cv_.notify_all();
  }

  void onLoginReturnWithReason(ZOOMSDK::LOGINSTATUS, ZOOMSDK::IAccountInfo*, ZOOMSDK::LoginFailReason) override {}
  void onLogout() override {}
  void onZoomIdentityExpired() override {}
  void onZoomAuthIdentityExpired() override {}

 private:
  ZoomClient* owner_ = nullptr;
};

class ZoomClient::MeetingEventHandler : public ZOOMSDK::IMeetingServiceEvent {
 public:
  explicit MeetingEventHandler(ZoomClient* owner) : owner_(owner) {}

  void onMeetingStatusChanged(ZOOMSDK::MeetingStatus status, int iResult = 0) override {
    std::lock_guard<std::mutex> lock(owner_->mutex_);
    owner_->last_join_code_ = iResult;
    std::cout << "[join] meeting status changed=" << static_cast<int>(status)
              << " error=" << iResult << std::endl;
    if (status == ZOOMSDK::MEETING_STATUS_INMEETING) {
      owner_->in_meeting_ = true;
      owner_->join_ok_ = true;
      owner_->join_done_ = true;
      owner_->SetState(RecorderState::InMeeting, std::nullopt);
      owner_->cv_.notify_all();
      return;
    }
    if (status == ZOOMSDK::MEETING_STATUS_FAILED ||
        status == ZOOMSDK::MEETING_STATUS_DISCONNECTING ||
        status == ZOOMSDK::MEETING_STATUS_ENDED) {
      owner_->join_ok_ = false;
      owner_->join_done_ = true;
      owner_->cv_.notify_all();
    }
  }

  void onMeetingParameterNotification(const ZOOMSDK::MeetingParameter*) override {}
  void onMeetingStatisticsWarningNotification(ZOOMSDK::StatisticsWarningType) override {}
  void onSuspendParticipantsActivities() override {}
  void onAICompanionActiveChangeNotice(bool) override {}
  void onMeetingTopicChanged(const zchar_t* sTopic) override {}
  void onMeetingFullToWatchLiveStream(const zchar_t* sLiveStreamUrl) override {}
  void onUserNetworkStatusChanged(ZOOMSDK::MeetingComponentType,
                                  ZOOMSDK::ConnectionQuality,
                                  unsigned int,
                                  bool) override {}
#if defined(WIN32)
  void onAppSignalPanelUpdated(ZOOMSDK::IMeetingAppSignalHandler*) override {}
#endif

 private:
  ZoomClient* owner_ = nullptr;
};

ZoomClient::ZoomClient(Recorder& recorder) : recorder_(recorder) {
  status_.state = RecorderState::Idle;
}

ZoomClient::~ZoomClient() = default;

void ZoomClient::SetState(RecorderState state, const std::optional<std::string>& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = state;
  status_.error = error;
}

bool ZoomClient::EnsureSdkLoaded(std::string& error_message) {
  if (sdk_loaded_) {
    return true;
  }
  ProbeResult result = ProbeSdkLoaded();
  if (!result.ok) {
    error_message = result.error;
    SetSdkError(result.error);
    return false;
  }
  sdk_loaded_ = true;
  return true;
}

bool ZoomClient::InitSdkOnce(std::string& error_message, int& code) {
  code = -1;
  if (sdk_inited_) {
    return true;
  }
  ZOOMSDK::tagInitParam init_param;
  init_param.strWebDomain = "https://zoom.us";
  init_param.enableLogByDefault = true;
  ZOOMSDK::SDKError err = ZOOMSDK::InitSDK(init_param);
  code = static_cast<int>(err);
  std::cout << "[sdk] InitSDK result=" << code << std::endl;
  if (err != ZOOMSDK::SDKERR_SUCCESS) {
    error_message = "InitSDK failed";
    return false;
  }
  sdk_inited_ = true;
  return true;
}

bool ZoomClient::SdkAuth(const std::string& jwt, std::string& error_message, int& code) {
  code = -1;
  std::string trimmed = StripWhitespace(jwt);
  if (trimmed.empty()) {
    error_message = "sdk_jwt empty";
    return false;
  }
  auto dot_count = std::count(trimmed.begin(), trimmed.end(), '.');
  if (dot_count != 2) {
    error_message = "BAD_JWT_FORMAT";
    code = -3;
    return false;
  }
  jwt_buffer_ = ToZString(trimmed);
  std::string prefix = trimmed.substr(0, std::min<size_t>(12, trimmed.size()));
  std::string suffix =
      trimmed.size() > 6 ? trimmed.substr(trimmed.size() - 6) : trimmed;
  std::cout << "[auth] calling SDKAuth jwt_len=" << jwt_buffer_.size()
            << " jwt_prefix=" << prefix << " jwt_suffix=" << suffix << std::endl;
  if (!InitSdkOnce(error_message, code)) {
    return false;
  }
  ZOOMSDK::IAuthService* auth_service = auth_service_;
  ZOOMSDK::SDKError err = ZOOMSDK::SDKERR_SUCCESS;
  if (!auth_service) {
    err = ZOOMSDK::CreateAuthService(&auth_service);
    code = static_cast<int>(err);
    std::cout << "[auth] CreateAuthService result=" << code << " (" << SdkErrorToString(err)
              << ")" << std::endl;
    if (err != ZOOMSDK::SDKERR_SUCCESS || !auth_service) {
      error_message = "CreateAuthService failed";
      return false;
    }
    auth_service_ = auth_service;
  }
  if (!auth_event_handler_) {
    auth_event_handler_ = std::make_unique<AuthEventHandler>(this);
  }
  auth_service->SetEvent(auth_event_handler_.get());

  ZOOMSDK::AuthContext auth_context;
  auth_context.jwt_token = jwt_buffer_.c_str();
  auto attempt_auth = [&]() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auth_done_ = false;
      auth_ok_ = false;
      last_auth_code_ = 0;
    }
    ZOOMSDK::SDKError attempt_err = auth_service->SDKAuth(auth_context);
    code = static_cast<int>(attempt_err);
    std::cout << "[auth] SDKAuth call returned=" << code << " ("
              << SdkErrorToString(attempt_err) << ")" << std::endl;
    return attempt_err;
  };

  err = attempt_auth();
  if (err != ZOOMSDK::SDKERR_SUCCESS) {
    std::cout << "[auth] SDKAuth failed with code=" << code << " (" << SdkErrorToString(err)
              << "), attempting profile reset" << std::endl;
    const std::string home_dir = ResolveHomeDir();
    LogHomeContents(home_dir);
    MaybeResetZoomProfileOnAuthFailure(home_dir);
    err = attempt_auth();
    if (err != ZOOMSDK::SDKERR_SUCCESS) {
      error_message = "SDKAuth call failed";
      return false;
    }
  }

  std::unique_lock<std::mutex> lock(mutex_);
  bool signaled = cv_.wait_for(lock, std::chrono::seconds(20), [&]() { return auth_done_; });
  if (!signaled) {
    error_message = "SDKAuth timeout";
    code = -2;
    return false;
  }
  if (!auth_ok_) {
    error_message = "SDKAuth failed";
    code = last_auth_code_;
    return false;
  }
  authed_ = true;
  return true;
}

bool ZoomClient::JoinMeeting(const std::string& meeting_id,
                             const std::string& passcode,
                             const std::string& display_name,
                             std::string& error_message,
                             int& code) {
  code = -1;
  if (meeting_id.empty() || display_name.empty()) {
    error_message = "meeting_id or display_name empty";
    return false;
  }
  if (!authed_) {
    error_message = "SDKAuth not completed";
    return false;
  }
  std::cout << "[join] calling JoinMeeting meeting_id=" << meeting_id
            << " has_passcode=" << (!passcode.empty() ? "true" : "false") << std::endl;

  ZOOMSDK::IMeetingService* meeting_service = nullptr;
  ZOOMSDK::SDKError err = ZOOMSDK::CreateMeetingService(&meeting_service);
  code = static_cast<int>(err);
  std::cout << "[join] CreateMeetingService result=" << code << std::endl;
  if (err != ZOOMSDK::SDKERR_SUCCESS || !meeting_service) {
    error_message = "CreateMeetingService failed";
    return false;
  }
  meeting_service_ = meeting_service;
  meeting_event_handler_ = std::make_unique<MeetingEventHandler>(this);
  meeting_service->SetEvent(meeting_event_handler_.get());

  ZOOMSDK::JoinParam join_param;
  join_param.userType = ZOOMSDK::SDK_UT_WITHOUT_LOGIN;
  ZOOMSDK::JoinParam4WithoutLogin& param = join_param.param.withoutloginuserJoin;
  display_name_z_ = ToZString(display_name);
  passcode_z_ = ToZString(passcode);
  param.meetingNumber = std::stoull(meeting_id);
  param.userName = display_name_z_.c_str();
  param.psw = passcode_z_.c_str();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    join_done_ = false;
    join_ok_ = false;
    last_join_code_ = 0;
    in_meeting_ = false;
  }

  err = meeting_service->Join(join_param);
  code = static_cast<int>(err);
  std::cout << "[join] JoinMeeting returned=" << code << std::endl;
  if (err != ZOOMSDK::SDKERR_SUCCESS) {
    error_message = "JoinMeeting call failed";
    return false;
  }

  std::unique_lock<std::mutex> lock(mutex_);
  bool signaled = cv_.wait_for(lock, std::chrono::seconds(30), [&]() { return join_done_; });
  if (!signaled) {
    error_message = "JoinMeeting timeout";
    code = -2;
    return false;
  }
  if (!join_ok_) {
    error_message = "JoinMeeting failed";
    code = last_join_code_;
    return false;
  }
  in_meeting_ = true;
  return true;
}

bool ZoomClient::JoinMeeting(const JoinRequest& request) {
  SetState(RecorderState::Authing, std::nullopt);

  std::string error;
  int code = 0;
  if (!EnsureSdkLoaded(error)) {
    SetState(RecorderState::Error, error);
    return false;
  }

  if (!SdkAuth(request.sdk_auth_token, error, code)) {
    SetState(RecorderState::Error, error);
    return false;
  }

  SetState(RecorderState::Joining, std::nullopt);
  auto info = ParseMeetingUrl(request.meeting_url);
  if (!info) {
    SetState(RecorderState::Error, "invalid meeting url");
    return false;
  }
  std::string passcode = request.passcode.value_or(info->pwd.value_or(""));
  if (!JoinMeeting(info->meeting_number, passcode, request.display_name, error, code)) {
    SetState(RecorderState::Error, error);
    return false;
  }

  if (!recorder_.StartSession()) {
    SetState(RecorderState::Error, "failed to start recorder session");
    return false;
  }

  SetState(RecorderState::Recording, std::nullopt);
  return true;
}

void ZoomClient::LeaveMeeting() {
  // TODO: call SDK leave meeting, stop raw audio callbacks.
  recorder_.StopSession();
  status_.state = RecorderState::Done;
}

RecorderStatus ZoomClient::Status() const {
  RecorderStatus current = recorder_.GetStatus();
  std::lock_guard<std::mutex> lock(mutex_);
  current.state = status_.state;
  current.error = status_.error;
  return current;
}

ProbeResult ZoomClient::ProbeSdkLoaded() {
  void* handle = dlopen("libmeetingsdk.so", RTLD_NOW | RTLD_GLOBAL);
  if (!handle) {
    const char* err = dlerror();
    sdk_loaded_ = false;
    sdk_error_ = err ? err : "unknown dlopen error";
    return {false, sdk_error_};
  }
  sdk_handle_ = handle;
  sdk_loaded_ = true;
  sdk_error_.clear();
  return {true, ""};
}

void ZoomClient::SetSdkError(const std::string& error) {
  SetState(RecorderState::Error, error);
  sdk_loaded_ = false;
  sdk_error_ = error;
}
