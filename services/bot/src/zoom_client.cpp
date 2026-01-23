#include "zoom_client.h"

#include <dlfcn.h>
#include <iostream>

ZoomClient::ZoomClient(Recorder& recorder) : recorder_(recorder) {
  status_.state = RecorderState::Idle;
}

bool ZoomClient::JoinMeeting(const JoinRequest& request) {
  status_.state = RecorderState::Joining;
  status_.error.reset();

  // TODO: Integrate Zoom Meeting SDK 6.7.2.7020 here.
  // Use request.sdk_auth_token and request.recording_token to initialize SDK,
  // join the meeting by URL/passcode, and register raw audio callbacks.

  if (!recorder_.StartSession()) {
    status_.state = RecorderState::Error;
    status_.error = "failed to start recorder session";
    return false;
  }

  status_.state = RecorderState::Recording;
  return true;
}

void ZoomClient::LeaveMeeting() {
  // TODO: call SDK leave meeting, stop raw audio callbacks.
  recorder_.StopSession();
  status_.state = RecorderState::Done;
}

RecorderStatus ZoomClient::Status() const {
  RecorderStatus current = recorder_.GetStatus();
  if (status_.state == RecorderState::Error) {
    current.state = RecorderState::Error;
    current.error = status_.error;
  }
  return current;
}

bool ZoomClient::ProbeSdkLoaded(std::string* error_message) const {
  void* handle = dlopen("libmeetingsdk.so", RTLD_NOW);
  if (!handle) {
    if (error_message) {
      const char* err = dlerror();
      *error_message = err ? err : "dlopen failed";
    }
    return false;
  }
  dlclose(handle);
  return true;
}
