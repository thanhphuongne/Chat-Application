#pragma once

#include <string>

namespace chat {

class User {
public:
    User();
    explicit User(std::string username);
    
    std::string get_username() const;
    void set_username(const std::string& username);

private:
    std::string username_;
};

} // namespace chat
