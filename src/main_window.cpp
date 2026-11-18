#include "main_window.hpp"

#include "mail_common.hpp"
#include "pop3_client.hpp"
#include "smtp_client.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <map>
#include <exception>
#include <string>

namespace {

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

QByteArray decodeQuotedPrintable(const QByteArray& input)
{
    QByteArray output;
    output.reserve(input.size());
    for (qsizetype i = 0; i < input.size(); ++i) {
        if (input[i] == '=' && i + 1 < input.size()) {
            if (input[i + 1] == '\r' && i + 2 < input.size() && input[i + 2] == '\n') {
                i += 2;
                continue;
            }
            if (input[i + 1] == '\n') {
                ++i;
                continue;
            }
            if (i + 2 < input.size()) {
                bool ok = false;
                const int byte = input.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    output.append(static_cast<char>(byte));
                    i += 2;
                    continue;
                }
            }
        }
        output.append(input[i]);
    }
    return output;
}

QString decodeEncodedWords(const std::string& value)
{
    const QString input = fromUtf8(value);
    const QRegularExpression expression(QStringLiteral("=\\?([^?]+)\\?([bBqQ])\\?([^?]*)\\?="));
    QString output;
    qsizetype offset = 0;
    auto matches = expression.globalMatch(input);
    while (matches.hasNext()) {
        const auto match = matches.next();
        output += input.mid(offset, match.capturedStart() - offset);
        QByteArray bytes = match.captured(3).toLatin1();
        if (match.captured(2).compare(QStringLiteral("B"), Qt::CaseInsensitive) == 0) {
            bytes = QByteArray::fromBase64(bytes);
        } else {
            bytes.replace('_', ' ');
            bytes = decodeQuotedPrintable(bytes);
        }
        output += QString::fromUtf8(bytes);
        offset = match.capturedEnd();
    }
    output += input.mid(offset);
    return output;
}

std::pair<std::map<std::string, std::string>, QByteArray> splitMimeEntity(QByteArray entity)
{
    entity.replace("\r\n", "\n");
    const qsizetype separator = entity.indexOf("\n\n");
    const QByteArray headerBlock = separator < 0 ? QByteArray{} : entity.left(separator);
    const QByteArray body = separator < 0 ? entity : entity.mid(separator + 2);

    std::map<std::string, std::string> headers;
    const QList<QByteArray> lines = headerBlock.split('\n');
    std::string lastHeader;
    for (const QByteArray& line : lines) {
        if (!line.isEmpty() && (line[0] == ' ' || line[0] == '\t') && !lastHeader.empty()) {
            headers[lastHeader] += " " + line.trimmed().toStdString();
            continue;
        }
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) continue;
        lastHeader = lowerAscii(line.left(colon).trimmed().toStdString());
        headers[lastHeader] = line.mid(colon + 1).trimmed().toStdString();
    }
    return {std::move(headers), body};
}

std::string mimeParameter(const std::string& header, const std::string& name)
{
    const QRegularExpression expression(QStringLiteral("(?:^|;)\\s*") +
        QString::fromStdString(name) + QStringLiteral("\\s*=\\s*(?:\"([^\"]*)\"|([^;\\s]*))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(QString::fromStdString(header));
    if (!match.hasMatch()) return {};
    return (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toStdString();
}

QString extractTextPart(const QByteArray& entity, bool allowHtml = true)
{
    auto [headers, body] = splitMimeEntity(entity);
    const auto getHeader = [&headers](const std::string& name) {
        const auto it = headers.find(name);
        return it == headers.end() ? std::string{} : it->second;
    };
    const std::string contentType = lowerAscii(getHeader("content-type"));
    if (contentType.rfind("multipart/", 0) == 0) {
        const std::string boundary = mimeParameter(getHeader("content-type"), "boundary");
        if (boundary.empty()) return {};
        const QByteArray marker = "--" + QByteArray::fromStdString(boundary);
        QString htmlFallback;
        qsizetype start = 0;
        while ((start = body.indexOf(marker, start)) >= 0) {
            start += marker.size();
            if (body.mid(start, 2) == "--") break;
            if (body.mid(start, 2) == "\r\n") start += 2;
            else if (body.mid(start, 1) == "\n") ++start;
            qsizetype end = body.indexOf(marker, start);
            if (end < 0) end = body.size();
            QByteArray part = body.mid(start, end - start).trimmed();
            const QString text = extractTextPart(part, allowHtml);
            if (!text.isEmpty()) {
                const auto [partHeaders, ignoredBody] = splitMimeEntity(part);
                const auto typeIt = partHeaders.find("content-type");
                const std::string partType = typeIt == partHeaders.end() ? "text/plain" : lowerAscii(typeIt->second);
                if (partType.find("text/plain") != std::string::npos) return text;
                if (htmlFallback.isEmpty()) htmlFallback = text;
            }
            start = end;
        }
        return htmlFallback;
    }

    const bool isHtml = contentType.find("text/html") != std::string::npos;
    if (!contentType.empty() && contentType.find("text/") != 0) return {};
    if (isHtml && !allowHtml) return {};

    QByteArray decoded = body;
    const std::string encoding = lowerAscii(getHeader("content-transfer-encoding"));
    if (encoding.find("base64") != std::string::npos)
        decoded = QByteArray::fromBase64(decoded);
    else if (encoding.find("quoted-printable") != std::string::npos)
        decoded = decodeQuotedPrintable(decoded);
    QString text = QString::fromUtf8(decoded);
    if (isHtml) {
        QTextDocument document;
        document.setHtml(text);
        text = document.toPlainText();
    }
    return text.trimmed();
}

QString formatMessage(const std::string& rawMessage)
{
    const auto [headers, body] = splitMimeEntity(QByteArray::fromStdString(rawMessage));
    const auto subject = headers.find("subject");
    const QString decodedSubject = subject == headers.end()
        ? QStringLiteral("(без темы)") : decodeEncodedWords(subject->second).trimmed();
    QString text = extractTextPart(QByteArray::fromStdString(rawMessage));
    if (text.isEmpty()) text = QStringLiteral("(текстовая часть письма не найдена)");
    return QStringLiteral("Тема: %1\n\n%2").arg(decodedSubject, text);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Почтовый клиент Gmail — SMTP / POP3");
    resize(1100, 820);

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);

    auto* credentialsBox = new QWidget(central);
    auto* credentialsLayout = new QFormLayout(credentialsBox);
    accountEdit_ = new QLineEdit;
    accountEdit_->setPlaceholderText("example@gmail.com");
    passwordEdit_ = new QLineEdit;
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText("16-значный пароль приложения Google");
    credentialsLayout->addRow("Учётная запись Gmail:", accountEdit_);
    credentialsLayout->addRow("Пароль приложения:", passwordEdit_);
    mainLayout->addWidget(credentialsBox);

    auto* tabs = new QTabWidget(central);

    // Вкладка отправки.
    auto* sendPage = new QWidget(tabs);
    auto* sendLayout = new QVBoxLayout(sendPage);
    auto* sendForm = new QFormLayout;
    fromEdit_ = new QLineEdit;
    fromEdit_->setPlaceholderText("example@gmail.com");
    toEdit_ = new QLineEdit;
    toEdit_->setPlaceholderText("recipient@example.com");
    subjectEdit_ = new QLineEdit;
    bodyEdit_ = new QTextEdit;
    bodyEdit_->setPlaceholderText("Введите текст письма...");
    bodyEdit_->setMinimumHeight(150);
    sendForm->addRow("Отправитель:", fromEdit_);
    sendForm->addRow("Получатель:", toEdit_);
    sendForm->addRow("Тема:", subjectEdit_);
    sendForm->addRow("Текст письма:", bodyEdit_);
    sendLayout->addLayout(sendForm);

    auto* sendButton = new QPushButton("Отправить письмо");
    sendLayout->addWidget(sendButton);
    sendLayout->addWidget(new QLabel("Журнал SMTP-сеанса:"));
    smtpLogEdit_ = new QPlainTextEdit;
    smtpLogEdit_->setReadOnly(true);
    sendLayout->addWidget(smtpLogEdit_, 1);
    connect(sendButton, &QPushButton::clicked, this, [this] { sendMail(); });

    // Вкладка получения и управления письмами.
    auto* receivePage = new QWidget(tabs);
    auto* receiveLayout = new QVBoxLayout(receivePage);
    auto* buttonsLayout = new QHBoxLayout;
    auto* refreshButton = new QPushButton("Обновить список");
    auto* retrieveButton = new QPushButton("Получить выбранное");
    auto* deleteButton = new QPushButton("Удалить выбранное");
    buttonsLayout->addWidget(refreshButton);
    buttonsLayout->addWidget(retrieveButton);
    buttonsLayout->addWidget(deleteButton);
    buttonsLayout->addStretch();
    receiveLayout->addLayout(buttonsLayout);

    auto* splitter = new QSplitter(Qt::Horizontal);
    messageList_ = new QListWidget;
    messageView_ = new QTextEdit;
    messageView_->setReadOnly(true);
    messageView_->setPlaceholderText("Выберите письмо и нажмите «Получить выбранное».");
    splitter->addWidget(messageList_);
    splitter->addWidget(messageView_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    receiveLayout->addWidget(splitter, 2);
    receiveLayout->addWidget(new QLabel("Журнал POP3-сеанса:"));
    pop3LogEdit_ = new QPlainTextEdit;
    pop3LogEdit_->setReadOnly(true);
    receiveLayout->addWidget(pop3LogEdit_, 1);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshMessages(); });
    connect(retrieveButton, &QPushButton::clicked, this, [this] { retrieveSelectedMessage(); });
    connect(deleteButton, &QPushButton::clicked, this, [this] { deleteSelectedMessage(); });
    connect(messageList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { retrieveSelectedMessage(); });

    tabs->addTab(sendPage, "Отправка (SMTP)");
    tabs->addTab(receivePage, "Почтовый ящик (POP3)");
    mainLayout->addWidget(tabs, 1);

    setCentralWidget(central);
}

bool MainWindow::checkCredentials()
{
    if (accountEdit_->text().trimmed().isEmpty() || passwordEdit_->text().isEmpty()) {
        QMessageBox::warning(this, "Не заданы данные", "Введите адрес Gmail и пароль приложения.");
        return false;
    }
    return true;
}

void MainWindow::appendSmtpLog(const QString& line)
{
    smtpLogEdit_->appendPlainText(line);
}

void MainWindow::appendPop3Log(const QString& line)
{
    pop3LogEdit_->appendPlainText(line);
}

void MainWindow::sendMail()
{
    if (!checkCredentials()) return;
    if (fromEdit_->text().trimmed().isEmpty() || toEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Не заданы адреса", "Укажите отправителя и получателя.");
        return;
    }

    try {
        appendSmtpLog("\n========== НОВЫЙ SMTP-СЕАНС ==========");
        const MailLogger logger = [this](const std::string& line) {
            appendSmtpLog(fromUtf8(line));
        };
        SmtpClient client(logger);
        client.sendMail(toUtf8(accountEdit_->text().trimmed()),
                        removeSpaces(toUtf8(passwordEdit_->text())),
                        toUtf8(fromEdit_->text().trimmed()),
                        toUtf8(toEdit_->text().trimmed()),
                        toUtf8(subjectEdit_->text()),
                        toUtf8(bodyEdit_->toPlainText()));
        QMessageBox::information(this, "SMTP", "Письмо успешно передано серверу Gmail.");
    }
    catch (const std::exception& error) {
        const QString message = QString::fromUtf8(error.what());
        appendSmtpLog("ОШИБКА: " + message);
        QMessageBox::critical(this, "Ошибка отправки", message);
    }
}

void MainWindow::refreshMessages()
{
    if (!checkCredentials()) return;

    try {
        appendPop3Log("\n========== ОБНОВЛЕНИЕ СПИСКА ==========");
        const MailLogger logger = [this](const std::string& line) {
            appendPop3Log(fromUtf8(line));
        };
        Pop3Client client(logger);
        const auto messages = client.listMessages(toUtf8(accountEdit_->text().trimmed()),
                                                  removeSpaces(toUtf8(passwordEdit_->text())));

        messageList_->clear();
        for (const auto& message : messages) {
            auto* item = new QListWidgetItem(
                QString("Сообщение №%1 — %2 байт").arg(message.number).arg(message.size));
            item->setData(Qt::UserRole, message.number);
            messageList_->addItem(item);
        }
        if (messages.empty())
            messageView_->clear();
    }
    catch (const std::exception& error) {
        const QString message = QString::fromUtf8(error.what());
        appendPop3Log("ОШИБКА: " + message);
        QMessageBox::critical(this, "Ошибка POP3", message);
    }
}

void MainWindow::retrieveSelectedMessage()
{
    if (!checkCredentials()) return;
    auto* item = messageList_->currentItem();
    if (!item) {
        QMessageBox::information(this, "Письмо не выбрано", "Выберите сообщение в списке.");
        return;
    }

    try {
        const int number = item->data(Qt::UserRole).toInt();
        appendPop3Log(QString("\n========== ПОЛУЧЕНИЕ СООБЩЕНИЯ №%1 ==========").arg(number));
        const MailLogger logger = [this](const std::string& line) {
            appendPop3Log(fromUtf8(line));
        };
        Pop3Client client(logger);
        const std::string message = client.retrieveMessage(
            toUtf8(accountEdit_->text().trimmed()),
            removeSpaces(toUtf8(passwordEdit_->text())), number, false);
        messageView_->setPlainText(formatMessage(message));
    }
    catch (const std::exception& error) {
        const QString message = QString::fromUtf8(error.what());
        appendPop3Log("ОШИБКА: " + message);
        QMessageBox::critical(this, "Ошибка получения", message);
    }
}

void MainWindow::deleteSelectedMessage()
{
    if (!checkCredentials()) return;
    auto* item = messageList_->currentItem();
    if (!item) {
        QMessageBox::information(this, "Письмо не выбрано", "Выберите сообщение в списке.");
        return;
    }

    const int number = item->data(Qt::UserRole).toInt();
    const auto answer = QMessageBox::question(
        this, "Подтверждение удаления",
        QString("Удалить сообщение №%1 из почтового ящика?").arg(number));
    if (answer != QMessageBox::Yes) return;

    try {
        appendPop3Log(QString("\n========== УДАЛЕНИЕ СООБЩЕНИЯ №%1 ==========").arg(number));
        const MailLogger logger = [this](const std::string& line) {
            appendPop3Log(fromUtf8(line));
        };
        Pop3Client client(logger);
        client.deleteMessage(toUtf8(accountEdit_->text().trimmed()),
                             removeSpaces(toUtf8(passwordEdit_->text())), number);
        refreshMessages();
    }
    catch (const std::exception& error) {
        const QString message = QString::fromUtf8(error.what());
        appendPop3Log("ОШИБКА: " + message);
        QMessageBox::critical(this, "Ошибка удаления", message);
    }
}
