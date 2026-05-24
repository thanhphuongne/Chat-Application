#include "user.hpp"
#include <utility>

namespace chat {

User::User() : username_("Anonymous") {}

User::User(std::string username) : username_(std::move(username)) {}

std::string User::get_username() const {
    return username_;
}

void User::set_username(const std::string& username) {
    username_ = username;
}

} // namespace chat
