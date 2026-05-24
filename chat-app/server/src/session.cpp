#include "session.hpp"
#include "chat_server.hpp"
#include "protocol.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

namespace chat {

chat_session::chat_session(boost::asio::ip::tcp::socket socket, chat_server& server)
    : socket_(std::move(socket)),
      server_(server),
      user_("Anonymous"),
      is_logged_in_(false),
      heartbeat_timer_(socket_.get_executor()),
      is_alive_(true),
      is_stopped_(false),
      rate_limit_start_(std::chrono::steady_clock::now()),
      message_count_in_window_(0),
      silence_until_(rate_limit_start_) {
    std::cout << "[SESSION] Created session for anonymous connection." << std::endl;
}

chat_session::~chat_session() {
    is_stopped_ = true;
    boost::system::error_code ec;
    heartbeat_timer_.cancel(ec);
    std::cout << "[SESSION] Destroyed session for user: " << user_.get_username() << std::endl;
}

boost::asio::ip::tcp::socket& chat_session::socket() {
    return socket_;
}

std::string chat_session::get_username() const {
    return user_.get_username();
}

void chat_session::start() {
    do_read_header();
    start_heartbeat();
}

void chat_session::deliver(const std::string& msg) {
    if (is_stopped_) return;
    
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push(protocol::pack(msg));
    if (!write_in_progress) {
        do_write();
    }
}

void chat_session::do_read_header() {
    if (is_stopped_) return;

    auto self(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_header_buf_, 4),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                uint32_t body_length = protocol::read_uint32_be(read_header_buf_);
                if (body_length > 65536) {
                    std::cerr << "[WARNING] Received message too large. Disconnecting client." << std::endl;
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
    if (is_stopped_) return;

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
    if (is_stopped_) return;

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

void chat_session::start_heartbeat() {
    if (is_stopped_) return;

    heartbeat_timer_.expires_after(std::chrono::seconds(15));
    
    auto self(shared_from_this());
    heartbeat_timer_.async_wait([this, self](const boost::system::error_code& ec) {
        if (ec || is_stopped_) return;

        if (!is_alive_) {
            std::cout << "[SERVER] Heartbeat timeout for user " << user_.get_username() << ". Disconnecting..." << std::endl;
            handle_error(boost::asio::error::connection_aborted, "heartbeat timeout");
            return;
        }

        is_alive_ = false;
        send_ping();
        start_heartbeat();
    });
}

void chat_session::send_ping() {
    // Send PING as text protocol frame
    deliver(protocol::encode("PING"));
}

bool chat_session::check_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    
    if (now < silence_until_) {
        return false;
    }

    if (now - rate_limit_start_ > std::chrono::seconds(1)) {
        rate_limit_start_ = now;
        message_count_in_window_ = 0;
    }

    message_count_in_window_++;
    if (message_count_in_window_ > 5) {
        silence_until_ = now + std::chrono::seconds(5);
        deliver(protocol::encode("ERROR", {"RATE_LIMIT_EXCEEDED", "You are sending messages too fast. Silenced for 5 seconds."}));
        std::cout << "[SERVER] User " << user_.get_username() << " silenced due to spamming." << std::endl;
        return false;
    }

    return true;
}

void chat_session::handle_message(const std::string& raw_msg) {
    is_alive_ = true;

    protocol::Message parsed_msg = protocol::parse(raw_msg);

    if (parsed_msg.type == "PONG") {
        return;
    }
    
    if (parsed_msg.type == "PING") {
        deliver(protocol::encode("PONG"));
        return;
    }

    if (parsed_msg.type == "REGISTER") {
        if (parsed_msg.args.size() < 2) {
            deliver(protocol::encode("ERROR", {"REG_FAILED", "Username and password cannot be empty."}));
            return;
        }
        std::string requested_username = parsed_msg.args[0];
        std::string password = parsed_msg.args[1];
        
        if (server_.register_db_user(requested_username, password)) {
            deliver(protocol::encode("OK", {"Account registered successfully."}));
        } else {
            deliver(protocol::encode("ERROR", {"REG_FAILED", "Username already exists."}));
        }
        return;
    }

    if (parsed_msg.type == "LOGIN") {
        if (is_logged_in_) {
            deliver(protocol::encode("ERROR", {"ALREADY_LOGGED_IN", "Already logged in."}));
            return;
        }
        if (parsed_msg.args.size() < 2 || parsed_msg.args[0].empty() || parsed_msg.args[1].empty()) {
            deliver(protocol::encode("ERROR", {"AUTH_FAILED", "Credentials cannot be empty."}));
            return;
        }
        
        std::string requested_username = parsed_msg.args[0];
        std::string password = parsed_msg.args[1];

        if (server_.is_user_online(requested_username)) {
            deliver(protocol::encode("ERROR", {"ALREADY_ONLINE", "User is already online."}));
            return;
        }

        if (server_.authenticate_db_user(requested_username, password)) {
            if (server_.register_user(shared_from_this(), requested_username)) {
                user_.set_username(requested_username);
                is_logged_in_ = true;
                deliver(protocol::encode("OK", {"Logged in successfully."}));
                std::cout << "[INFO] User authenticated: " << user_.get_username() << std::endl;
            } else {
                deliver(protocol::encode("ERROR", {"SERVER_ERROR", "Could not bind session."}));
            }
        } else {
            deliver(protocol::encode("ERROR", {"AUTH_FAILED", "Invalid username or password."}));
        }
        return;
    }

    if (!is_logged_in_) {
        deliver(protocol::encode("ERROR", {"UNAUTHORIZED", "You must log in first."}));
        return;
    }

    if (!check_rate_limit()) {
        return;
    }

    if (parsed_msg.type == "JOIN") {
        if (parsed_msg.args.empty()) return;
        std::string room_name = parsed_msg.args[0];
        server_.join_room(shared_from_this(), room_name);
    } 
    else if (parsed_msg.type == "LEAVE") {
        // Spec has 'LEAVE' without args. Server will find rooms client is in and remove them.
        server_.leave_all_rooms(shared_from_this());
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
        if (!server_.deliver_private(user_.get_username(), target_user, content)) {
            deliver(protocol::encode("ERROR", {"USER_OFFLINE", "User " + target_user + " is offline."}));
        }
    } 
    else if (parsed_msg.type == "LIST_ROOMS") {
        std::vector<std::string> rooms = server_.get_rooms();
        std::string rooms_list = "";
        for (size_t i = 0; i < rooms.size(); ++i) {
            rooms_list += (i > 0 ? "," : "") + rooms[i];
        }
        deliver(protocol::encode("ROOM_LIST", {rooms_list}));
    } 
    else if (parsed_msg.type == "LIST_USERS") {
        std::vector<std::string> users = server_.get_users();
        std::string users_list = "";
        for (size_t i = 0; i < users.size(); ++i) {
            users_list += (i > 0 ? "," : "") + users[i];
        }
        deliver(protocol::encode("USER_LIST", {users_list}));
    } 
    else if (parsed_msg.type == "GET_HISTORY") {
        if (parsed_msg.args.empty()) return;
        std::string target = parsed_msg.args[0];
        server_.send_history_to_session(shared_from_this(), target);
    }
    else if (parsed_msg.type == "QUIT") {
        std::cout << "[INFO] User requested quit: " << user_.get_username() << std::endl;
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}

void chat_session::handle_error(const boost::system::error_code& ec, const std::string& context) {
    if (!is_stopped_) {
        is_stopped_ = true;
        boost::system::error_code timer_ec;
        heartbeat_timer_.cancel(timer_ec);

        if (ec != boost::asio::error::operation_aborted) {
            std::cerr << "[INFO] Session error in context '" << context << "' for " << user_.get_username() 
                      << ": " << ec.message() << std::endl;
            server_.unregister_session(shared_from_this());
        }
    }
}

} // namespace chat
