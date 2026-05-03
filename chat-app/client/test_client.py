import socket
import threading
import struct
import sys

def pack_msg(payload: str) -> bytes:
    """Pack payload into length-prefix frame: [4 byte length] + [payload]"""
    payload_bytes = payload.encode('utf-8')
    length = len(payload_bytes)
    return struct.pack('>I', length) + payload_bytes

def read_exact(sock, num_bytes) -> bytes:
    """Read exact number of bytes from socket, handling partial reads"""
    data = b''
    while len(data) < num_bytes:
        packet = sock.recv(num_bytes - len(data))
        if not packet:
            return None
        data += packet
    return data

def parse_and_format(raw_msg: str) -> str:
    """Parse raw server message split by '|' and return friendly visual text"""
    parts = raw_msg.strip().split('|')
    if not parts or not parts[0]:
        return raw_msg

    msg_type = parts[0]
    args = parts[1:]

    if msg_type == "LOGIN_SUCCESS":
        return f"[SYSTEM] Login successful! Welcome {args[0]}."
    elif msg_type == "LOGIN_FAIL":
        return f"[SYSTEM] Login failed: {args[0] if args else 'Unknown reason'}."
    elif msg_type == "ROOM_JOINED":
        return f"[SYSTEM] You joined room: {args[0]}."
    elif msg_type == "ROOM_LEFT":
        return f"[SYSTEM] You left room: {args[0]}."
    elif msg_type == "JOIN_NOTIFY":
        return f"[ROOM: {args[0]}] User {args[1]} has joined the room."
    elif msg_type == "LEAVE_NOTIFY":
        return f"[ROOM: {args[0]}] User {args[1]} has left the room."
    elif msg_type == "ROOM_MSG":
        return f"[ROOM: {args[0]}] {args[1]}: {args[2]}"
    elif msg_type == "PRIVATE_MSG":
        return f"[PM from {args[0]}]: {args[1]}"
    elif msg_type == "PRIVATE_CONFIRM":
        return f"[PM to {args[0]}]: {args[1]}"
    elif msg_type == "ROOMS":
        rooms = args[0] if args and args[0] else "(none)"
        return f"[SYSTEM] Active rooms: {rooms}"
    elif msg_type == "USERS":
        users = args[0] if args and args[0] else "(none)"
        return f"[SYSTEM] Online users: {users}"
    elif msg_type == "ERROR":
        return f"[ERROR] {args[0]}"
    else:
        return f"[SERVER]: {raw_msg}"

def receive_loop(sock, stop_event):
    """Thread function to continuously read length-prefixed messages from server"""
    while not stop_event.is_set():
        try:
            # Read 4-byte length prefix
            header = read_exact(sock, 4)
            if not header:
                print("\n[SYSTEM] Disconnected from server (connection closed by host).")
                stop_event.set()
                break
            
            length = struct.unpack('>I', header)[0]
            
            # Read payload of 'length' bytes
            payload = read_exact(sock, length)
            if not payload:
                print("\n[SYSTEM] Disconnected from server (incomplete payload).")
                stop_event.set()
                break
            
            raw_msg = payload.decode('utf-8')
            formatted_msg = parse_and_format(raw_msg)
            
            # Print message and restore console prompt
            print(f"\r{formatted_msg}")
            print("> ", end="", flush=True)
            
        except Exception as e:
            if not stop_event.is_set():
                print(f"\n[ERROR] Receiver thread error: {e}")
                stop_event.set()
            break

def print_help():
    print("\n==============================================")
    print("               AVAILABLE COMMANDS             ")
    print("==============================================")
    print("  /rooms                   - List active chat rooms")
    print("  /users                   - List online users")
    print("  /join <room>             - Join a chat room")
    print("  /leave <room>            - Leave a chat room")
    print("  /msg <room> <content>    - Send public message in room")
    print("  /pm <user> <content>     - Send private message to user")
    print("  /help                    - Display this help message")
    print("  /quit                    - Disconnect and exit")
    print("==============================================\n")

def main():
    host = "127.0.0.1"
    port = 8080

    print("==============================================")
    print("       Asynchronous Python Client Test        ")
    print("==============================================")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        print(f"[CLIENT] Connecting to {host}:{port}...")
        sock.connect((host, port))
        print("[CLIENT] Connected successfully!")
    except Exception as e:
        print(f"[CRITICAL] Connection failed: {e}")
        return

    stop_event = threading.Event()
    
    # Start receiver thread
    recv_thread = threading.Thread(target=receive_loop, args=(sock, stop_event), daemon=True)
    recv_thread.start()

    # Enter username loop
    username = ""
    while not stop_event.is_set() and not username:
        username = input("Enter username to log in: ").strip()
        if not username:
            continue
        
        # Send login command
        sock.sendall(pack_msg(f"LOGIN|{username}"))
        
        # Give the receiver thread a brief moment to catch and print login response
        # In a CLI interactive test, this is simple and sufficient
        import time
        time.sleep(0.5)

    if not stop_event.is_set():
        print_help()

    # Command loop
    try:
        while not stop_event.is_set():
            line = input("> ").strip()
            if not line:
                continue

            if line.startswith('/'):
                parts = line.split(' ', 1)
                cmd = parts[0]
                args_str = parts[1] if len(parts) > 1 else ""

                if cmd == "/quit":
                    sock.sendall(pack_msg("QUIT"))
                    stop_event.set()
                    break
                elif cmd == "/help":
                    print_help()
                elif cmd == "/rooms":
                    sock.sendall(pack_msg("LIST_ROOMS"))
                elif cmd == "/users":
                    sock.sendall(pack_msg("LIST_USERS"))
                elif cmd == "/join":
                    if not args_str:
                        print("[SYSTEM] Usage: /join <room_name>")
                    else:
                        sock.sendall(pack_msg(f"JOIN|{args_str}"))
                elif cmd == "/leave":
                    if not args_str:
                        print("[SYSTEM] Usage: /leave <room_name>")
                    else:
                        sock.sendall(pack_msg(f"LEAVE|{args_str}"))
                elif cmd == "/msg":
                    msg_parts = args_str.split(' ', 1)
                    if len(msg_parts) < 2 or not msg_parts[0] or not msg_parts[1]:
                        print("[SYSTEM] Usage: /msg <room_name> <message_content>")
                    else:
                        sock.sendall(pack_msg(f"MSG|{msg_parts[0]}|{msg_parts[1]}"))
                elif cmd == "/pm":
                    pm_parts = args_str.split(' ', 1)
                    if len(pm_parts) < 2 or not pm_parts[0] or not pm_parts[1]:
                        print("[SYSTEM] Usage: /pm <username> <message_content>")
                    else:
                        sock.sendall(pack_msg(f"PRIVATE|{pm_parts[0]}|{pm_parts[1]}"))
                else:
                    print(f"[SYSTEM] Unknown command: {cmd}. Type /help to see command lists.")
            else:
                print("[SYSTEM] Please use commands. Format: '/msg <room> <text>' or '/pm <user> <text>'. Type /help for assistance.")
    except KeyboardInterrupt:
        print("\n[CLIENT] Exiting...")
    finally:
        stop_event.set()
        sock.close()
        print("=== Chat Client Terminated ===")

if __name__ == "__main__":
    main()
