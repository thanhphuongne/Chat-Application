#include "chat_client.hpp"
#include "../../server/include/protocol.hpp"
#include <iostream>

// ANSI escape codes for coloring console output
#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[1;31m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_WHITE   "\033[1;37m"

namespace chat {

chat_client::chat_client(boost::asio::io_context& io_context, std::string host, std::string port)
    : io_context_(io_context),
      resolver_(io_context),
      socket_(io_context),
      reconnect_timer_(io_context),
      host_(std::move(host)),
      port_(std::move(port)),
      connected_(false),
      reconnect_scheduled_(false),
      was_logged_in_(false),
      auth_response_received_(false),
      auth_success_(false) {}

chat_client::~chat_client() {
    close();
}

bool chat_client::is_connected() const {
    return connected_;
}

void chat_client::start() {
    do_read_header();
}

bool chat_client::connect_sync() {
    try {
        std::cout << "[CLIENT] Resolving address " << host_ << ":" << port_ << "..." << std::endl;
        auto endpoints = resolver_.resolve(host_, port_);
        
        std::cout << "[CLIENT] Connecting to server..." << std::endl;
        boost::asio::connect(socket_, endpoints);
        connected_ = true;
        std::cout << "[CLIENT] Connection established!" << std::endl;
        
        start();
        return true;
    } catch (const std::exception& e) {
        std::cerr << ANSI_RED << "[CLIENT] Connection failed: " << e.what() << ANSI_RESET << std::endl;
        connected_ = false;
        start_reconnect_timer();
        return false;
    }
}

void chat_client::do_connect() {
    if (connected_) return;

    reconnect_scheduled_ = false;
    std::cout << "[CLIENT] Attempting to reconnect..." << std::endl;

    auto endpoints = resolver_.resolve(host_, port_);
    auto self = this;
    boost::asio::async_connect(socket_, endpoints,
        [this](boost::system::error_code ec, const boost::asio::ip::tcp::endpoint& /*endpoint*/) {
            if (!ec) {
                std::cout << ANSI_GREEN << "\n[CLIENT] Reconnected successfully!" << ANSI_RESET << std::endl;
                std::cout << "> " << std::flush;
                connected_ = true;
                
                start();

                // Auto re-authenticate if previously logged in
                if (was_logged_in_) {
                    std::cout << "[CLIENT] Auto re-authenticating..." << std::endl;
                    std::string payload = protocol::encode("LOGIN", {username_, password_});
                    send_message(payload);
                }
            } else {
                connected_ = false;
                start_reconnect_timer();
            }
        });
}

void chat_client::start_reconnect_timer() {
    if (reconnect_scheduled_) return;
    reconnect_scheduled_ = true;

    std::cout << "[CLIENT] Retry connection in 5 seconds..." << std::endl;
    reconnect_timer_.expires_after(std::chrono::seconds(5));
    reconnect_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (!ec) {
            do_connect();
        }
    });
}

bool chat_client::login(const std::string& username, const std::string& password) {
    if (!connected_) return false;

    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        auth_response_received_ = false;
        auth_success_ = false;
    }

    username_ = username;
    password_ = password;

    std::string login_payload = protocol::encode("LOGIN", {username, password});
    send_message(login_payload);

    std::unique_lock<std::mutex> lock(auth_mutex_);
    auth_cv_.wait(lock, [this]() { return auth_response_received_; });

    if (auth_success_) {
        was_logged_in_ = true;
    }
    return auth_success_;
}

bool chat_client::register_acc(const std::string& username, const std::string& password) {
    if (!connected_) return false;

    {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        auth_response_received_ = false;
        auth_success_ = false;
    }

    std::string reg_payload = protocol::encode("REGISTER", {username, password});
    send_message(reg_payload);

    std::unique_lock<std::mutex> lock(auth_mutex_);
    auth_cv_.wait(lock, [this]() { return auth_response_received_; });

    return auth_success_;
}

void chat_client::send_message(const std::string& msg) {
    if (!connected_) return;

    std::string frame = protocol::pack(msg);
    
    std::lock_guard<std::mutex> lock(write_mutex_);
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push(frame);
    if (!write_in_progress) {
        do_write();
    }
}

void chat_client::close() {
    if (connected_) {
        connected_ = false;
        boost::system::error_code ec;
        
        std::string quit_payload = protocol::encode("QUIT");
        std::string frame = protocol::pack(quit_payload);
        boost::asio::write(socket_, boost::asio::buffer(frame), ec);

        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
    boost::system::error_code timer_ec;
    reconnect_timer_.cancel(timer_ec);
}

void chat_client::do_read_header() {
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_header_buf_, 4),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                uint32_t body_length = protocol::read_uint32_be(read_header_buf_);
                do_read_body(body_length);
            } else {
                handle_error(ec, "read header");
            }
        });
}

void chat_client::do_read_body(uint32_t body_length) {
    read_body_buf_.resize(body_length);
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_body_buf_.data(), body_length),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                std::string raw_msg(read_body_buf_.begin(), read_body_buf_.end());
                handle_server_message(raw_msg);
                do_read_header();
            } else {
                handle_error(ec, "read body");
            }
        });
}

void chat_client::do_write() {
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(write_mutex_);
                write_msgs_.pop();
                if (!write_msgs_.empty()) {
                    do_write();
                }
            } else {
                handle_error(ec, "write");
            }
        });
}

void chat_client::handle_server_message(const std::string& raw_msg) {
    protocol::Message parsed_msg = protocol::parse(raw_msg);

    // Auto PING-PONG responses
    if (parsed_msg.type == "PING") {
        send_message(protocol::encode("PONG"));
        return;
    }

    if (parsed_msg.type == "OK") {
        std::lock_guard<std::mutex> lock(auth_mutex_);
        auth_success_ = true;
        auth_response_received_ = true;
        auth_cv_.notify_all();
        return;
    } 
    else if (parsed_msg.type == "ERROR") {
        std::string code = parsed_msg.args.empty() ? "UNKNOWN" : parsed_msg.args[0];
        std::string desc = parsed_msg.args.size() < 2 ? "" : parsed_msg.args[1];
        
        // If it's a verification response block, trigger the CV lock
        if (code == "AUTH_FAILED" || code == "REG_FAILED" || code == "ALREADY_ONLINE") {
            std::lock_guard<std::mutex> lock(auth_mutex_);
            auth_success_ = false;
            auth_error_msg_ = desc;
            auth_response_received_ = true;
            auth_cv_.notify_all();
            
            std::cerr << ANSI_RED << "\n[ERROR] " << code << ": " << desc << ANSI_RESET << std::endl;
            std::cout << "> " << std::flush;
            return;
        }
    }

    // Clean console cursor lines
    std::cout << "\r";
    
    if (parsed_msg.type == "USER_JOIN") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << ANSI_YELLOW << "[SYSTEM] User " << parsed_msg.args[1] << " joined room: " << parsed_msg.args[0] << ANSI_RESET << std::endl;
        }
    } 
    else if (parsed_msg.type == "USER_LEAVE") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << ANSI_YELLOW << "[SYSTEM] User " << parsed_msg.args[1] << " left room: " << parsed_msg.args[0] << ANSI_RESET << std::endl;
        }
    } 
    else if (parsed_msg.type == "SYSTEM") {
        if (!parsed_msg.args.empty()) {
            std::cout << ANSI_YELLOW << "[SYSTEM] " << parsed_msg.args[0] << ANSI_RESET << std::endl;
        }
    } 
    else if (parsed_msg.type == "MSG") {
        // Format: MSG|room|from_user|content|timestamp
        if (parsed_msg.args.size() >= 4) {
            std::cout << ANSI_CYAN << "[" << parsed_msg.args[3] << "] [Room: " << parsed_msg.args[0] << "] " 
                      << parsed_msg.args[1] << ": " << parsed_msg.args[2] << ANSI_RESET << std::endl;
        }
    } 
    else if (parsed_msg.type == "PRIVATE") {
        // Format: PRIVATE|from_user|content|timestamp
        if (parsed_msg.args.size() >= 3) {
            std::cout << ANSI_GREEN << "[" << parsed_msg.args[2] << "] [Private] " 
                      << parsed_msg.args[0] << ": " << parsed_msg.args[1] << ANSI_RESET << std::endl;
        }
    } 
    else if (parsed_msg.type == "ROOM_LIST") {
        std::string list = parsed_msg.args.empty() ? "(none)" : parsed_msg.args[0];
        std::cout << ANSI_WHITE << "[ROOMS] Active rooms: " << list << ANSI_RESET << std::endl;
    } 
    else if (parsed_msg.type == "USER_LIST") {
        std::string list = parsed_msg.args.empty() ? "(none)" : parsed_msg.args[0];
        std::cout << ANSI_WHITE << "[USERS] Online users: " << list << ANSI_RESET << std::endl;
    } 
    else if (parsed_msg.type == "ERROR") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << ANSI_RED << "[ERROR] " << parsed_msg.args[0] << ": " << parsed_msg.args[1] << ANSI_RESET << std::endl;
        }
    } 
    else {
        std::cout << "[RAW SERVER]: " << raw_msg << std::endl;
    }

    std::cout << "> " << std::flush;
}

void chat_client::handle_error(const boost::system::error_code& ec, const std::string& context) {
    if (connected_) {
        if (ec != boost::asio::error::operation_aborted) {
            std::cerr << ANSI_RED << "\n[CLIENT] Connection error in context '" << context << "': " << ec.message() << ANSI_RESET << std::endl;
        }
        connected_ = false;
        boost::system::error_code socket_ec;
        socket_.close(socket_ec);
        
        std::cout << ANSI_YELLOW << "[CLIENT] Disconnected! Attempting auto-reconnect..." << ANSI_RESET << std::endl;
        std::cout << "> " << std::flush;
        start_reconnect_timer();
    }
}

} // namespace chat
