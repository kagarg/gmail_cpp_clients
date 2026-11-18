#pragma once

#include <QMainWindow>

class QLineEdit;
class QTextEdit;
class QPlainTextEdit;
class QListWidget;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QLineEdit* accountEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;

    QLineEdit* fromEdit_ = nullptr;
    QLineEdit* toEdit_ = nullptr;
    QLineEdit* subjectEdit_ = nullptr;
    QTextEdit* bodyEdit_ = nullptr;
    QPlainTextEdit* smtpLogEdit_ = nullptr;

    QListWidget* messageList_ = nullptr;
    QTextEdit* messageView_ = nullptr;
    QPlainTextEdit* pop3LogEdit_ = nullptr;

    void sendMail();
    void refreshMessages();
    void retrieveSelectedMessage();
    void deleteSelectedMessage();
    void appendSmtpLog(const QString& line);
    void appendPop3Log(const QString& line);
    bool checkCredentials();
};
