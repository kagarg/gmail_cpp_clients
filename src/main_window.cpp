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
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

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
            removeSpaces(toUtf8(passwordEdit_->text())), number);
        messageView_->setPlainText(fromUtf8(message));
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
