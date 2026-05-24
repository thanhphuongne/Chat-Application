#include "protocol.hpp"
#include <algorithm>

namespace chat {
namespace protocol {

void write_uint32_be(uint8_t* buffer, uint32_t value) {
    buffer[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    buffer[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[3] = static_cast<uint8_t>(value & 0xFF);
}

uint32_t read_uint32_be(const uint8_t* buffer) {
    return (static_cast<uint32_t>(buffer[0]) << 24) |
           (static_cast<uint32_t>(buffer[1]) << 16) |
           (static_cast<uint32_t>(buffer[2]) << 8)  |
           (static_cast<uint32_t>(buffer[3]));
}

Message parse(const std::string& payload) {
    Message msg;
    if (payload.empty()) {
        return msg;
    }

    std::string clean_payload = payload;
    clean_payload.erase(std::remove(clean_payload.begin(), clean_payload.end(), '\r'), clean_payload.end());
    clean_payload.erase(std::remove(clean_payload.begin(), clean_payload.end(), '\n'), clean_payload.end());

    std::string::size_type start = 0;
    std::string::size_type end = clean_payload.find('|');

    if (end == std::string::npos) {
        msg.type = clean_payload;
        return msg;
    }

    msg.type = clean_payload.substr(0, end);
    start = end + 1;

    while ((end = clean_payload.find('|', start)) != std::string::npos) {
        msg.args.push_back(clean_payload.substr(start, end - start));
        start = end + 1;
    }
    msg.args.push_back(clean_payload.substr(start));
    return msg;
}

std::string encode(const std::string& type, const std::vector<std::string>& args) {
    std::string payload = type;
    for (const auto& arg : args) {
        payload += "|" + arg;
    }
    return payload;
}

std::string pack(const std::string& payload) {
    uint32_t len = static_cast<uint32_t>(payload.length());
    std::string frame(4 + len, '\0');
    write_uint32_be(reinterpret_cast<uint8_t*>(&frame[0]), len);
    std::copy(payload.begin(), payload.end(), frame.begin() + 4);
    return frame;
}

} // namespace protocol
} // namespace chat
