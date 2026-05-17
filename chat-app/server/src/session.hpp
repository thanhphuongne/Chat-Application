#pragma once

#include "room.hpp"
#include <boost/asio.hpp>
#include <queue>
#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <atomic>

namespace chat {

class chat_server; // Forward declaration

class chat_session 
    : public chat_participant, 
      public std::enable_shared_from_this<chat_session> {
public:
    chat_session(boost::asio::ip::tcp::socket socket, chat_server& server);
    ~chat_session() override;
    
    void start();
    void deliver(const std::string& msg) override;
    std::string get_username() const override;

    boost::asio::ip::tcp::socket& socket();

private:
    void do_read_header();
    void do_read_body(uint32_t body_length);
    void do_write();
    
    // Heartbeat mechanism (Ping-Pong)
    void start_heartbeat();
    void send_ping();
    
    // Anti-spam mechanism (Rate Limiting)
    bool check_rate_limit();

    void handle_message(const std::string& raw_msg);
    void handle_error(const boost::system::error_code& ec, const std::string& context);

    boost::asio::ip::tcp::socket socket_;
    chat_server& server_;
    std::string username_;
    bool is_logged_in_;
    
    // Buffers for reading length-prefixed frames
    uint8_t read_header_buf_[4];
    std::vector<char> read_body_buf_;

    // Queue of packed frames to write to client
    std::queue<std::string> write_msgs_;

    // Heartbeat states
    boost::asio::steady_timer heartbeat_timer_;
    std::atomic<bool> is_alive_;
    bool is_stopped_;

    // Anti-spam states
    std::chrono::steady_clock::time_point rate_limit_start_;
    int message_count_in_window_;
    std::chrono::steady_clock::time_point silence_until_;
};

} // namespace chat
