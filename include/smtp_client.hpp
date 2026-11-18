#pragma once

#include "mail_common.hpp"

#include <string>

class SmtpClient {
public:
    explicit SmtpClient(MailLogger logger = {});

    // Отправляет обычное текстовое письмо через smtp.gmail.com:465 (implicit TLS).
    void sendMail(const std::string& username,
                  const std::string& appPassword,
                  const std::string& from,
                  const std::string& to,
                  const std::string& subject,
                  const std::string& body);

private:
    MailLogger logger_;
    void log(const std::string& text) const;
};
