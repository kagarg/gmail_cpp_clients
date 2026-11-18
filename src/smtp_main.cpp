#include "smtp_client.hpp"
#include "mail_common.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    try {
        std::ofstream logFile("smtp_1.log", std::ios::out | std::ios::trunc);
        if (!logFile)
            throw std::runtime_error("Не удалось открыть файл smtp_1.log для записи");

        const MailLogger logger = [&logFile](const std::string& line) {
            std::cout << line << '\n';
            logFile << line << '\n';
            logFile.flush();
        };

        std::string username;
        std::string from;
        std::string to;
        std::string subject;

        std::cout << "=== SMTP-клиент Gmail ===\n";
        std::cout << "Адрес учётной записи Gmail (логин): ";
        std::getline(std::cin, username);
        const std::string password = readConsolePassword();

        std::cout << "Адрес отправителя (обычно совпадает с логином Gmail): ";
        std::getline(std::cin, from);
        std::cout << "Адрес получателя: ";
        std::getline(std::cin, to);
        std::cout << "Тема письма: ";
        std::getline(std::cin, subject);
        const std::string body = readMultilineBody();

        SmtpClient client(logger);
        client.sendMail(username, password, from, to, subject, body);
        std::cout << "\nПисьмо успешно передано SMTP-серверу Gmail.\n";
        std::cout << "Журнал сеанса: smtp_1.log\n";
    }
    catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
