import React from 'react';
import { Bar } from 'react-chartjs-2';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  BarElement,
  Tooltip,
  Legend
} from 'chart.js';

ChartJS.register(CategoryScale, LinearScale, BarElement, Tooltip, Legend);

export default function RealizedPnLChart({ data }) {
  const labels = data.map(d => d.name || `Client ${d.id}`);
  const values = data.map(d => d.value);
  return (
    <Bar
      data={{
        labels,
        datasets: [{
          label: 'Realized PnL',
          data: values,
          backgroundColor: values.map(v => v >= 0 ? 'rgba(49, 196, 141, 0.6)' : 'rgba(239, 68, 68, 0.6)')
        }]
      }}
      options={{
        responsive: true,
        plugins: { legend: { display: false } },
        scales: { x: { ticks: { color: '#e6e9ee' } }, y: { ticks: { color: '#e6e9ee' } } }
      }}
    />
  );
}
