#pragma once

#include "session.hpp"
#include "room.hpp"
#include "database.hpp"
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
    void leave_all_rooms(const std::shared_ptr<chat_session>& session);
    void deliver_to_room(const std::string& room_name, const std::string& msg, const std::shared_ptr<chat_session>& sender);
    bool deliver_private(const std::string& sender_username, const std::string& target_username, const std::string& content);
    
    std::vector<std::string> get_rooms() const;
    std::vector<std::string> get_users() const;

    // Database integrations
    bool register_db_user(const std::string& username, const std::string& password);
    bool authenticate_db_user(const std::string& username, const std::string& password);
    void send_history_to_session(const std::shared_ptr<chat_session>& session, const std::string& target);
    bool is_user_online(const std::string& username) const;

private:
    void do_accept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    
    std::map<std::string, std::shared_ptr<chat_room>> rooms_;
    std::map<std::string, std::shared_ptr<chat_session>> online_users_;
    std::set<std::shared_ptr<chat_session>> anonymous_sessions_;

    // Persistent Database
    Database db_;
};

} // namespace chat
