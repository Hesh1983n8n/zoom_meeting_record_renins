#pragma once

#include <optional>
#include <string>

#include "recorder.h"

struct JoinRequest {
  std::string meeting_url;
  std::optional<std::string> passcode;
  std::string display_name;
  std::string sdk_auth_token;
  std::optional<std::string> recording_token;
};

struct ProbeResult {
  bool ok = false;
  std::string error;
};

class ZoomClient {
 public:
  explicit ZoomClient(Recorder& recorder);

  bool EnsureSdkLoaded(std::string& error_message);
  bool SdkAuth(const std::string& jwt, std::string& error_message, int& code);
  bool JoinMeeting(const std::string& meeting_id,
                   const std::string& passcode,
                   const std::string& display_name,
                   std::string& error_message,
                   int& code);
  bool JoinMeeting(const JoinRequest& request);
  void LeaveMeeting();
  RecorderStatus Status() const;
  ProbeResult ProbeSdkLoaded();
  void SetSdkError(const std::string& error);
  bool SdkLoaded() const { return sdk_loaded_; }
  std::string SdkError() const { return sdk_error_; }

 private:
  Recorder& recorder_;
  RecorderStatus status_{};
  void* sdk_handle_ = nullptr;
  bool sdk_loaded_ = false;
  std::string sdk_error_;
};
