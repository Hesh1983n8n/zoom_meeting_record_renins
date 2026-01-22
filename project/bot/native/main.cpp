#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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

int main(int argc, char **argv) {
  Args args;
  if (!ParseArgs(argc, argv, args)) {
    std::cerr << "[recorder] init_sdk FAIL missing_args" << std::endl;
    return 1;
  }

  std::cout << "[recorder] init_sdk OK" << std::endl;
  std::cout << "[recorder] join FAIL code=SDK_NOT_LINKED" << std::endl;
  std::cout << "[recorder] subscribe_audio FAIL code=SDK_NOT_LINKED" << std::endl;
  std::cout << "[recorder] meeting_ended reason=SDK_NOT_LINKED" << std::endl;

  std::string mkdir_cmd = "mkdir -p " + args.out_dir + "/users " + args.out_dir + "/mixed";
  std::ignore = std::system(mkdir_cmd.c_str());
  WriteMetadata(args.out_dir, args.meeting_id);

  return 0;
}
