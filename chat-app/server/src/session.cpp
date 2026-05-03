#include "session.hpp"
#include "chat_server.hpp"
#include "protocol.hpp"
#include <iostream>

namespace chat {

chat_session::chat_session(boost::asio::ip::tcp::socket socket, chat_server& server)
    : socket_(std::move(socket)), server_(server), username_("Anonymous"), is_logged_in_(false) {
    std::cout << "[SESSION] Created session for anonymous connection." << std::endl;
}

chat_session::~chat_session() {
    std::cout << "[SESSION] Destroyed session for user: " << username_ << std::endl;
}

boost::asio::ip::tcp::socket& chat_session::socket() {
    return socket_;
}

std::string chat_session::get_username() const {
    return username_;
}

void chat_session::start() {
    do_read_header();
}

void chat_session::deliver(const std::string& msg) {
    bool write_in_progress = !write_msgs_.empty();
    // Pack message into length-prefix frame before sending
    write_msgs_.push(protocol::pack(msg));
    if (!write_in_progress) {
        do_write();
    }
}

void chat_session::do_read_header() {
    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_header_buf_, 4),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                uint32_t body_length = protocol::read_uint32_be(read_header_buf_);
                // Enforce buffer size limit (e.g., max 64KB per message to prevent memory flooding)
                if (body_length > 65536) {
                    std::cerr << "[WARNING] Received message too large (" << body_length << " bytes). Disconnecting client." << std::endl;
                    handle_error(ec, "message too large");
                    return;
                }
                do_read_body(body_length);
            } else {
                handle_error(ec, "read header");
            }
        });
}

void chat_session::do_read_body(uint32_t body_length) {
    auto self(shared_from_this());
    read_body_buf_.resize(body_length);
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_body_buf_.data(), body_length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                std::string raw_msg(read_body_buf_.begin(), read_body_buf_.end());
                handle_message(raw_msg);
                do_read_header();
            } else {
                handle_error(ec, "read body");
            }
        });
}

void chat_session::do_write() {
    auto self(shared_from_this());
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                write_msgs_.pop();
                if (!write_msgs_.empty()) {
                    do_write();
                }
            } else {
                handle_error(ec, "write");
            }
        });
}

void chat_session::handle_message(const std::string& raw_msg) {
    protocol::Message parsed_msg = protocol::parse(raw_msg);
    std::cout << "[DEBUG] Received type: " << parsed_msg.type << " (Args count: " << parsed_msg.args.size() << ") from: " << username_ << std::endl;

    if (parsed_msg.type == "LOGIN") {
        if (is_logged_in_) {
            deliver(protocol::encode("LOGIN_FAIL", {"Already logged in."}));
            return;
        }
        if (parsed_msg.args.empty() || parsed_msg.args[0].empty()) {
            deliver(protocol::encode("LOGIN_FAIL", {"Username cannot be empty."}));
            return;
        }
        
        std::string requested_username = parsed_msg.args[0];
        if (server_.register_user(shared_from_this(), requested_username)) {
            username_ = requested_username;
            is_logged_in_ = true;
            deliver(protocol::encode("LOGIN_SUCCESS", {username_}));
            std::cout << "[INFO] User registered successfully: " << username_ << std::endl;
        } else {
            deliver(protocol::encode("LOGIN_FAIL", {"Username already taken."}));
        }
        return;
    }

    // All other commands require authentication
    if (!is_logged_in_) {
        deliver(protocol::encode("ERROR", {"You must log in first."}));
        return;
    }

    if (parsed_msg.type == "JOIN") {
        if (parsed_msg.args.empty()) return;
        std::string room_name = parsed_msg.args[0];
        server_.join_room(shared_from_this(), room_name);
    } 
    else if (parsed_msg.type == "LEAVE") {
        if (parsed_msg.args.empty()) return;
        std::string room_name = parsed_msg.args[0];
        server_.leave_room(shared_from_this(), room_name);
    } 
    else if (parsed_msg.type == "MSG") {
        if (parsed_msg.args.size() < 2) return;
        std::string room_name = parsed_msg.args[0];
        std::string content = parsed_msg.args[1];
        server_.deliver_to_room(room_name, content, shared_from_this());
    } 
    else if (parsed_msg.type == "PRIVATE") {
        if (parsed_msg.args.size() < 2) return;
        std::string target_user = parsed_msg.args[0];
        std::string content = parsed_msg.args[1];
        if (!server_.deliver_private(username_, target_user, content)) {
            deliver(protocol::encode("ERROR", {"User " + target_user + " is not online."}));
        }
    } 
    else if (parsed_msg.type == "LIST_ROOMS") {
        std::vector<std::string> rooms = server_.get_rooms();
        std::string rooms_list = "";
        for (size_t i = 0; i < rooms.size(); ++i) {
            rooms_list += (i > 0 ? "," : "") + rooms[i];
        }
        deliver(protocol::encode("ROOMS", {rooms_list}));
    } 
    else if (parsed_msg.type == "LIST_USERS") {
        std::vector<std::string> users = server_.get_users();
        std::string users_list = "";
        for (size_t i = 0; i < users.size(); ++i) {
            users_list += (i > 0 ? "," : "") + users[i];
        }
        deliver(protocol::encode("USERS", {users_list}));
    } 
    else if (parsed_msg.type == "QUIT") {
        std::cout << "[INFO] User requested quit: " << username_ << std::endl;
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}

void chat_session::handle_error(const boost::system::error_code& ec, const std::string& context) {
    if (ec != boost::asio::error::operation_aborted) {
        std::cerr << "[INFO] Session error in context '" << context << "' for " << username_ 
                  << ": " << ec.message() << std::endl;
        // Unregister session from rooms and server maps
        server_.unregister_session(shared_from_this());
    }
}

} // namespace chat
