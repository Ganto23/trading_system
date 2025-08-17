import React from 'react';

export default function OrderBook({ bids = [], asks = [] }) {
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
      <div>
        <strong>Bids</strong>
        <table>
          <thead>
            <tr><th>Price</th><th>Qty</th></tr>
          </thead>
          <tbody>
            {bids.slice(0, 50).map((o, i) => (
              <tr key={`b-${i}`}>
                <td>{o.price.toFixed(2)}</td>
                <td>{o.quantity}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <div>
        <strong>Asks</strong>
        <table>
          <thead>
            <tr><th>Price</th><th>Qty</th></tr>
          </thead>
          <tbody>
            {asks.slice(0, 50).map((o, i) => (
              <tr key={`a-${i}`}>
                <td>{o.price.toFixed(2)}</td>
                <td>{o.quantity}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
