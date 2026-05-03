#pragma once

#include "session.hpp"
#include "room.hpp"
#include <boost/asio.hpp>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <vector>

namespace chat {

class chat_server {
public:
    chat_server(boost::asio::io_context& io_context, short port);

    // Client management & protocol actions
    bool register_user(const std::shared_ptr<chat_session>& session, const std::string& username);
    void unregister_session(const std::shared_ptr<chat_session>& session);
    
    void join_room(const std::shared_ptr<chat_session>& session, const std::string& room_name);
    void leave_room(const std::shared_ptr<chat_session>& session, const std::string& room_name);
    void deliver_to_room(const std::string& room_name, const std::string& msg, const std::shared_ptr<chat_session>& sender);
    bool deliver_private(const std::string& sender_username, const std::string& target_username, const std::string& content);
    
    std::vector<std::string> get_rooms() const;
    std::vector<std::string> get_users() const;

private:
    void do_accept();

    boost::asio::ip::tcp::acceptor acceptor_;
    
    // Rooms and user mapping (Note: Boost.Asio's single-threaded io_context runs 
    // synchronously on one thread, so mutexes are not strictly needed if we 
    // run io_context.run() on a single thread. However, we keep things clean and modular)
    std::map<std::string, std::shared_ptr<chat_room>> rooms_;
    std::map<std::string, std::shared_ptr<chat_session>> online_users_;
    std::set<std::shared_ptr<chat_session>> anonymous_sessions_;
};

} // namespace chat
