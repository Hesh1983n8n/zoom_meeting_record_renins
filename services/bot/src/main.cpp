#include <atomic>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "http_server.h"
#include "recorder.h"
#include "zoom_client.h"

namespace {
std::string GetEnvOrDefault(const char* key, const std::string& def) {
  const char* val = std::getenv(key);
  if (!val) {
    return def;
  }
  return val;
}

int GetEnvInt(const char* key, int def) {
  const char* val = std::getenv(key);
  if (!val) {
    return def;
  }
  return std::atoi(val);
}

bool GetEnvBool(const char* key, bool def) {
  const char* val = std::getenv(key);
  if (!val) {
    return def;
  }
  return std::string(val) == "true" || std::string(val) == "1";
}

std::optional<std::string> JsonGetString(const std::string& body, const std::string& key) {
  std::string target = "\"" + key + "\"";
  auto pos = body.find(target);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = body.find(':', pos);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = body.find('"', pos);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  auto end = body.find('"', pos + 1);
  if (end == std::string::npos) {
    return std::nullopt;
  }
  return body.substr(pos + 1, end - pos - 1);
}

std::string StateToString(RecorderState state) {
  switch (state) {
    case RecorderState::Idle:
      return "idle";
    case RecorderState::Joining:
      return "joining";
    case RecorderState::Recording:
      return "recording";
    case RecorderState::Finalizing:
      return "finalizing";
    case RecorderState::Done:
      return "done";
    case RecorderState::Error:
      return "error";
    default:
      return "unknown";
  }
}

std::string ExtractMeetingId(const std::string& meeting_url) {
  std::string::size_type pos = meeting_url.find("/j/");
  if (pos == std::string::npos) {
    return "";
  }
  pos += 3;
  std::string::size_type end = pos;
  while (end < meeting_url.size() && std::isdigit(static_cast<unsigned char>(meeting_url[end]))) {
    ++end;
  }
  return meeting_url.substr(pos, end - pos);
}

std::string TokenPrefix(const std::string& token, size_t length = 12) {
  if (token.size() <= length) {
    return token;
  }
  return token.substr(0, length);
}

std::string StripWhitespace(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (!std::isspace(static_cast<unsigned char>(ch))) {
      out.push_back(ch);
    }
  }
  return out;
}
}

std::atomic<bool>* g_running = nullptr;

void HandleSignal(int) {
  if (g_running) {
    g_running->store(false);
  }
}

int main() {
  try {
    std::atomic<bool> running{true};
    g_running = &running;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    RecorderConfig config;
    config.records_dir = GetEnvOrDefault("RECORDS_DIR", "/records");
    config.final_mix_enable = GetEnvBool("FINAL_MIX_ENABLE", true);
    config.final_mix_sample_rate = GetEnvInt("FINAL_MIX_SAMPLE_RATE", 48000);
    config.final_mix_channels = GetEnvInt("FINAL_MIX_CHANNELS", 1);
    config.vad.frame_ms = GetEnvInt("VAD_FRAME_MS", 20);
    config.vad.energy_threshold = std::stof(GetEnvOrDefault("VAD_ENERGY_THRESHOLD", "0.015"));
    config.vad.min_speech_ms = GetEnvInt("VAD_MIN_SPEECH_MS", 200);
    config.vad.min_silence_ms = GetEnvInt("VAD_MIN_SILENCE_MS", 400);

    Recorder recorder(config);
    ZoomClient zoom_client(recorder);
    HttpServer server;

    ProbeResult probe = zoom_client.ProbeSdkLoaded();
    if (probe.ok) {
      std::cout << "SDK dlopen: OK" << std::endl;
    } else {
      std::cout << "SDK dlopen: FAIL: " << probe.error << std::endl;
      zoom_client.SetSdkError(probe.error.empty() ? "sdk_dlopen_failed" : probe.error);
    }

    server.AddRoute("POST", "/api/v1/join", [&](const HttpRequest& req) {
      JoinRequest join_request;
      auto meeting_url = JsonGetString(req.body, "meeting_url");
      auto display_name = JsonGetString(req.body, "display_name");
      auto sdk_auth_token = JsonGetString(req.body, "sdk_auth_token");
      if (!sdk_auth_token) {
        sdk_auth_token = JsonGetString(req.body, "sdk_jwt");
      }
      if (!sdk_auth_token) {
        sdk_auth_token = JsonGetString(req.body, "signature");
      }
      auto recording_token = JsonGetString(req.body, "recording_token");
      auto passcode = JsonGetString(req.body, "passcode");

      if (!meeting_url || !display_name || !sdk_auth_token) {
        if (meeting_url && display_name && !sdk_auth_token) {
          return HttpResponse{400, "{\"ok\":false,\"error\":\"MISSING_SDK_JWT\"}", "application/json"};
        }
        return HttpResponse{400, "{\"error\":\"missing_fields\"}", "application/json"};
      }

      std::string cleaned_token = StripWhitespace(*sdk_auth_token);
      std::string meeting_id = ExtractMeetingId(*meeting_url);
      std::cout << "[join] request received" << std::endl;
      std::cout << "[join] meeting_id=" << meeting_id
                << " pwd_present=" << (passcode ? "true" : "false")
                << " display_name=" << *display_name
                << " sdk_jwt_prefix=" << TokenPrefix(cleaned_token) << std::endl;

      std::string error;
      int code = 0;
      std::cout << "[join] ensure_sdk_loaded" << std::endl;
      if (!zoom_client.EnsureSdkLoaded(error)) {
        std::cout << "[join] SDK load failed: " << error << std::endl;
        return HttpResponse{500, "{\"ok\":false,\"error\":\"SDK_LOAD_FAILED\"}", "application/json"};
      }
      std::cout << "[join] sdk_auth" << std::endl;
      if (!zoom_client.SdkAuth(cleaned_token, error, code)) {
        std::cout << "[join] SDKAuth failed code=" << code << " error=" << error << std::endl;
        return HttpResponse{500,
                            "{\"ok\":false,\"error\":\"SDK_AUTH_FAILED\",\"code\":" + std::to_string(code) + "}",
                            "application/json"};
      }
      std::cout << "[join] join_meeting" << std::endl;
      if (!zoom_client.JoinMeeting(meeting_id, passcode.value_or(""), *display_name, error, code)) {
        std::cout << "[join] JoinMeeting failed code=" << code << " error=" << error << std::endl;
        return HttpResponse{
            500, "{\"ok\":false,\"error\":\"JOIN_FAILED\",\"code\":" + std::to_string(code) + "}", "application/json"};
      }
      return HttpResponse{200,
                          "{\"ok\":true,\"state\":\"joining\",\"meeting_id\":\"" + meeting_id + "\"}",
                          "application/json"};
      (void)join_request;
    });

    server.AddRoute("POST", "/api/v1/leave", [&](const HttpRequest&) {
      zoom_client.LeaveMeeting();
      return HttpResponse{200, "{\"status\":\"ok\"}", "application/json"};
    });

    server.AddRoute("POST", "/api/v1/shutdown", [&](const HttpRequest&) {
      running.store(false);
      return HttpResponse{200, "{\"status\":\"ok\"}", "application/json"};
    });

    server.AddRoute("GET", "/api/v1/status", [&](const HttpRequest&) {
      RecorderStatus status = zoom_client.Status();
      std::string body = "{\"state\":\"" + StateToString(status.state) + "\",\"participants\":" +
                         std::to_string(status.participants) + ",\"session_id\":\"" + status.session_id + "\"";
      if (status.error) {
        body += ",\"error\":\"" + *status.error + "\"";
      }
      body += ",\"sdk_loaded\":" + std::string(zoom_client.SdkLoaded() ? "true" : "false");
      std::string sdk_error = zoom_client.SdkError();
      if (!sdk_error.empty()) {
        body += ",\"sdk_error\":\"" + sdk_error + "\"";
      }
      body += "}";
      return HttpResponse{200, body, "application/json"};
    });

    int port = GetEnvInt("BOT_PORT", 3667);
    if (!server.Start(port)) {
      int err = server.LastErrorCode();
      std::cerr << "Failed to bind port " << port << ": " << std::strerror(err) << std::endl;
      return 1;
    }

    std::cout << "Zoom bot recorder listening on port " << port << std::endl;
    while (running.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    server.Stop();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FATAL: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "FATAL: unknown exception" << std::endl;
    return 1;
  }
}
