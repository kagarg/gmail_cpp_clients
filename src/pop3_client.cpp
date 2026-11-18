#include "pop3_client.hpp"
#include "tls_connection.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string sendCommand(TlsConnection& connection,
                        const std::string& command,
                        const MailLogger& logger,
                        const std::string& logText = {})
{
    connection.writeAll(command + "\r\n");
    if (logger)
        logger("C: " + (logText.empty() ? command : logText));
    const std::string response = connection.readLine();
    if (logger) logger("S: " + response);
    return response;
}

void requireOk(const std::string& response, const std::string& stage)
{
    if (response.rfind("+OK", 0) != 0)
        throw std::runtime_error(stage + ": POP3-сервер вернул ошибку: " + response);
}

void login(TlsConnection& connection,
           const std::string& username,
           const std::string& appPassword,
           const MailLogger& logger)
{
    if (logger) logger("--- Подключение к pop.gmail.com:995 (TLS) ---");
    connection.connectTo("pop.gmail.com", "995");

    const std::string greeting = connection.readLine();
    if (logger) logger("S: " + greeting);
    requireOk(greeting, "Приветствие сервера");

    requireOk(sendCommand(connection, "USER " + username, logger), "USER");
    requireOk(sendCommand(connection, "PASS " + appPassword, logger,
                          "PASS <пароль приложения скрыт>"), "PASS");
}

std::vector<std::string> readMultiline(TlsConnection& connection,
                                       const MailLogger& logger,
                                       std::size_t maxBytes = 50 * 1024 * 1024)
{
    std::vector<std::string> lines;
    std::size_t totalBytes = 0;
    for (;;) {
        std::string line = connection.readLine();
        if (line == ".")
            break;
        if (line.rfind("..", 0) == 0)
            line.erase(0, 1); // POP3 dot-unstuffing.

        totalBytes += line.size() + 2;
        if (totalBytes > maxBytes)
            throw std::runtime_error("Ответ POP3 превышает установленный предел 50 МиБ");

        if (logger) logger("S: " + line);
        lines.push_back(std::move(line));
    }
    if (logger) logger("S: . [конец многострочного ответа]");
    return lines;
}

void quit(TlsConnection& connection, const MailLogger& logger)
{
    requireOk(sendCommand(connection, "QUIT", logger), "QUIT");
    if (logger) logger("--- POP3-сеанс завершён ---");
}

} // namespace

Pop3Client::Pop3Client(MailLogger logger)
    : logger_(std::move(logger))
{
}

void Pop3Client::log(const std::string& text) const
{
    if (logger_) logger_(text);
}

std::vector<PopMessageInfo> Pop3Client::listMessages(const std::string& username,
                                                     const std::string& appPassword)
{
    if (username.empty() || appPassword.empty() || username.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Укажите корректный логин и пароль");

    TlsConnection connection;
    login(connection, username, appPassword, logger_);

    const std::string response = sendCommand(connection, "LIST", logger_);
    requireOk(response, "LIST");
    const auto lines = readMultiline(connection, logger_);

    std::vector<PopMessageInfo> messages;
    for (const std::string& line : lines) {
        std::istringstream input(line);
        PopMessageInfo item;
        if (input >> item.number >> item.size)
            messages.push_back(item);
    }

    quit(connection, logger_);
    log("Получено сообщений в списке: " + std::to_string(messages.size()));
    return messages;
}

std::string Pop3Client::retrieveMessage(const std::string& username,
                                        const std::string& appPassword,
                                        int messageNumber)
{
    if (username.empty() || appPassword.empty() || username.find_first_of("\r\n") != std::string::npos || messageNumber <= 0)
        throw std::invalid_argument("Неверные параметры получения письма");

    TlsConnection connection;
    login(connection, username, appPassword, logger_);

    const std::string response = sendCommand(connection,
                                              "RETR " + std::to_string(messageNumber),
                                              logger_);
    requireOk(response, "RETR");
    const auto lines = readMultiline(connection, logger_);

    std::string message;
    for (const std::string& line : lines) {
        message += line;
        message += "\r\n";
    }

    quit(connection, logger_);
    return message;
}

void Pop3Client::deleteMessage(const std::string& username,
                               const std::string& appPassword,
                               int messageNumber)
{
    if (username.empty() || appPassword.empty() || username.find_first_of("\r\n") != std::string::npos || messageNumber <= 0)
        throw std::invalid_argument("Неверные параметры удаления письма");

    TlsConnection connection;
    login(connection, username, appPassword, logger_);

    requireOk(sendCommand(connection, "DELE " + std::to_string(messageNumber), logger_),
              "DELE");
    // POP3 выполняет фактическое удаление только после успешного QUIT.
    quit(connection, logger_);
    log("Сообщение №" + std::to_string(messageNumber) + " помечено на удаление и подтверждено QUIT.");
}
