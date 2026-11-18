#include "mail_common.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#  include <conio.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

std::string base64Encode(const std::string& input)
{
    if (input.empty())
        return {};

    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Строка слишком велика для кодирования Base64");

    const std::size_t outputSize = 4 * ((input.size() + 2) / 3);
    std::string output(outputSize, '\0');
    const int encoded = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(output.data()),
        reinterpret_cast<const unsigned char*>(input.data()),
        static_cast<int>(input.size()));
    if (encoded < 0)
        throw std::runtime_error("Ошибка кодирования Base64");

    output.resize(static_cast<std::size_t>(encoded));
    return output;
}

std::string removeSpaces(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }), value.end());
    return value;
}

std::string readConsolePassword()
{
    std::cout << "Пароль приложения Google (ввод скрыт): " << std::flush;
    std::string password;

#ifdef _WIN32
    for (;;) {
        const int ch = _getch();
        if (ch == '\r' || ch == '\n')
            break;
        if (ch == 3) // Ctrl+C
            throw std::runtime_error("Ввод пароля прерван");
        if (ch == '\b') {
            if (!password.empty())
                password.pop_back();
            continue;
        }
        if (ch >= 32 && ch <= 126)
            password.push_back(static_cast<char>(ch));
    }
    std::cout << '\n';
#else
    termios oldState{};
    const bool canHide = (::isatty(STDIN_FILENO) == 1 &&
                          ::tcgetattr(STDIN_FILENO, &oldState) == 0);
    if (canHide) {
        termios newState = oldState;
        newState.c_lflag &= static_cast<tcflag_t>(~ECHO);
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &newState);
    }
    std::getline(std::cin, password);
    if (canHide) {
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldState);
        std::cout << '\n';
    }
#endif

    password = removeSpaces(std::move(password));
    if (password.empty())
        throw std::runtime_error("Пароль не должен быть пустым");
    return password;
}

std::string readMultilineBody()
{
    std::cout << "Введите текст письма. Для завершения введите точку (.) на отдельной строке.\n";
    std::string body;
    std::string line;
    bool first = true;

    while (std::getline(std::cin, line)) {
        if (line == ".")
            break;
        if (line.rfind("..", 0) == 0)
            line.erase(0, 1); // Позволяет отправить строку, начинающуюся с точки.
        if (!first)
            body += '\n';
        body += line;
        first = false;
    }
    return body;
}
