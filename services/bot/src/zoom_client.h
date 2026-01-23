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

class ZoomClient {
 public:
  explicit ZoomClient(Recorder& recorder);

  bool JoinMeeting(const JoinRequest& request);
  void LeaveMeeting();
  RecorderStatus Status() const;
  bool ProbeSdkLoaded(std::string* error_message);
  void SetSdkError(const std::string& error);

 private:
  Recorder& recorder_;
  RecorderStatus status_{};
  void* sdk_handle_ = nullptr;
};
