# Trading System WebSocket API Documentation

Interact with the trading system via WebSocket (default port: **9001**). All messages are JSON objects. Authenticate first before submitting orders or querying data.

---

## Authentication

**Request:**
```json
{"type": "auth", "token": "your_secret_token"}
```

**Response:**
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
{"type": "submit_response", "success": true, "id": 123}
```
- `id`: order ID assigned by the system

---

## Cancel Order

**Request:**
```json
{"type": "cancel", "id": 123}
```
- `id`: unsigned integer (required, must be owned by user)

**Response:**
```json
{"type": "cancel_response", "success": true}
```

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
{"type": "modify_response", "success": true}
```

---

## Get Order Status

**Request:**
```json
{"type": "getOrderStatus", "id": 123}
```
- `id`: unsigned integer (required, must be owned by user)

**Response:**
```json
{"type": "order_status_response", "id": 123, "status": 1}
```
- `status`: integer (0=Open, 1=Filled, 2=Canceled, 3=NotFound)

---

## Get Order Book Snapshot

**Request:**
```json
{"type": "getOrderBookSnapshot"}
```
## WebSocket API

### Authentication

Authenticate with a token:

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

### Order Actions

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
  "id": 12345
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
  "success": true
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
  "success": true
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
  "status": 0
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
