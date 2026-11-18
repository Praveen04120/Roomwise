# Multi-stage production Dockerfile for Roomwise C++17 HTTP Server
FROM gcc:13-bookworm AS builder

WORKDIR /src
COPY include/ include/
COPY backend/ backend/

RUN g++ -std=c++17 -O2 -Wall -Iinclude -Ibackend \
    backend/main.cpp backend/storage.cpp backend/scheduler.cpp backend/routes.cpp \
    -o /src/roomwise -pthread

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/roomwise /app/roomwise
COPY frontend/ /app/frontend/
COPY samples/ /app/samples/
COPY data/ /app/seed-data/

# Persistent storage volume mount point
ENV PORT=3000
ENV HOST=0.0.0.0
ENV DATA_DIR=/app/data
ENV FRONTEND_DIR=/app/frontend

EXPOSE 3000

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD curl -fsS http://127.0.0.1:${PORT}/api/health || exit 1

# Seed persistent volume only if empty on first startup
CMD ["/bin/sh", "-c", "mkdir -p \"$DATA_DIR\" && for f in floors.json rooms.json timetable.json settings.json; do [ -f \"$DATA_DIR/$f\" ] || cp \"/app/seed-data/$f\" \"$DATA_DIR/$f\"; done && exec /app/roomwise --host \"$HOST\" --port \"$PORT\" --data \"$DATA_DIR\" --frontend \"$FRONTEND_DIR\""]
