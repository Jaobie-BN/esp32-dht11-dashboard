const tempEl = document.getElementById('temp');
const humEl = document.getElementById('hum');
const tempTimeEl = document.getElementById('tempTime');
const humTimeEl = document.getElementById('humTime');

const ctx = document.getElementById('chart');
const data = { labels: [], datasets: [
  { label: 'Temperature (°C)', data: [], yAxisID: 'y1' },
  { label: 'Humidity (%)',    data: [], yAxisID: 'y2' }
]};

const chart = new Chart(ctx, {
  type: 'line', data,
  options: {
    responsive: true,
    interaction: { mode: 'index', intersect: false },
    scales: {
      y1: { type: 'linear', position: 'left'  },
      y2: { type: 'linear', position: 'right', grid: { drawOnChartArea: false } },
      x:  { ticks: { maxRotation: 0 } }
    }
  }
});

const fmt = (d) => new Date(d).toLocaleString();

async function loadInitial() {
  const res = await fetch('/api/readings/recent?limit=50');
  const arr = await res.json();
  data.labels = arr.map(r => new Date(r.ts).toLocaleTimeString());
  data.datasets[0].data = arr.map(r => r.temperature);
  data.datasets[1].data = arr.map(r => r.humidity);
  chart.update();

  const latest = arr[arr.length - 1];
  if (latest) {
    tempEl.textContent = Number(latest.temperature).toFixed(1);
    humEl.textContent  = Number(latest.humidity).toFixed(1);
    tempTimeEl.textContent = fmt(latest.ts);
    humTimeEl.textContent  = fmt(latest.ts);
  }
}
loadInitial();

// Socket.IO จากโดเมนเดียวกัน
const socket = io();
socket.on('new_reading', (r) => {
  tempEl.textContent = Number(r.temperature).toFixed(1);
  humEl.textContent  = Number(r.humidity).toFixed(1);
  tempTimeEl.textContent = fmt(r.ts);
  humTimeEl.textContent  = fmt(r.ts);

  data.labels.push(new Date(r.ts).toLocaleTimeString());
  data.datasets[0].data.push(r.temperature);
  data.datasets[1].data.push(r.humidity);
  if (data.labels.length > 50) {
    data.labels.shift();
    data.datasets[0].data.shift();
    data.datasets[1].data.shift();
  }
  chart.update();
});
