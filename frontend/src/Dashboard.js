import React, { useEffect, useState, useRef } from "react";

const WS_URL = "ws://localhost:9001";

export default function Dashboard({ token }) {
  const [connectionStatus, setConnectionStatus] = useState("Connecting...");
  const [aggregateRealized, setAggregateRealized] = useState(0);
  const [aggregateUnrealized, setAggregateUnrealized] = useState(0);
  const [openOrders, setOpenOrders] = useState(null);
  const [orderBook, setOrderBook] = useState({ bids: [], asks: [] });
  const [trades, setTrades] = useState([]);
  const [ws, setWs] = useState(null);
  const [agentSeries, setAgentSeries] = useState({}); // { client_id: [{t,r,u}] }
  const colorMapRef = useRef({});
  const nextColorIdxRef = useRef(0);
  const palette = ["#2563eb","#10b981","#f59e0b","#ef4444","#8b5cf6","#ec4899","#0ea5e9","#84cc16","#f97316","#14b8a6","#6366f1","#dc2626","#059669","#d946ef","#475569"];
  const ensureColor = (id) => {
    if (!colorMapRef.current[id]) {
      colorMapRef.current[id] = palette[nextColorIdxRef.current % palette.length];
      nextColorIdxRef.current++;
    }
    return colorMapRef.current[id];
  };

  useEffect(() => {
  const socket = new window.WebSocket(WS_URL);
    setWs(socket);

    socket.onopen = () => {
      setConnectionStatus("Connected");
      // Authenticate
      socket.send(JSON.stringify({ type: "auth", token }));
      // Request initial data
  socket.send(JSON.stringify({ type: "getAllPnL" }));
      socket.send(JSON.stringify({ type: "getOpenOrdersCount" }));
      socket.send(JSON.stringify({ type: "getOrderBookSnapshot" }));
      socket.send(JSON.stringify({ type: "getTradeHistory" }));
    };
    socket.onerror = () => setConnectionStatus("Error");
    socket.onmessage = async (event) => {
      let raw = event.data;
      // Normalize binary payloads
      if (raw instanceof Blob) {
        try { raw = await raw.text(); } catch (e) { console.error('Blob read error', e); return; }
      } else if (raw instanceof ArrayBuffer) {
        try { raw = new TextDecoder().decode(raw); } catch (e) { console.error('AB decode error', e); return; }
      }
      if (typeof raw !== 'string') {
        try { raw = String(raw); } catch { return; }
      }
      raw = raw.trim();
      if (!raw || (raw[0] !== '{' && raw[0] !== '[')) return; // ignore non-JSON frames
      let msg; try { msg = JSON.parse(raw); } catch (e) { console.error('JSON parse error', raw); return; }
      const now = Date.now();
      switch (msg.type) {
        case 'all_pnl_response':
        case 'all_pnl_push': {
          const updated = { ...agentSeries };
          let sumR = 0, sumU = 0;
          (msg.clients || []).forEach(c => {
            sumR += c.realized; sumU += c.unrealized; ensureColor(c.client_id);
            const arr = updated[c.client_id] ? [...updated[c.client_id]] : [];
            arr.push({ t: now, r: c.realized, u: c.unrealized });
            if (arr.length > 600) arr.shift();
            updated[c.client_id] = arr;
          });
          setAgentSeries(updated);
          setAggregateRealized(sumR);
          setAggregateUnrealized(sumU);
          break;
        }
        case 'open_orders_count_response':
          setOpenOrders(msg.count); break;
        case 'order_book_snapshot_response':
          setOrderBook({ bids: msg.bids, asks: msg.asks }); break;
        case 'trade_history_response':
          setTrades(msg.trades); break;
        case 'execution': {
          if (socket.readyState === 1) {
            socket.send(JSON.stringify({ type: 'getAllPnL' }));
          }
          break;
        }
        default: break;
      }
    };
    return () => socket.close();
  }, [token]);

  // Periodic refresh for aggregate in case a push was missed
  useEffect(() => {
    if (!ws) return;
    const id = setInterval(() => {
      if (ws.readyState === 1) {
        ws.send(JSON.stringify({ type: 'getAllPnL' }));
      }
    }, 1000);
    return () => clearInterval(id);
  }, [ws]);

  return (
    <div className="w-full max-w-4xl mx-auto p-4">
      <div className="flex justify-between items-center mb-6">
        <h2 className="text-xl font-bold">Trading System Dashboard</h2>
        <span className={`px-3 py-1 rounded text-white ${connectionStatus === "Connected" ? "bg-green-600" : "bg-red-600"}`}>{connectionStatus}</span>
      </div>
      <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
        <MetricCard label="Agg Realized PnL" value={aggregateRealized.toFixed(4)} />
        <MetricCard label="Agg Unrealized PnL" value={aggregateUnrealized.toFixed(4)} />
        <MetricCard label="Open Orders" value={openOrders} />
      </div>
      <div className="mb-6 bg-white p-4 rounded shadow">
        <h3 className="font-semibold mb-2">Agent Unrealized PnL</h3>
        <MultiAgentChart seriesMap={agentSeries} colors={colorMapRef.current} height={240} />
        <div className="flex flex-wrap gap-3 mt-3 text-xs">
          {Object.entries(colorMapRef.current).map(([id, color]) => (
            <span key={id} className="flex items-center"><span className="inline-block w-3 h-3 rounded-sm mr-1" style={{background:color}}></span>Agent {id}</span>
          ))}
        </div>
      </div>
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mb-6">
        <OrderBookTable bids={orderBook.bids} asks={orderBook.asks} />
        <TradeHistoryTable trades={trades} />
      </div>
    </div>
  );
}

function MultiAgentChart({ seriesMap, colors, height = 240 }) {
  const ids = Object.keys(seriesMap);
  if (!ids.length) return <div className="text-sm text-gray-500">Waiting for agent data...</div>;
  let minV = Infinity, maxV = -Infinity, tMin = Infinity, tMax = -Infinity;
  ids.forEach(id => seriesMap[id].forEach(p => { if (p.u < minV) minV = p.u; if (p.u > maxV) maxV = p.u; if (p.t < tMin) tMin = p.t; if (p.t > tMax) tMax = p.t; }));
  if (!isFinite(minV)) minV = 0; if (!isFinite(maxV)) maxV = 0; if (minV === maxV) { minV -= 0.001; maxV += 0.001; }
  const span = maxV - minV; const pad = span * 0.15; minV -= pad; maxV += pad; const micro = (maxV - minV) < 0.05;
  if (micro) { const mid = (maxV + minV)/2; minV = mid - 0.05/2; maxV = mid + 0.05/2; }
  const width = 800; const timeSpan = (tMax - tMin) || 1;
  const scaleX = t => ((t - tMin)/timeSpan) * width;
  const scaleY = v => height - ((v - minV)/(maxV - minV)) * (height - 28) - 14;
  const ticks = Array.from({length:5},(_,i)=> maxV - (i/4)*(maxV-minV));
  const zeroY = (0>=minV && 0<=maxV) ? scaleY(0) : null;
  const buildPath = arr => arr.map((p,i)=> `${i?'L':'M'}${scaleX(p.t).toFixed(2)},${scaleY(p.u).toFixed(2)}`).join(' ');
  return (
    <div className="overflow-x-auto">
      <svg width={width} height={height} className="w-full select-none" style={{fontFamily:'monospace'}}>
        <rect x={0} y={0} width={width} height={height} fill="#fafafa" stroke="#e2e8f0" />
        {ticks.map((val,i)=>{const y=scaleY(val);return <g key={i}><line x1={0} x2={width} y1={y} y2={y} stroke="#eee"/><text x={4} y={y-2} fontSize={10} fill="#555">{val.toFixed(micro?4:2)}</text></g>;})}
        {zeroY!==null && <line x1={0} x2={width} y1={zeroY} y2={zeroY} stroke="#999" strokeDasharray="4,4" />}
        {ids.map(id => { const arr = seriesMap[id]; if (!arr.length) return null; const path = buildPath(arr); const last = arr[arr.length-1]; const c = colors[id]; return (
          <g key={id}>
            <path d={path} fill="none" stroke={c} strokeWidth={1.8} />
            <circle cx={scaleX(last.t)} cy={scaleY(last.u)} r={3} fill={c} stroke="#fff" strokeWidth={1} />
            <text x={scaleX(last.t)+6} y={scaleY(last.u)+4} fontSize={9} fill={c}>{last.u.toFixed(4)}</text>
          </g>
        );})}
        {micro && <text x={width-4} y={12} fontSize={9} textAnchor="end" fill="#555">micro-scale</text>}
      </svg>
    </div>
  );
}

function MetricCard({ label, value }) {
  return (
    <div className="bg-white rounded shadow p-4 text-center">
      <div className="text-gray-500 mb-2">{label}</div>
      <div className="text-2xl font-bold">{value !== null ? value : '-'}</div>
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
