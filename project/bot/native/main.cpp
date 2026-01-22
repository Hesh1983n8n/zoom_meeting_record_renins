#include <dlfcn.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

// Zoom Meeting SDK for Linux v6.7.2.7020 headers:
// - meeting_service_interface.h
// - rawdata/rawdata_audio_helper_interface.h
#include "meeting_service_interface.h"
#include "zoom_sdk.h"
#include "zoom_sdk_raw_data_def.h"

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

void LogSdkError(const std::string &label, SDKError code) {
  std::cout << label << " code=" << static_cast<int>(code)
            << " name=" << SDKErrorToString(code) << std::endl;
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
  void onMeetingStatusChanged(MeetingStatus status, int iResult) override {
    std::cout << "[recorder] meeting_status status=" << static_cast<int>(status)
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
  Args args;
  if (!ParseArgs(argc, argv, args)) {
    std::cerr << "[recorder] init_sdk FAIL missing_args" << std::endl;
    return 1;
  }

  const char *ld_path = std::getenv("LD_LIBRARY_PATH");
  std::cout << "[recorder] LD_LIBRARY_PATH=" << (ld_path ? ld_path : "") << std::endl;

  void *handle = dlopen("libzoomsdk.so", RTLD_NOW | RTLD_GLOBAL);
  if (!handle) {
    std::cerr << "[recorder] dlopen libzoomsdk.so FAIL " << dlerror() << std::endl;
  } else {
    std::cout << "[recorder] dlopen libzoomsdk.so OK" << std::endl;
    dlclose(handle);
  }

  InitParam init_param;
  init_param.strWebDomain = "https://zoom.us";
  SDKError init_ret = InitSDK(init_param);
  LogSdkError("[recorder] init_sdk", init_ret);
  if (init_ret != SDKERR_SUCCESS) {
    return 2;
  }

  IMeetingService *meeting_service = nullptr;
  SDKError meeting_ret = CreateMeetingService(&meeting_service);
  LogSdkError("[recorder] create_meeting_service", meeting_ret);
  if (meeting_ret != SDKERR_SUCCESS || !meeting_service) {
    return 3;
  }

  MeetingEventHandler meeting_events;
  meeting_service->SetEvent(&meeting_events);

  JoinParam join_param;
  join_param.userType = SDK_UT_WITHOUT_LOGIN;
  auto &join_without_login = join_param.param.withoutlogin;
  join_without_login.meetingNumber = std::stoull(args.meeting_id);
  join_without_login.psw = args.passcode.c_str();
  join_without_login.userName = args.display_name.c_str();
  join_without_login.userZAK = "";
  join_without_login.app_privilege_token = args.signature.c_str();

  std::cout << "[recorder] join_meeting start" << std::endl;
  SDKError join_ret = meeting_service->Join(join_param);
  LogSdkError("[recorder] join", join_ret);

#if ZOOMSDK_HAS_RAW_AUDIO && defined(ENABLE_RAW_AUDIO)
  IZoomSDKAudioRawDataHelper *audio_helper = nullptr;
#endif

#if ZOOMSDK_HAS_RAW_AUDIO && defined(ENABLE_RAW_AUDIO)
  if (!audio_helper) {
    std::cerr << "[recorder] subscribe_audio SKIPPED helper_unavailable" << std::endl;
  } else {
    AudioRawDelegate audio_delegate(args.out_dir);
    SDKError sub_ret = audio_helper->subscribe(&audio_delegate);
    LogSdkError("[recorder] subscribe_audio", sub_ret);
  }
#else
  std::cout << "[recorder] subscribe_audio SKIPPED raw_audio_disabled" << std::endl;
#endif

  std::string mkdir_cmd = "mkdir -p " + args.out_dir + "/users " + args.out_dir + "/mixed";
  std::ignore = std::system(mkdir_cmd.c_str());
  WriteMetadata(args.out_dir, args.meeting_id);

  std::this_thread::sleep_for(std::chrono::seconds(5));

#if ZOOMSDK_HAS_RAW_AUDIO && defined(ENABLE_RAW_AUDIO)
  if (audio_helper) {
    audio_helper->unSubscribe();
  }
#endif

  CleanUPSDK();
  std::cout << "[recorder] meeting_ended" << std::endl;
  return 0;
}
