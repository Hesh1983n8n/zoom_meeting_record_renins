#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {
std::string ReadLine(int fd) {
  std::string line;
  char c;
  while (read(fd, &c, 1) == 1) {
    if (c == '\r') {
      read(fd, &c, 1);
      break;
    }
    line.push_back(c);
  }
  return line;
}
}

void HttpServer::AddRoute(const std::string& method, const std::string& path, HttpHandler handler) {
  routes_[method + " " + path] = std::move(handler);
}

bool HttpServer::Start(int port) {
  if (running_) {
    return false;
  }
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    last_error_code_ = errno;
    return false;
  }
  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    last_error_code_ = errno;
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  if (listen(server_fd_, 8) < 0) {
    last_error_code_ = errno;
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  running_ = true;
  server_thread_ = std::thread(&HttpServer::RunLoop, this);
  return true;
}

void HttpServer::Stop() {
  running_ = false;
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

HttpResponse HttpServer::HandleRequest(const HttpRequest& request) {
  if (request.method == "POST" && request.path == "/api/v1/join") {
    std::printf("[join] request received\n");
  }
  auto it = routes_.find(request.method + " " + request.path);
  if (it == routes_.end()) {
    return {404, "{\"error\":\"not_found\"}", "application/json"};
  }
  return it->second(request);
}

void HttpServer::RunLoop() {
  while (running_) {
    int client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      continue;
    }

    HttpRequest request;
    std::string request_line = ReadLine(client_fd);
    std::istringstream iss(request_line);
    iss >> request.method >> request.path;

    std::string header_line;
    size_t content_length = 0;
    while (true) {
      header_line = ReadLine(client_fd);
      if (header_line.empty()) {
        break;
      }
      auto pos = header_line.find(':');
      if (pos != std::string::npos) {
        std::string key = header_line.substr(0, pos);
        std::string value = header_line.substr(pos + 1);
        while (!value.empty() && value.front() == ' ') {
          value.erase(value.begin());
        }
        request.headers[key] = value;
        if (key == "Content-Length") {
          content_length = static_cast<size_t>(std::stoul(value));
        }
      }
    }

    if (content_length > 0) {
      request.body.resize(content_length);
      size_t read_total = 0;
      while (read_total < content_length) {
        ssize_t read_bytes = read(client_fd, request.body.data() + read_total, content_length - read_total);
        if (read_bytes <= 0) {
          break;
        }
        read_total += static_cast<size_t>(read_bytes);
      }
    }

    HttpResponse response = HandleRequest(request);
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << " OK\r\n";
    out << "Content-Type: " << response.content_type << "\r\n";
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "Connection: close\r\n\r\n";
    out << response.body;

    std::string response_str = out.str();
    send(client_fd, response_str.data(), response_str.size(), 0);
    close(client_fd);
  }
}
