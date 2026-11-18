#pragma once

#include <functional>
#include <string>
#include <vector>

using MailLogger = std::function<void(const std::string&)>;

struct PopMessageInfo {
    int number = 0;
    long long size = 0;
};

std::string base64Encode(const std::string& input);
std::string readConsolePassword();
std::string readMultilineBody();
std::string removeSpaces(std::string value);
