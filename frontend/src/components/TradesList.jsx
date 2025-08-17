import React from 'react';

export default function TradesList({ trades = [] }) {
  const last = trades.slice(-20).reverse();
  return (
    <table>
      <thead>
        <tr>
          <th>Time</th>
          <th>Price</th>
          <th>Qty</th>
        </tr>
      </thead>
      <tbody>
        {last.map((t, i) => (
          <tr key={i}>
            <td>{new Date(t.timestamp * 1000).toLocaleTimeString()}</td>
            <td>{t.price}</td>
            <td>{t.quantity}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
