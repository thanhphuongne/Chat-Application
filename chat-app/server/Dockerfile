# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    libboost-system-dev \
    libboost-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy Makefile and source code
COPY Makefile ./
COPY src/ ./src/

# Compile the server
RUN make clean && make

# Run stage
FROM debian:bookworm-slim

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    libboost-system1.81.0 \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the compiled binary from builder stage
COPY --from=builder /app/chat_server .

# Expose port
EXPOSE 8080

# Run the server
ENTRYPOINT ["./chat_server", "8080"]
