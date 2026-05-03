#include "chat_client.hpp"
#include "../../server/src/protocol.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <sstream>

void print_help() {
    std::cout << "\n==============================================" << std::endl;
    std::cout << "               AVAILABLE COMMANDS             " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "  /rooms                   - List active chat rooms" << std::endl;
    std::cout << "  /users                   - List online users" << std::endl;
    std::cout << "  /join <room>             - Join a chat room" << std::endl;
    std::cout << "  /leave <room>            - Leave a chat room" << std::endl;
    std::cout << "  /msg <room> <content>    - Send public message in room" << std::endl;
    std::cout << "  /pm <user> <content>     - Send private message to user" << std::endl;
    std::cout << "  /help                    - Display this help message" << std::endl;
    std::cout << "  /quit                    - Disconnect and exit" << std::endl;
    std::cout << "==============================================\n" << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        std::string host = "127.0.0.1";
        std::string port = "8080";

        if (argc > 1) {
            host = argv[1];
        }
        if (argc > 2) {
            port = argv[2];
        }

        std::cout << "==============================================" << std::endl;
        std::cout << "         Asynchronous Chat Client             " << std::endl;
        std::cout << "==============================================" << std::endl;

        boost::asio::io_context io_context;
        chat::chat_client client(io_context, host, port);

        if (!client.connect()) {
            std::cerr << "[CRITICAL] Could not connect to chat server." << std::endl;
            return 1;
        }

        // Run the Asio io_context loop in a background thread to handle async reads/writes
        std::thread io_thread([&io_context]() {
            io_context.run();
        });

        // Loop username login registration
        std::string username;
        bool logged_in = false;
        while (client.is_connected() && !logged_in) {
            std::cout << "Enter username to log in: ";
            std::getline(std::cin, username);
            if (username.empty()) continue;

            if (client.login(username)) {
                logged_in = true;
                std::cout << "[SYSTEM] Successfully logged in as " << username << "!" << std::endl;
            } else {
                std::cout << "[SYSTEM] Please choose a different username." << std::endl;
            }
        }

        if (logged_in) {
            print_help();

            std::string line;
            while (client.is_connected() && std::getline(std::cin, line)) {
                if (line.empty()) {
                    std::cout << "> " << std::flush;
                    continue;
                }

                if (line[0] == '/') {
                    // It's a command
                    std::istringstream iss(line);
                    std::string cmd;
                    iss >> cmd;

                    if (cmd == "/quit") {
                        break;
                    } 
                    else if (cmd == "/help") {
                        print_help();
                    } 
                    else if (cmd == "/rooms") {
                        client.send_message(chat::protocol::encode("LIST_ROOMS"));
                    } 
                    else if (cmd == "/users") {
                        client.send_message(chat::protocol::encode("LIST_USERS"));
                    } 
                    else if (cmd == "/join") {
                        std::string room;
                        iss >> room;
                        if (room.empty()) {
                            std::cout << "[SYSTEM] Usage: /join <room_name>" << std::endl;
                        } else {
                            client.send_message(chat::protocol::encode("JOIN", {room}));
                        }
                    } 
                    else if (cmd == "/leave") {
                        std::string room;
                        iss >> room;
                        if (room.empty()) {
                            std::cout << "[SYSTEM] Usage: /leave <room_name>" << std::endl;
                        } else {
                            client.send_message(chat::protocol::encode("LEAVE", {room}));
                        }
                    } 
                    else if (cmd == "/msg") {
                        std::string room;
                        iss >> room;
                        
                        std::string content;
                        std::getline(iss, content);
                        // Trim leading spaces from content
                        if (!content.empty() && content[0] == ' ') {
                            content = content.substr(1);
                        }

                        if (room.empty() || content.empty()) {
                            std::cout << "[SYSTEM] Usage: /msg <room_name> <message_content>" << std::endl;
                        } else {
                            client.send_message(chat::protocol::encode("MSG", {room, content}));
                        }
                    } 
                    else if (cmd == "/pm") {
                        std::string target_user;
                        iss >> target_user;
                        
                        std::string content;
                        std::getline(iss, content);
                        if (!content.empty() && content[0] == ' ') {
                            content = content.substr(1);
                        }

                        if (target_user.empty() || content.empty()) {
                            std::cout << "[SYSTEM] Usage: /pm <username> <message_content>" << std::endl;
                        } else {
                            client.send_message(chat::protocol::encode("PRIVATE", {target_user, content}));
                        }
                    } 
                    else {
                        std::cout << "[SYSTEM] Unknown command: " << cmd << ". Type /help to see command lists." << std::endl;
                    }
                } else {
                    std::cout << "[SYSTEM] Please use commands. Format: '/msg <room> <text>' or '/pm <user> <text>'. Type /help for assistance." << std::endl;
                }
                
                std::cout << "> " << std::flush;
            }
        }

        // Clean shutdown
        client.close();
        
        // Wait for IO thread to exit
        if (io_thread.joinable()) {
            io_thread.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL] Client Exception: " << e.what() << std::endl;
    }

    std::cout << "=== Chat Client Terminated ===" << std::endl;
    return 0;
}
