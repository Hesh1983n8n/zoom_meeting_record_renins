#include "zoom_client.h"

#include <dlfcn.h>
#include <iostream>
#include <regex>

#include "meeting_url.h"

namespace {
std::string StripWhitespace(const std::string& value) {
  return std::regex_replace(value, std::regex(R"(\s+)"), "");
}
}

ZoomClient::ZoomClient(Recorder& recorder) : recorder_(recorder) {
  status_.state = RecorderState::Idle;
}

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
  error_message = "InitSDK not implemented";
  return false;
}

bool ZoomClient::SdkAuth(const std::string& jwt, std::string& error_message, int& code) {
  code = -1;
  std::string trimmed = StripWhitespace(jwt);
  if (trimmed.empty()) {
    error_message = "sdk_jwt empty";
    return false;
  }
  std::cout << "[auth] calling SDKAuth jwt_prefix=" << trimmed.substr(0, 12) << std::endl;
  if (!InitSdkOnce(error_message, code)) {
    return false;
  }
  error_message = "SDKAuth not implemented";
  return false;
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
  error_message = "JoinMeeting not implemented";
  return false;
}

bool ZoomClient::JoinMeeting(const JoinRequest& request) {
  SetState(RecorderState::Joining, std::nullopt);

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
