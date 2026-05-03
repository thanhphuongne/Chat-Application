#include "room.hpp"
#include <algorithm>
#include <iostream>

namespace chat {

chat_room::chat_room(std::string name) : name_(std::move(name)) {}

std::string chat_room::get_name() const {
    return name_;
}

void chat_room::join(chat_participant_ptr participant) {
    participants_.insert(participant);
    std::cout << "[ROOM: " << name_ << "] User " << participant->get_username() << " joined." << std::endl;
}

void chat_room::leave(chat_participant_ptr participant) {
    if (participants_.erase(participant) > 0) {
        std::cout << "[ROOM: " << name_ << "] User " << participant->get_username() << " left." << std::endl;
    }
}

void chat_room::deliver(const std::string& msg, const chat_participant_ptr& sender) {
    for (const auto& participant : participants_) {
        if (participant != sender) {
            participant->deliver(msg);
        }
    }
}

std::vector<std::string> chat_room::get_participants() const {
    std::vector<std::string> user_list;
    for (const auto& participant : participants_) {
        user_list.push_back(participant->get_username());
    }
    return user_list;
}

bool chat_room::empty() const {
    return participants_.empty();
}

} // namespace chat
