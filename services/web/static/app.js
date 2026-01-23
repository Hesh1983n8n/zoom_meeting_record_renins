const statusEl = document.getElementById('status');
const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');

async function fetchStatus() {
  const resp = await fetch('/api/status');
  const data = await resp.json();
  statusEl.textContent = JSON.stringify(data, null, 2);
}

startBtn.addEventListener('click', async () => {
  const meetingUrl = document.getElementById('meetingUrl').value.trim();
  const passcode = document.getElementById('passcode').value.trim();

  const resp = await fetch('/api/start', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ meeting_url: meetingUrl, passcode: passcode || null })
  });

  const data = await resp.json();
  statusEl.textContent = JSON.stringify(data, null, 2);
  await fetchStatus();
});

stopBtn.addEventListener('click', async () => {
  const resp = await fetch('/api/stop', { method: 'POST' });
  const data = await resp.json();
  statusEl.textContent = JSON.stringify(data, null, 2);
  await fetchStatus();
});

setInterval(fetchStatus, 4000);
fetchStatus();
