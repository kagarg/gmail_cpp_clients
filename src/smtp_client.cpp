#include "smtp_client.hpp"
#include "tls_connection.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>
#include <stdexcept>
#include <vector>

namespace {

struct SmtpResponse {
    int code = 0;
    std::vector<std::string> lines;
};

SmtpResponse readResponse(TlsConnection& connection, const MailLogger& logger)
{
    SmtpResponse response;
    std::string line = connection.readLine();
    if (logger) logger("S: " + line);
    if (line.size() < 3 || !std::isdigit(static_cast<unsigned char>(line[0])) ||
        !std::isdigit(static_cast<unsigned char>(line[1])) ||
        !std::isdigit(static_cast<unsigned char>(line[2]))) {
        throw std::runtime_error("Некорректный ответ SMTP-сервера: " + line);
    }

    response.code = std::stoi(line.substr(0, 3));
    response.lines.push_back(line);

    while (line.size() >= 4 && line[3] == '-') {
        line = connection.readLine();
        if (logger) logger("S: " + line);
        response.lines.push_back(line);
        if (line.size() < 4 || line.substr(0, 3) != response.lines.front().substr(0, 3))
            throw std::runtime_error("Некорректный многострочный ответ SMTP-сервера");
    }
    return response;
}

SmtpResponse command(TlsConnection& connection,
                     const std::string& text,
                     const MailLogger& logger,
                     const std::string& logText = {})
{
    connection.writeAll(text + "\r\n");
    if (logger)
        logger("C: " + (logText.empty() ? text : logText));
    return readResponse(connection, logger);
}

void requireCode(const SmtpResponse& response,
                 std::initializer_list<int> expected,
                 const std::string& stage)
{
    if (std::find(expected.begin(), expected.end(), response.code) == expected.end()) {
        std::ostringstream message;
        message << stage << ": SMTP-сервер вернул код " << response.code;
        if (!response.lines.empty())
            message << " (" << response.lines.back() << ")";
        throw std::runtime_error(message.str());
    }
}

bool containsNewline(const std::string& value)
{
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

void validateAddress(const std::string& address, const std::string& fieldName)
{
    if (address.empty() || address.find('@') == std::string::npos ||
        address.find_first_of("<> ,\t\r\n") != std::string::npos)
        throw std::invalid_argument("Некорректный адрес в поле «" + fieldName + "»");
}

std::string currentRfc5322Date()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[64]{};
    if (std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S +0000", &utc) == 0)
        throw std::runtime_error("Не удалось сформировать заголовок Date");
    return buffer;
}

std::string makeMailData(const std::string& from,
                         const std::string& to,
                         const std::string& subject,
                         const std::string& body)
{
    std::string data;
    data += "Date: " + currentRfc5322Date() + "\r\n";
    data += "From: <" + from + ">\r\n";
    data += "To: <" + to + ">\r\n";
    data += "Subject: =?UTF-8?B?" + base64Encode(subject) + "?=\r\n";
    data += "MIME-Version: 1.0\r\n";
    data += "Content-Type: text/plain; charset=UTF-8\r\n";
    data += "Content-Transfer-Encoding: base64\r\n";
    data += "\r\n";

    const std::string encodedBody = base64Encode(body);
    for (std::size_t offset = 0; offset < encodedBody.size(); offset += 76) {
        data.append(encodedBody, offset, std::min<std::size_t>(76, encodedBody.size() - offset));
        data += "\r\n";
    }
    return data;
}

void sendData(TlsConnection& connection, const std::string& data, const MailLogger& logger)
{
    std::size_t pos = 0;
    while (pos < data.size()) {
        const std::size_t end = data.find("\r\n", pos);
        const std::size_t length = (end == std::string::npos) ? data.size() - pos : end - pos;
        std::string line = data.substr(pos, length);
        if (!line.empty() && line.front() == '.')
            line.insert(line.begin(), '.'); // SMTP dot-stuffing.

        connection.writeAll(line + "\r\n");
        if (logger) logger("C: " + line);

        if (end == std::string::npos)
            break;
        pos = end + 2;
    }

    connection.writeAll(".\r\n");
    if (logger) logger("C: . [конец DATA]");
}

} // namespace

SmtpClient::SmtpClient(MailLogger logger)
    : logger_(std::move(logger))
{
}

void SmtpClient::log(const std::string& text) const
{
    if (logger_) logger_(text);
}

void SmtpClient::sendMail(const std::string& username,
                          const std::string& appPassword,
                          const std::string& from,
                          const std::string& to,
                          const std::string& subject,
                          const std::string& body)
{
    if (username.empty() || appPassword.empty() || from.empty() || to.empty())
        throw std::invalid_argument("Логин, пароль, адрес отправителя и адрес получателя обязательны");
    if (containsNewline(username))
        throw std::invalid_argument("Логин не должен содержать перевод строки");
    validateAddress(from, "отправитель");
    validateAddress(to, "получатель");

    TlsConnection connection;
    log("--- Подключение к smtp.gmail.com:465 (TLS) ---");
    connection.connectTo("smtp.gmail.com", "465");

    const SmtpResponse greeting = readResponse(connection, logger_);
    requireCode(greeting, {220}, "Приветствие сервера");

    requireCode(command(connection, "EHLO localhost", logger_), {250}, "EHLO");
    requireCode(command(connection, "AUTH LOGIN", logger_), {334}, "AUTH LOGIN");
    requireCode(command(connection, base64Encode(username), logger_, "<данные авторизации скрыты>"),
                {334}, "Имя пользователя");
    requireCode(command(connection, base64Encode(appPassword), logger_, "<пароль приложения скрыт>"),
                {235}, "Пароль приложения");

    requireCode(command(connection, "MAIL FROM:<" + from + ">", logger_),
                {250}, "MAIL FROM");
    requireCode(command(connection, "RCPT TO:<" + to + ">", logger_),
                {250, 251}, "RCPT TO");
    requireCode(command(connection, "DATA", logger_), {354}, "DATA");

    sendData(connection, makeMailData(from, to, subject, body), logger_);
    requireCode(readResponse(connection, logger_), {250}, "Передача письма");

    const SmtpResponse quitResponse = command(connection, "QUIT", logger_);
    requireCode(quitResponse, {221}, "QUIT");
    log("--- SMTP-сеанс завершён ---");
}
