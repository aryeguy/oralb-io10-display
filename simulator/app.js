(() => {
  const $ = (selector) => document.querySelector(selector);
  const surfaces = [...document.querySelectorAll('.surface')];
  const hasBackend = location.protocol !== 'file:';
  let currentState = null;
  let socket = null;
  let reconnectTimer = null;
  let toastTimer = null;

  const pressureDisplay = {
    low: ['LOW PRESSURE', 'Low', '#61aef4'],
    normal: ['GOOD PRESSURE', 'Normal', '#3ee49a'],
    high: ['TOO MUCH PRESSURE', 'High', '#ff6b79'],
  };

  function titleCase(value) {
    if (!value) return '—';
    return String(value).replaceAll('_', ' ').replace(/\b\w/g, (char) => char.toUpperCase());
  }

  function formatTime(total) {
    const value = Math.max(0, Number(total) || 0);
    const minutes = Math.floor(value / 60);
    const seconds = Math.floor(value % 60);
    return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
  }

  function showToast(message) {
    const toast = $('#toast');
    toast.textContent = message;
    toast.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove('show'), 4200);
  }

  async function post(path, body) {
    if (!hasBackend) {
      localDemo.action(body.action || body.mode);
      return;
    }
    try {
      const response = await fetch(path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      if (!response.ok) throw new Error((await response.text()) || `Request failed (${response.status})`);
      const payload = await response.json();
      if (payload.state) render(payload.state);
    } catch (error) {
      showToast(error.message);
    }
  }

  function render(state) {
    currentState = state;
    const brush = state.brush || {};
    const connection = state.connection || {};
    const position = brush.position || {};
    const calibration = state.calibration || {};
    const coverage = Array.isArray(position.coverage) ? position.coverage : Array(16).fill(0);
    const pressure = pressureDisplay[brush.pressure] || [`${titleCase(brush.pressure)} PRESSURE`, titleCase(brush.pressure), '#9badbd'];
    const connected = connection.status === 'connected';
    const demo = state.mode === 'mock';

    $('#modeName').textContent = titleCase(brush.mode || 'daily_clean').toUpperCase();
    $('#timer').textContent = formatTime(brush.elapsed_seconds);
    $('#goalValue').textContent = formatTime(brush.target_seconds || 120);
    $('#batteryValue').textContent = Number.isFinite(brush.battery) ? `${brush.battery}%` : '—';
    $('#pressureText').textContent = pressure[0];
    $('#pressureDot').style.background = pressure[2];
    $('#pressureDot').style.boxShadow = `0 0 0 4px ${pressure[2]}1f`;

    const pacer = Number(brush.pacer_sector) || 0;
    const pacerTotal = brush.pacer_sector_count || '—';
    const activeSurface = Number(position.active_surface) || 0;
    surfaces.forEach((surface, index) => {
      surface.classList.remove('partial', 'complete', 'active', 'calibration-target');
      if (coverage[index] >= 95) surface.classList.add('complete');
      else if (coverage[index] > 0) surface.classList.add('partial');
      if (activeSurface === index + 1) surface.classList.add('active');
      if (Number(position.calibration_target) === index + 1) surface.classList.add('calibration-target');
    });

    if (position.status === 'simulated') {
      const average = coverage.reduce((sum, value) => sum + value, 0) / 16;
      $('#coverageValue').textContent = `${Math.round(average)}%`;
      $('#coverageLabel').textContent = 'coverage';
      $('#surfaceDetail').textContent = `Demo surface ${activeSurface} of 16`;
      $('#positionBadge').textContent = 'SIMULATED POSITION';
      $('#positionBadge').classList.add('simulated');
    } else if (calibration.active) {
      $('#coverageValue').textContent = `${calibration.completed_surfaces || 0}/16`;
      $('#coverageLabel').textContent = 'calibrated';
      $('#surfaceDetail').textContent = calibration.current_label || 'Waiting for brush';
      $('#positionBadge').textContent = calibration.brushing ? 'CALIBRATING POSITION' : 'TURN BRUSH ON';
      $('#positionBadge').classList.remove('simulated');
    } else if (calibration.trained) {
      const average = coverage.reduce((sum, value) => sum + value, 0) / 16;
      $('#coverageValue').textContent = `${Math.round(average)}%`;
      $('#coverageLabel').textContent = 'coverage';
      const confidence = Math.round((position.confidence || 0) * 100);
      $('#surfaceDetail').textContent = activeSurface ? `${surfaces[activeSurface - 1].getAttribute('aria-label')} · ${confidence}%` : brush.brushing ? 'Finding physical position…' : 'Position model ready';
      $('#positionBadge').textContent = activeSurface ? 'LIVE PHYSICAL POSITION' : 'POSITION READY';
      $('#positionBadge').classList.remove('simulated');
    } else {
      $('#coverageValue').textContent = '—';
      $('#coverageLabel').textContent = 'position';
      $('#surfaceDetail').textContent = 'Run one guided calibration';
      $('#positionBadge').textContent = brush.motion_packet_count ? 'CALIBRATION REQUIRED' : 'POSITION AWAITING DATA';
      $('#positionBadge').classList.remove('simulated');
    }

    $('#metricPressure').textContent = pressure[1];
    $('#metricForce').textContent = Number.isFinite(brush.pressure_force) ? `Force ${brush.pressure_force}` : brush.source === 'advertisement' ? 'High flag only' : 'FF0B live';
    $('#metricPacer').textContent = pacer ? `${pacer} / ${pacerTotal}` : `— / ${pacerTotal}`;
    $('#metricPacerTime').textContent = Number.isFinite(brush.pacer_sector_timer) ? `${brush.pacer_sector_timer}s in interval` : 'Timed sector';
    $('#metricSignal').textContent = Number.isFinite(brush.rssi) ? `${brush.rssi} dBm` : '—';
    $('#metricAge').textContent = Number.isFinite(brush.packet_age_seconds) ? `${brush.packet_age_seconds}s since packet` : 'No packet';
    $('#metricMotion').textContent = `${Number(brush.motion_rate_hz || 0).toFixed(1)} Hz`;
    $('#metricMotionCount').textContent = `${brush.motion_packet_count || 0} packets`;
    $('#motionHex').textContent = brush.motion_payload_hex || 'Connect directly to receive FF0D motion packets.';
    $('#motionFormat').textContent = brush.motion_format ? titleCase(brush.motion_format).toUpperCase() : 'NO DATA';

    $('#liveMode').classList.toggle('active', !demo);
    $('#mockMode').classList.toggle('active', demo);
    $('#liveControls').hidden = demo;
    $('#mockControls').hidden = !demo;
    $('#connectButton').hidden = connected;
    $('#disconnectButton').hidden = !connected;
    $('#connectButton').disabled = !state.devices?.length || ['connecting', 'reconnecting'].includes(connection.status);

    renderDevices(state.devices || [], connection.device_id);
    renderConnection(state);
    renderCalibration(state);
    renderEvents(state.events || []);
  }

  function renderCalibration(state) {
    const calibration = state.calibration || {};
    const connection = state.connection || {};
    const demo = state.mode === 'mock';
    const active = Boolean(calibration.active);
    const trained = Boolean(calibration.trained);
    const target = $('#calibrationTarget');
    const instruction = $('#calibrationInstruction');
    const pill = $('#positionModelPill');

    $('#calibrationCard').hidden = demo;
    $('#calibrationStart').hidden = active;
    $('#calibrationCancel').hidden = !active;
    $('#calibrationStart').disabled = connection.status !== 'connected' && !(state.devices || []).length;
    $('#calibrationStart').textContent = trained ? 'Recalibrate surfaces' : 'Calibrate surfaces';
    pill.className = 'source-pill';

    let progress = 0;
    if (calibration.stage === 'complete') progress = 100;
    else if (active) {
      let within = 0;
      if (calibration.stage === 'move') within = Math.max(0, 2 - calibration.seconds_remaining) / 6;
      if (calibration.stage === 'collecting') within = (2 + Math.max(0, 4 - calibration.seconds_remaining)) / 6;
      progress = ((Number(calibration.completed_surfaces || 0) + within) / 16) * 100;
    } else if (trained) progress = 100;
    $('#calibrationProgress').style.width = `${Math.min(100, progress)}%`;

    if (demo) {
      pill.textContent = 'DEMO';
      pill.classList.add('neutral');
      return;
    }
    if (active) {
      pill.textContent = `${calibration.completed_surfaces || 0} / 16`;
      pill.classList.add('demo');
      target.textContent = calibration.current_label || 'Waiting for brush';
      if (!calibration.brushing) {
        instruction.textContent = 'Turn the brush on. The guided pass starts automatically and pauses whenever the motor stops.';
      } else if (calibration.stage === 'move') {
        instruction.textContent = `Move to the highlighted surface · collecting in ${Number(calibration.seconds_remaining || 0).toFixed(1)}s`;
      } else {
        instruction.textContent = `Brush this surface normally · ${Number(calibration.seconds_remaining || 0).toFixed(1)}s remaining`;
      }
    } else if (calibration.stage === 'error') {
      pill.textContent = 'TRY AGAIN';
      pill.classList.add('neutral');
      target.textContent = 'Calibration was not saved';
      instruction.textContent = calibration.error || 'Keep the brush running through the complete guided pass.';
    } else if (trained) {
      pill.textContent = calibration.model_source === 'public_prior_plus_local_calibration' ? 'PUBLIC + LOCAL' : 'MODEL READY';
      pill.classList.add('direct');
      target.textContent = 'Physical position tracking ready';
      instruction.textContent = calibration.model_source === 'public_prior_plus_local_calibration'
        ? 'Public brush-motion patterns are aligned to your saved grip. Recalibrate only if your grip changes.'
        : 'The model is stored only on this Mac. Recalibrate if you change how you hold the brush.';
    } else {
      pill.textContent = 'NOT CALIBRATED';
      pill.classList.add('neutral');
      target.textContent = 'Calibration required';
      instruction.textContent = 'One guided pass teaches this Mac how your brush motion maps to the 16 surfaces.';
    }
  }

  function renderDevices(devices, selectedId) {
    const select = $('#deviceSelect');
    const previous = select.value || selectedId;
    select.replaceChildren();
    if (!devices.length) {
      select.add(new Option('Still scanning…', ''));
      return;
    }
    devices.forEach((device) => {
      const signal = Number.isFinite(device.rssi) ? ` · ${device.rssi} dBm` : '';
      select.add(new Option(`${device.name}${signal}`, device.id));
    });
    if ([...select.options].some((option) => option.value === previous)) select.value = previous;
  }

  function renderConnection(state) {
    const connection = state.connection || {};
    const brush = state.brush || {};
    const title = $('#connectionTitle');
    const message = $('#connectionMessage');
    const pill = $('#sourcePill');
    message.classList.toggle('error', connection.status === 'error');
    pill.className = 'source-pill';

    if (state.mode === 'mock') {
      title.textContent = 'Virtual iO 10';
      message.textContent = brush.brushing ? 'Demo session is running through all 16 visual surfaces.' : 'Demo mode is ready. Start brushing to exercise the complete interface.';
      pill.textContent = 'DEMO';
      pill.classList.add('demo');
      return;
    }

    if (connection.status === 'connected') {
      title.textContent = connection.device_name || 'Oral-B connected';
      message.textContent = 'Direct GATT notifications are streaming from the brush to this browser.';
      pill.textContent = 'DIRECT';
      pill.classList.add('direct');
    } else if (connection.status === 'connecting') {
      title.textContent = 'Connecting directly…';
      message.textContent = 'Claiming the brush Bluetooth slot and subscribing to live characteristics.';
      pill.textContent = 'CONNECTING';
    } else if (connection.status === 'reconnecting') {
      title.textContent = 'Reconnecting to brush';
      message.textContent = connection.error || 'The brush released its idle connection. The Mac will reconnect when it is visible.';
      pill.textContent = 'RETRYING';
    } else if (connection.status === 'error') {
      title.textContent = 'Bluetooth needs attention';
      message.textContent = connection.error || 'The Bluetooth bridge could not start.';
      pill.textContent = 'ERROR';
    } else if (state.devices?.length) {
      title.textContent = brush.source === 'advertisement' ? 'Receiving passive data' : 'Toothbrush discovered';
      message.textContent = 'Passive packets are already shown. Connect directly for low/normal/high pressure, battery, and raw motion.';
      pill.textContent = 'PASSIVE';
    } else {
      title.textContent = 'Scanning for your brush';
      message.textContent = 'Wake the toothbrush or lift it from the charger so the Mac can see it.';
      pill.textContent = 'SCANNING';
    }
  }

  function renderEvents(events) {
    const list = $('#packetList');
    list.replaceChildren();
    if (!events.length) {
      const empty = document.createElement('div');
      empty.className = 'empty-packets';
      empty.textContent = 'Packets will appear here in real time.';
      list.append(empty);
      return;
    }
    [...events].reverse().slice(0, 16).forEach((event) => {
      const row = document.createElement('div');
      row.className = 'packet-row';
      const timestamp = document.createElement('time');
      timestamp.textContent = event.time ? new Date(event.time).toLocaleTimeString([], { hour12: false, minute: '2-digit', second: '2-digit' }) : '—';
      const name = document.createElement('strong');
      name.textContent = event.name || event.source;
      const value = document.createElement('code');
      value.textContent = event.hex || event.error || '—';
      row.append(timestamp, name, value);
      list.append(row);
    });
  }

  function connectSocket() {
    if (!hasBackend) return;
    clearTimeout(reconnectTimer);
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    socket = new WebSocket(`${scheme}://${location.host}/ws`);
    socket.addEventListener('open', () => {
      $('#socketDot').className = 'live-dot online';
      $('#socketText').textContent = 'Mac bridge online';
    });
    socket.addEventListener('message', (event) => {
      try { render(JSON.parse(event.data)); } catch (error) { console.error(error); }
    });
    socket.addEventListener('close', () => {
      $('#socketDot').className = 'live-dot error';
      $('#socketText').textContent = 'Bridge reconnecting…';
      reconnectTimer = setTimeout(connectSocket, 1500);
    });
    socket.addEventListener('error', () => socket.close());
  }

  const localDemo = {
    running: false,
    elapsed: 0,
    last: performance.now(),
    state: {
      type: 'state', mode: 'mock', connection: { status: 'demo', device_id: 'mock-brush', device_name: 'Virtual iO 10' }, devices: [], events: [],
      calibration: { active: false, trained: false, stage: 'not_started', completed_surfaces: 0, total_surfaces: 16 },
      brush: { valid: true, source: 'mock', name: 'Virtual iO 10', state: 'idle', brushing: false, elapsed_seconds: 0, pressure: 'normal', mode: 'daily_clean', battery: 87, rssi: -48, pacer_sector: 0, pacer_sector_count: 6, pacer_sector_timer: 0, target_seconds: 120, motion_packet_count: 0, motion_rate_hz: 0, position: { status: 'simulated', active_surface: 1, coverage: Array(16).fill(0) } },
    },
    action(action) {
      if (action === 'live') {
        showToast('Live Bluetooth requires starting the Mac bridge with start_macos.command.');
        return;
      }
      if (action === 'mock') return;
      if (action === 'start') { if (this.elapsed >= 120) this.elapsed = 0; this.running = true; }
      if (action === 'pause') this.running = false;
      if (action === 'reset') { this.running = false; this.elapsed = 0; }
      this.render();
    },
    render() {
      const brush = this.state.brush;
      const duration = 120 / 16;
      const active = Math.min(16, Math.floor(this.elapsed / duration) + 1);
      brush.elapsed_seconds = Math.floor(this.elapsed);
      brush.brushing = this.running;
      brush.state = this.running ? 'running' : 'idle';
      brush.pressure = this.running && Math.floor(this.elapsed) % 29 >= 22 && Math.floor(this.elapsed) % 29 < 26 ? 'high' : 'normal';
      brush.pacer_sector = this.running ? Math.min(6, Math.floor(this.elapsed / 20) + 1) : 0;
      brush.pacer_sector_timer = Math.floor(this.elapsed) % 20;
      brush.position.active_surface = active;
      brush.position.coverage = Array.from({ length: 16 }, (_, index) => Math.max(0, Math.min(100, (this.elapsed - index * duration) / duration * 100)));
      render(this.state);
    },
    tick(now) {
      const delta = (now - this.last) / 1000;
      this.last = now;
      if (this.running) {
        this.elapsed = Math.min(120, this.elapsed + delta);
        if (this.elapsed >= 120) this.running = false;
      }
      this.render();
      requestAnimationFrame((time) => this.tick(time));
    },
  };

  $('#liveMode').addEventListener('click', () => post('/api/mode', { mode: 'live' }));
  $('#mockMode').addEventListener('click', () => post('/api/mode', { mode: 'mock' }));
  $('#connectButton').addEventListener('click', () => post('/api/connect', { device_id: $('#deviceSelect').value }));
  $('#disconnectButton').addEventListener('click', () => post('/api/disconnect', {}));
  $('#mockStart').addEventListener('click', () => post('/api/mock', { action: 'start' }));
  $('#mockPause').addEventListener('click', () => post('/api/mock', { action: 'pause' }));
  $('#mockReset').addEventListener('click', () => post('/api/mock', { action: 'reset' }));
  $('#calibrationStart').addEventListener('click', () => post('/api/calibration/start', {}));
  $('#calibrationCancel').addEventListener('click', () => post('/api/calibration/cancel', {}));

  if (hasBackend) {
    connectSocket();
  } else {
    $('#socketDot').className = 'live-dot online';
    $('#socketText').textContent = 'Standalone demo';
    $('#downloadCapture').hidden = true;
    localDemo.render();
    requestAnimationFrame((time) => localDemo.tick(time));
  }
})();
