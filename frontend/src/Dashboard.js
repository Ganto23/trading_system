import React, { useEffect, useState } from "react";

const WS_URL = "ws://localhost:9001";

export default function Dashboard({ token }) {
  const [connectionStatus, setConnectionStatus] = useState("Connecting...");
  const [realizedPnL, setRealizedPnL] = useState(null);
  const [unrealizedPnL, setUnrealizedPnL] = useState(null);
  const [openOrders, setOpenOrders] = useState(null);
  const [orderBook, setOrderBook] = useState({ bids: [], asks: [] });
  const [trades, setTrades] = useState([]);
  const [ws, setWs] = useState(null);

  useEffect(() => {
    const socket = new window.WebSocket(WS_URL);
    setWs(socket);

    socket.onopen = () => {
      setConnectionStatus("Connected");
      // Authenticate
      socket.send(JSON.stringify({ type: "auth", token }));
      // Request initial data
      socket.send(JSON.stringify({ type: "getRealizedPnL" }));
      socket.send(JSON.stringify({ type: "getUnrealizedPnL" }));
      socket.send(JSON.stringify({ type: "getOpenOrdersCount" }));
      socket.send(JSON.stringify({ type: "getOrderBookSnapshot" }));
      socket.send(JSON.stringify({ type: "getTradeHistory" }));
    };
    socket.onclose = () => setConnectionStatus("Disconnected");
    socket.onerror = () => setConnectionStatus("Error");
    socket.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.type === "realized_pnl_response") setRealizedPnL(msg.pnl);
      if (msg.type === "unrealized_pnl_response") setUnrealizedPnL(msg.pnl);
      if (msg.type === "open_orders_count_response") setOpenOrders(msg.count);
      if (msg.type === "order_book_snapshot_response") setOrderBook({ bids: msg.bids, asks: msg.asks });
      if (msg.type === "trade_history_response") setTrades(msg.trades);
    };
    return () => socket.close();
  }, [token]);

  return (
    <div className="w-full max-w-4xl mx-auto p-4">
      <div className="flex justify-between items-center mb-6">
        <h2 className="text-xl font-bold">Trading System Dashboard</h2>
        <span className={`px-3 py-1 rounded text-white ${connectionStatus === "Connected" ? "bg-green-600" : "bg-red-600"}`}>{connectionStatus}</span>
      </div>
      <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
        <MetricCard label="Realized PnL" value={realizedPnL} />
        <MetricCard label="Unrealized PnL" value={unrealizedPnL} />
        <MetricCard label="Open Orders" value={openOrders} />
      </div>
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mb-6">
        <OrderBookTable bids={orderBook.bids} asks={orderBook.asks} />
        <TradeHistoryTable trades={trades} />
      </div>
    </div>
  );
}

function MetricCard({ label, value }) {
  return (
    <div className="bg-white rounded shadow p-4 text-center">
      <div className="text-gray-500 mb-2">{label}</div>
      <div className="text-2xl font-bold">{value !== null ? value : "-"}</div>
    </div>
  );
}

function OrderBookTable({ bids, asks }) {
  return (
    <div>
      <h3 className="font-semibold mb-2">Order Book</h3>
      <div className="flex gap-4">
        <div className="w-1/2">
          <div className="font-bold text-green-700 mb-1">Bids</div>
          <table className="w-full text-sm">
            <thead><tr><th>Price</th><th>Qty</th></tr></thead>
            <tbody>
              {bids.map(bid => (
                <tr key={bid.id}>
                  <td>{bid.price}</td>
                  <td>{bid.quantity}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <div className="w-1/2">
          <div className="font-bold text-red-700 mb-1">Asks</div>
          <table className="w-full text-sm">
            <thead><tr><th>Price</th><th>Qty</th></tr></thead>
            <tbody>
              {asks.map(ask => (
                <tr key={ask.id}>
                  <td>{ask.price}</td>
                  <td>{ask.quantity}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}

function TradeHistoryTable({ trades }) {
  return (
    <div>
      <h3 className="font-semibold mb-2">Trade History</h3>
      <table className="w-full text-sm">
        <thead><tr><th>Buy ID</th><th>Sell ID</th><th>Price</th><th>Qty</th><th>Time</th></tr></thead>
        <tbody>
          {trades.map(trade => (
            <tr key={trade.timestamp + '-' + trade.buy_order_id + '-' + trade.sell_order_id}>
              <td>{trade.buy_order_id}</td>
              <td>{trade.sell_order_id}</td>
              <td>{trade.price}</td>
              <td>{trade.quantity}</td>
              <td>{trade.timestamp}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
