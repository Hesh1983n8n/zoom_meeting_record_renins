#pragma once

#include <condition_variable>
#include <mutex>
#include <memory>
#include <optional>
#include <string>

#include "recorder.h"

namespace ZOOMSDK {
class IAuthService;
class IMeetingService;
}  // namespace ZOOMSDK

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
  ~ZoomClient();

  ZoomClient(const ZoomClient&) = delete;
  ZoomClient& operator=(const ZoomClient&) = delete;
  ZoomClient(ZoomClient&&) noexcept;
  ZoomClient& operator=(ZoomClient&&) noexcept;

  bool EnsureSdkLoaded(std::string& error_message);
  bool InitSdkOnce(std::string& error_message, int& code);
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
  struct AuthEventHandler;
  struct MeetingEventHandler;

  void SetState(RecorderState state, const std::optional<std::string>& error = std::nullopt);
  Recorder& recorder_;
  RecorderStatus status_{};
  void* sdk_handle_ = nullptr;
  bool sdk_loaded_ = false;
  bool sdk_inited_ = false;
  bool authed_ = false;
  bool in_meeting_ = false;
  std::string sdk_error_;
  int last_auth_code_ = 0;
  int last_join_code_ = 0;
  bool auth_done_ = false;
  bool auth_ok_ = false;
  bool join_done_ = false;
  bool join_ok_ = false;
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;

  ZOOMSDK::IAuthService* auth_service_ = nullptr;
  ZOOMSDK::IMeetingService* meeting_service_ = nullptr;
  std::unique_ptr<AuthEventHandler> auth_event_handler_;
  std::unique_ptr<MeetingEventHandler> meeting_event_handler_;
};
