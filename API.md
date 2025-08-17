# Trading System WebSocket API Documentation

Interact with the trading system via WebSocket (default port: **9001**). All messages are JSON objects. Authenticate first before submitting orders or querying data.

---

## Authentication

Send this first. Optionally include a human-friendly algorithm name that will be echoed in PnL responses.

Request:
```json
{"type": "auth", "token": "your_secret_token", "name": "VWAP"}
```

Response:
```json
{"type": "auth_response", "success": true}
```

---

## Submit Order

**Request:**
```json
{"type": "submit", "price": 100.5, "qty": 10, "is_buy": true}
```
- `price`: number (required)
- `qty`: unsigned integer (required)
- `is_buy`: boolean (required)

**Response:**
```json
{"type": "submit_response", "success": true, "id": 123, "filled_qty": 4, "status": 0}
```
- `id`: order ID assigned by the system
- `filled_qty`: quantity immediately filled on match (0 if resting)
- `status`: 0=Open,1=Filled,2=Canceled,3=NotFound

---

## Cancel Order

**Request:**
```json
{"type": "cancel", "id": 123}
```
- `id`: unsigned integer (required, must be owned by user)

**Response:**
```json
{"type": "cancel_response", "success": true, "status": 2}
```
- `status`: final status (2 = Canceled) or existing status if cancellation failed

---

## Modify Order

**Request:**
```json
{"type": "modify", "id": 123, "price": 101.0, "qty": 5}
```
- `id`: unsigned integer (required, must be owned by user)
- `price`: number (required)
- `qty`: unsigned integer (required)

**Response:**
```json
{"type": "modify_response", "success": true, "status": 0}
```
- `status`: resulting status (normally 0=Open, could be 1 if fully filled during modify)

---

## Get Order Status

**Request:**
```json
{"type": "getOrderStatus", "id": 123}
```
- `id`: unsigned integer (required, must be owned by user)

**Response:**
```json
{"type": "order_status_response", "id": 123, "status": 1, "status_text": "filled"}
```
- `status`: integer (0=Open, 1=Filled, 2=Canceled, 3=NotFound)
- `status_text`: human-readable string

---

## Get Order Book Snapshot

**Request:**
```json
{"type": "getOrderBookSnapshot"}
```
## WebSocket API

### Endpoint Summary Table

| Endpoint Type         | Request Example                      | Response Example / Description                |
|----------------------|--------------------------------------|-----------------------------------------------|
| Authenticate         | `{ "type": "auth", "token": "..." }` | `{ "type": "auth_response", "success": true }` |
| Submit Order         | `{ "type": "submit", "price": 101.5, "qty": 10, "is_buy": true }` | `{ "type": "submit_response", "success": true, "id": 12345, "filled_qty": 0, "status": 0 }` |
| Cancel Order         | `{ "type": "cancel", "id": 12345 }` | `{ "type": "cancel_response", "success": true, "status": 2 }` |
| Modify Order         | `{ "type": "modify", "id": 12345, "price": 102.0, "qty": 5 }` | `{ "type": "modify_response", "success": true, "status": 0 }` |
| Get Order Status     | `{ "type": "getOrderStatus", "id": 12345 }` | `{ "type": "order_status_response", "id": 12345, "status": 0, "status_text": "open" }` |
| Get Order Book       | `{ "type": "getOrderBookSnapshot" }` | `{ "type": "order_book_snapshot_response", "bids": [...], "asks": [...] }` |
| Get Trade History    | `{ "type": "getTradeHistory" }` | `{ "type": "trade_history_response", "trades": [...] }` |
| Get Open Orders      | `{ "type": "getOpenOrdersCount" }` | `{ "type": "open_orders_count_response", "count": 2 }` |
| Get Realized PnL     | `{ "type": "getRealizedPnL" }` | `{ "type": "realized_pnl_response", "pnl": 15.25 }` |
| Get Unrealized PnL   | `{ "type": "getUnrealizedPnL" }` | `{ "type": "unrealized_pnl_response", "pnl": -3.50 }` |

#### All PnL (response shape)
Request:
```json
{ "type": "getAllPnL" }
```
Response:
```json
{
  "type": "all_pnl_response",
  "clients": [
    { "client_id": 1, "name": "VWAP", "position": 0, "realized": 0, "unrealized": 0, "avg_cost": 0 }
  ]
}
```

---

### Error Response Examples

#### Invalid Token
```json
{
  "type": "auth_response",
  "success": false,
  "message": "Invalid token"
}
```

#### Insufficient Quantity
```json
{
  "type": "submit_response",
  "success": false,
  "message": "Quantity must be greater than zero"
}
```

#### Missing Required Fields
```json
{
  "type": "error",
  "message": "Missing required fields for submit"
}
```

---

### Endpoint Details

#### Authenticate
```json
{
  "type": "auth",
  "token": "your_secret_token"
}
```
Response:
```json
{
  "type": "auth_response",
  "success": true
}
```

#### Submit Order
```json
{
  "type": "submit",
  "price": 101.5,
  "qty": 10,
  "is_buy": true
}
```
Response:
```json
{
  "type": "submit_response",
  "success": true,
  "id": 12345,
  "filled_qty": 0,
  "status": 0
}
```

#### Cancel Order
```json
{
  "type": "cancel",
  "id": 12345
}
```
Response:
```json
{
  "type": "cancel_response",
  "success": true,
  "status": 2
}
```

#### Modify Order
```json
{
  "type": "modify",
  "id": 12345,
  "price": 102.0,
  "qty": 5
}
```
Response:
```json
{
  "type": "modify_response",
  "success": true,
  "status": 0
}
```

#### Get Order Status
```json
{
  "type": "getOrderStatus",
  "id": 12345
}
```
Response:
```json
{
  "type": "order_status_response",
  "id": 12345,
  "status": 0,
  "status_text": "open"
}
```

#### Get Order Book Snapshot
```json
{
  "type": "getOrderBookSnapshot"
}
```
Response:
```json
{
  "type": "order_book_snapshot_response",
  "bids": [ ... ],
  "asks": [ ... ]
}
```

#### Get Trade History
```json
{
  "type": "getTradeHistory"
}
```
Response:
```json
{
  "type": "trade_history_response",
  "trades": [ ... ]
}
```

#### Get Open Orders Count
```json
{
  "type": "getOpenOrdersCount"
}
```
Response:
```json
{
  "type": "open_orders_count_response",
  "count": 2
}
```

#### Get Realized PnL
```json
{
  "type": "getRealizedPnL"
}
```
Response:
```json
{
  "type": "realized_pnl_response",
  "pnl": 15.25
}
```

#### Get Unrealized PnL
```json
{
  "type": "getUnrealizedPnL"
}
```
Response:
```json
{
  "type": "unrealized_pnl_response",
  "pnl": -3.50
}
```

---

### Realized vs. Unrealized PnL
- **Realized PnL** now reflects position-based accounting: when you reduce an existing position (long or short) the closed quantity realizes PnL using your average cost and trade price. Building or flipping through zero resets average cost.
- **Unrealized PnL** = Mark-to-market of your current net position using last trade price (fallback: mid of best bid/ask, else best side) plus (optionally) any edge value of resting open orders. Reported on demand.

Per-user state now includes:
```json
{
  "position": 25,
  "avg_cost": 101.32,
  "realized_pnl": 12.75
}
```

Execution events deliver incremental updates (see below).

---

### Execution Event (Push)
When any of your orders trades, you receive an execution payload:
```json
{
  "type": "execution",
  "order_id": 12345,
  "side": "buy",
  "price": 101.5,
  "quantity": 5,
  "position": 20,
  "avg_cost": 101.40,
  "realized_pnl": 7.25
}
```
Use these to update client-side portfolio state without polling.

---

### New Metrics & Endpoints

#### Get Open Orders Count

Returns the number of open orders for the authenticated user.

Request:
```json
{
  "type": "getOpenOrdersCount"
}
```
Response:
```json
{
  "type": "open_orders_count_response",
  "count": 2
}
```

#### Get Realized PnL

Returns the realized profit and loss for the authenticated user.

Request:
```json
{
  "type": "getRealizedPnL"
}
```
Response:
```json
{
  "type": "realized_pnl_response",
  "pnl": 15.25
}
```

#### Get Unrealized PnL

Returns the unrealized profit and loss for the authenticated user, based on current market prices.

Request:
```json
{
  "type": "getUnrealizedPnL"
}
```
Response:
```json
{
  "type": "unrealized_pnl_response",
  "pnl": -3.50
}
```

---

### Realized vs. Unrealized PnL

- **Realized PnL** is updated automatically when trades are executed (order fills).
- **Unrealized PnL** is calculated on demand using current best bid/ask prices for open orders.

Both metrics are available per user via the API endpoints above.

---

### PnL Broadcast (Push)
Server pushes aggregate PnL snapshots to all clients after trades:
```json
{
  "type": "all_pnl_push",
  "clients": [
    { "client_id": 1, "name": "VWAP", "position": 5, "realized": 12.5, "unrealized": -0.75, "avg_cost": 100.2 }
  ]
}
```

Notes:
- If a client did not supply a name at auth, the server falls back to "Client <id>".
- WebSocket frames may be sent as binary containing JSON text. Clients should handle Blob/ArrayBuffer and parse JSON accordingly.

---

### Correlation IDs (corr)

Requests may include an unsigned integer field `corr`. If present, the server echoes it in the corresponding direct response:
```json
{ "type": "getAllPnL", "corr": 42 }
```
Response:
```json
{ "type": "all_pnl_response", "clients": [...], "corr": 42 }
```
