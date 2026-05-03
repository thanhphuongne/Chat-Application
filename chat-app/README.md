# Real-time Chat Application (Boost.Asio C++17)

Dự án này là một ứng dụng **Real-time Chat Client-Server** hiệu năng cao, xây dựng trên mô hình I/O bất đồng bộ (Asynchronous Network I/O) sử dụng thư viện **Boost.Asio** trong C++17. Ứng dụng hỗ trợ đa phòng chat (room), nhắn tin riêng tư (private message), quản lý trạng thái online/offline của người dùng, đóng gói tin nhắn dạng Length-Prefix Framing và vận hành dễ dàng thông qua Docker & Makefiles.

---

## Tính năng chính

### 1. Kiến trúc mạng & Hiệu năng
- **Asynchronous Processing**: Server xử lý tất cả các kết nối client đồng thời trên một luồng I/O bất đồng bộ bằng `boost::asio::io_context`, giảm thiểu chi phí chuyển ngữ cảnh (context switch) của CPU so với mô hình đa luồng truyền thống (thread-per-connection).
- **Message Framing (Length-Prefix)**: Giải quyết triệt để vấn đề "dính gói" (packet fragmentation) và "đọc thiếu" (partial read) trên giao thức TCP bằng cách tiền tố hóa mỗi gói tin gửi đi bằng `[4 byte độ dài payload (Big Endian)]` + `[Payload]`.
- **Graceful Shutdown**: Server bắt các tín hiệu hệ thống (`SIGINT`, `SIGTERM`) để đóng sạch sẽ mọi kết nối của client, giải phóng tài nguyên socket và kết thúc luồng hoạt động mà không bị crash.

### 2. Tính năng Chat (Application Layer)
- **Đăng ký tài khoản tạm thời**: Nhập username khi kết nối; tự động kiểm tra trùng lặp trên server.
- **Quản lý phòng (Room)**:
  - Lệnh `/rooms` để liệt kê các phòng chat đang hoạt động.
  - Lệnh `/join <room_name>` để tham gia vào phòng chat (tạo mới nếu phòng chưa tồn tại).
  - Lệnh `/leave <room_name>` để rời phòng.
  - Tự động dọn dẹp và giải phóng tài nguyên phòng chat khi không còn ai tham gia.
  - Gửi tin nhắn công khai trong phòng chat qua lệnh `/msg <room_name> <nội dung>`.
- **Tin nhắn riêng tư (Private Message)**: Gửi tin nhắn trực tiếp 1-1 giữa các user thông qua lệnh `/pm <username> <nội dung>`.
- **Thông báo sự kiện**: Tự động thông báo tới các thành viên trong phòng khi có user khác join/leave.
- **Trạng thái online**: Xem danh sách các user đang hoạt động qua lệnh `/users`.
- **Xử lý ngắt kết nối đột ngột**: Cập nhật tức thời trạng thái online, rời khỏi toàn bộ phòng đang tham gia và thông báo cho các người dùng khác khi có client mất kết nối.

---

## Cấu trúc thư mục

```text
chat-app/
├── server/
│   ├── src/
│   │   ├── main.cpp            # Khởi tạo io_context, signal_set và chạy chat_server
│   │   ├── chat_server.hpp/cpp # Quản lý acceptor, vòng lặp accept, session pool và phòng chat
│   │   ├── session.hpp/cpp     # Phiên làm việc của 1 client, xử lý async_read/write length-prefix
│   │   ├── room.hpp/cpp        # Quản lý danh sách thành viên trong phòng, broadcast tin nhắn
│   │   └── protocol.hpp        # Parser tin nhắn (phân tách '|') và utility đóng gói frame
│   ├── Makefile                # Compile server trên môi trường Linux
│   └── Dockerfile              # Multi-stage Dockerfile tối ưu hóa kích thước image cho server
├── client/
│   ├── src/
│   │   ├── main.cpp            # Vòng lặp đọc CLI input và thực thi các lệnh chat
│   │   └── chat_client.hpp/cpp # Quản lý kết nối client, luồng nhận tin nhắn ngầm
│   └── Makefile                # Compile client trên Linux
├── docker-compose.yml          # Triển khai nhanh cụm Server bằng Docker Compose
└── README.md                   # Tài liệu hướng dẫn dự án
```

---

## Chi tiết Giao thức truyền thông (Protocol)

Mỗi tin nhắn gửi qua TCP bao gồm **4 byte độ dài** đi trước payload.
Dưới đây là cấu trúc chuỗi Payload (các tham số phân tách bằng ký tự `|`):

| Hướng gửi | Lệnh / Sự kiện | Cú pháp Payload | Mô tả |
| :--- | :--- | :--- | :--- |
| **Client → Server** | Đăng nhập | `LOGIN\|username` | Yêu cầu đăng ký tên đăng nhập |
| **Server → Client** | Đăng nhập thành công | `LOGIN_SUCCESS\|username` | Xác nhận đăng nhập thành công |
| **Server → Client** | Đăng nhập thất bại | `LOGIN_FAIL\|reason` | Từ chối đăng nhập (trùng tên, tên trống,...) |
| **Client → Server** | Tham gia phòng | `JOIN\|room_name` | Tham gia phòng chat `room_name` |
| **Server → Client** | Xác nhận vào phòng | `ROOM_JOINED\|room_name` | Trả về thông báo bạn đã vào phòng |
| **Server → Client** | User khác vào phòng | `JOIN_NOTIFY\|room_name\|username` | Thông báo cho các thành viên trong phòng |
| **Client → Server** | Rời phòng | `LEAVE\|room_name` | Rời khỏi phòng chat `room_name` |
| **Server → Client** | Xác nhận rời phòng | `ROOM_LEFT\|room_name` | Trả về thông báo bạn đã rời phòng |
| **Server → Client** | User khác rời phòng | `LEAVE_NOTIFY\|room_name\|username` | Thông báo cho các thành viên trong phòng |
| **Client → Server** | Chat trong phòng | `MSG\|room_name\|content` | Gửi tin nhắn tới phòng |
| **Server → Client** | Nhận tin phòng | `ROOM_MSG\|room_name\|sender\|content` | Truyền tin nhắn phòng tới các thành viên |
| **Client → Server** | Chat riêng tư | `PRIVATE\|target_user\|content` | Gửi tin nhắn mật tới `target_user` |
| **Server → Client** | Nhận tin riêng tư | `PRIVATE_MSG\|sender\|content` | Gửi tin nhắn mật tới người nhận |
| **Server → Client** | Xác nhận tin riêng tư | `PRIVATE_CONFIRM\|target_user\|content` | Gửi xác nhận hiển thị lại cho người gửi |
| **Client → Server** | Xem danh sách phòng | `LIST_ROOMS` | Yêu cầu danh sách các phòng |
| **Server → Client** | Trả về danh sách phòng | `ROOMS\|room1,room2,...` | Trả về chuỗi tên phòng cách nhau bởi dấu phẩy |
| **Client → Server** | Xem danh sách user | `LIST_USERS` | Yêu cầu danh sách người dùng online |
| **Server → Client** | Trả về danh sách user | `USERS\|user1,user2,...` | Trả về chuỗi username cách nhau bởi dấu phẩy |
| **Client → Server** | Thoát | `QUIT` | Thông báo đóng kết nối sạch sẽ |

---

## Hướng dẫn Build và Chạy dự án

Yêu cầu tiên quyết: Máy của bạn đã cài đặt **Docker** & **docker-compose** (đối với Server) và thư viện **Boost C++** (đối với Client).

### 1. Khởi động Server bằng Docker Compose (Khuyên dùng)
Việc sử dụng Docker giúp bạn bỏ qua các công đoạn cài đặt Boost phức tạp trên máy cục bộ.
Chạy lệnh sau tại thư mục gốc `chat-app`:

```bash
docker-compose up --build
```
Server sẽ được build tự động và lắng nghe trên cổng `8080`.

*(Hoặc nếu bạn chạy trên Linux có sẵn Boost và muốn build thủ công không qua Docker: `cd server && make` và chạy `./chat_server 8080`)*

### 2. Biên dịch Client (C++ CLI)
Đảm bảo máy bạn đã cài thư viện Boost (ví dụ trên Ubuntu/Debian: `sudo apt-get install libboost-system-dev libboost-dev`).
Tại thư mục gốc `chat-app`, chuyển tới thư mục client và tiến hành build:

```bash
cd client
make
```
File thực thi `chat_client` sẽ được tạo ra.

### 3. Trải nghiệm ứng dụng Chat
Mở nhiều cửa sổ terminal khác nhau để giả lập nhiều user và chạy client:

```bash
./chat_client 127.0.0.1 8080
```

1. **Đăng nhập**: Nhập username bạn muốn (ví dụ: `Alice`).
2. **Liệt kê phòng**: Gõ `/rooms` (ban đầu chưa có phòng nào).
3. **Tham gia phòng**: Gõ `/join lobby`.
4. **Liệt kê user online**: Gõ `/users`.
5. **Gửi tin nhắn trong phòng**: Gõ `/msg lobby Hello everyone!`.
6. **Chat riêng tư**: Mở terminal thứ hai, đăng nhập bằng tên `Bob`, gõ `/pm Alice Hi Alice, this is private message.`.
7. **Thoát**: Gõ `/quit` để ngắt kết nối an toàn.
