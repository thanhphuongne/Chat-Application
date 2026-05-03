#pragma once

#include "room.hpp"
#include <boost/asio.hpp>
#include <queue>
#include <string>
#include <memory>
#include <vector>

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
    void handle_message(const std::string& raw_msg);
    void handle_error(const boost::system::error_code& ec, const std::string& context);

    boost::asio::ip::tcp::socket socket_;
    chat_server& server_;
    std::string username_;
    bool is_logged_in_;
    
    // Buffer for reading length prefix (4 bytes)
    uint8_t read_header_buf_[4];
    // Buffer for reading body
    std::vector<char> read_body_buf_;

    // Queue of packed frames to write to client
    std::queue<std::string> write_msgs_;
};

} // namespace chat
