#include "chat_server.hpp"
#include "protocol.hpp"
#include <iostream>
#include <algorithm>

namespace chat {

chat_server::chat_server(boost::asio::io_context& io_context, short port)
    : io_context_(io_context),
      acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      db_("database") {
    std::cout << "[SERVER] Database and listen acceptor initialized on port " << port << "." << std::endl;
    do_accept();
}

void chat_server::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec) {
                boost::system::error_code endpoint_ec;
                auto remote_ep = socket.remote_endpoint(endpoint_ec);
                std::string client_ip = endpoint_ec ? "unknown" : remote_ep.address().to_string();
                unsigned short client_port = endpoint_ec ? 0 : remote_ep.port();
                
                std::cout << "[SERVER] New connection accepted from " << client_ip << ":" << client_port << std::endl;
                
                auto new_session = std::make_shared<chat_session>(std::move(socket), *this);
                anonymous_sessions_.insert(new_session);
                new_session->start();
            } else {
                std::cerr << "[SERVER] Accept error: " << ec.message() << std::endl;
            }
            do_accept();
        });
}

bool chat_server::register_user(const std::shared_ptr<chat_session>& session, const std::string& username) {
    if (online_users_.find(username) != online_users_.end() || username == "Anonymous") {
        return false;
    }
    anonymous_sessions_.erase(session);
    online_users_[username] = session;
    return true;
}

void chat_server::unregister_session(const std::shared_ptr<chat_session>& session) {
    anonymous_sessions_.erase(session);
    
    std::string username = session->get_username();
    auto user_it = online_users_.find(username);
    if (user_it != online_users_.end() && user_it->second == session) {
        online_users_.erase(user_it);
    }
    
    std::vector<std::string> rooms_to_clean;
    for (auto& pair : rooms_) {
        pair.second->leave(session);
        pair.second->deliver(protocol::encode("LEAVE_NOTIFY", {pair.first, username}));
        
        if (pair.second->empty()) {
            rooms_to_clean.push_back(pair.first);
        }
    }
    
    for (const auto& room_name : rooms_to_clean) {
        rooms_.erase(room_name);
        std::cout << "[SERVER] Deleted empty room: " << room_name << std::endl;
    }
}

void chat_server::join_room(const std::shared_ptr<chat_session>& session, const std::string& room_name) {
    auto it = rooms_.find(room_name);
    if (it == rooms_.end()) {
        auto new_room = std::make_shared<chat_room>(room_name);
        rooms_[room_name] = new_room;
        it = rooms_.find(room_name);
        std::cout << "[SERVER] Created new room: " << room_name << std::endl;
    }
    
    it->second->join(session);
    session->deliver(protocol::encode("ROOM_JOINED", {room_name}));
    it->second->deliver(protocol::encode("JOIN_NOTIFY", {room_name, session->get_username()}), session);
}

void chat_server::leave_room(const std::shared_ptr<chat_session>& session, const std::string& room_name) {
    auto it = rooms_.find(room_name);
    if (it != rooms_.end()) {
        it->second->leave(session);
        it->second->deliver(protocol::encode("LEAVE_NOTIFY", {room_name, session->get_username()}));
        session->deliver(protocol::encode("ROOM_LEFT", {room_name}));
        
        if (it->second->empty()) {
            rooms_.erase(it);
            std::cout << "[SERVER] Deleted empty room: " << room_name << std::endl;
        }
    }
}

void chat_server::deliver_to_room(const std::string& room_name, const std::string& msg, const std::shared_ptr<chat_session>& sender) {
    auto it = rooms_.find(room_name);
    if (it != rooms_.end()) {
        // Save to Database History
        db_.save_message(sender->get_username(), room_name, msg);

        // Broadcast to Room
        std::string formatted = protocol::encode("ROOM_MSG", {room_name, sender->get_username(), msg});
        it->second->deliver(formatted);
    } else {
        sender->deliver(protocol::encode("ERROR", {"Room " + room_name + " does not exist."}));
    }
}

bool chat_server::deliver_private(const std::string& sender_username, const std::string& target_username, const std::string& content) {
    auto target_it = online_users_.find(target_username);
    auto sender_it = online_users_.find(sender_username);
    
    // Save PM to Database History
    db_.save_message(sender_username, "PM:" + target_username, content);

    if (target_it != online_users_.end()) {
        std::string formatted_msg = protocol::encode("PRIVATE_MSG", {sender_username, content});
        target_it->second->deliver(formatted_msg);
        
        // Confirm to sender
        if (sender_it != online_users_.end() && sender_username != target_username) {
            sender_it->second->deliver(protocol::encode("PRIVATE_CONFIRM", {target_username, content}));
        }
        return true;
    }
    
    // Target is offline, but message is saved to history. 
    // We send confirm to sender but inform recipient is offline
    if (sender_it != online_users_.end()) {
        sender_it->second->deliver(protocol::encode("PRIVATE_CONFIRM", {target_username, content}));
        sender_it->second->deliver(protocol::encode("ERROR", {"User " + target_username + " is offline. Message saved to history."}));
    }
    return true;
}

std::vector<std::string> chat_server::get_rooms() const {
    std::vector<std::string> room_names;
    for (const auto& pair : rooms_) {
        room_names.push_back(pair.first);
    }
    return room_names;
}

std::vector<std::string> chat_server::get_users() const {
    std::vector<std::string> online_names;
    for (const auto& pair : online_users_) {
        online_names.push_back(pair.first);
    }
    return online_names;
}

// Database wrappers
bool chat_server::register_db_user(const std::string& username, const std::string& password) {
    return db_.register_user(username, password);
}

bool chat_server::authenticate_db_user(const std::string& username, const std::string& password) {
    return db_.authenticate_user(username, password);
}

bool chat_server::is_user_online(const std::string& username) const {
    return online_users_.find(username) != online_users_.end();
}

void chat_server::send_history_to_session(const std::shared_ptr<chat_session>& session, const std::string& target) {
    std::vector<DBMessage> history = db_.get_history(target);
    for (const auto& msg : history) {
        session->deliver(protocol::encode("HISTORY_MSG", {target, msg.sender, msg.content, msg.timestamp}));
    }
}

} // namespace chat
