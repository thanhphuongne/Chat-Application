# High-Performance Asynchronous Chat Application (C++ Boost.Asio)

Dự án này là một hệ thống **Real-time Chat Client-Server** hiệu năng cao, viết bằng C++ sử dụng thư viện **Boost.Asio** để xử lý hàng trăm kết nối đồng thời theo mô hình mạng bất đồng bộ (Asynchronous Network I/O). Dự án được thiết kế chuẩn chỉ, sạch sẽ, phân chia cấu trúc rõ ràng giữa Headers (`include/`) và Source Files (`src/`), hỗ trợ lưu trữ Database persistent, heartbeat giám sát kết nối và chống spam.

---

## Kiến trúc hệ thống

```text
┌──────────────────────┐
│  Chat Client (C++)   │  ◄─── ANSI terminal colors
│  - Async read thread │  ◄─── Command menus (/join, /leave, /pub, /msg)
│  - Auto-reconnect    │  ◄─── Auto PING-PONG keepalive
└──────────┬───────────┘
           │ TCP (Length-Prefix Frame: [4 byte length] + [Payload])
           ▼
┌────────────────────────────────────────────────────────┐
│  Chat Server (Boost.Asio C++17)                        │
│  ┌────────────────┐                                    │
│  │    Acceptor    │ ◄─── Async connection accept loops │
│  └───────┬────────┘                                    │
│          ▼                                             │
│  ┌────────────────┐                                    │
│  │    Session     │ ◄─── One session per client        │
│  └───────┬────────┘      - Anti-Spam Rate Limit        │
│          │               - Heartbeat Timeout Check     │
│          ▼                                             │
│  ┌────────────────┐      ┌────────────────┐            │
│  │  Room Manager  │ ◄─── │  User Manager  │            │
│  └───────┬────────┘      └────────┬───────┘            │
│          ▼                        ▼                    │
│  ┌────────────────────────────────────────┐            │
│  │   Secure File-Database (SHA-256 Auth)  │            │
│  └────────────────────────────────────────┘            │
└────────────────────────────────────────────────────────┘
```

---

## Cấu trúc thư mục dự án

```text
chat-application/
├── server/
│   ├── include/                # Tất cả file tiêu đề (.hpp) của Server
│   │   ├── chat_server.hpp     # Quản lý acceptor, sessions và phòng chat
│   │   ├── session.hpp         # Xử lý kết nối, heartbeat và rate limit cho từng client
│   │   ├── room.hpp            # Giao diện participant và lớp phòng chat
│   │   ├── user.hpp            # Đối tượng thông tin người dùng
│   │   └── database.hpp        # Cơ sở dữ liệu lưu file + SHA-256 Hasher
│   ├── src/                    # Triển khai mã nguồn (.cpp) của Server
│   │   ├── main.cpp
│   │   ├── chat_server.cpp
│   │   ├── session.cpp
│   │   ├── room.cpp
│   │   ├── user.cpp
│   │   └── protocol.cpp        # Đóng gói và giải mã khung truyền tin length-prefix
│   ├── Makefile                # Hỗ trợ biên dịch server trên Linux
│   └── Dockerfile              # Dockerfile đa tầng (multi-stage) tối ưu dung lượng
├── client/
│   ├── include/                # File tiêu đề của Client
│   │   └── chat_client.hpp     # Socket client kết nối và luồng đọc ghi ngầm
│   ├── src/                    # Mã nguồn triển khai Client
│   │   ├── main.cpp            # Menu điều khiển và nhập lệnh console
│   │   └── chat_client.cpp     # Logic kết nối bất đồng bộ và tự động Reconnect
│   └── Makefile                # Biên dịch client console C++
├── docker-compose.yml          # Khởi chạy server container nhanh chóng
├── scripts/
│   └── load_test.py            # Script Python asyncio test tải 100 clients đồng thời
└── README.md                   # Tài liệu hướng dẫn
```

---

## Thiết kế Giao thức truyền thông (Protocol)

Mỗi gói tin TCP gửi đi đều được tiền tố hóa bằng **4 byte độ dài** (Big Endian) để chống dính gói.
Định dạng chuỗi Payload (phân tách bởi ký tự `|`):

### 1. Client → Server
- `REGISTER|username|password` : Đăng ký tài khoản mật khẩu mới.
- `LOGIN|username|password` : Đăng nhập tài khoản.
- `JOIN|room_name` : Tham gia phòng chat `room_name`.
- `LEAVE` : Rời khỏi phòng chat hiện tại.
- `MSG|room_name|content` : Gửi tin nhắn công khai tới phòng.
- `PRIVATE|target_user|content` : Gửi tin nhắn riêng tư tới `target_user`.
- `LIST_ROOMS` : Yêu cầu danh sách phòng.
- `LIST_USERS` : Yêu cầu danh sách user online.
- `GET_HISTORY|target` : Tải lịch sử chat (target là tên phòng hoặc cuộc PM).
- `PING` : Client gửi kiểm tra kết nối.
- `QUIT` : Đóng kết nối sạch sẽ.

### 2. Server → Client
- `OK|message` : Phản hồi xác nhận thành công.
- `ERROR|code|description` : Phản hồi lỗi (Ví dụ: `ERROR|AUTH_FAILED|Invalid credentials`).
- `SYSTEM|message` : Tin nhắn thông báo hệ thống chung.
- `USER_JOIN|room|username` : Thông báo user khác join room.
- `USER_LEAVE|room|username` : Thông báo user khác leave room.
- `MSG|room|sender|content|timestamp` : Phát tin nhắn phòng tới các thành viên.
- `PRIVATE|sender|content|timestamp` : Phát tin nhắn riêng tư tới người nhận & người gửi.
- `ROOM_LIST|room1,room2,...` : Trả về danh sách phòng.
- `USER_LIST|user1,user2,...` : Trả về danh sách user.
- `PONG` : Server phản hồi heartbeat.

---

## Hướng dẫn cài đặt và khởi chạy

### 1. Khởi động Server (bằng Docker)
Cách nhanh nhất và sạch sẽ nhất là chạy server qua Docker để không cần cài Boost trên máy host:
```bash
docker-compose up --build
```
Server sẽ chạy và lắng nghe ở cổng `8080`.

*(Hoặc biên dịch cục bộ nếu có sẵn Boost trên Linux: `cd server && make && ./chat_server 8080`)*

### 2. Biên dịch và chạy Client Console (C++)
Đảm bảo bạn đã cài đặt Boost C++ trên máy host.
```bash
cd client
make
./chat_client 127.0.0.1 8080
```
- Bạn có thể lựa chọn đăng nhập (`Login`) hoặc đăng ký tài khoản (`Register`) ngay trên menu giao diện Console.
- Client hỗ trợ **tự động Reconnect**: Nếu Server tắt hoặc mạng bị đứt, client sẽ chuyển sang trạng thái chờ và kết nối lại ngay khi server online mà bạn không cần mở lại ứng dụng.
- Client hiển thị màu sắc ANSI phân biệt: Xanh lá (Tin nhắn riêng), Xanh Cyan (Tin nhắn phòng), Vàng (Thông báo hệ thống), Đỏ (Lỗi).

### Các lệnh được hỗ trợ tại Client:
- `/rooms` : Xem danh sách phòng đang hoạt động.
- `/users` : Xem danh sách người dùng online.
- `/join <room_name>` : Vào phòng chat.
- `/leave` : Rời phòng chat hiện tại.
- `/pub <room_name> <nội_dung>` : Gửi tin nhắn công khai vào phòng chat.
- `/msg <username> <nội_dung>` : Gửi tin nhắn riêng tư tới user chỉ định.
- `/help` : Hiển thị bảng trợ giúp lệnh.
- `/quit` : Thoát ứng dụng.

---

## Kiểm thử tải (Load Testing)

Chúng ta có một script Python giả lập kiểm thử tải bất đồng bộ cực kỳ mạnh mẽ tại `scripts/load_test.py`. Script này sử dụng `asyncio` để kết nối **100 client đồng thời**, thực hiện đăng ký, đăng nhập, join phòng, và liên tục spam tin nhắn cũng như duy trì Ping-Pong liên tục lên Server C++ nhằm kiểm tra độ chịu tải.

Chạy script test tải:
```bash
python scripts/load_test.py
```
Sau khi hoàn thành, script sẽ in ra báo cáo chi tiết về tốc độ phản hồi, lượng tin nhắn gửi nhận thành công và tỷ lệ thất bại.
