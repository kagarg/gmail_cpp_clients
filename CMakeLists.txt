cmake_minimum_required(VERSION 3.20)
project(GmailCppClients LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(mail_core
    src/tls_connection.cpp
    src/mail_common.cpp
    src/smtp_client.cpp
    src/pop3_client.cpp
)
target_include_directories(mail_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mail_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)

if(WIN32)
    target_link_libraries(mail_core PUBLIC ws2_32)
endif()

# Программа 1: консольный SMTP-клиент.
add_executable(smtp_client src/smtp_main.cpp)
target_link_libraries(smtp_client PRIVATE mail_core)

# Программа 2: консольный POP3-клиент.
add_executable(pop3_client src/pop3_main.cpp)
target_link_libraries(pop3_client PRIVATE mail_core)

# Программа 3: GUI-клиент. Консольные приложения можно собрать и без Qt.
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    add_executable(mail_gui
        src/gui_main.cpp
        src/main_window.cpp
        src/main_window.hpp
    )
    target_link_libraries(mail_gui PRIVATE mail_core Qt6::Widgets)
else()
    message(STATUS "Qt6 Widgets не найден: цель mail_gui пропущена; smtp_client и pop3_client доступны")
endif()

if(MSVC)
    target_compile_options(mail_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(mail_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
