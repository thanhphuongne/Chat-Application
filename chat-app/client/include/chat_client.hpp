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

    void start();
    bool connect_sync();
    bool login(const std::string& username, const std::string& password);
    bool register_acc(const std::string& username, const std::string& password);
    void send_message(const std::string& msg);
    void close();
    bool is_connected() const;

private:
    void do_connect();
    void do_read_header();
    void do_read_body(uint32_t body_length);
    void do_write();
    void start_reconnect_timer();

    void handle_server_message(const std::string& raw_msg);
    void handle_error(const boost::system::error_code& ec, const std::string& context);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer reconnect_timer_;
    
    std::string host_;
    std::string port_;
    std::atomic<bool> connected_;
    std::atomic<bool> reconnect_scheduled_;
    
    std::string username_;
    std::string password_;
    bool was_logged_in_;

    // Auth synchronization
    std::mutex auth_mutex_;
    std::condition_variable auth_cv_;
    bool auth_response_received_;
    bool auth_success_;
    std::string auth_error_msg_;

    uint8_t read_header_buf_[4];
    std::vector<char> read_body_buf_;

    std::queue<std::string> write_msgs_;
    std::mutex write_mutex_;
};

} // namespace chat
