#pragma once

#include <string>
#include <string_view>

#include <openssl/ssl.h>

#ifdef _WIN32
#  include <winsock2.h>
   using socket_handle_t = SOCKET;
#  define INVALID_SOCKET_HANDLE INVALID_SOCKET
#else
   using socket_handle_t = int;
#  define INVALID_SOCKET_HANDLE (-1)
#endif

// TCP-соединение с неявным TLS (implicit SSL/TLS).
// Проверяются цепочка сертификатов и соответствие имени хоста сертификату.
class TlsConnection {
public:
    TlsConnection() = default;
    ~TlsConnection();

    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;

    void connectTo(const std::string& host, const std::string& port);
    void writeAll(std::string_view data);
    std::string readLine(); // Читает одну строку, завершающуюся LF; CRLF удаляется.
    void close() noexcept;

private:
    socket_handle_t socket_ = INVALID_SOCKET_HANDLE;
    SSL_CTX* context_ = nullptr;
    SSL* ssl_ = nullptr;
    std::string pending_;
};
