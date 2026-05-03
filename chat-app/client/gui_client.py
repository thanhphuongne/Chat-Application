import tkinter as tk
from tkinter import ttk, messagebox
import socket
import threading
import struct
import queue
import sys

# Color Palette (Modern Dark Theme)
BG_COLOR = "#121212"       # Dark background
SIDEBAR_COLOR = "#1f1f1f"  # Sidebar grey
CHAT_BG = "#181818"       # Chat area dark
ACCENT_COLOR = "#00adb5"   # Cyan highlight
TEXT_COLOR = "#eeeeee"     # Primary text
MUTED_TEXT = "#8c8c8c"     # Secondary/Muted text
BUBBLE_OTHER = "#2d2d2d"   # Bubble for other users
BUBBLE_ME = "#0f4c5c"      # Bubble for current user
SYSTEM_COLOR = "#393e46"   # System notification box

class ChatGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("CppChat - Real-time Client")
        self.root.geometry("900x600")
        self.root.configure(bg=BG_COLOR)

        self.sock = None
        self.username = ""
        self.stop_event = threading.Event()
        self.msg_queue = queue.Queue()

        # Chat history mapping: { "room_name": "history text...", "PM:username": "history text..." }
        self.chat_histories = {}
        self.current_target = ""  # Active room or user PM (e.g. "lobby" or "PM:Alice")

        # Set up modern styles
        self.setup_styles()

        # Initial login screen
        self.show_login_screen()

    def setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('.', bg=BG_COLOR, fg=TEXT_COLOR, font=("Segoe UI", 10))
        style.configure('TFrame', background=BG_COLOR)
        style.configure('Sidebar.TFrame', background=SIDEBAR_COLOR)
        style.configure('TLabel', background=BG_COLOR, foreground=TEXT_COLOR)
        style.configure('Sidebar.TLabel', background=SIDEBAR_COLOR, foreground=TEXT_COLOR)
        style.configure('Header.TLabel', background=BG_COLOR, foreground=ACCENT_COLOR, font=("Segoe UI", 14, "bold"))
        
        style.configure('TButton', background=ACCENT_COLOR, foreground="#ffffff", borderwidth=0, font=("Segoe UI", 10, "bold"))
        style.map('TButton', background=[('active', '#007f87')])

        style.configure('TEntry', fieldbackground="#2d2d2d", foreground=TEXT_COLOR, insertcolor=TEXT_COLOR, borderwidth=0)
        style.configure('Sidebar.TButton', background="#393e46", foreground=TEXT_COLOR, font=("Segoe UI", 9))
        style.map('Sidebar.TButton', background=[('active', '#2d2d2d')])

    def pack_msg(self, payload: str) -> bytes:
        payload_bytes = payload.encode('utf-8')
        return struct.pack('>I', len(payload_bytes)) + payload_bytes

    def read_exact(self, num_bytes) -> bytes:
        data = b''
        while len(data) < num_bytes:
            packet = self.sock.recv(num_bytes - len(data))
            if not packet:
                return None
            data += packet
        return data

    def show_login_screen(self):
        self.login_frame = ttk.Frame(self.root)
        self.login_frame.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

        title_lbl = ttk.Label(self.login_frame, text="CONNECT TO CPPCHAT", style="Header.TLabel")
        title_lbl.pack(pady=20)

        # Host Entry
        ttk.Label(self.login_frame, text="Server Host:").pack(anchor=tk.W, pady=2)
        self.host_entry = ttk.Entry(self.login_frame, width=30)
        self.host_entry.insert(0, "127.0.0.1")
        self.host_entry.pack(pady=5)

        # Port Entry
        ttk.Label(self.login_frame, text="Server Port:").pack(anchor=tk.W, pady=2)
        self.port_entry = ttk.Entry(self.login_frame, width=30)
        self.port_entry.insert(0, "8080")
        self.port_entry.pack(pady=5)

        # Username Entry
        ttk.Label(self.login_frame, text="Username:").pack(anchor=tk.W, pady=2)
        self.user_entry = ttk.Entry(self.login_frame, width=30)
        self.user_entry.pack(pady=5)
        self.user_entry.focus()

        self.connect_btn = ttk.Button(self.login_frame, text="Connect & Login", command=self.attempt_login)
        self.connect_btn.pack(pady=25, fill=tk.X)
        
        self.root.bind('<Return>', lambda e: self.attempt_login())

    def attempt_login(self):
        host = self.host_entry.get().strip()
        port_str = self.port_entry.get().strip()
        username = self.user_entry.get().strip()

        if not host or not port_str or not username:
            messagebox.showerror("Error", "All fields are required!")
            return

        try:
            port = int(port_str)
        except ValueError:
            messagebox.showerror("Error", "Port must be an integer!")
            return

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.sock.connect((host, port))
        except Exception as e:
            messagebox.showerror("Connection Failed", f"Could not connect to {host}:{port}\nError: {e}")
            return

        # Send login command
        try:
            self.sock.sendall(self.pack_msg(f"LOGIN|{username}"))
            
            # Read response synchronously first to verify login
            header = self.read_exact(4)
            if not header:
                raise Exception("Server closed connection.")
            length = struct.unpack('>I', header)[0]
            payload = self.read_exact(length).decode('utf-8')
            
            parts = payload.split('|')
            if parts[0] == "LOGIN_SUCCESS":
                self.username = username
                self.login_frame.destroy()
                self.build_chat_interface()
                
                # Start background receiver thread
                self.root.unbind('<Return>')
                self.receiver_thread = threading.Thread(target=self.receive_loop, daemon=True)
                self.receiver_thread.start()
                
                # Periodically process queue messages in the main GUI thread
                self.root.after(100, self.process_queue)
            else:
                reason = parts[1] if len(parts) > 1 else "Unknown error"
                messagebox.showerror("Login Failed", f"Server rejected username:\n{reason}")
                self.sock.close()
        except Exception as e:
            messagebox.showerror("Error", f"Failed during login negotiation:\n{e}")
            self.sock.close()

    def receive_loop(self):
        while not self.stop_event.is_set():
            try:
                header = self.read_exact(4)
                if not header:
                    self.msg_queue.put(("DISCONNECT", "Connection closed by server."))
                    break
                length = struct.unpack('>I', header)[0]
                payload = self.read_exact(length)
                if not payload:
                    self.msg_queue.put(("DISCONNECT", "Server connection terminated."))
                    break
                
                raw_msg = payload.decode('utf-8')
                self.msg_queue.put(("MSG", raw_msg))
            except Exception as e:
                if not self.stop_event.is_set():
                    self.msg_queue.put(("DISCONNECT", f"Connection error: {e}"))
                break

    def process_queue(self):
        try:
            while True:
                msg_type, content = self.msg_queue.get_nowait()
                if msg_type == "DISCONNECT":
                    messagebox.showwarning("Disconnected", content)
                    self.root.destroy()
                    sys.exit(0)
                elif msg_type == "MSG":
                    self.handle_incoming_message(content)
        except queue.Empty:
            pass
        self.root.after(100, self.process_queue)

    def handle_incoming_message(self, raw_msg):
        parts = raw_msg.split('|')
        if not parts or not parts[0]:
            return

        cmd = parts[0]
        args = parts[1:]

        if cmd == "ROOM_JOINED":
            room_name = args[0]
            self.add_room_to_sidebar(room_name)
            self.switch_chat_target(room_name)
            self.append_system_msg(room_name, f"You joined room: {room_name}")
            self.request_list_refresh()

        elif cmd == "ROOM_LEFT":
            room_name = args[0]
            self.remove_room_from_sidebar(room_name)
            self.append_system_msg(room_name, f"You left room: {room_name}")
            if self.current_target == room_name:
                self.switch_chat_target("")

        elif cmd == "JOIN_NOTIFY":
            room_name, user = args[0], args[1]
            self.append_system_msg(room_name, f"User {user} has joined the room.")

        elif cmd == "LEAVE_NOTIFY":
            room_name, user = args[0], args[1]
            self.append_system_msg(room_name, f"User {user} has left the room.")

        elif cmd == "ROOM_MSG":
            room_name, sender, text = args[0], args[1], args[2]
            self.append_chat_msg(room_name, sender, text)

        elif cmd == "PRIVATE_MSG":
            sender, text = args[0], args[1]
            target_key = f"PM:{sender}"
            self.add_pm_to_sidebar(sender)
            self.append_chat_msg(target_key, sender, text)

        elif cmd == "PRIVATE_CONFIRM":
            target, text = args[0], args[1]
            target_key = f"PM:{target}"
            self.add_pm_to_sidebar(target)
            self.append_chat_msg(target_key, self.username, text)

        elif cmd == "ROOMS":
            rooms_list = args[0].split(',') if args and args[0] else []
            self.update_available_rooms(rooms_list)

        elif cmd == "USERS":
            users_list = args[0].split(',') if args and args[0] else []
            self.update_online_users(users_list)

        elif cmd == "ERROR":
            messagebox.showerror("Server Error", args[0])

    def build_chat_interface(self):
        # Configure Grid
        self.root.columnconfigure(0, weight=1)
        self.root.columnconfigure(1, weight=3)
        self.root.rowconfigure(0, weight=1)

        # 1. Sidebar Frame
        sidebar = ttk.Frame(self.root, style="Sidebar.TFrame")
        sidebar.grid(row=0, column=0, sticky="nsew", padx=2, pady=2)
        sidebar.columnconfigure(0, weight=1)
        sidebar.rowconfigure(2, weight=1)  # Expand list box
        sidebar.rowconfigure(4, weight=1)  # Expand list box

        # User Profile Label
        user_lbl = ttk.Label(sidebar, text=f"👤 {self.username}", style="Header.TLabel", background=SIDEBAR_COLOR)
        user_lbl.grid(row=0, column=0, padx=10, pady=15, sticky="w")

        # Section: Join Room Form
        join_lbl = ttk.Label(sidebar, text="JOIN ROOM", style="Sidebar.TLabel", font=("Segoe UI", 9, "bold"))
        join_lbl.grid(row=1, column=0, padx=10, pady=(10, 2), sticky="w")
        
        join_form = ttk.Frame(sidebar, style="Sidebar.TFrame")
        join_form.grid(row=2, column=0, padx=10, pady=5, sticky="ew")
        join_form.columnconfigure(0, weight=3)
        join_form.columnconfigure(1, weight=1)

        self.room_name_entry = ttk.Entry(join_form)
        self.room_name_entry.grid(row=0, column=0, sticky="ew", padx=(0, 5))
        self.room_name_entry.insert(0, "lobby")

        join_btn = ttk.Button(join_form, text="Join", command=self.cmd_join_room)
        join_btn.grid(row=0, column=1, sticky="ew")

        # Section: Connected Rooms list
        rooms_lbl = ttk.Label(sidebar, text="CONNECTED TARGETS", style="Sidebar.TLabel", font=("Segoe UI", 9, "bold"))
        rooms_lbl.grid(row=3, column=0, padx=10, pady=(15, 2), sticky="w")

        self.targets_listbox = tk.Listbox(sidebar, bg=SIDEBAR_COLOR, fg=TEXT_COLOR, bd=0, 
                                          highlightthickness=0, selectbackground=ACCENT_COLOR, 
                                          selectforeground="#ffffff", font=("Segoe UI", 10))
        self.targets_listbox.grid(row=4, column=0, padx=10, pady=5, sticky="nsew")
        self.targets_listbox.bind('<<ListboxSelect>>', self.on_target_selected)

        # Section: Online Users / Server Rooms Info
        info_frame = ttk.Frame(sidebar, style="Sidebar.TFrame")
        info_frame.grid(row=5, column=0, padx=10, pady=10, sticky="ew")
        info_frame.columnconfigure(0, weight=1)
        info_frame.columnconfigure(1, weight=1)

        refresh_btn = ttk.Button(info_frame, text="🔄 Refresh Stats", command=self.request_list_refresh)
        refresh_btn.grid(row=0, column=0, columnspan=2, sticky="ew", pady=5)

        self.users_lbl = ttk.Label(sidebar, text="Users online: 1 | Active rooms: 0", style="Sidebar.TLabel", font=("Segoe UI", 8), foreground=MUTED_TEXT)
        self.users_lbl.grid(row=6, column=0, padx=10, pady=5, sticky="w")

        # 2. Main Chat Frame
        self.chat_frame = ttk.Frame(self.root)
        self.chat_frame.grid(row=0, column=1, sticky="nsew", padx=2, pady=2)
        self.chat_frame.columnconfigure(0, weight=1)
        self.chat_frame.rowconfigure(1, weight=1) # Chat history area gets weight

        # Chat Header
        self.chat_header_lbl = ttk.Label(self.chat_frame, text="Select a room or user from sidebar to start chatting.", style="Header.TLabel")
        self.chat_header_lbl.grid(row=0, column=0, padx=15, pady=15, sticky="w")

        # Chat Display Box
        self.chat_display = tk.Text(self.chat_frame, bg=CHAT_BG, fg=TEXT_COLOR, bd=0, 
                                    highlightthickness=0, wrap=tk.WORD, font=("Segoe UI", 11), state=tk.DISABLED)
        self.chat_display.grid(row=1, column=0, padx=15, pady=5, sticky="nsew")

        # Scrollbar for Chat Box
        scrollbar = ttk.Scrollbar(self.chat_frame, command=self.chat_display.yview)
        scrollbar.grid(row=1, column=1, sticky="ns", pady=5)
        self.chat_display['yscrollcommand'] = scrollbar.set

        # Input Area Frame
        input_frame = ttk.Frame(self.chat_frame)
        input_frame.grid(row=2, column=0, columnspan=2, padx=15, pady=15, sticky="ew")
        input_frame.columnconfigure(0, weight=5)
        input_frame.columnconfigure(1, weight=1)

        self.msg_entry = ttk.Entry(input_frame, font=("Segoe UI", 10))
        self.msg_entry.grid(row=0, column=0, sticky="ew", padx=(0, 10), ipady=5)
        self.msg_entry.bind('<Return>', lambda e: self.send_chat_message())

        self.send_btn = ttk.Button(input_frame, text="Send Message", command=self.send_chat_message)
        self.send_btn.grid(row=0, column=1, sticky="ew", ipady=4)

        # Initial stats request
        self.request_list_refresh()

    def cmd_join_room(self):
        room_name = self.room_name_entry.get().strip()
        if room_name:
            self.sock.sendall(self.pack_msg(f"JOIN|{room_name}"))
            self.room_name_entry.delete(0, tk.END)

    def send_chat_message(self):
        if not self.current_target:
            messagebox.showinfo("Hint", "Join a room or click a user first to send messages.")
            return

        text = self.msg_entry.get().strip()
        if not text:
            return

        if self.current_target.startswith("PM:"):
            # Send private message
            target_user = self.current_target.replace("PM:", "")
            self.sock.sendall(self.pack_msg(f"PRIVATE|{target_user}|{text}"))
        else:
            # Send room message
            self.sock.sendall(self.pack_msg(f"MSG|{self.current_target}|{text}"))

        self.msg_entry.delete(0, tk.END)

    def request_list_refresh(self):
        if self.sock:
            self.sock.sendall(self.pack_msg("LIST_ROOMS"))
            self.sock.sendall(self.pack_msg("LIST_USERS"))

    def add_room_to_sidebar(self, room_name):
        if room_name not in self.chat_histories:
            self.chat_histories[room_name] = ""
            self.update_targets_listbox()

    def remove_room_from_sidebar(self, room_name):
        if room_name in self.chat_histories:
            del self.chat_histories[room_name]
            self.update_targets_listbox()

    def add_pm_to_sidebar(self, username):
        key = f"PM:{username}"
        if key not in self.chat_histories:
            self.chat_histories[key] = ""
            self.update_targets_listbox()

    def update_targets_listbox(self):
        self.targets_listbox.delete(0, tk.END)
        for key in sorted(self.chat_histories.keys()):
            display_name = f"💬 [Room] {key}" if not key.startswith("PM:") else f"🔒 [PM] {key.replace('PM:', '')}"
            self.targets_listbox.insert(tk.END, display_name)

    def on_target_selected(self, event):
        selection = self.targets_listbox.curselection()
        if not selection:
            return
        
        index = selection[0]
        display_name = self.targets_listbox.get(index)
        
        # Resolve internal key from display name
        if "[Room]" in display_name:
            target_key = display_name.replace("💬 [Room] ", "")
        else:
            target_key = "PM:" + display_name.replace("🔒 [PM] ", "")

        self.switch_chat_target(target_key)

    def switch_chat_target(self, target_key):
        self.current_target = target_key
        if not target_key:
            self.chat_header_lbl.config(text="Select a room or user from sidebar to start chatting.")
            self.set_chat_display_content("")
        else:
            title = f"Room: {target_key}" if not target_key.startswith("PM:") else f"Private Chat with {target_key.replace('PM:', '')}"
            self.chat_header_lbl.config(text=title)
            
            # Load history
            history = self.chat_histories.get(target_key, "")
            self.set_chat_display_content(history)

    def set_chat_display_content(self, text):
        self.chat_display.config(state=tk.NORMAL)
        self.chat_display.delete('1.0', tk.END)
        self.chat_display.insert(tk.END, text)
        self.chat_display.see(tk.END)
        self.chat_display.config(state=tk.DISABLED)

    def append_chat_msg(self, target_key, sender, message):
        formatted = f"[{sender}]: {message}\n"
        
        # Save to history
        if target_key not in self.chat_histories:
            self.chat_histories[target_key] = ""
        self.chat_histories[target_key] += formatted
        
        # If currently active tab, update screen
        if self.current_target == target_key:
            self.chat_display.config(state=tk.NORMAL)
            self.chat_display.insert(tk.END, formatted)
            self.chat_display.see(tk.END)
            self.chat_display.config(state=tk.DISABLED)
            
        # Highlight in listbox if not active (basic unread alert logic)
        self.update_targets_listbox_selection()

    def append_system_msg(self, target_key, message):
        formatted = f"📢 [SYSTEM] {message}\n"
        
        if target_key not in self.chat_histories:
            self.chat_histories[target_key] = ""
        self.chat_histories[target_key] += formatted
        
        if self.current_target == target_key:
            self.chat_display.config(state=tk.NORMAL)
            self.chat_display.insert(tk.END, formatted)
            self.chat_display.see(tk.END)
            self.chat_display.config(state=tk.DISABLED)

    def update_targets_listbox_selection(self):
        # Keeps selection highlight consistent with current_target
        for i in range(self.targets_listbox.size()):
            name = self.targets_listbox.get(i)
            resolved = name.replace("💬 [Room] ", "") if "[Room]" in name else "PM:" + name.replace("🔒 [PM] ", "")
            if resolved == self.current_target:
                self.targets_listbox.select_set(i)
                break

    def update_available_rooms(self, rooms):
        # We can print these to the system console, but let's show status count
        pass

    def update_online_users(self, users):
        count_users = len(users)
        self.users_lbl.config(text=f"Users online: {count_users} | Targets: {len(self.chat_histories)}")

        # Create quick button links on user list if needed, or just let users type PM target.
        # To make it visual, we can pop up a PM target selector or dynamically check online users.

    def on_closing(self):
        self.stop_event.set()
        if self.sock:
            try:
                # Notify exit
                self.sock.sendall(self.pack_msg("QUIT"))
            except:
                pass
            self.sock.close()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = ChatGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
