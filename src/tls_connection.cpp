#include "tls_connection.hpp"

#include <openssl/err.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace {

std::string opensslError(const std::string& prefix)
{
    const unsigned long code = ERR_get_error();
    if (code == 0)
        return prefix;

    std::array<char, 256> buffer{};
    ERR_error_string_n(code, buffer.data(), buffer.size());
    return prefix + ": " + buffer.data();
}

void closeSocket(socket_handle_t socket) noexcept
{
    if (socket == INVALID_SOCKET_HANDLE)
        return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

void setSocketTimeout(socket_handle_t socket)
{
#ifdef _WIN32
    const DWORD timeoutMs = 30000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

} // namespace

TlsConnection::~TlsConnection()
{
    close();
}

void TlsConnection::connectTo(const std::string& host, const std::string& port)
{
    close();

#ifdef _WIN32
    static const bool winsockReady = [] {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("Не удалось инициализировать WinSock");
        return true;
    }();
    (void)winsockReady;
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const int gaiResult = getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
    if (gaiResult != 0) {
#ifdef _WIN32
        throw std::runtime_error("Ошибка DNS/getaddrinfo: " + std::to_string(gaiResult));
#else
        throw std::runtime_error(std::string("Ошибка DNS/getaddrinfo: ") + gai_strerror(gaiResult));
#endif
    }

    std::string lastSocketError = "Не удалось подключиться к серверу";
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        socket_handle_t candidate = ::socket(address->ai_family,
                                             address->ai_socktype,
                                             address->ai_protocol);
        if (candidate == INVALID_SOCKET_HANDLE)
            continue;

        setSocketTimeout(candidate);
        if (::connect(candidate, address->ai_addr,
#ifdef _WIN32
                      static_cast<int>(address->ai_addrlen)
#else
                      address->ai_addrlen
#endif
                      ) == 0) {
            socket_ = candidate;
            break;
        }

#ifdef _WIN32
        lastSocketError = "Ошибка TCP connect: " + std::to_string(WSAGetLastError());
#else
        lastSocketError = "Ошибка TCP connect: " + std::string(std::strerror(errno));
#endif
        closeSocket(candidate);
    }
    freeaddrinfo(addresses);

    if (socket_ == INVALID_SOCKET_HANDLE)
        throw std::runtime_error(lastSocketError);

    OPENSSL_init_ssl(0, nullptr);
    context_ = SSL_CTX_new(TLS_client_method());
    if (!context_) {
        close();
        throw std::runtime_error(opensslError("SSL_CTX_new завершился ошибкой"));
    }

    SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION);
    SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(context_) != 1) {
        close();
        throw std::runtime_error(opensslError(
            "Не удалось загрузить доверенные корневые сертификаты. "
            "Настройте SSL_CERT_FILE/SSL_CERT_DIR"));
    }

    ssl_ = SSL_new(context_);
    if (!ssl_) {
        close();
        throw std::runtime_error(opensslError("SSL_new завершился ошибкой"));
    }

    if (SSL_set_tlsext_host_name(ssl_, host.c_str()) != 1 ||
        SSL_set1_host(ssl_, host.c_str()) != 1) {
        close();
        throw std::runtime_error(opensslError("Не удалось настроить проверку имени TLS-сервера"));
    }

    if (SSL_set_fd(ssl_, static_cast<int>(socket_)) != 1) {
        close();
        throw std::runtime_error(opensslError("SSL_set_fd завершился ошибкой"));
    }

    if (SSL_connect(ssl_) != 1) {
        const std::string error = opensslError("TLS handshake завершился ошибкой");
        close();
        throw std::runtime_error(error);
    }

    if (SSL_get_verify_result(ssl_) != X509_V_OK) {
        const long result = SSL_get_verify_result(ssl_);
        close();
        throw std::runtime_error("Сертификат TLS-сервера не прошёл проверку, код: " +
                                 std::to_string(result));
    }
}

void TlsConnection::writeAll(std::string_view data)
{
    if (!ssl_)
        throw std::runtime_error("TLS-соединение не установлено");

    std::size_t sent = 0;
    while (sent < data.size()) {
        const std::size_t remaining = data.size() - sent;
        const int chunk = static_cast<int>(remaining > static_cast<std::size_t>(INT_MAX)
                                               ? INT_MAX : remaining);
        const int result = SSL_write(ssl_, data.data() + sent, chunk);
        if (result <= 0)
            throw std::runtime_error(opensslError("Ошибка записи в TLS-соединение"));
        sent += static_cast<std::size_t>(result);
    }
}

std::string TlsConnection::readLine()
{
    if (!ssl_)
        throw std::runtime_error("TLS-соединение не установлено");

    constexpr std::size_t maxLineLength = 2 * 1024 * 1024;
    for (;;) {
        const std::size_t newline = pending_.find('\n');
        if (newline != std::string::npos) {
            std::string line = pending_.substr(0, newline);
            pending_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }

        if (pending_.size() > maxLineLength)
            throw std::runtime_error("Сервер прислал строку чрезмерной длины");

        std::array<char, 8192> buffer{};
        const int result = SSL_read(ssl_, buffer.data(), static_cast<int>(buffer.size()));
        if (result > 0) {
            pending_.append(buffer.data(), static_cast<std::size_t>(result));
            continue;
        }

        const int sslError = SSL_get_error(ssl_, result);
        if (sslError == SSL_ERROR_ZERO_RETURN)
            throw std::runtime_error("Сервер закрыл TLS-соединение");
        throw std::runtime_error(opensslError("Ошибка чтения из TLS-соединения"));
    }
}

void TlsConnection::close() noexcept
{
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (context_) {
        SSL_CTX_free(context_);
        context_ = nullptr;
    }
    closeSocket(socket_);
    socket_ = INVALID_SOCKET_HANDLE;
    pending_.clear();
}
