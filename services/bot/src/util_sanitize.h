#pragma once

#include <string>

std::string SanitizeFilename(const std::string& input, size_t max_len = 64);
