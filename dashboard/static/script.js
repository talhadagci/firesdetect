// ============================================================
// CHART CONFIGURATIONS
// ============================================================
const chartOptions = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: { legend: { display: false }, tooltip: { mode: 'index', intersect: false } },
    scales: {
        x: { display: false },
        y: { grid: { color: 'rgba(255, 255, 255, 0.05)' }, ticks: { color: '#94a3b8' } }
    },
    animation: { duration: 0 }
};

// Temp Chart
const ctxTemp = document.getElementById('tempChart').getContext('2d');
const tempChart = new Chart(ctxTemp, {
    type: 'line',
    data: { labels: [], datasets: [{ data: [], borderColor: '#f59e0b', backgroundColor: 'rgba(245, 158, 11, 0.1)', fill: true, tension: 0.4, pointRadius: 0 }] },
    options: { ...chartOptions, scales: { ...chartOptions.scales, y: { ...chartOptions.scales.y, min: 10, max: 60 } } }
});

// Gas Chart
const ctxGas = document.getElementById('gasChart').getContext('2d');
const gasChart = new Chart(ctxGas, {
    type: 'line',
    data: { labels: [], datasets: [{ data: [], borderColor: '#3b82f6', backgroundColor: 'rgba(59, 130, 246, 0.1)', fill: true, tension: 0.4, pointRadius: 0 }] },
    options: { ...chartOptions, scales: { ...chartOptions.scales, y: { ...chartOptions.scales.y, min: 0 } } }
});

// Risk Chart
const ctxRisk = document.getElementById('riskChart').getContext('2d');
const riskChart = new Chart(ctxRisk, {
    type: 'line',
    data: { labels: [], datasets: [{ data: [], borderColor: '#ef4444', backgroundColor: 'rgba(239, 68, 68, 0.1)', fill: true, tension: 0.4, pointRadius: 0 }] },
    options: { ...chartOptions, scales: { ...chartOptions.scales, y: { ...chartOptions.scales.y, min: 0, max: 100 } } }
});

// Flame Chart
const ctxFlame = document.getElementById('flameChart').getContext('2d');
const flameChart = new Chart(ctxFlame, {
    type: 'line',
    data: { labels: [], datasets: [{ data: [], borderColor: '#eab308', backgroundColor: 'rgba(234, 179, 8, 0.1)', fill: true, stepped: true, pointRadius: 0 }] },
    options: { ...chartOptions, scales: { ...chartOptions.scales, y: { ...chartOptions.scales.y, min: 0, max: 1.1, ticks: { stepSize: 1, callback: v => v === 1 ? 'FIRE' : 'SAFE' } } } }
});

const MAX_POINTS = 30;

function updateDashboard(data) {
    const timeStr = new Date().toLocaleTimeString();

    // 1. Alert Card & Overlays
    const alertCard = document.getElementById('alert-card');
    const alertIcon = document.getElementById('alert-icon');
    const alertText = document.getElementById('alert-text');
    const warningOverlay = document.getElementById('warning-overlay');
    const dangerOverlay = document.getElementById('danger-overlay');

    const alertSubtext = document.getElementById('alert-subtext');
    if (data.status === 'HIGH') {
        alertCard.className = 'card alert-card danger';
        alertIcon.textContent = '🚨'; alertText.textContent = 'YANGIN ALARMI';
        alertSubtext.textContent = `${data.active_sensors} sensör aktif! Acil durum.`;
        dangerOverlay.classList.remove('hidden'); warningOverlay.classList.add('hidden');
    } else if (data.status === 'MEDIUM') {
        alertCard.className = 'card alert-card warning';
        alertIcon.textContent = '⚠️'; alertText.textContent = 'KRİTİK RİSK';
        alertSubtext.textContent = `${data.active_sensors} sensörde anormallik var.`;
        dangerOverlay.classList.add('hidden'); warningOverlay.classList.remove('hidden');
    } else {
        alertCard.className = 'card alert-card safe';
        alertIcon.textContent = '🛡️'; alertText.textContent = 'GÜVENLİ';
        alertSubtext.textContent = 'Tüm sensörler normal.';
        dangerOverlay.classList.add('hidden'); warningOverlay.classList.add('hidden');
    }

    // 2. Risk Circle
    const pct = Math.round(data.risk * 100);
    document.getElementById('risk-val').textContent = pct + '%';
    document.getElementById('risk-status').textContent = data.status + ' RISK';
    const color = data.status === 'HIGH' ? '#ef4444' : (data.status === 'MEDIUM' ? '#eab308' : '#10b981');
    document.getElementById('risk-circle').style.background = `conic-gradient(${color} ${pct}%, rgba(255,255,255,0.05) 0%)`;

    // 3. Values
    document.getElementById('active-count').textContent = data.active_sensors;
    document.getElementById('current-temp').textContent = data.temperature.toFixed(1);
    document.getElementById('hum-val').textContent = data.humidity.toFixed(1) + '%';
    document.getElementById('current-gas').textContent = data.gas;
    document.getElementById('current-flame-adc').textContent = data.flame_raw;

    // Indicators
    document.getElementById('ind-temp').className = 'indicator ' + (data.temp_high ? 'on' : 'off');
    document.getElementById('ind-gas').className = 'indicator ' + (data.gas_high ? 'on' : 'off');
    document.getElementById('ind-flame').className = 'indicator ' + (data.flame_detected ? 'on' : 'off');

    // 4. Charts
    [tempChart, gasChart, riskChart, flameChart].forEach(c => {
        c.data.labels.push(timeStr);
        if (c.data.labels.length > MAX_POINTS) { c.data.labels.shift(); c.data.datasets[0].data.shift(); }
    });
    tempChart.data.datasets[0].data.push(data.temperature);
    gasChart.data.datasets[0].data.push(data.gas);
    riskChart.data.datasets[0].data.push(pct);
    flameChart.data.datasets[0].data.push(data.flame);
    
    tempChart.update(); gasChart.update(); riskChart.update(); flameChart.update();

    // 5. Table
    const tbody = document.getElementById('history-tbody');
    const tr = document.createElement('tr');
    tr.innerHTML = `
        <td>${timeStr}</td>
        <td>${data.temperature.toFixed(1)}</td>
        <td>${data.gas}</td>
        <td>${data.flame_raw}</td>
        <td>${pct}%</td>
        <td><span class="badge ${data.status.toLowerCase()}">${data.status}</span></td>
    `;
    tbody.insertBefore(tr, tbody.firstChild);
    if (tbody.children.length > 15) tbody.removeChild(tbody.lastChild);
}

function showDisconnect(msg) {
    document.getElementById('disconnect-screen').classList.remove('hidden');
    // Main içeriği gizle ama Header'ı bırak
    document.querySelector('main').style.opacity = '0.3';
    document.querySelector('main').style.pointerEvents = 'none';
}

function hideDisconnect() {
    document.getElementById('disconnect-screen').classList.add('hidden');
    document.querySelector('main').style.opacity = '1';
    document.querySelector('main').style.pointerEvents = 'auto';
}

function fetchData() {
    fetch('/data')
        .then(r => r.json())
        .then(d => {
            if (d.connected === false) showDisconnect(d.message);
            else { hideDisconnect(); updateDashboard(d); }
        })
        .catch(e => showDisconnect("Sunucu Hatası"));
}

setInterval(fetchData, 1000);
fetchData();
