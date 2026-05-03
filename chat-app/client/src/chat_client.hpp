#pragma once

#include <boost/asio.hpp>
#include <string>
#include <queue>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace chat {

class chat_client {
public:
    chat_client(boost::asio::io_context& io_context, std::string host, std::string port);
    ~chat_client();

    bool connect();
    bool login(const std::string& username);
    void send_message(const std::string& msg);
    void close();
    bool is_connected() const;

private:
    void start_io_loop();
    void do_read_header();
    void do_read_body(uint32_t body_length);
    void do_write();
    void handle_server_message(const std::string& raw_msg);
    void handle_error(const boost::system::error_code& ec, const std::string& context);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::string host_;
    std::string port_;
    std::atomic<bool> connected_;

    // Synchronization for login response
    std::mutex login_mutex_;
    std::condition_variable login_cv_;
    bool login_response_received_;
    bool login_success_;
    std::string login_error_msg_;

    // Buffer for reading length prefix (4 bytes)
    uint8_t read_header_buf_[4];
    // Buffer for reading body
    std::vector<char> read_body_buf_;

    // Queue of packed frames to write to server
    std::queue<std::string> write_msgs_;
    std::mutex write_mutex_;
};

} // namespace chat
