# A.E.T.H.E.R. API Documentation

Comprehensive API documentation for the A.E.T.H.E.R. P2P Node.

## Overview

The A.E.T.H.E.R. API provides RESTful endpoints for:
- **DHT Operations**: Store, retrieve, and manage distributed key-value data
- **Peer Management**: Connect, disconnect, and monitor peers
- **Node Control**: Monitor and configure node settings
- **Metrics**: Access performance and network metrics

## Base URL

```
http://localhost:7821/api/v1
```

## Authentication

The API supports two authentication methods:

### API Key Authentication
Include your API key in the `X-API-Key` header:
```bash
curl -H "X-API-Key: your-api-key" http://localhost:7821/api/v1/status
```

### Bearer Token Authentication
Include your token in the Authorization header:
```bash
curl -H "Authorization: Bearer your-token" http://localhost:7821/api/v1/status
```

## Quick Start

### Store a Value

```bash
curl -X POST http://localhost:7821/api/v1/dht/store \
  -H "Content-Type: application/json" \
  -d '{
    "key": "68656c6c6f",
    "value": "776f726c64"
  }'
```

### Retrieve a Value

```bash
curl http://localhost:7821/api/v1/dht/get/68656c6c6f
```

### List Connected Peers

```bash
curl http://localhost:7821/api/v1/peers
```

### Get Node Status

```bash
curl http://localhost:7821/api/v1/status
```

## API Endpoints

### Node Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health` | Health check |
| GET | `/status` | Node status and statistics |

### DHT Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/dht/store` | Store key-value |
| GET | `/dht/get/{key}` | Get value by key |
| DELETE | `/dht/delete/{key}` | Delete key-value |
| GET | `/dht/find/{key}` | Find responsible nodes |

### Peer Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/peers` | List peers |
| GET | `/peers/{peer_id}` | Get peer details |
| DELETE | `/peers/{peer_id}` | Disconnect peer |
| POST | `/peers/connect` | Connect to peer |

### Network Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/network/config` | Get config |
| PUT | `/network/config` | Update config |
| POST | `/network/bootstrap` | Add bootstrap node |

### Metrics Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/metrics` | Get metrics (Prometheus format) |
| GET | `/metrics/summary` | Get metrics summary |

## Data Formats

### Keys and Values

Keys and values can be encoded in:
- **Hex** (default): `68656c6c6f` for "hello"
- **Base64**: `aGVsbG8=` for "hello"

Specify encoding using the `encoding` query parameter:
```bash
curl http://localhost:7821/api/v1/dht/get/aGVsbG8=?encoding=base64
```

### Trust Levels

Peers are assigned trust levels based on behavior:

| Level | Description |
|-------|-------------|
| `VERIFIED` | Long-term trusted peer |
| `TRUSTED` | Good standing |
| `NEUTRAL` | Default for new peers |
| `DISTRUSTED` | Low trust, limited interaction |
| `BLACKLISTED` | Known bad actor |

## Error Handling

Errors return standard HTTP status codes with JSON error bodies:

```json
{
  "error": "Key not found",
  "code": "NOT_FOUND",
  "details": {
    "key": "68656c6c6f"
  }
}
```

### Common Status Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 201 | Created |
| 204 | No Content |
| 400 | Bad Request |
| 404 | Not Found |
| 409 | Conflict |
| 500 | Internal Error |
| 503 | Service Unavailable |

## Rate Limiting

API requests are rate-limited to prevent abuse:
- Default: 100 requests per minute per IP
- DHT operations: 50 per minute
- Peer operations: 20 per minute

Rate limit headers are included in responses:
```
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 95
X-RateLimit-Reset: 1625140800
```

## WebSocket API

Real-time updates are available via WebSocket:

```javascript
const ws = new WebSocket('ws://localhost:7821/ws');

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Event:', data.type, data.payload);
};

// Subscribe to events
ws.send(JSON.stringify({
  type: 'subscribe',
  channels: ['peers', 'dht']
}));
```

### Event Types

| Event | Description |
|-------|-------------|
| `peer.connected` | New peer connected |
| `peer.disconnected` | Peer disconnected |
| `dht.store` | Value stored |
| `dht.get` | Value retrieved |
| `network.status` | Network status change |

## SDKs and Client Libraries

### Python

```python
from aether import Client

client = Client('localhost', 7821)
client.connect()

# Store value
client.store('key', 'value')

# Get value
value = client.get('key')
print(value)

client.disconnect()
```

### JavaScript

```javascript
const { Client } = require('aether-client');

const client = new Client('localhost', 7821);
await client.connect();

// Store value
await client.store('key', 'value');

// Get value
const value = await client.get('key');
console.log(value);

await client.disconnect();
```

## OpenAPI Specification

The full OpenAPI 3.0 specification is available at:
- [openapi.yaml](./openapi.yaml)

You can view it using Swagger UI:
```bash
# Using Docker
docker run -p 8080:8080 -e SWAGGER_JSON=/api/openapi.yaml \
  -v $(pwd)/docs/api:/api swaggerapi/swagger-ui
```

## Testing

Test the API using the included test suite:

```bash
# Run API tests
pytest tests/test_api.py

# Run with coverage
pytest tests/test_api.py --cov=aether
```

## Support

For issues and questions:
- GitHub Issues: https://github.com/aether/issues
- Documentation: https://docs.aether.network
