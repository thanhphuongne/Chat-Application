#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace chat {
namespace protocol {

void write_uint32_be(uint8_t* buffer, uint32_t value);
uint32_t read_uint32_be(const uint8_t* buffer);

struct Message {
    std::string type;
    std::vector<std::string> args;
};

Message parse(const std::string& payload);
std::string encode(const std::string& type, const std::vector<std::string>& args = {});
std::string pack(const std::string& payload);

} // namespace protocol
} // namespace chat
