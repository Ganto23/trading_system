import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

const WS_URL = (import.meta.env.VITE_WS_URL) || 'ws://localhost:9001';
const AUTH_TOKEN = (import.meta.env.VITE_AUTH_TOKEN) || 'your_secret_token';

export default function useWebSocket() {
  const wsRef = useRef(null);
  const [ready, setReady] = useState(false);
  const [pnlByClient, setPnlByClient] = useState([]);
  const [trades, setTrades] = useState([]);
  const [orderBook, setOrderBook] = useState({ bids: [], asks: [] });
  const [lastError, setLastError] = useState(null);

  // Centralized message handler to keep update logic in one place
  const handleMessage = useCallback((msg) => {
    if (!msg || typeof msg !== 'object') return;
    const t = msg.type;
    console.log('[WS] handle:', t, msg);
    switch (t) {
      case 'welcome':
      case 'auth_response':
        // no UI state change needed
        return;
      case 'all_pnl_response':
      case 'all_pnl_push': {
        const list = msg.clients || [];
        console.log('[WS] setPnlByClient size=', list.length);
        setPnlByClient(list);
        return;
      }
      case 'order_book_snapshot_response': {
        const bids = msg.bids || [];
        const asks = msg.asks || [];
        console.log('[WS] setOrderBook bids=', bids.length, 'asks=', asks.length);
        setOrderBook({ bids, asks });
        return;
      }
      case 'trade_history_response': {
        const th = msg.trades || [];
        console.log('[WS] setTrades (history) size=', th.length);
        setTrades(th);
        return;
      }
      case 'trade': {
        console.log('[WS] append trade px=', msg.price, 'qty=', msg.quantity, 'buy=', msg.buy_order_id, 'sell=', msg.sell_order_id);
        setTrades((prev) => [...prev, msg]);
        return;
      }
      case 'execution': {
        console.log('[WS] execution for order', msg.order_id);
        return;
      }
      case 'error': {
        console.warn('[WS] error:', msg.message);
        setLastError(msg.message || 'error');
        return;
      }
      default:
        console.log('[WS] unhandled message type:', t, msg);
        return;
    }
  }, []);

  const connect = useCallback(() => {
    const ws = new WebSocket(WS_URL);
    ws.binaryType = 'blob'; // ensure binary frames arrive as Blob
    wsRef.current = ws;

    ws.onopen = () => {
      console.log('[WS] open -> sending auth');
      setReady(true);
      ws.send(JSON.stringify({ type: 'auth', token: AUTH_TOKEN }));
      // Initial pulls
      ws.send(JSON.stringify({ type: 'getAllPnL', corr: 1 }));
      ws.send(JSON.stringify({ type: 'getOrderBookSnapshot', corr: 2 }));
      ws.send(JSON.stringify({ type: 'getTradeHistory', corr: 3 }));
    };

    ws.onmessage = (ev) => {
      // Handle string vs binary frames; parse and route all to handleMessage
      if (typeof ev.data === 'string') {
        console.log('[WS] recv raw (string length=', ev.data.length, ')');
        try {
          const msg = JSON.parse(ev.data);
          handleMessage(msg);
        } catch (e) {
          console.error('[WS] message parse error (string)', e);
        }
        return;
      }

      if (ev.data instanceof Blob) {
        console.log('[WS] recv raw: Blob size=', ev.data.size);
        ev.data.text().then((t) => {
          const preview = t.length > 400 ? t.slice(0, 400) + '…' : t;
          console.log('[WS] blob->text preview:', preview);
          try {
            const msg = JSON.parse(t);
            console.log('[WS] blob parsed type:', msg.type);
            handleMessage(msg);
          } catch (e) {
            console.warn('[WS] blob text is not JSON:', e);
          }
        }).catch((e) => console.warn('[WS] blob.text() failed:', e));
        return;
      }

      if (ev.data instanceof ArrayBuffer) {
        console.log('[WS] recv raw: ArrayBuffer bytes=', ev.data.byteLength);
        try {
          const text = new TextDecoder('utf-8').decode(new Uint8Array(ev.data));
          const preview = text.length > 400 ? text.slice(0, 400) + '…' : text;
          console.log('[WS] arraybuffer->text preview:', preview);
          const msg = JSON.parse(text);
          handleMessage(msg);
        } catch (e) {
          console.warn('[WS] arraybuffer decode/parse failed:', e);
        }
        return;
      }

      console.log('[WS] recv raw (unknown type):', Object.prototype.toString.call(ev.data));
    };

    ws.onerror = (e) => { console.error('[WS] onerror', e); setLastError('socket error'); };
    ws.onclose = () => {
      console.warn('[WS] close -> reconnecting in 1s');
      setReady(false);
      // retry connect in 1s
      setTimeout(() => connect(), 1000);
    };
  }, []);

  useEffect(() => {
    connect();
    return () => { wsRef.current && wsRef.current.close(); };
  }, [connect]);

  const send = useCallback((obj) => {
    const ws = wsRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      console.log('[WS] send:', obj);
      ws.send(JSON.stringify(obj));
    } else {
      console.warn('[WS] send skipped (socket not open):', obj);
    }
  }, []);

  return useMemo(() => ({ ready, pnlByClient, trades, orderBook, send, lastError }), [ready, pnlByClient, trades, orderBook, send, lastError]);
}
