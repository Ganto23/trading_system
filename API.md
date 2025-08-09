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

**Response:**
```json
{
  "type": "order_book_snapshot_response",
  "bids": [
    {"id": 123, "price": 100.5, "quantity": 10, "is_buy": true, "status": 0}
  ],
  "asks": [
    {"id": 124, "price": 101.0, "quantity": 5, "is_buy": false, "status": 0}
  ]
}
```

---

## Get Trade History

**Request:**
```json
{"type": "getTradeHistory"}
```

**Response:**
```json
{
  "type": "trade_history_response",
  "trades": [
    {"buy_order_id": 123, "sell_order_id": 124, "price": 101.0, "quantity": 5, "timestamp": 1691590000}
  ]
}
```

---

## Error Responses

All errors return a message of this form:
```json
{"type": "error", "message": "Description of error"}
```

---

## Status Codes
- `0`: Open
- `1`: Filled
- `2`: Canceled
- `3`: NotFound

---

## Notes
- All requests must be valid JSON.
- You must authenticate before submitting, modifying, or canceling orders.
- Only the owner of an order can modify or cancel it.
- All responses are JSON objects.

---

## Example Workflow
1. Authenticate
2. Submit an order
3. Query order status
4. Cancel or modify order
5. Get order book or trade history

---

For more details, see the main README or source code.
