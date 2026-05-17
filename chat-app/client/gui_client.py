import tkinter as tk
from tkinter import ttk, messagebox
import socket
import threading
import struct
import queue
import sys
import time

# Color Palette (Premium Material Dark)
BG_COLOR = "#121212"         # Deep black background
SIDEBAR_COLOR = "#1e1e1e"    # Dark grey for sidebar
CHAT_BG = "#151515"          # Dark charcoal for chat area
ACCENT_COLOR = "#00adb5"     # Teal cyan for accents/buttons
TEXT_COLOR = "#eeeeee"       # White text
MUTED_TEXT = "#7a7a7a"       # Secondary muted grey text
BUBBLE_ME = "#005f73"        # Deep ocean teal for own messages
BUBBLE_OTHER = "#2b2d42"     # Slate blue-grey for other messages
SYSTEM_COLOR = "#1e2022"     # Dark notification box

class ChatGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("CppChat - Professional Client")
        self.root.geometry("950x650")
        self.root.configure(bg=BG_COLOR)

        self.sock = None
        self.username = ""
        self.stop_event = threading.Event()
        self.msg_queue = queue.Queue()

        # Chat history maps: { "target": [ {"sender": "x", "content": "y", "ts": "z"}, ... ] }
        self.chat_histories = {}
        self.current_target = "" # Active room or private chat (e.g. "lobby" or "PM:Alice")
        
        self.online_users_list = []
        self.active_rooms_list = []
        
        self.setup_styles()
        self.show_auth_screen()

    def setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('.', bg=BG_COLOR, fg=TEXT_COLOR, font=("Segoe UI", 10))
        style.configure('TFrame', background=BG_COLOR)
        style.configure('Sidebar.TFrame', background=SIDEBAR_COLOR)
        style.configure('TLabel', background=BG_COLOR, foreground=TEXT_COLOR)
        style.configure('Sidebar.TLabel', background=SIDEBAR_COLOR, foreground=TEXT_COLOR)
        style.configure('Header.TLabel', background=BG_COLOR, foreground=ACCENT_COLOR, font=("Segoe UI", 15, "bold"))
        
        # Tabs Style
        style.configure('TNotebook', background=BG_COLOR, borderwidth=0)
        style.configure('TNotebook.Tab', background=SIDEBAR_COLOR, foreground=MUTED_TEXT, padding=[10, 5], font=("Segoe UI", 9, "bold"))
        style.map('TNotebook.Tab', background=[('selected', BG_COLOR)], foreground=[('selected', ACCENT_COLOR)])

        style.configure('TButton', background=ACCENT_COLOR, foreground="#ffffff", borderwidth=0, font=("Segoe UI", 10, "bold"))
        style.map('TButton', background=[('active', '#007f87')])

        style.configure('TEntry', fieldbackground="#2b2b2b", foreground=TEXT_COLOR, insertcolor=TEXT_COLOR, borderwidth=0)
        style.configure('Sidebar.TButton', background="#2b2b2b", foreground=TEXT_COLOR, font=("Segoe UI", 9))
        style.map('Sidebar.TButton', background=[('active', '#3a3a3a')])

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

    def show_auth_screen(self):
        self.auth_frame = ttk.Frame(self.root)
        self.auth_frame.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

        title_lbl = ttk.Label(self.auth_frame, text="🔒 CPPCHAT SECURED", style="Header.TLabel")
        title_lbl.pack(pady=20)

        # Host/Port Settings Frame
        net_frame = ttk.Frame(self.auth_frame)
        net_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(net_frame, text="Server:").grid(row=0, column=0, sticky="w", padx=5)
        self.host_entry = ttk.Entry(net_frame, width=15)
        self.host_entry.insert(0, "127.0.0.1")
        self.host_entry.grid(row=0, column=1, padx=5)

        ttk.Label(net_frame, text="Port:").grid(row=0, column=2, sticky="w", padx=5)
        self.port_entry = ttk.Entry(net_frame, width=6)
        self.port_entry.insert(0, "8080")
        self.port_entry.grid(row=0, column=3, padx=5)

        # Tabbed Login / Register Selector
        self.notebook = ttk.Notebook(self.auth_frame)
        self.notebook.pack(pady=15, fill=tk.BOTH, expand=True)

        # Tab 1: Login
        self.login_tab = ttk.Frame(self.notebook, padding=15)
        self.notebook.add(self.login_tab, text="LOGIN")

        ttk.Label(self.login_tab, text="Username:").pack(anchor=tk.W, pady=2)
        self.login_user = ttk.Entry(self.login_tab, width=32)
        self.login_user.pack(pady=5)
        
        ttk.Label(self.login_tab, text="Password:").pack(anchor=tk.W, pady=2)
        self.login_pass = ttk.Entry(self.login_tab, show="*", width=32)
        self.login_pass.pack(pady=5)

        login_btn = ttk.Button(self.login_tab, text="Secure Log In", command=self.attempt_login)
        login_btn.pack(pady=20, fill=tk.X)

        # Tab 2: Register
        self.reg_tab = ttk.Frame(self.notebook, padding=15)
        self.notebook.add(self.reg_tab, text="REGISTER")

        ttk.Label(self.reg_tab, text="Desired Username:").pack(anchor=tk.W, pady=2)
        self.reg_user = ttk.Entry(self.reg_tab, width=32)
        self.reg_user.pack(pady=5)

        ttk.Label(self.reg_tab, text="Password:").pack(anchor=tk.W, pady=2)
        self.reg_pass = ttk.Entry(self.reg_tab, show="*", width=32)
        self.reg_pass.pack(pady=5)

        reg_btn = ttk.Button(self.reg_tab, text="Create Account", command=self.attempt_register)
        reg_btn.pack(pady=20, fill=tk.X)

    def attempt_connect(self) -> bool:
        if self.sock:
            try:
                self.sock.close()
            except:
                pass

        host = self.host_entry.get().strip()
        port_str = self.port_entry.get().strip()

        try:
            port = int(port_str)
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((host, port))
            return True
        except Exception as e:
            messagebox.showerror("Connection Error", f"Could not connect to {host}:{port_str}\nError: {e}")
            return False

    def attempt_login(self):
        username = self.login_user.get().strip()
        password = self.login_pass.get()

        if not username or not password:
            messagebox.showerror("Error", "Username and password required!")
            return

        if not self.attempt_connect():
            return

        try:
            self.sock.sendall(self.pack_msg(f"LOGIN|{username}|{password}"))
            
            # Read verification response synchronously
            header = self.read_exact(4)
            if not header:
                raise Exception("Server terminated link.")
            length = struct.unpack('>I', header)[0]
            payload = self.read_exact(length).decode('utf-8')
            
            parts = payload.split('|')
            if parts[0] == "LOGIN_SUCCESS":
                self.username = username
                self.auth_frame.destroy()
                self.build_chat_interface()
                
                # Start receiver thread
                self.receiver_thread = threading.Thread(target=self.receive_loop, daemon=True)
                self.receiver_thread.start()
                self.root.after(100, self.process_queue)
            else:
                reason = parts[1] if len(parts) > 1 else "Invalid credentials."
                messagebox.showerror("Login Failed", f"Access Denied:\n{reason}")
                self.sock.close()
        except Exception as e:
            messagebox.showerror("Error", f"Login sequence failed:\n{e}")
            self.sock.close()

    def attempt_register(self):
        username = self.reg_user.get().strip()
        password = self.reg_pass.get()

        if not username or not password:
            messagebox.showerror("Error", "Desired username and password required!")
            return

        if not self.attempt_connect():
            return

        try:
            self.sock.sendall(self.pack_msg(f"REGISTER|{username}|{password}"))
            
            header = self.read_exact(4)
            if not header:
                raise Exception("Server closed socket.")
            length = struct.unpack('>I', header)[0]
            payload = self.read_exact(length).decode('utf-8')
            
            parts = payload.split('|')
            if parts[0] == "REGISTER_SUCCESS":
                messagebox.showinfo("Success", f"Account '{username}' created successfully!\nYou can now log in.")
                self.notebook.select(self.login_tab)
                self.login_user.delete(0, tk.END)
                self.login_user.insert(0, username)
                self.login_pass.focus()
            else:
                reason = parts[1] if len(parts) > 1 else "Unknown error"
                messagebox.showerror("Register Failed", f"Could not create account:\n{reason}")
            self.sock.close()
        except Exception as e:
            messagebox.showerror("Error", f"Register sequence failed:\n{e}")
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
                    self.msg_queue.put(("DISCONNECT", "Connection closed by server."))
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

        # Auto PING-PONG heartbeat
        if cmd == "PING":
            try:
                self.sock.sendall(self.pack_msg("PONG"))
            except:
                pass
            return

        elif cmd == "SPAM_WARNING":
            messagebox.showwarning("Spam Warning", args[0])

        elif cmd == "ROOM_JOINED":
            room_name = args[0]
            self.add_target_to_sidebar(room_name)
            self.switch_chat_target(room_name)
            self.request_history(room_name)
            self.request_list_refresh()

        elif cmd == "ROOM_LEFT":
            room_name = args[0]
            self.remove_target_from_sidebar(room_name)
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
            self.add_target_to_sidebar(target_key)
            self.append_chat_msg(target_key, sender, text)

        elif cmd == "PRIVATE_CONFIRM":
            target, text = args[0], args[1]
            target_key = f"PM:{target}"
            self.add_target_to_sidebar(target_key)
            self.append_chat_msg(target_key, self.username, text)

        elif cmd == "HISTORY_MSG":
            # Form: HISTORY_MSG|target|sender|content|timestamp
            target, sender, text, timestamp = args[0], args[1], args[2], args[3]
            # Since history messages are sent in order, we can append them
            self.append_chat_msg(target, sender, text, timestamp, is_history=True)

        elif cmd == "ROOMS":
            self.active_rooms_list = args[0].split(',') if args and args[0] else []
            self.update_stats_display()

        elif cmd == "USERS":
            self.online_users_list = args[0].split(',') if args and args[0] else []
            self.update_stats_display()

        elif cmd == "ERROR":
            self.append_system_msg(self.current_target, f"Error: {args[0]}")

    def build_chat_interface(self):
        self.root.columnconfigure(0, weight=1)
        self.root.columnconfigure(1, weight=3)
        self.root.rowconfigure(0, weight=1)

        # 1. Sidebar Panel
        sidebar = ttk.Frame(self.root, style="Sidebar.TFrame")
        sidebar.grid(row=0, column=0, sticky="nsew", padx=2, pady=2)
        sidebar.columnconfigure(0, weight=1)
        sidebar.rowconfigure(4, weight=2)  # Connected Targets List
        sidebar.rowconfigure(6, weight=1)  # Online Stats List

        # User Info
        user_lbl = ttk.Label(sidebar, text=f"👤 {self.username}", style="Header.TLabel", background=SIDEBAR_COLOR)
        user_lbl.grid(row=0, column=0, padx=15, pady=15, sticky="w")

        # Join Room Input Box
        join_lbl = ttk.Label(sidebar, text="JOIN ROOM", style="Sidebar.TLabel", font=("Segoe UI", 9, "bold"))
        join_lbl.grid(row=1, column=0, padx=15, pady=(10, 2), sticky="w")

        join_form = ttk.Frame(sidebar, style="Sidebar.TFrame")
        join_form.grid(row=2, column=0, padx=15, pady=5, sticky="ew")
        join_form.columnconfigure(0, weight=3)
        join_form.columnconfigure(1, weight=1)

        self.room_name_entry = ttk.Entry(join_form)
        self.room_name_entry.grid(row=0, column=0, sticky="ew", padx=(0, 5))
        self.room_name_entry.insert(0, "lobby")

        join_btn = ttk.Button(join_form, text="Join", command=self.cmd_join_room)
        join_btn.grid(row=0, column=1, sticky="ew")

        # Targets Section (Active channels)
        targets_lbl = ttk.Label(sidebar, text="CONNECTED TARGETS", style="Sidebar.TLabel", font=("Segoe UI", 9, "bold"))
        targets_lbl.grid(row=3, column=0, padx=15, pady=(15, 2), sticky="w")

        self.targets_listbox = tk.Listbox(sidebar, bg=SIDEBAR_COLOR, fg=TEXT_COLOR, bd=0, 
                                          highlightthickness=0, selectbackground=ACCENT_COLOR, 
                                          selectforeground="#ffffff", font=("Segoe UI", 10))
        self.targets_listbox.grid(row=4, column=0, padx=15, pady=5, sticky="nsew")
        self.targets_listbox.bind('<<ListboxSelect>>', self.on_target_selected)

        # Global Online/Rooms Stats (Interactive Listbox)
        stats_lbl = ttk.Label(sidebar, text="ONLINE DIRECTORY (Double click to Join/Chat)", style="Sidebar.TLabel", font=("Segoe UI", 8, "bold"), foreground=ACCENT_COLOR)
        stats_lbl.grid(row=5, column=0, padx=15, pady=(15, 2), sticky="w")

        self.stats_listbox = tk.Listbox(sidebar, bg=SIDEBAR_COLOR, fg=MUTED_TEXT, bd=0,
                                        highlightthickness=0, selectbackground=ACCENT_COLOR,
                                        selectforeground="#ffffff", font=("Segoe UI", 9))
        self.stats_listbox.grid(row=6, column=0, padx=15, pady=5, sticky="nsew")
        self.stats_listbox.bind('<Double-Button-1>', self.on_directory_double_click)

        # Refresh Stats Action
        refresh_btn = ttk.Button(sidebar, text="🔄 Refresh Directory", command=self.request_list_refresh)
        refresh_btn.grid(row=7, column=0, padx=15, pady=10, sticky="ew")

        # Connection Status Line
        self.status_lbl = ttk.Label(sidebar, text="Connection: Secured 🟢", style="Sidebar.TLabel", font=("Segoe UI", 8), foreground=MUTED_TEXT)
        self.status_lbl.grid(row=8, column=0, padx=15, pady=5, sticky="w")

        # 2. Main Chat Frame
        self.chat_frame = ttk.Frame(self.root)
        self.chat_frame.grid(row=0, column=1, sticky="nsew", padx=2, pady=2)
        self.chat_frame.columnconfigure(0, weight=1)
        self.chat_frame.rowconfigure(1, weight=1)

        # Chat Header Label
        self.chat_header_lbl = ttk.Label(self.chat_frame, text="Select a channel from directory or sidebar to start chatting.", style="Header.TLabel")
        self.chat_header_lbl.grid(row=0, column=0, padx=20, pady=15, sticky="w")

        # Text display styled to look like bubble chat (using rich tags in Tkinter Text)
        self.chat_display = tk.Text(self.chat_frame, bg=CHAT_BG, fg=TEXT_COLOR, bd=0, 
                                    highlightthickness=0, wrap=tk.WORD, font=("Segoe UI", 11), state=tk.DISABLED)
        self.chat_display.grid(row=1, column=0, padx=20, pady=5, sticky="nsew")

        # Tags configuration for custom formatting (looks like real speech bubble alignment)
        self.chat_display.tag_configure("me_header", justify="right", font=("Segoe UI", 9, "bold"), spacing1=10)
        self.chat_display.tag_configure("me_bubble", justify="right", rmargin=20, font=("Segoe UI", 10), foreground="#a8e6cf", spacing2=3)
        self.chat_display.tag_configure("other_header", justify="left", font=("Segoe UI", 9, "bold"), spacing1=10)
        self.chat_display.tag_configure("other_bubble", justify="left", lmargin1=20, lmargin2=20, font=("Segoe UI", 10), foreground="#ffd3b6", spacing2=3)
        self.chat_display.tag_configure("sys_msg", justify="center", font=("Segoe UI", 9, "italic"), foreground=MUTED_TEXT, spacing1=10, spacing3=5)

        scrollbar = ttk.Scrollbar(self.chat_frame, command=self.chat_display.yview)
        scrollbar.grid(row=1, column=1, sticky="ns", pady=5)
        self.chat_display['yscrollcommand'] = scrollbar.set

        # Text input panel
        input_frame = ttk.Frame(self.chat_frame)
        input_frame.grid(row=2, column=0, columnspan=2, padx=20, pady=15, sticky="ew")
        input_frame.columnconfigure(0, weight=5)
        input_frame.columnconfigure(1, weight=1)

        self.msg_entry = ttk.Entry(input_frame, font=("Segoe UI", 10))
        self.msg_entry.grid(row=0, column=0, sticky="ew", padx=(0, 10), ipady=5)
        self.msg_entry.bind('<Return>', lambda e: self.send_chat_message())

        self.send_btn = ttk.Button(input_frame, text="Send Message", command=self.send_chat_message)
        self.send_btn.grid(row=0, column=1, sticky="ew", ipady=4)

        # Initial Refresh
        self.request_list_refresh()

    def cmd_join_room(self):
        room_name = self.room_name_entry.get().strip()
        if room_name:
            self.sock.sendall(self.pack_msg(f"JOIN|{room_name}"))
            self.room_name_entry.delete(0, tk.END)

    def send_chat_message(self):
        if not self.current_target:
            messagebox.showinfo("Hint", "Select a room or online user from directory to chat.")
            return

        text = self.msg_entry.get().strip()
        if not text:
            return

        if self.current_target.startswith("PM:"):
            target_user = self.current_target.replace("PM:", "")
            self.sock.sendall(self.pack_msg(f"PRIVATE|{target_user}|{text}"))
        else:
            self.sock.sendall(self.pack_msg(f"MSG|{self.current_target}|{text}"))

        self.msg_entry.delete(0, tk.END)

    def request_list_refresh(self):
        if self.sock:
            self.sock.sendall(self.pack_msg("LIST_ROOMS"))
            self.sock.sendall(self.pack_msg("LIST_USERS"))

    def request_history(self, target):
        if self.sock:
            self.sock.sendall(self.pack_msg(f"GET_HISTORY|{target}"))

    def add_target_to_sidebar(self, target_key):
        if target_key not in self.chat_histories:
            self.chat_histories[target_key] = []
            self.update_targets_listbox()

    def remove_target_from_sidebar(self, target_key):
        if target_key in self.chat_histories:
            del self.chat_histories[target_key]
            self.update_targets_listbox()

    def update_targets_listbox(self):
        self.targets_listbox.delete(0, tk.END)
        for key in sorted(self.chat_histories.keys()):
            display = f"💬 Room: {key}" if not key.startswith("PM:") else f"🔒 PM: {key.replace('PM:', '')}"
            self.targets_listbox.insert(tk.END, display)

    def on_target_selected(self, event):
        selection = self.targets_listbox.curselection()
        if not selection: return
        
        display = self.targets_listbox.get(selection[0])
        if "Room: " in display:
            target_key = display.replace("💬 Room: ", "")
        else:
            target_key = "PM:" + display.replace("🔒 PM: ", "")
            
        self.switch_chat_target(target_key)

    def on_directory_double_click(self, event):
        selection = self.stats_listbox.curselection()
        if not selection: return

        item_str = self.stats_listbox.get(selection[0])
        if "[User]" in item_str:
            target_user = item_str.replace("👤 [User] ", "").replace(" (You)", "")
            if target_user == self.username:
                return
            target_key = f"PM:{target_user}"
            self.add_target_to_sidebar(target_key)
            self.switch_chat_target(target_key)
        elif "[Room]" in item_str:
            room_name = item_str.replace("🏠 [Room] ", "")
            # Check if already joined, if not, join room
            if room_name not in self.chat_histories:
                self.sock.sendall(self.pack_msg(f"JOIN|{room_name}"))
            else:
                self.switch_chat_target(room_name)

    def switch_chat_target(self, target_key):
        self.current_target = target_key
        if not target_key:
            self.chat_header_lbl.config(text="Select a channel from directory or sidebar to start chatting.")
            self.set_chat_display_content([])
        else:
            title = f"Room Channel: {target_key}" if not target_key.startswith("PM:") else f"Secure Session with {target_key.replace('PM:', '')}"
            self.chat_header_lbl.config(text=title)
            
            # Load message list from history
            msgs = self.chat_histories.get(target_key, [])
            
            # If history is empty (and we just selected it), pull history from server
            if not msgs:
                self.request_history(target_key)
            
            self.set_chat_display_content(msgs)

    def set_chat_display_content(self, msg_list):
        self.chat_display.config(state=tk.NORMAL)
        self.chat_display.delete('1.0', tk.END)
        for msg in msg_list:
            self.insert_msg_to_widget(msg)
        self.chat_display.see(tk.END)
        self.chat_display.config(state=tk.DISABLED)

    def insert_msg_to_widget(self, msg):
        sender = msg["sender"]
        text = msg["content"]
        timestamp = msg["ts"]
        is_sys = msg.get("sys", False)

        if is_sys:
            self.chat_display.insert(tk.END, f"{text}\n", "sys_msg")
        elif sender == self.username:
            header = f"You • {timestamp}\n"
            body = f"{text}\n"
            self.chat_display.insert(tk.END, header, "me_header")
            self.chat_display.insert(tk.END, body, "me_bubble")
        else:
            header = f"{sender} • {timestamp}\n"
            body = f"{text}\n"
            self.chat_display.insert(tk.END, header, "other_header")
            self.chat_display.insert(tk.END, body, "other_bubble")

    def append_chat_msg(self, target_key, sender, message, timestamp=None, is_history=False):
        if not timestamp:
            timestamp = time.strftime("%H:%M:%S")

        msg_obj = {"sender": sender, "content": message, "ts": timestamp}

        if target_key not in self.chat_histories:
            self.chat_histories[target_key] = []
            
        # Avoid duplicate history records (e.g. history messages matching standard updates)
        history_list = self.chat_histories[target_key]
        is_duplicate = any(m["sender"] == sender and m["content"] == message and m["ts"] == timestamp for m in history_list)
        
        if not is_duplicate:
            if is_history:
                # Insert at correct location if loading historic messages
                history_list.append(msg_obj)
                # Sort history lists based on timestamp
                history_list.sort(key=lambda m: m["ts"])
            else:
                history_list.append(msg_obj)

        if self.current_target == target_key:
            # Refresh complete pane to keep order correct
            self.set_chat_display_content(self.chat_histories[target_key])
            
        self.update_targets_listbox_selection()

    def append_system_msg(self, target_key, message):
        if not target_key: return
        msg_obj = {"sender": "SYSTEM", "content": f"📢 {message}", "ts": "", "sys": True}
        
        if target_key not in self.chat_histories:
            self.chat_histories[target_key] = []
        self.chat_histories[target_key].append(msg_obj)

        if self.current_target == target_key:
            self.chat_display.config(state=tk.NORMAL)
            self.insert_msg_to_widget(msg_obj)
            self.chat_display.see(tk.END)
            self.chat_display.config(state=tk.DISABLED)

    def update_targets_listbox_selection(self):
        for i in range(self.targets_listbox.size()):
            name = self.targets_listbox.get(i)
            resolved = name.replace("💬 Room: ", "") if "Room: " in name else "PM:" + name.replace("🔒 PM: ", "")
            if resolved == self.current_target:
                self.targets_listbox.select_set(i)
                break

    def update_stats_display(self):
        self.stats_listbox.delete(0, tk.END)
        
        # Insert online users
        for u in sorted(self.online_users_list):
            lbl = f"👤 [User] {u} (You)" if u == self.username else f"👤 [User] {u}"
            self.stats_listbox.insert(tk.END, lbl)

        # Insert active server rooms
        for r in sorted(self.active_rooms_list):
            self.stats_listbox.insert(tk.END, f"🏠 [Room] {r}")

        self.users_lbl.config(text=f"Users online: {len(self.online_users_list)} | Active rooms: {len(self.active_rooms_list)}")

    def on_closing(self):
        self.stop_event.set()
        if self.sock:
            try:
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
