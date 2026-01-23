#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

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
}

int main() {
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

  server.AddRoute("POST", "/api/v1/join", [&](const HttpRequest& req) {
    JoinRequest join_request;
    auto meeting_url = JsonGetString(req.body, "meeting_url");
    auto display_name = JsonGetString(req.body, "display_name");
    auto sdk_auth_token = JsonGetString(req.body, "sdk_auth_token");
    auto recording_token = JsonGetString(req.body, "recording_token");
    auto passcode = JsonGetString(req.body, "passcode");

    if (!meeting_url || !display_name || !sdk_auth_token) {
      return HttpResponse{400, "{\"error\":\"missing_fields\"}", "application/json"};
    }
    join_request.meeting_url = *meeting_url;
    join_request.display_name = *display_name;
    join_request.sdk_auth_token = *sdk_auth_token;
    if (recording_token) {
      join_request.recording_token = *recording_token;
    }
    if (passcode) {
      join_request.passcode = *passcode;
    }

    bool ok = zoom_client.JoinMeeting(join_request);
    if (!ok) {
      return HttpResponse{500, "{\"error\":\"join_failed\"}", "application/json"};
    }
    return HttpResponse{200, "{\"status\":\"ok\"}", "application/json"};
  });

  server.AddRoute("POST", "/api/v1/leave", [&](const HttpRequest&) {
    zoom_client.LeaveMeeting();
    return HttpResponse{200, "{\"status\":\"ok\"}", "application/json"};
  });

  server.AddRoute("GET", "/api/v1/status", [&](const HttpRequest&) {
    RecorderStatus status = zoom_client.Status();
    std::string body = "{\"state\":\"" + StateToString(status.state) + "\",\"participants\":" +
                       std::to_string(status.participants) + ",\"session_id\":\"" + status.session_id + "\"";
    if (status.error) {
      body += ",\"error\":\"" + *status.error + "\"";
    }
    body += "}";
    return HttpResponse{200, body, "application/json"};
  });

  int port = GetEnvInt("BOT_PORT", 3667);
  if (!server.Start(port)) {
    std::cerr << "Failed to start server" << std::endl;
    return 1;
  }

  std::cout << "Zoom bot recorder listening on port " << port << std::endl;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "quit") {
      break;
    }
  }
  server.Stop();
  return 0;
}
