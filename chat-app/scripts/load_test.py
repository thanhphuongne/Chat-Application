import asyncio
import struct
import random
import time
import sys

# Protocol framing helpers
def pack_msg(payload: str) -> bytes:
    payload_bytes = payload.encode('utf-8')
    return struct.pack('>I', len(payload_bytes)) + payload_bytes

async def read_exact(reader, num_bytes) -> bytes:
    try:
        data = await reader.readexactly(num_bytes)
        return data
    except Exception:
        return None

# Stat counters
successful_connections = 0
login_failures = 0
messages_sent = 0
pings_replied = 0

async def simulate_client(client_id, host, port):
    global successful_connections, login_failures, messages_sent, pings_replied
    
    username = f"Bot_{client_id}_{random.randint(1000, 9999)}"
    password = "password123"
    
    try:
        reader, writer = await asyncio.open_connection(host, port)
    except Exception as e:
        print(f"[Client {client_id}] Failed to connect: {e}")
        return

    successful_connections += 1
    
    # Receive loop task running concurrently
    async def read_loop():
        global pings_replied
        while True:
            try:
                header = await read_exact(reader, 4)
                if not header:
                    break
                length = struct.unpack('>I', header)[0]
                payload_bytes = await read_exact(reader, length)
                if not payload_bytes:
                    break
                
                payload = payload_bytes.decode('utf-8')
                parts = payload.split('|')
                
                if parts[0] == "PING":
                    # Respond to heartbeat immediately
                    writer.write(pack_msg("PONG"))
                    await writer.drain()
                    pings_replied += 1
            except asyncio.CancelledError:
                break
            except Exception:
                break

    read_task = asyncio.create_task(read_loop())

    try:
        # 1. Register Account
        writer.write(pack_msg(f"REGISTER|{username}|{password}"))
        await writer.drain()
        await asyncio.sleep(0.5)

        # 2. Login
        writer.write(pack_msg(f"LOGIN|{username}|{password}"))
        await writer.drain()
        await asyncio.sleep(0.5)

        # 3. Join a lobby room
        writer.write(pack_msg("JOIN|lobby"))
        await writer.drain()
        await asyncio.sleep(0.5)

        # 4. Message sending loop
        for _ in range(15): # Send 15 messages then quit
            if read_task.done():
                break
                
            msg_content = f"Hello load test! Message index: {random.randint(1, 1000)}"
            writer.write(pack_msg(f"MSG|lobby|{msg_content}"))
            await writer.drain()
            messages_sent += 1
            
            # Random delay between 1.0 to 3.0 seconds to simulate human behavior
            await asyncio.sleep(random.uniform(1.0, 3.0))

        # 5. Leave room and Quit
        writer.write(pack_msg("LEAVE"))
        writer.write(pack_msg("QUIT"))
        await writer.drain()
        await asyncio.sleep(0.5)

    except Exception as e:
        print(f"[Client {client_id}] Session error: {e}")
    finally:
        read_task.cancel()
        writer.close()
        await writer.wait_closed()

async def main():
    host = "127.0.0.1"
    port = 8080
    num_clients = 100

    print("==============================================")
    print("      Boost.Asio Server Load Test Script      ")
    print(f"      Target: {host}:{port}")
    print(f"      Simulating: {num_clients} Concurrent Clients")
    print("==============================================")
    
    start_time = time.time()
    
    # Launch clients with slight stagger to prevent local connection bottleneck
    tasks = []
    for i in range(num_clients):
        tasks.append(simulate_client(i, host, port))
        await asyncio.sleep(0.05)
        
    await asyncio.gather(*tasks)
    
    duration = time.time() - start_time
    
    print("\n==============================================")
    print("                 LOAD TEST REPORT             ")
    print("==============================================")
    print(f"Total Test Duration   : {duration:.2f} seconds")
    print(f"Successful Connections: {successful_connections}/{num_clients}")
    print(f"Total Messages Sent   : {messages_sent}")
    print(f"PING/PONG Heartbeats  : {pings_replied}")
    print(f"Avg Messages / Sec    : {messages_sent / duration:.2f}")
    print("==============================================")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Allow running with python load_test.py <host> <port> <num_clients>
        pass
    asyncio.run(main())
