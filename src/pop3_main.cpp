#include "pop3_client.hpp"
#include "mail_common.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printMessages(const std::vector<PopMessageInfo>& messages)
{
    if (messages.empty()) {
        std::cout << "Почтовый ящик пуст (или POP3 не возвращает доступных сообщений).\n";
        return;
    }

    std::cout << "\nСписок сообщений:\n";
    for (const auto& item : messages) {
        std::cout << "  № " << item.number << " | размер: " << item.size << " байт\n";
    }
}

int readNumber(const std::string& prompt)
{
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    try {
        std::size_t used = 0;
        const int value = std::stoi(line, &used);
        if (used != line.size()) return -1;
        return value;
    }
    catch (...) {
        return -1;
    }
}

} // namespace

int main()
{
    try {
        std::ofstream logFile("pop3_2.log", std::ios::out | std::ios::trunc);
        if (!logFile)
            throw std::runtime_error("Не удалось открыть файл pop3_2.log для записи");

        const MailLogger logger = [&logFile](const std::string& line) {
            std::cout << line << '\n';
            logFile << line << '\n';
            logFile.flush();
        };

        std::string username;
        std::cout << "=== POP3-клиент Gmail ===\n";
        std::cout << "Адрес учётной записи Gmail: ";
        std::getline(std::cin, username);
        const std::string password = readConsolePassword();

        Pop3Client client(logger);
        auto messages = client.listMessages(username, password);
        printMessages(messages);

        for (;;) {
            std::cout << "\nДействия: 1 — обновить список, 2 — получить письмо, "
                         "3 — удалить письмо, 0 — выход\n";
            const int action = readNumber("Выберите действие: ");
            if (action == 0)
                break;
            if (action == 1) {
                messages = client.listMessages(username, password);
                printMessages(messages);
            }
            else if (action == 2) {
                const int number = readNumber("Номер сообщения для получения: ");
                if (number <= 0) {
                    std::cout << "Некорректный номер.\n";
                    continue;
                }
                const std::string message = client.retrieveMessage(username, password, number);
                std::cout << "\n========== СОДЕРЖИМОЕ ПИСЬМА №" << number
                          << " ==========\n" << message
                          << "\n========================================\n";
            }
            else if (action == 3) {
                const int number = readNumber("Номер сообщения для удаления: ");
                if (number <= 0) {
                    std::cout << "Некорректный номер.\n";
                    continue;
                }
                client.deleteMessage(username, password, number);
                messages = client.listMessages(username, password);
                printMessages(messages);
            }
            else {
                std::cout << "Неизвестная команда.\n";
            }
        }

        std::cout << "Завершение работы. Журнал POP3-сеансов: pop3_2.log\n";
    }
    catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
