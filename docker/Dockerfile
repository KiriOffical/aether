# A.E.T.H.E.R. Node Docker Image
# Multi-stage build for minimal production image

# Stage 1: Build
FROM python:3.11-slim as builder

WORKDIR /build

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements and install dependencies
COPY python/requirements.txt .
RUN pip install --no-cache-dir --user -r requirements.txt

# Copy source code
COPY python/aether ./aether

# Stage 2: Production
FROM python:3.11-slim

# Create non-root user
RUN groupadd -r aether && useradd -r -g aether aether

WORKDIR /app

# Copy installed packages from builder
COPY --from=builder /root/.local /home/aether/.local

# Copy application code
COPY --from=builder /build/aether ./aether

# Copy entrypoint script
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Set environment variables
ENV PATH=/home/aether/.local/bin:$PATH \
    PYTHONUNBUFFERED=1 \
    AETHER_DATA_DIR=/aether_data

# Create data directory
RUN mkdir -p /aether_data && chown -R aether:aether /aether_data /app

# Switch to non-root user
USER aether

# Expose default port
EXPOSE 7821

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD python -c "import socket; s = socket.socket(); s.settimeout(5); result = s.connect_ex(('localhost', 7821)); s.close(); exit(0 if result == 0 else 1)"

# Run the node
ENTRYPOINT ["/entrypoint.sh"]
CMD ["run"]
