#include <chrono>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QEventLoop>

// Zoom Meeting SDK for Linux v6.7.2.7020 headers:
// - meeting_service_interface.h
// - auth_service_interface.h
// - rawdata/rawdata_audio_helper_interface.h
#include "meeting_service_interface.h"
#include "zoom_sdk.h"
#include "zoom_sdk_raw_data_def.h"
#include "auth_service_interface.h"
#include "rawdata/rawdata_audio_helper_interface.h"
#include "rawdata/zoom_rawdata_api.h"

#if __has_include("rawdata_audio_helper_interface.h")
#include "rawdata_audio_helper_interface.h"
#define ZOOMSDK_HAS_RAW_AUDIO 1
#elif __has_include("rawdata/rawdata_audio_helper_interface.h")
#include "rawdata/rawdata_audio_helper_interface.h"
#define ZOOMSDK_HAS_RAW_AUDIO 1
#else
#define ZOOMSDK_HAS_RAW_AUDIO 0
#endif

using namespace ZOOM_SDK_NAMESPACE;

const char *SDKErrorToString(SDKError code) {
  switch (code) {
    case SDKERR_SUCCESS:
      return "SDKERR_SUCCESS";
    case SDKERR_INVALID_PARAMETER:
      return "SDKERR_INVALID_PARAMETER";
    case SDKERR_UNINITIALIZE:
      return "SDKERR_UNINITIALIZE";
    case SDKERR_UNAUTHENTICATION:
      return "SDKERR_UNAUTHENTICATION";
    case SDKERR_NO_PERMISSION:
      return "SDKERR_NO_PERMISSION";
    case SDKERR_SERVICE_FAILED:
      return "SDKERR_SERVICE_FAILED";
    default:
      return "SDKERR_UNKNOWN";
  }
}

const char *MeetingFailCodeToString(int code) {
  switch (code) {
    case MEETING_SUCCESS:
      return "MEETING_SUCCESS";
    case MEETING_FAIL_CONNECTION_ERR:
      return "MEETING_FAIL_CONNECTION_ERR";
    case MEETING_FAIL_PASSWORD_ERR:
      return "MEETING_FAIL_PASSWORD_ERR";
    case MEETING_FAIL_ENFORCE_LOGIN:
      return "MEETING_FAIL_ENFORCE_LOGIN";
    case MEETING_FAIL_HOST_DISALLOW_OUTSIDE_USER_JOIN:
      return "MEETING_FAIL_HOST_DISALLOW_OUTSIDE_USER_JOIN";
    case MEETING_FAIL_UNABLE_TO_JOIN_EXTERNAL_MEETING:
      return "MEETING_FAIL_UNABLE_TO_JOIN_EXTERNAL_MEETING";
    case MEETING_FAIL_BLOCKED_BY_ACCOUNT_ADMIN:
      return "MEETING_FAIL_BLOCKED_BY_ACCOUNT_ADMIN";
    case MEETING_FAIL_NEED_SIGN_IN_FOR_PRIVATE_MEETING:
      return "MEETING_FAIL_NEED_SIGN_IN_FOR_PRIVATE_MEETING";
    default:
      return "MEETING_FAIL_(other/unknown)";
  }
}

const char *MeetingStatusToString(MeetingStatus status) {
  switch (status) {
    case MEETING_STATUS_IDLE:
      return "MEETING_STATUS_IDLE";
    case MEETING_STATUS_CONNECTING:
      return "MEETING_STATUS_CONNECTING";
    case MEETING_STATUS_WAITINGFORHOST:
      return "MEETING_STATUS_WAITINGFORHOST";
    case MEETING_STATUS_INMEETING:
      return "MEETING_STATUS_INMEETING";
    case MEETING_STATUS_DISCONNECTING:
      return "MEETING_STATUS_DISCONNECTING";
    case MEETING_STATUS_RECONNECTING:
      return "MEETING_STATUS_RECONNECTING";
    case MEETING_STATUS_FAILED:
      return "MEETING_STATUS_FAILED";
    case MEETING_STATUS_ENDED:
      return "MEETING_STATUS_ENDED";
    case MEETING_STATUS_IN_WAITING_ROOM:
      return "MEETING_STATUS_IN_WAITING_ROOM";
    case MEETING_STATUS_WEBINAR_PROMOTE:
      return "MEETING_STATUS_WEBINAR_PROMOTE";
    case MEETING_STATUS_WEBINAR_DEPROMOTE:
      return "MEETING_STATUS_WEBINAR_DEPROMOTE";
    case MEETING_STATUS_JOIN_BREAKOUT_ROOM:
      return "MEETING_STATUS_JOIN_BREAKOUT_ROOM";
    case MEETING_STATUS_LEAVE_BREAKOUT_ROOM:
      return "MEETING_STATUS_LEAVE_BREAKOUT_ROOM";
    case MEETING_STATUS_WAITING_EXTERNAL_SESSION_KEY:
      return "MEETING_STATUS_WAITING_EXTERNAL_SESSION_KEY";
    default:
      return "MEETING_STATUS_(other/unknown)";
  }
}

void LogSdkError(const std::string &label, SDKError code) {
  std::cout << label << " code=" << static_cast<int>(code)
            << " name=" << SDKErrorToString(code) << std::endl;
}

std::string Base64UrlDecode(const std::string &input) {
  std::string base64 = input;
  for (char &c : base64) {
    if (c == '-') {
      c = '+';
    } else if (c == '_') {
      c = '/';
    }
  }
  while (base64.size() % 4 != 0) {
    base64.push_back('=');
  }
  static const std::string kChars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<unsigned char> bytes;
  int val = 0;
  int valb = -8;
  for (unsigned char c : base64) {
    if (c == '=') {
      break;
    }
    size_t idx = kChars.find(c);
    if (idx == std::string::npos) {
      continue;
    }
    val = (val << 6) + static_cast<int>(idx);
    valb += 6;
    if (valb >= 0) {
      bytes.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return std::string(bytes.begin(), bytes.end());
}

std::string DecodeJwtPayload(const std::string &jwt_token) {
  size_t first_dot = jwt_token.find('.');
  if (first_dot == std::string::npos) {
    return "";
  }
  size_t second_dot = jwt_token.find('.', first_dot + 1);
  if (second_dot == std::string::npos) {
    return "";
  }
  std::string payload = jwt_token.substr(first_dot + 1, second_dot - first_dot - 1);
  return Base64UrlDecode(payload);
}

struct Args {
  std::string meeting_id;
  std::string passcode;
  std::string display_name;
  std::string signature;
  std::string out_dir;
  std::string mode = "per_user";
};

bool ParseArgs(int argc, char **argv, Args &args) {
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    if (key == "--meeting_id" && i + 1 < argc) {
      args.meeting_id = argv[++i];
    } else if (key == "--passcode" && i + 1 < argc) {
      args.passcode = argv[++i];
    } else if (key == "--display_name" && i + 1 < argc) {
      args.display_name = argv[++i];
    } else if (key == "--signature" && i + 1 < argc) {
      args.signature = argv[++i];
    } else if (key == "--out_dir" && i + 1 < argc) {
      args.out_dir = argv[++i];
    } else if (key == "--mode" && i + 1 < argc) {
      args.mode = argv[++i];
    }
  }

  return !args.meeting_id.empty() && !args.display_name.empty() &&
         !args.signature.empty() && !args.out_dir.empty();
}

void WriteMetadata(const std::string &out_dir, const std::string &meeting_id) {
  std::ofstream metadata(out_dir + "/metadata.json");
  if (!metadata.is_open()) {
    std::cerr << "[recorder] metadata_write_fail" << std::endl;
    return;
  }

  metadata << "{\n";
  metadata << "  \"meeting_id\": \"" << meeting_id << "\",\n";
  metadata << "  \"participants\": [],\n";
  metadata << "  \"events\": []\n";
  metadata << "}\n";
}

class MeetingEventHandler : public IMeetingServiceEvent {
 public:
  std::atomic<int> last_status{static_cast<int>(MEETING_STATUS_IDLE)};
  std::atomic<int> last_result{0};

  void onMeetingStatusChanged(MeetingStatus status, int iResult) override {
    last_status.store(static_cast<int>(status));
    last_result.store(iResult);
    std::cout << "[recorder] meeting_status status=" << MeetingStatusToString(status)
              << " (" << static_cast<int>(status) << ")"
              << " result=" << MeetingFailCodeToString(iResult)
              << " (" << iResult << ")";
    if (status == MEETING_STATUS_FAILED && iResult == MEETING_FAIL_UNABLE_TO_JOIN_EXTERNAL_MEETING) {
      std::cout << " hint=\"external meeting blocked, publish app or use meeting from same account\"";
    }
    std::cout << std::endl;
  }

  void onMeetingNeedPassword(bool bNeedPassword, const zchar_t *psMeetingPassword) {
    std::cout << "[recorder] meeting_need_password need=" << bNeedPassword
              << " pwd_present=" << (psMeetingPassword && *psMeetingPassword ? 1 : 0)
              << std::endl;
  }

  void onJoinMeetingResult(MeetingStatus status, int iResult) {
    std::cout << "[recorder] join_result status=" << static_cast<int>(status)
              << " result=" << iResult << std::endl;
  }

  void onMeetingStatisticsWarningNotification(StatisticsWarningType) override {}
  void onMeetingParameterNotification(const MeetingParameter *) override {}
  void onSuspendParticipantsActivities() override {}
  void onAICompanionActiveChangeNotice(bool) override {}
  void onMeetingTopicChanged(const zchar_t *) override {}
  void onMeetingFullToWatchLiveStream(const zchar_t *) override {}
  void onUserNetworkStatusChanged(MeetingComponentType, ConnectionQuality,
                                  unsigned int, bool) override {}
};

class AuthEventHandler : public IAuthServiceEvent {
 public:
  AuthEventHandler(std::atomic<bool> &authed,
                   std::atomic<bool> &auth_done,
                   std::atomic<int> &auth_ret)
      : authed_(authed), auth_done_(auth_done), auth_ret_(auth_ret) {}

  void onAuthenticationReturn(AuthResult ret) override {
    std::cout << "[recorder] auth_return ret=" << static_cast<int>(ret)
              << std::endl;
    auth_done_.store(true);
    authed_.store(ret == AUTHRET_SUCCESS);
    auth_ret_.store(static_cast<int>(ret));
  }

  void onLoginReturnWithReason(LOGINSTATUS, IAccountInfo *,
                               LoginFailReason) override {}
  void onLogout() override {}
  void onZoomIdentityExpired() override {}
  void onZoomAuthIdentityExpired() override {}

 private:
  std::atomic<bool> &authed_;
  std::atomic<bool> &auth_done_;
  std::atomic<int> &auth_ret_;
};

#if ZOOMSDK_HAS_RAW_AUDIO
class AudioRawDelegate : public IZoomSDKAudioRawDataDelegate {
 public:
  explicit AudioRawDelegate(const std::string &out_dir) : out_dir_(out_dir) {}

  void onMixedAudioRawDataReceived(AudioRawData *data) override {
    if (!first_packet_logged_) {
      std::cout << "[recorder] first_audio_packet_received mode=mixed" << std::endl;
      first_packet_logged_ = true;
    }
    WriteWavChunk(data, out_dir_ + "/mixed/chunks", "mixed");
  }

  void onOneWayAudioRawDataReceived(AudioRawData *data, unsigned int userId) override {
    if (!first_packet_logged_) {
      std::cout << "[recorder] first_audio_packet_received mode=per_user" << std::endl;
      first_packet_logged_ = true;
    }
    WriteWavChunk(data, out_dir_ + "/users/" + std::to_string(userId) + "/chunks",
                  std::to_string(userId));
  }

  void onShareAudioRawDataReceived(AudioRawData *, uint32_t) override {}
  void onOneWayInterpreterAudioRawDataReceived(AudioRawData *, const zchar_t *) override {}

 private:
  void WriteWavChunk(AudioRawData *data, const std::string &dir, const std::string &prefix) {
    if (!data) {
      return;
    }
    std::string mkdir_cmd = "mkdir -p " + dir;
    std::ignore = std::system(mkdir_cmd.c_str());

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string file_path = dir + "/" + prefix + "_" + std::to_string(now) + ".wav";
    std::ofstream out(file_path, std::ios::binary);
    if (!out.is_open()) {
      return;
    }

    int sample_rate = data->GetSampleRate();
    int channels = data->GetChannelNum();
    int data_len = data->GetBufferLen();
    const char *buffer = static_cast<const char *>(data->GetBuffer());

    int byte_rate = sample_rate * channels * 2;
    int block_align = channels * 2;
    int data_size = data_len;
    int file_size = 36 + data_size;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char *>(&file_size), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int fmt_chunk_size = 16;
    short audio_format = 1;
    out.write(reinterpret_cast<const char *>(&fmt_chunk_size), 4);
    out.write(reinterpret_cast<const char *>(&audio_format), 2);
    out.write(reinterpret_cast<const char *>(&channels), 2);
    out.write(reinterpret_cast<const char *>(&sample_rate), 4);
    out.write(reinterpret_cast<const char *>(&byte_rate), 4);
    out.write(reinterpret_cast<const char *>(&block_align), 2);
    short bits_per_sample = 16;
    out.write(reinterpret_cast<const char *>(&bits_per_sample), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char *>(&data_size), 4);
    out.write(buffer, data_size);
  }

  bool first_packet_logged_ = false;
  std::string out_dir_;
};
#endif

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);

  Args args;
  if (!ParseArgs(argc, argv, args)) {
    std::cerr << "[recorder] init_sdk FAIL missing_args" << std::endl;
    return 1;
  }

  std::string home_dir = "/tmp/meetai_home";
  std::string clean_cmd = "rm -rf " + home_dir + " && mkdir -p " + home_dir;
  std::ignore = std::system(clean_cmd.c_str());
  setenv("HOME", home_dir.c_str(), 1);

  const char *ld_path = std::getenv("LD_LIBRARY_PATH");
  std::cout << "[recorder] LD_LIBRARY_PATH=" << (ld_path ? ld_path : "") << std::endl;

  InitParam init_param;
  init_param.strWebDomain = "https://zoom.us";
  SDKError init_ret = InitSDK(init_param);
  LogSdkError("[recorder] init_sdk", init_ret);
  if (init_ret != SDKERR_SUCCESS) {
    return 2;
  }

  IAuthService *auth_service = nullptr;
  SDKError auth_service_ret = CreateAuthService(&auth_service);
  LogSdkError("[recorder] create_auth_service", auth_service_ret);
  if (auth_service_ret != SDKERR_SUCCESS || !auth_service) {
    return 3;
  }

  std::atomic<bool> authed{false};
  std::atomic<bool> auth_done{false};
  std::atomic<int> auth_ret_code{-1};
  AuthEventHandler auth_events(authed, auth_done, auth_ret_code);
  auth_service->SetEvent(&auth_events);
  std::cout << "[recorder] set_auth_event_handler OK ptr=" << &auth_events
            << std::endl;

  AuthContext auth_ctx;
  auth_ctx.jwt_token = args.signature.c_str();

  std::cout << "[recorder] auth_debug epoch=" << time(nullptr) << std::endl;
  std::string decoded_payload = DecodeJwtPayload(args.signature);
  if (!decoded_payload.empty()) {
    std::cout << "[recorder] auth_debug payload=" << decoded_payload << std::endl;
  }

  std::cout << "[recorder] auth_start" << std::endl;
  SDKError auth_ret = auth_service->SDKAuth(auth_ctx);
  std::cout << "[recorder] auth_call rc=" << static_cast<int>(auth_ret)
            << std::endl;
  if (auth_ret != SDKERR_SUCCESS) {
    return 4;
  }

  auto auth_start = std::chrono::steady_clock::now();
  while (!auth_done.load()) {
    app.processEvents(QEventLoop::AllEvents, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - auth_start;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 60) {
      std::cerr << "[recorder] auth timeout" << std::endl;
      CleanUPSDK();
      return 5;
    }
  }

  if (!authed.load()) {
    std::cerr << "[recorder] auth failed ret=" << auth_ret_code.load() << std::endl;
    CleanUPSDK();
    return 6;
  }

  std::cout << "[recorder] auth ok -> continue to join" << std::endl;

  IMeetingService *meeting_service = nullptr;
  SDKError meeting_ret = CreateMeetingService(&meeting_service);
  LogSdkError("[recorder] create_meeting_service", meeting_ret);
  if (meeting_ret != SDKERR_SUCCESS || !meeting_service) {
    CleanUPSDK();
    return 7;
  }

  auto meeting_events = std::make_unique<MeetingEventHandler>();
  meeting_service->SetEvent(meeting_events.get());

  JoinParam join_param;
  join_param.userType = ZOOMSDK::SDK_UT_WITHOUT_LOGIN;
  ZOOMSDK::JoinParam4WithoutLogin &join_without_login =
      join_param.param.withoutloginuserJoin;
  join_without_login.meetingNumber = std::stoull(args.meeting_id);
  if (!args.passcode.empty()) {
    join_without_login.psw = args.passcode.c_str();
  } else {
    join_without_login.psw = "";
  }
  join_without_login.userName = args.display_name.c_str();
  join_without_login.userZAK = "";

  std::cout << "[recorder] join_debug passcode_len=" << args.passcode.size()
            << std::endl;
  std::cout << "[recorder] join_start" << std::endl;
  SDKError join_ret = meeting_service->Join(join_param);
  LogSdkError("[recorder] join", join_ret);

  std::string mkdir_cmd =
      "mkdir -p " + args.out_dir + "/users " + args.out_dir + "/mixed";
  std::ignore = std::system(mkdir_cmd.c_str());
  WriteMetadata(args.out_dir, args.meeting_id);

  bool connect_only = (args.mode == "connect_only");
#if ZOOMSDK_HAS_RAW_AUDIO && defined(ENABLE_RAW_AUDIO)
  ZOOMSDK::IZoomSDKAudioRawDataHelper *audio_helper = nullptr;
  std::unique_ptr<AudioRawDelegate> audio_delegate;
  if (connect_only) {
    std::cout << "[recorder] connect_only mode: skipping raw audio subscribe"
              << std::endl;
  } else {
    audio_helper = ZOOMSDK::GetAudioRawdataHelper();
    if (!audio_helper) {
      std::cerr << "[recorder] subscribe_audio SKIPPED helper_unavailable" << std::endl;
    }
  }
#endif

  auto meeting_start = std::chrono::steady_clock::now();
  auto last_log = meeting_start;
  bool in_meeting = false;
  bool subscribed = false;
  while (true) {
    app.processEvents(QEventLoop::AllEvents, 50);

    int status_value = meeting_events->last_status.load();
    int result_value = meeting_events->last_result.load();

    if (status_value == static_cast<int>(MEETING_STATUS_INMEETING)) {
      if (!in_meeting) {
        std::cout << "[recorder] in_meeting" << std::endl;
        in_meeting = true;
      }
#if ZOOMSDK_HAS_RAW_AUDIO && defined(ENABLE_RAW_AUDIO)
      if (!subscribed && audio_helper && !connect_only) {
        audio_delegate = std::make_unique<AudioRawDelegate>(args.out_dir);
        SDKError sub_ret = audio_helper->subscribe(audio_delegate.get());
        LogSdkError("[recorder] subscribe_audio", sub_ret);
        subscribed = (sub_ret == SDKERR_SUCCESS);
      }
#endif
    } else if (status_value == static_cast<int>(MEETING_STATUS_IN_WAITING_ROOM)) {
      std::cout << "[recorder] waiting_room (host must admit Meet.Ai)" << std::endl;
    } else if (status_value == static_cast<int>(MEETING_STATUS_WAITINGFORHOST)) {
      std::cout << "[recorder] waiting_for_host" << std::endl;
    } else if (status_value == static_cast<int>(MEETING_STATUS_FAILED) ||
               status_value == static_cast<int>(MEETING_STATUS_ENDED)) {
      std::cerr << "[recorder] meeting_end_or_fail status=" << status_value
                << " result=" << result_value << std::endl;
      break;
    }

    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - meeting_start)
            .count();
    if (elapsed_seconds > 600) {
      std::cerr << "[recorder] timeout waiting/meeting elapsed=" << elapsed_seconds
                << "s" << std::endl;
      break;
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - last_log)
            .count() >= 2) {
      std::cout << "[recorder] still running, status=" << status_value
                << std::endl;
      last_log = std::chrono::steady_clock::now();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

#if ZOOMSDK_HAS_RAW_AUDIO && defined(ENABLE_RAW_AUDIO)
  if (audio_helper) {
    audio_helper->unSubscribe();
  }
#endif

  CleanUPSDK();
  std::cout << "[recorder] meeting_ended" << std::endl;
  return 0;
}
