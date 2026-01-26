#include "zoom_client.h"

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <dlfcn.h>
#include <iostream>
#include <locale>
#include <regex>
#include <type_traits>

#include "meeting_url.h"
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

namespace {
std::string StripWhitespace(const std::string& value) {
  return std::regex_replace(value, std::regex(R"(\s+)"), "");
}

std::basic_string<zchar_t> ToZString(const std::string& value) {
  if constexpr (std::is_same_v<zchar_t, wchar_t>) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(value);
  } else {
    return std::basic_string<zchar_t>(value.begin(), value.end());
  }
}

}

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
  ZOOMSDK::IAuthService* auth_service = nullptr;
  ZOOMSDK::SDKError err = ZOOMSDK::CreateAuthService(&auth_service);
  code = static_cast<int>(err);
  std::cout << "[auth] CreateAuthService result=" << code << std::endl;
  if (err != ZOOMSDK::SDKERR_SUCCESS || !auth_service) {
    error_message = "CreateAuthService failed";
    return false;
  }
  auth_service_ = auth_service;
  auth_event_handler_ = std::make_unique<AuthEventHandler>(this);
  auth_service->SetEvent(auth_event_handler_.get());

  ZOOMSDK::AuthContext auth_context;
  auth_context.jwt_token = jwt_buffer_.c_str();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auth_done_ = false;
    auth_ok_ = false;
    last_auth_code_ = 0;
  }
  err = auth_service->SDKAuth(auth_context);
  code = static_cast<int>(err);
  std::cout << "[auth] SDKAuth call returned=" << code << std::endl;
  if (err != ZOOMSDK::SDKERR_SUCCESS) {
    error_message = "SDKAuth call failed";
    return false;
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
