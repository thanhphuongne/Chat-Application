#pragma once

#include <string>
#include <set>
#include <memory>
#include <vector>

namespace chat {

class chat_participant {
public:
    virtual ~chat_participant() {}
    virtual void deliver(const std::string& msg) = 0;
    virtual std::string get_username() const = 0;
};

using chat_participant_ptr = std::shared_ptr<chat_participant>;

class chat_room {
public:
    explicit chat_room(std::string name);
    std::string get_name() const;
    void join(chat_participant_ptr participant);
    void leave(chat_participant_ptr participant);
    void deliver(const std::string& msg, const chat_participant_ptr& sender = nullptr);
    std::vector<std::string> get_participants() const;
    bool empty() const;

private:
    std::string name_;
    std::set<chat_participant_ptr> participants_;
};

} // namespace chat
