#pragma once

#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> headers;
};

struct HttpResponse {
  int status = 200;
  std::string body;
  std::string content_type = "application/json";
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
 public:
  void AddRoute(const std::string& method, const std::string& path, HttpHandler handler);
  bool Start(int port);
  void Stop();

 private:
  HttpResponse HandleRequest(const HttpRequest& request);
  void RunLoop(int port);

  std::map<std::string, HttpHandler> routes_;
  bool running_ = false;
  std::thread server_thread_;
};
