const statusEl = document.getElementById('status');
const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');

let errorEl = document.getElementById('error');
if (!errorEl) {
  errorEl = document.createElement('pre');
  errorEl.id = 'error';
  errorEl.style.color = '#b00020';
  errorEl.textContent = '';
  statusEl.insertAdjacentElement('afterend', errorEl);
}

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
  if (data && data.ok === false) {
    errorEl.textContent = JSON.stringify({
      error: data.error,
      details: data.details,
      status_code: data.status_code,
      auth_url: data.auth_url,
      bot_url: data.bot_url,
      response_snippet: data.response_snippet
    }, null, 2);
  } else {
    errorEl.textContent = '';
  }
  await fetchStatus();
});

stopBtn.addEventListener('click', async () => {
  const resp = await fetch('/api/stop', { method: 'POST' });
  const data = await resp.json();
  statusEl.textContent = JSON.stringify(data, null, 2);
  if (data && data.ok === false) {
    errorEl.textContent = JSON.stringify({
      error: data.error,
      details: data.details,
      status_code: data.status_code,
      response_snippet: data.response_snippet
    }, null, 2);
  } else {
    errorEl.textContent = '';
  }
  await fetchStatus();
});

setInterval(fetchStatus, 4000);
fetchStatus();
