#include "chat_client.hpp"
#include "../../server/include/protocol.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <sstream>

#define ANSI_RESET   "\033[0m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_WHITE   "\033[1;37m"

void print_cli_help() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "                 CLIENT CONSOLE COMMANDS                " << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "  /rooms                       - List active chat rooms" << std::endl;
    std::cout << "  /users                       - List online users on server" << std::endl;
    std::cout << "  /join <room_name>            - Join a chat room" << std::endl;
    std::cout << "  /leave                       - Leave current chat rooms" << std::endl;
    std::cout << "  /pub <room_name> <content>   - Send message to a room" << std::endl;
    std::cout << "  /msg <username> <content>    - Send private message to user" << std::endl;
    std::cout << "  /help                        - Display this help list" << std::endl;
    std::cout << "  /quit                        - Disconnect and exit" << std::endl;
    std::cout << "========================================================\n" << std::endl;
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
        std::cout << "      Asynchronous C++ Chat Client Console     " << std::endl;
        std::cout << "==============================================" << std::endl;

        boost::asio::io_context io_context;
        chat::chat_client client(io_context, host, port);

        if (!client.connect_sync()) {
            std::cerr << "[CLIENT] Working in offline reconnect mode." << std::endl;
        }

        // Run the Asio event loop in a background thread
        std::thread io_thread([&io_context]() {
            io_context.run();
        });

        // Loop Authentication screen
        bool logged_in = false;
        while (!logged_in) {
            std::cout << "\nSelect action:\n[1] Log In\n[2] Register Account\n[3] Quit\nChoice: ";
            std::string choice;
            std::getline(std::cin, choice);

            if (choice == "3") {
                break;
            }

            if (choice != "1" && choice != "2") {
                std::cout << "[SYSTEM] Invalid selection!" << std::endl;
                continue;
            }

            std::string username, password;
            std::cout << "Enter username: ";
            std::getline(std::cin, username);
            std::cout << "Enter password: ";
            std::getline(std::cin, password);

            if (username.empty() || password.empty()) {
                std::cout << "[SYSTEM] Credentials cannot be empty!" << std::endl;
                continue;
            }

            if (choice == "1") {
                // Login
                if (client.login(username, password)) {
                    logged_in = true;
                    std::cout << ANSI_GREEN << "[SYSTEM] Login successful! Welcome " << username << "." << ANSI_RESET << std::endl;
                } else {
                    std::cout << "[SYSTEM] Login failed. Check credentials or connection." << std::endl;
                }
            } else {
                // Register
                if (client.register_acc(username, password)) {
                    std::cout << ANSI_GREEN << "[SYSTEM] Registration successful! You can now log in." << ANSI_RESET << std::endl;
                } else {
                    std::cout << "[SYSTEM] Registration failed. Username may be taken." << std::endl;
                }
            }
        }

        if (logged_in) {
            print_cli_help();

            std::string line;
            std::cout << "> " << std::flush;
            while (std::getline(std::cin, line)) {
                if (line.empty()) {
                    std::cout << "> " << std::flush;
                    continue;
                }

                if (line[0] == '/') {
                    std::istringstream iss(line);
                    std::string cmd;
                    iss >> cmd;

                    if (cmd == "/quit") {
                        break;
                    } 
                    else if (cmd == "/help") {
                        print_cli_help();
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
                        client.send_message(chat::protocol::encode("LEAVE"));
                    } 
                    else if (cmd == "/pub") {
                        std::string room;
                        iss >> room;
                        
                        std::string content;
                        std::getline(iss, content);
                        if (!content.empty() && content[0] == ' ') {
                            content = content.substr(1);
                        }

                        if (room.empty() || content.empty()) {
                            std::cout << "[SYSTEM] Usage: /pub <room_name> <message_content>" << std::endl;
                        } else {
                            client.send_message(chat::protocol::encode("MSG", {room, content}));
                        }
                    } 
                    else if (cmd == "/msg") {
                        std::string target_user;
                        iss >> target_user;
                        
                        std::string content;
                        std::getline(iss, content);
                        if (!content.empty() && content[0] == ' ') {
                            content = content.substr(1);
                        }

                        if (target_user.empty() || content.empty()) {
                            std::cout << "[SYSTEM] Usage: /msg <username> <message_content>" << std::endl;
                        } else {
                            client.send_message(chat::protocol::encode("PRIVATE", {target_user, content}));
                        }
                    } 
                    else {
                        std::cout << "[SYSTEM] Unknown command: " << cmd << ". Type /help for help." << std::endl;
                    }
                } else {
                    std::cout << "[SYSTEM] Please use commands. Type /help for assistance." << std::endl;
                }
                
                std::cout << "> " << std::flush;
            }
        }

        // Cleanup
        client.close();
        
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL] Client Exception: " << e.what() << std::endl;
    }

    std::cout << "=== Chat Client Terminated ===" << std::endl;
    return 0;
}
