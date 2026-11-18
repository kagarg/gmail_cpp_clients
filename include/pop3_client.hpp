#pragma once

#include "mail_common.hpp"

#include <string>
#include <vector>

class Pop3Client {
public:
    explicit Pop3Client(MailLogger logger = {});

    std::vector<PopMessageInfo> listMessages(const std::string& username,
                                             const std::string& appPassword);
    std::string retrieveMessage(const std::string& username,
                                const std::string& appPassword,
                                int messageNumber,
                                bool logMessageContents = true);
    void deleteMessage(const std::string& username,
                       const std::string& appPassword,
                       int messageNumber);

private:
    MailLogger logger_;
    void log(const std::string& text) const;
};
