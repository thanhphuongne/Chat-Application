#include "chat_client.hpp"
#include "../../server/src/protocol.hpp"
#include <iostream>

namespace chat {

chat_client::chat_client(boost::asio::io_context& io_context, std::string host, std::string port)
    : io_context_(io_context),
      resolver_(io_context),
      socket_(io_context),
      host_(std::move(host)),
      port_(std::move(port)),
      connected_(false),
      login_response_received_(false),
      login_success_(false) {}

chat_client::~chat_client() {
    close();
}

bool chat_client::is_connected() const {
    return connected_;
}

bool chat_client::connect() {
    try {
        std::cout << "[CLIENT] Resolving server address " << host_ << ":" << port_ << "..." << std::endl;
        auto endpoints = resolver_.resolve(host_, port_);
        
        std::cout << "[CLIENT] Connecting to server..." << std::endl;
        boost::asio::connect(socket_, endpoints);
        connected_ = true;
        std::cout << "[CLIENT] Connected successfully!" << std::endl;

        // Start async read loop
        start_io_loop();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[CLIENT] Connection error: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

bool chat_client::login(const std::string& username) {
    if (!connected_) return false;

    {
        std::lock_guard<std::mutex> lock(login_mutex_);
        login_response_received_ = false;
        login_success_ = false;
    }

    // Send login packet
    std::string login_payload = protocol::encode("LOGIN", {username});
    send_message(login_payload);

    // Wait for response from server (which will be processed in the async read loop)
    std::unique_lock<std::mutex> lock(login_mutex_);
    login_cv_.wait(lock, [this]() { return login_response_received_; });

    return login_success_;
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
        
        // Notify server we're leaving
        std::string quit_payload = protocol::encode("QUIT");
        std::string frame = protocol::pack(quit_payload);
        boost::asio::write(socket_, boost::asio::buffer(frame), ec);

        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
        std::cout << "[CLIENT] Disconnected from server." << std::endl;
    }
}

void chat_client::start_io_loop() {
    do_read_header();
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

    if (parsed_msg.type == "LOGIN_SUCCESS") {
        std::lock_guard<std::mutex> lock(login_mutex_);
        login_success_ = true;
        login_response_received_ = true;
        login_cv_.notify_all();
        return;
    } 
    else if (parsed_msg.type == "LOGIN_FAIL") {
        std::lock_guard<std::mutex> lock(login_mutex_);
        login_success_ = false;
        login_error_msg_ = parsed_msg.args.empty() ? "Unknown error" : parsed_msg.args[0];
        login_response_received_ = true;
        std::cerr << "\n[SYSTEM] Login failed: " << login_error_msg_ << std::endl;
        login_cv_.notify_all();
        return;
    }

    // Print to console nicely
    std::cout << "\r"; // Move cursor to start of line to overwrite the prompt prefix "> "
    
    if (parsed_msg.type == "ROOM_JOINED") {
        if (!parsed_msg.args.empty()) {
            std::cout << "[SYSTEM] You joined room: " << parsed_msg.args[0] << std::endl;
        }
    } 
    else if (parsed_msg.type == "ROOM_LEFT") {
        if (!parsed_msg.args.empty()) {
            std::cout << "[SYSTEM] You left room: " << parsed_msg.args[0] << std::endl;
        }
    } 
    else if (parsed_msg.type == "JOIN_NOTIFY") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << "[ROOM: " << parsed_msg.args[0] << "] User " << parsed_msg.args[1] << " has joined the room." << std::endl;
        }
    } 
    else if (parsed_msg.type == "LEAVE_NOTIFY") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << "[ROOM: " << parsed_msg.args[0] << "] User " << parsed_msg.args[1] << " has left the room." << std::endl;
        }
    } 
    else if (parsed_msg.type == "ROOM_MSG") {
        if (parsed_msg.args.size() >= 3) {
            std::cout << "[ROOM: " << parsed_msg.args[0] << "] " << parsed_msg.args[1] << ": " << parsed_msg.args[2] << std::endl;
        }
    } 
    else if (parsed_msg.type == "PRIVATE_MSG") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << "[PM from " << parsed_msg.args[0] << "]: " << parsed_msg.args[1] << std::endl;
        }
    } 
    else if (parsed_msg.type == "PRIVATE_CONFIRM") {
        if (parsed_msg.args.size() >= 2) {
            std::cout << "[PM to " << parsed_msg.args[0] << "]: " << parsed_msg.args[1] << std::endl;
        }
    } 
    else if (parsed_msg.type == "ROOMS") {
        std::string list = parsed_msg.args.empty() ? "(none)" : parsed_msg.args[0];
        std::cout << "[SYSTEM] Active rooms: " << list << std::endl;
    } 
    else if (parsed_msg.type == "USERS") {
        std::string list = parsed_msg.args.empty() ? "(none)" : parsed_msg.args[0];
        std::cout << "[SYSTEM] Online users: " << list << std::endl;
    } 
    else if (parsed_msg.type == "ERROR") {
        if (!parsed_msg.args.empty()) {
            std::cout << "[ERROR] " << parsed_msg.args[0] << std::endl;
        }
    } 
    else {
        // Unknown raw format
        std::cout << "[RAW SERVER]: " << raw_msg << std::endl;
    }

    std::cout << "> " << std::flush; // Restore prompt prefix
}

void chat_client::handle_error(const boost::system::error_code& ec, const std::string& context) {
    if (connected_) {
        if (ec != boost::asio::error::operation_aborted) {
            std::cerr << "\n[CLIENT] Connection error in context '" << context << "': " << ec.message() << std::endl;
        }
        connected_ = false;
        std::cout << "\n[CLIENT] Press Enter to exit." << std::endl;
    }
}

} // namespace chat
