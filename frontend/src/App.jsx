import React, { useEffect, useMemo } from 'react';
import useWebSocket from './hooks/useWebSocket.js';
import RealizedPnLChart from './components/RealizedPnLChart.jsx';
import UnrealizedPnLChart from './components/UnrealizedPnLChart.jsx';
import OrderBook from './components/OrderBook.jsx';
import TradesList from './components/TradesList.jsx';

export default function App() {
  const {
    pnlByClient,
    trades,
    orderBook,
    send
  } = useWebSocket();

  // Poll every 0.1s as a safety net (UI refresh)
  useEffect(() => {
    const t = setInterval(() => {
      send({ type: 'getOrderBookSnapshot' });
      send({ type: 'getTradeHistory' });
      send({ type: 'getAllPnL' });
    }, 100);
    return () => clearInterval(t);
  }, [send]);

  const realized = useMemo(() => pnlByClient.map(c => ({ id: c.client_id, name: c.name, value: c.realized })), [pnlByClient]);
  const unrealized = useMemo(() => pnlByClient.map(c => ({ id: c.client_id, name: c.name, value: c.unrealized })), [pnlByClient]);

  return (
    <div className="app">
      <div className="card realized">
        <h2>Realized PnL</h2>
        <RealizedPnLChart data={realized} />
        <div className="badge">updates every 0.1s + on server pushes</div>
      </div>
      <div className="card unrealized">
        <h2>Unrealized PnL</h2>
        <UnrealizedPnLChart data={unrealized} />
      </div>
      <div className="card orderbook">
        <h2>Order Book</h2>
        <OrderBook bids={orderBook.bids} asks={orderBook.asks} />
      </div>
      <div className="card trades">
        <h2>Recent Trades</h2>
        <TradesList trades={trades} />
      </div>
    </div>
  );
}
