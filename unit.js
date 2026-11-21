/**
 * RapidAid Service Unit Interface (unit.js)
 * Manages the Unit view for role: unit.
 * Features:
 * - Real-time polling of unit_get_state every 2 seconds
 * - Tab navigation: Availability & Resources, Dispatch Alert
 * - Web Audio API alert beeper for Pending dispatches
 * - Resource management (Hospital beds, Shelter capacity, Mobile unit counters)
 * - Subunit management (Ambulances, Fire trucks, Police units)
 * - Alert lifecycle transitions (Accept, Decline, En Route, On Scene, Transporting, Clear)
 * - Strict textContent usage for all dynamic text to prevent XSS.
 * - Uses modules.css classes (prefixed unit-) and CSS variables for dark/night mode.
 * - Dispatches rapidaid:session-expired CustomEvent and stops timers on not_logged_in / session_expired.
 */

window.RapidAidUnit = (function() {
  'use strict';

  let pollInterval = null;
  let audioCtx = null;
  let beepInterval = null;
  let activeTab = 'resources';
  let unitState = null;
  let inFlight = false;
  let currentAlertMap = null;
  let currentAlertId = null;
  let currentCardRenderedState = null;
  let currentPolyline = null;
  let currentIncidentMarker = null;

  function getToken() {
    return (typeof sessionStorage !== 'undefined' && sessionStorage.getItem('rapidaid_token')) || '';
  }

  function checkSessionExpired(res) {
    if (!res) return;
    const code = res.code || (res.error === 'not_logged_in' || res.error === 'session_expired' ? res.error : null);
    if (code === 'not_logged_in' || code === 'session_expired') {
      if (typeof window !== 'undefined' && typeof window.dispatchEvent === 'function') {
        window.dispatchEvent(new CustomEvent('rapidaid:session-expired', { detail: { code: code } }));
      }
      stop();
    }
  }

  async function callBridge(action, payload = {}) {
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.callBridge === 'function') {
      try {
        const r = await window.RapidAid.callBridge(action, payload);
        checkSessionExpired(r);
        return r;
      } catch (err) {
        return { ok: false, error: err.message || 'Error communicating with bridge' };
      }
    }

    const token = getToken();
    try {
      const res = await fetch('/api/bridge', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, token, payload })
      });
      const data = await res.json();
      checkSessionExpired(data);
      return data;
    } catch (err) {
      return { ok: false, error: 'Network error communicating with server' };
    }
  }

  // Global listener for session expiry
  if (typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
    window.addEventListener('rapidaid:session-expired', function() {
      stop();
    });
  }

  function startBeeping() {
    if (beepInterval) return;
    try {
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (!AudioCtx) return;
      if (!audioCtx) audioCtx = new AudioCtx();
      if (audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
      const triggerBeep = () => {
        try {
          const osc = audioCtx.createOscillator();
          const gain = audioCtx.createGain();
          osc.type = 'sine';
          osc.frequency.setValueAtTime(880, audioCtx.currentTime);
          gain.gain.setValueAtTime(0.25, audioCtx.currentTime);
          gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.35);
          osc.connect(gain);
          gain.connect(audioCtx.destination);
          osc.start();
          osc.stop(audioCtx.currentTime + 0.35);
        } catch (e) {}
      };
      triggerBeep();
      beepInterval = setInterval(triggerBeep, 1600);
    } catch (e) {}
  }

  function stopBeeping() {
    if (beepInterval) {
      clearInterval(beepInterval);
      beepInterval = null;
    }
  }

  function init(username, profile = null) {
    const unitView = document.getElementById('unit-view');
    if (!unitView) return;

    unitView.style.display = 'block';

    const nameSpan = document.getElementById('unit-header-name');
    const kindSpan = document.getElementById('unit-header-kind');
    if (nameSpan) nameSpan.textContent = username;
    if (kindSpan && profile && profile.kind) kindSpan.textContent = profile.kind.replace('_', ' ').toUpperCase();

    initTabEvents();
    startPolling();
  }

  function stop() {
    stopPolling();
    stopBeeping();
    cleanupAlertMap();
    const unitView = document.getElementById('unit-view');
    if (unitView) unitView.style.display = 'none';
  }

  function startPolling() {
    stopPolling();
    fetchState();
    pollInterval = setInterval(fetchState, 2000);
  }

  function stopPolling() {
    if (pollInterval) {
      clearInterval(pollInterval);
      pollInterval = null;
    }
  }

  async function fetchState() {
    if (inFlight) return;
    const res = await callBridge('unit_get_state');
    if (res && res.ok) {
      unitState = res;
      renderAll();
    }
  }

  function initTabEvents() {
    const tabRes = document.getElementById('tab-unit-resources');
    const tabAlerts = document.getElementById('tab-unit-alerts');
    const btnLogout = document.getElementById('btn-unit-logout');

    if (tabRes) {
      tabRes.onclick = () => {
        activeTab = 'resources';
        tabRes.classList.add('active');
        if (tabAlerts) tabAlerts.classList.remove('active');
        const vRes = document.getElementById('view-unit-resources');
        const vAlerts = document.getElementById('view-unit-alerts');
        if (vRes) vRes.style.display = 'block';
        if (vAlerts) vAlerts.style.display = 'none';
      };
    }

    if (tabAlerts) {
      tabAlerts.onclick = () => {
        activeTab = 'alerts';
        tabAlerts.classList.add('active');
        if (tabRes) tabRes.classList.remove('active');
        const vRes = document.getElementById('view-unit-resources');
        const vAlerts = document.getElementById('view-unit-alerts');
        if (vAlerts) vAlerts.style.display = 'block';
        if (vRes) vRes.style.display = 'none';
        if (currentAlertMap) {
          setTimeout(() => {
            try { currentAlertMap.invalidateSize(); } catch (e) {}
          }, 100);
        }
      };
    }

    if (btnLogout) {
      btnLogout.onclick = async () => {
        stop();
        await callBridge('logout');
        sessionStorage.removeItem('rapidaid_token');
        if (window.RapidAidApp && window.RapidAidApp.showAuthView) {
          window.RapidAidApp.showAuthView('Logged out successfully.');
        }
      };
    }
  }

  function renderAll() {
    if (!unitState) return;

    const nameSpan = document.getElementById('unit-header-name');
    const kindSpan = document.getElementById('unit-header-kind');
    if (nameSpan && unitState.profile) nameSpan.textContent = unitState.profile.name || unitState.profile.username;
    if (kindSpan && unitState.profile) kindSpan.textContent = (unitState.profile.kind || '').replace('_', ' ').toUpperCase();

    // Check open alert status
    const openA = unitState.openAlert;
    const badge = document.getElementById('badge-unit-pending');

    if (openA && openA.state === 'Pending') {
      if (badge) badge.style.display = 'inline';
      startBeeping();
    } else {
      if (badge) badge.style.display = 'none';
      stopBeeping();
    }

    renderResourcesTab();
    renderAlertsTab();
  }

  // =========================================================================
  // Tab 1: Availability & Resources Rendering
  // =========================================================================
  function renderResourcesTab() {
    const container = document.getElementById('view-unit-resources');
    if (!container || !unitState) return;

    container.textContent = ''; // Clear previous content cleanly

    const prof = unitState.profile || {};
    const kind = prof.kind || '';
    const status = unitState.status || 'Available';
    const isMobile = (kind === 'ambulance' || kind === 'fire_truck' || kind === 'police_unit');

    // Section Header
    const h3 = document.createElement('h3');
    h3.textContent = 'Unit Status & Operational Controls';
    container.appendChild(h3);

    // 1. Status & Availability Box
    const statusBox = document.createElement('div');
    statusBox.className = 'unit-res-box';

    const statusLine = document.createElement('div');
    statusLine.className = 'unit-res-stats';
    const stLabel = document.createElement('strong');
    stLabel.textContent = 'Current Status: ';
    statusLine.appendChild(stLabel);

    const stVal = document.createElement('span');
    stVal.textContent = status;
    stVal.className = 'status-chip status-chip-' + status.toLowerCase();
    statusLine.appendChild(stVal);
    statusBox.appendChild(statusLine);

    if (isMobile) {
      const toggleBtn = document.createElement('button');
      toggleBtn.className = 'unit-btn';

      if (status === 'Dispatched') {
        toggleBtn.disabled = true;
        toggleBtn.textContent = 'Status Locked: Dispatched to Incident';
        const expl = document.createElement('span');
        expl.className = 'unit-empty-text';
        expl.textContent = ' (Availability cannot be changed while actively dispatched)';
        statusBox.appendChild(toggleBtn);
        statusBox.appendChild(expl);
      } else {
        const makeAvail = (status === 'Unavailable');
        toggleBtn.textContent = makeAvail ? 'Mark as Available' : 'Mark as Unavailable';
        toggleBtn.onclick = async () => {
          toggleBtn.disabled = true;
          const r = await callBridge('unit_set_availability', { available: makeAvail });
          if (!r.ok && r.error) {
            showInlineError(statusBox, r.error);
          }
          await fetchState();
        };
        statusBox.appendChild(toggleBtn);
      }
    }
    container.appendChild(statusBox);

    // 2. Geographic Location Controls
    const locBox = document.createElement('div');
    locBox.className = 'unit-loc-box';

    const locH4 = document.createElement('h4');
    locH4.className = 'unit-res-title';
    locH4.textContent = 'Unit Geographic Coordinates';
    locBox.appendChild(locH4);

    const locInputs = document.createElement('div');
    locInputs.className = 'unit-controls-row';

    const latLabel = document.createElement('label');
    latLabel.className = 'unit-label';
    latLabel.textContent = 'Lat: ';
    const latIn = document.createElement('input');
    latIn.type = 'number';
    latIn.step = 'any';
    latIn.value = prof.lat !== undefined ? prof.lat : '';
    latIn.className = 'unit-input unit-input-coord';
    latLabel.appendChild(latIn);

    const lonLabel = document.createElement('label');
    lonLabel.className = 'unit-label';
    lonLabel.textContent = 'Lon: ';
    const lonIn = document.createElement('input');
    lonIn.type = 'number';
    lonIn.step = 'any';
    lonIn.value = prof.lon !== undefined ? prof.lon : '';
    lonIn.className = 'unit-input unit-input-coord';
    lonLabel.appendChild(lonIn);

    const btnUpdateLoc = document.createElement('button');
    btnUpdateLoc.textContent = 'Update Position';
    btnUpdateLoc.className = 'unit-btn unit-btn-primary';
    btnUpdateLoc.onclick = async () => {
      btnUpdateLoc.disabled = true;
      const lat = parseFloat(latIn.value);
      const lon = parseFloat(lonIn.value);
      const res = await callBridge('unit_update_location', { lat, lon });
      if (!res.ok && res.error) {
        showInlineError(locBox, res.error);
      }
      btnUpdateLoc.disabled = false;
      await fetchState();
    };

    const btnGeo = document.createElement('button');
    btnGeo.textContent = 'Use My Browser Location';
    btnGeo.className = 'unit-btn';
    btnGeo.onclick = () => {
      if (!navigator.geolocation) {
        showInlineError(locBox, 'Browser geolocation not supported');
        return;
      }
      navigator.geolocation.getCurrentPosition(
        async pos => {
          latIn.value = pos.coords.latitude.toFixed(5);
          lonIn.value = pos.coords.longitude.toFixed(5);
          btnUpdateLoc.click();
        },
        err => {
          showInlineError(locBox, 'Geolocation access denied: ' + err.message);
        }
      );
    };

    locInputs.appendChild(latLabel);
    locInputs.appendChild(lonLabel);
    locInputs.appendChild(btnUpdateLoc);
    locInputs.appendChild(btnGeo);
    locBox.appendChild(locInputs);
    container.appendChild(locBox);

    // 3. Kind-Specific Resource Management
    const resBox = document.createElement('div');
    resBox.className = 'unit-res-box';

    const resources = unitState.resources || {};

    if (kind === 'ambulance' || kind === 'fire_truck' || kind === 'police_unit') {
      const resH4 = document.createElement('h4');
      resH4.className = 'unit-res-title';
      const countName = (kind === 'ambulance') ? 'Paramedic Crew Count' :
                         (kind === 'fire_truck') ? 'Water Capacity (Litres)' : 'Officer Staffing Count';
      resH4.textContent = countName;
      resBox.appendChild(resH4);

      const countVal = (kind === 'ambulance') ? (resources.paramedics || 0) :
                       (kind === 'fire_truck') ? (resources.waterLitres || 0) : (resources.officers || 0);

      const d = document.createElement('div');
      d.className = 'unit-controls-row';

      const inVal = document.createElement('input');
      inVal.type = 'number';
      inVal.min = '0';
      inVal.value = countVal;
      inVal.className = 'unit-input unit-input-count';

      const btnUpdRes = document.createElement('button');
      btnUpdRes.textContent = 'Update Resource Count';
      btnUpdRes.className = 'unit-btn unit-btn-primary';
      btnUpdRes.onclick = async () => {
        btnUpdRes.disabled = true;
        const val = parseInt(inVal.value, 10);
        const r = await callBridge('unit_update_resources', { value: val });
        if (!r.ok && r.error) {
          showInlineError(resBox, r.error);
        }
        btnUpdRes.disabled = false;
        await fetchState();
      };

      d.appendChild(inVal);
      d.appendChild(btnUpdRes);
      resBox.appendChild(d);
      container.appendChild(resBox);
    } else if (kind === 'hospital') {
      const resH4 = document.createElement('h4');
      resH4.className = 'unit-res-title';
      resH4.textContent = 'Hospital Bed Occupancy & Capacity';
      resBox.appendChild(resH4);

      const stats = document.createElement('div');
      stats.className = 'unit-res-stats';
      stats.textContent = `Total Beds: ${resources.totalBeds || 0} | Occupied: ${resources.occupiedBeds || 0} | Available: ${resources.availableBeds || 0}`;
      resBox.appendChild(stats);

      const bedControls = document.createElement('div');
      bedControls.className = 'unit-controls-row';

      const bedIn = document.createElement('input');
      bedIn.type = 'number';
      bedIn.min = '1';
      bedIn.value = '1';
      bedIn.className = 'unit-input unit-input-count';

      const btnAdmit = document.createElement('button');
      btnAdmit.textContent = 'Admit Patients';
      btnAdmit.className = 'unit-btn unit-btn-success';
      btnAdmit.onclick = async () => {
        btnAdmit.disabled = true;
        const c = parseInt(bedIn.value, 10) || 0;
        const r = await callBridge('unit_update_resources', { action: 'admit', count: c });
        if (!r.ok && r.error) showInlineError(resBox, r.error);
        btnAdmit.disabled = false;
        await fetchState();
      };

      const btnDischarge = document.createElement('button');
      btnDischarge.textContent = 'Discharge Patients';
      btnDischarge.className = 'unit-btn unit-btn-primary';
      btnDischarge.onclick = async () => {
        btnDischarge.disabled = true;
        const c = parseInt(bedIn.value, 10) || 0;
        const r = await callBridge('unit_update_resources', { action: 'discharge', count: c });
        if (!r.ok && r.error) showInlineError(resBox, r.error);
        btnDischarge.disabled = false;
        await fetchState();
      };

      bedControls.appendChild(bedIn);
      bedControls.appendChild(btnAdmit);
      bedControls.appendChild(btnDischarge);
      resBox.appendChild(bedControls);
      container.appendChild(resBox);

      // Hospital Subunits (Ambulances)
      renderSubunitBox(container, 'Owned Hospital Ambulances', 'Add Ambulance', 'Paramedics');
    } else if (kind === 'shelter') {
      const resH4 = document.createElement('h4');
      resH4.className = 'unit-res-title';
      resH4.textContent = 'Shelter Capacity & Occupancy';
      resBox.appendChild(resH4);

      const stats = document.createElement('div');
      stats.className = 'unit-res-stats' + (resources.isFull ? ' unit-res-stats-alert' : '');
      stats.textContent = `Capacity: ${resources.capacity || 0} | Occupied: ${resources.occupied || 0} | Available Space: ${resources.availableSpace || 0} | Full: ${resources.isFull ? 'YES' : 'NO'}`;
      resBox.appendChild(stats);

      const shelterControls = document.createElement('div');
      shelterControls.className = 'unit-controls-row';

      const sIn = document.createElement('input');
      sIn.type = 'number';
      sIn.min = '1';
      sIn.value = '1';
      sIn.className = 'unit-input unit-input-count';

      const btnAdmit = document.createElement('button');
      btnAdmit.textContent = 'Admit People';
      btnAdmit.className = 'unit-btn unit-btn-success';
      btnAdmit.onclick = async () => {
        btnAdmit.disabled = true;
        const c = parseInt(sIn.value, 10) || 0;
        const r = await callBridge('unit_update_resources', { action: 'admit', count: c });
        if (!r.ok && r.error) showInlineError(resBox, r.error);
        btnAdmit.disabled = false;
        await fetchState();
      };

      const btnLeave = document.createElement('button');
      btnLeave.textContent = 'Depart People';
      btnLeave.className = 'unit-btn unit-btn-primary';
      btnLeave.onclick = async () => {
        btnLeave.disabled = true;
        const c = parseInt(sIn.value, 10) || 0;
        const r = await callBridge('unit_update_resources', { action: 'leave', count: c });
        if (!r.ok && r.error) showInlineError(resBox, r.error);
        btnLeave.disabled = false;
        await fetchState();
      };

      shelterControls.appendChild(sIn);
      shelterControls.appendChild(btnAdmit);
      shelterControls.appendChild(btnLeave);
      resBox.appendChild(shelterControls);
      container.appendChild(resBox);
    } else if (kind === 'fire_station') {
      renderSubunitBox(container, 'Assigned Fire Trucks', 'Add Fire Truck', 'Water Litres');
    } else if (kind === 'police_station') {
      renderSubunitBox(container, 'Assigned Police Patrol Units', 'Add Police Unit', 'Officers');
    }
  }

  function renderSubunitBox(container, title, addTitle, countLabel) {
    const box = document.createElement('div');
    box.className = 'unit-res-box';

    const h4 = document.createElement('h4');
    h4.className = 'unit-res-title';
    h4.textContent = title;
    box.appendChild(h4);

    const subunits = unitState.subunits || [];
    if (subunits.length === 0) {
      const p = document.createElement('p');
      p.className = 'unit-empty-text';
      p.textContent = 'No child sub-units registered yet.';
      box.appendChild(p);
    } else {
      const tableWrapper = document.createElement('div');
      tableWrapper.className = 'unit-table-responsive';

      const table = document.createElement('table');
      table.className = 'unit-table';

      const thead = document.createElement('thead');
      const trHead = document.createElement('tr');
      ['ID', 'Name', countLabel, 'Status', 'Action'].forEach(lbl => {
        const th = document.createElement('th');
        th.className = 'unit-th';
        th.textContent = lbl;
        trHead.appendChild(th);
      });
      thead.appendChild(trHead);
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      subunits.forEach(sub => {
        const tr = document.createElement('tr');

        const tdId = document.createElement('td');
        tdId.className = 'unit-td';
        tdId.textContent = sub.id;

        const tdName = document.createElement('td');
        tdName.className = 'unit-td';
        tdName.textContent = sub.name;

        const tdCount = document.createElement('td');
        tdCount.className = 'unit-td';
        tdCount.textContent = sub.count;

        const tdStat = document.createElement('td');
        tdStat.className = 'unit-td';
        const spStat = document.createElement('span');
        spStat.textContent = sub.status || (sub.available ? 'Available' : 'Unavailable');
        spStat.className = 'status-chip status-chip-' + (sub.available ? 'active' : 'disabled');
        tdStat.appendChild(spStat);

        const tdAct = document.createElement('td');
        tdAct.className = 'unit-td';
        const btnToggle = document.createElement('button');
        btnToggle.textContent = sub.available ? 'Make Unavailable' : 'Make Available';
        btnToggle.className = 'unit-btn';
        btnToggle.onclick = async () => {
          btnToggle.disabled = true;
          const r = await callBridge('unit_set_subunit_availability', { subunitId: sub.id, available: !sub.available });
          if (!r.ok && r.error) showInlineError(box, r.error);
          await fetchState();
        };
        tdAct.appendChild(btnToggle);

        tr.appendChild(tdId);
        tr.appendChild(tdName);
        tr.appendChild(tdCount);
        tr.appendChild(tdStat);
        tr.appendChild(tdAct);
        tbody.appendChild(tr);
      });
      table.appendChild(tbody);
      tableWrapper.appendChild(table);
      box.appendChild(tableWrapper);
    }

    // Add Form
    const addContainer = document.createElement('div');
    addContainer.className = 'unit-controls-row';

    const nameIn = document.createElement('input');
    nameIn.type = 'text';
    nameIn.placeholder = 'Subunit Name (e.g. Unit 1)';
    nameIn.className = 'unit-input';

    const countIn = document.createElement('input');
    countIn.type = 'number';
    countIn.placeholder = countLabel;
    countIn.min = '0';
    countIn.value = '1';
    countIn.className = 'unit-input unit-input-count';

    const btnAdd = document.createElement('button');
    btnAdd.textContent = addTitle;
    btnAdd.className = 'unit-btn unit-btn-primary';
    btnAdd.onclick = async () => {
      const sname = nameIn.value.trim();
      const scount = parseInt(countIn.value, 10) || 0;
      if (!sname) {
        showInlineError(box, 'Please specify a subunit name.');
        return;
      }
      btnAdd.disabled = true;
      const r = await callBridge('unit_add_subunit', { name: sname, count: scount });
      if (!r.ok && r.error) {
        showInlineError(box, r.error);
      } else {
        nameIn.value = '';
      }
      btnAdd.disabled = false;
      await fetchState();
    };

    addContainer.appendChild(nameIn);
    addContainer.appendChild(countIn);
    addContainer.appendChild(btnAdd);
    box.appendChild(addContainer);

    container.appendChild(box);
  }

  // =========================================================================
  // Tab 2: Dispatch Alert Rendering
  // =========================================================================
  function cleanupAlertMap() {
    if (currentAlertMap) {
      try {
        currentAlertMap.remove();
      } catch (e) {}
      currentAlertMap = null;
    }
    currentAlertId = null;
    currentCardRenderedState = null;
    currentPolyline = null;
    currentIncidentMarker = null;
  }

  function renderAlertsTab() {
    const container = document.getElementById('view-unit-alerts');
    if (!container || !unitState) return;

    let alertSlot = document.getElementById('unit-alert-slot');
    let historySlot = document.getElementById('unit-history-slot');

    if (!alertSlot || !historySlot) {
      container.textContent = '';
      alertSlot = document.createElement('div');
      alertSlot.id = 'unit-alert-slot';
      container.appendChild(alertSlot);

      historySlot = document.createElement('div');
      historySlot.id = 'unit-history-slot';
      container.appendChild(historySlot);
    }

    const openA = unitState.openAlert;
    const prof = unitState.profile || {};
    const kind = prof.kind || '';

    if (!openA) {
      if (currentAlertMap || currentAlertId !== null) {
        cleanupAlertMap();
      }
      renderNoAlertBox(alertSlot);
    } else {
      if (currentAlertId !== openA.id || !document.getElementById('unit-active-alert-card')) {
        cleanupAlertMap();
        currentAlertId = openA.id;
        renderNewAlertCard(alertSlot, openA, kind);
      } else {
        updateExistingAlertCard(openA, kind);
      }
    }

    renderHistoryTable(historySlot);
  }

  function renderNoAlertBox(slot) {
    if (slot.querySelector('#unit-no-alert-box')) return;
    slot.textContent = '';

    const noBox = document.createElement('div');
    noBox.id = 'unit-no-alert-box';
    noBox.className = 'unit-empty-box';

    const noH3 = document.createElement('h3');
    noH3.className = 'unit-empty-title';
    noH3.textContent = 'No Active Dispatch Alert';
    noBox.appendChild(noH3);

    const noP = document.createElement('p');
    noP.className = 'unit-empty-text';
    noP.textContent = 'Unit is on standby. Incoming dispatches will sound an audible alarm and appear here.';
    noBox.appendChild(noP);
    slot.appendChild(noBox);
  }

  function renderNewAlertCard(slot, openA, kind) {
    slot.textContent = '';

    const card = document.createElement('div');
    card.id = 'unit-active-alert-card';
    card.className = 'unit-alert-card' + (openA.state === 'Pending' ? ' unit-alert-card-pending' : '');

    const cardHead = document.createElement('div');
    cardHead.className = 'unit-alert-header';

    const cTitle = document.createElement('h3');
    cTitle.id = 'unit-alert-title';
    cTitle.className = 'unit-alert-title';
    cTitle.textContent = `Active Dispatch Alert #${openA.id} [${openA.state}]`;
    cardHead.appendChild(cTitle);

    const timerSpan = document.createElement('span');
    timerSpan.id = 'unit-alert-timer';
    updateTimerSpan(timerSpan, openA);
    cardHead.appendChild(timerSpan);
    card.appendChild(cardHead);

    const detailsGrid = document.createElement('div');
    detailsGrid.id = 'unit-alert-details-grid';
    detailsGrid.className = 'unit-details-grid';

    const addField = (lbl, val, fieldId = '') => {
      const p = document.createElement('p');
      p.className = 'unit-detail-item';
      if (fieldId) p.id = fieldId;
      const st = document.createElement('strong');
      st.textContent = lbl + ': ';
      p.appendChild(st);
      const sp = document.createElement('span');
      sp.className = 'field-val';
      sp.textContent = val !== undefined && val !== null ? val : 'N/A';
      p.appendChild(sp);
      detailsGrid.appendChild(p);
    };

    const incidentIdVal = (openA.incidentId !== undefined && openA.incidentId !== null)
      ? openA.incidentId
      : openA.id;
    const incText = (incidentIdVal !== undefined && incidentIdVal !== null)
      ? ("Incident #" + incidentIdVal)
      : 'N/A';
    addField('Incident ID', incText, 'field-incident-id');

    const roleVal = openA.role || openA.assignmentRole || 'primary';
    addField('Role', roleVal, 'field-role');

    let distStr = 'N/A';
    if (openA.distanceMeters !== undefined && openA.distanceMeters !== null) {
      distStr = (Number(openA.distanceMeters) / 1000).toFixed(2) + " km";
    } else if (openA.distance !== undefined && openA.distance !== null) {
      distStr = typeof openA.distance === 'number'
        ? (openA.distance / 1000).toFixed(2) + " km"
        : String(openA.distance);
    }
    addField('Distance', distStr, 'field-distance');

    addField('Emergency Type', openA.type, 'field-emergency-type');
    addField('Severity Level', `${openA.severity} / 5`, 'field-severity');

    const isHospOrShelter = (
      kind === 'hospital' ||
      kind === 'shelter' ||
      openA.kind === 'hospital' ||
      openA.kind === 'shelter' ||
      openA.role === 'hospital' ||
      openA.role === 'shelter' ||
      openA.assignmentRole === 'hospital' ||
      openA.assignmentRole === 'shelter'
    );
    const peopleCount = (openA.peopleAffected !== undefined && openA.peopleAffected !== null)
      ? openA.peopleAffected
      : ((openA.people !== undefined && openA.people !== null)
        ? openA.people
        : (openA.peopleCount !== undefined ? openA.peopleCount : 'N/A'));

    if (isHospOrShelter) {
      addField('Incoming patients/people', peopleCount, 'field-people');
    } else {
      addField('People Affected', peopleCount, 'field-people');
    }

    addField('Location', openA.locationText, 'field-location');
    if (openA.lat !== undefined && openA.lon !== undefined && openA.lat !== null && openA.lon !== null) {
      addField('Coordinates', `${Number(openA.lat).toFixed(5)}, ${Number(openA.lon).toFixed(5)}`, 'field-coords');
    }
    addField('Dispatcher Note', openA.note || 'None', 'field-note');

    card.appendChild(detailsGrid);

    const mapDiv = document.createElement('div');
    mapDiv.id = 'unit-alert-map';
    mapDiv.className = 'unit-map';
    card.appendChild(mapDiv);

    const btnRow = document.createElement('div');
    btnRow.id = 'unit-alert-buttons';
    btnRow.className = 'unit-btn-row';
    card.appendChild(btnRow);

    slot.appendChild(card);

    initAlertMap(mapDiv, openA);
    renderActionButtons(btnRow, card, openA, kind);
    currentCardRenderedState = openA.state;
  }

  function initAlertMap(mapDiv, openA) {
    if (typeof L === 'undefined') return;

    try {
      const alertMap = L.map(mapDiv, {
        zoomControl: false,
        attributionControl: false
      }).setView([openA.lat || 0, openA.lon || 0], 14);

      L.tileLayer('https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png', {
        maxZoom: 19
      }).addTo(alertMap);

      const bounds = [];

      // Destination Incident Marker
      if (openA.lat !== undefined && openA.lon !== undefined && openA.lat !== null && openA.lon !== null) {
        const destIcon = L.divIcon({
          className: 'unit-dest-marker',
          html: '<div style="background:#dc3545; color:white; border-radius:50%; width:24px; height:24px; display:flex; align-items:center; justify-content:center; font-weight:bold; font-size:12px; border:2px solid white; box-shadow:0 1px 4px rgba(0,0,0,0.4);">!</div>',
          iconSize: [24, 24],
          iconAnchor: [12, 12]
        });

        currentIncidentMarker = L.marker([openA.lat, openA.lon], { icon: destIcon }).addTo(alertMap);
        const popupDiv = document.createElement('div');
        const boldP = document.createElement('strong');
        boldP.textContent = 'Incident Destination:';
        popupDiv.appendChild(boldP);
        popupDiv.appendChild(document.createElement('br'));
        const locSpan = document.createElement('span');
        locSpan.textContent = openA.locationText;
        popupDiv.appendChild(locSpan);
        currentIncidentMarker.bindPopup(popupDiv);

        bounds.push([openA.lat, openA.lon]);
      }

      // Unit Origin Marker
      const prof = unitState ? unitState.profile : null;
      if (prof && prof.lat !== undefined && prof.lon !== undefined && prof.lat !== null && prof.lon !== null) {
        const unitIcon = L.divIcon({
          className: 'unit-origin-marker',
          html: '<div style="background:#28a745; color:white; border-radius:50%; width:20px; height:20px; display:flex; align-items:center; justify-content:center; font-weight:bold; font-size:10px; border:2px solid white; box-shadow:0 1px 4px rgba(0,0,0,0.4);">U</div>',
          iconSize: [20, 20],
          iconAnchor: [10, 10]
        });

        L.marker([prof.lat, prof.lon], { icon: unitIcon })
          .addTo(alertMap)
          .bindPopup('Your Current Position');

        bounds.push([prof.lat, prof.lon]);
      }

      // Route polyline (server coordinates [lon, lat] converted to [lat, lon])
      if (openA.route) {
        let latLngs = [];
        if (Array.isArray(openA.route.coordinates)) {
          latLngs = openA.route.coordinates.map(c => [c[1], c[0]]);
        } else if (openA.route.geometry && Array.isArray(openA.route.geometry.coordinates)) {
          latLngs = openA.route.geometry.coordinates.map(c => [c[1], c[0]]);
        }

        if (latLngs.length > 0) {
          currentPolyline = L.polyline(latLngs, {
            color: '#0066cc',
            weight: 4,
            opacity: 0.85
          }).addTo(alertMap);
          latLngs.forEach(ll => bounds.push(ll));
        }
      }

      if (bounds.length > 1) {
        alertMap.fitBounds(bounds, { padding: [25, 25] });
      } else if (bounds.length === 1) {
        alertMap.setView(bounds[0], 15);
      }

      setTimeout(() => {
        try { alertMap.invalidateSize(); } catch (e) {}
      }, 250);

      currentAlertMap = alertMap;
    } catch (err) {
      console.error('Error initializing Leaflet alert map:', err);
    }
  }

  function updateExistingAlertCard(openA, kind) {
    const card = document.getElementById('unit-active-alert-card');
    if (!card) return;

    if (openA.state === 'Pending') {
      card.classList.add('unit-alert-card-pending');
    } else {
      card.classList.remove('unit-alert-card-pending');
    }

    const cTitle = document.getElementById('unit-alert-title');
    if (cTitle) {
      cTitle.textContent = `Active Dispatch Alert #${openA.id} [${openA.state}]`;
    }

    const timerSpan = document.getElementById('unit-alert-timer');
    if (timerSpan) {
      updateTimerSpan(timerSpan, openA);
    }

    const distField = document.getElementById('field-distance');
    if (distField) {
      let distStr = 'N/A';
      if (openA.distanceMeters !== undefined && openA.distanceMeters !== null) {
        distStr = (Number(openA.distanceMeters) / 1000).toFixed(2) + " km";
      } else if (openA.distance !== undefined && openA.distance !== null) {
        distStr = typeof openA.distance === 'number'
          ? (openA.distance / 1000).toFixed(2) + " km"
          : String(openA.distance);
      }
      const valSpan = distField.querySelector('.field-val');
      if (valSpan) valSpan.textContent = distStr;
    }

    const peopleField = document.getElementById('field-people');
    if (peopleField) {
      const peopleCount = (openA.peopleAffected !== undefined && openA.peopleAffected !== null)
        ? openA.peopleAffected
        : ((openA.people !== undefined && openA.people !== null)
          ? openA.people
          : (openA.peopleCount !== undefined ? openA.peopleCount : 'N/A'));
      const valSpan = peopleField.querySelector('.field-val');
      if (valSpan) valSpan.textContent = peopleCount;
    }

    if (currentCardRenderedState !== openA.state) {
      const btnRow = document.getElementById('unit-alert-buttons');
      if (btnRow) {
        btnRow.textContent = '';
        renderActionButtons(btnRow, card, openA, kind);
      }
      currentCardRenderedState = openA.state;
    }
  }

  function updateTimerSpan(timerSpan, openA) {
    if (openA.state === 'Pending') {
      const secs = openA.secondsLeft !== undefined ? openA.secondsLeft : 60;
      if (secs <= 0) {
        timerSpan.textContent = 'Acknowledge: OVERDUE (0s)';
        timerSpan.className = 'unit-alert-timer unit-alert-timer-urgent';
      } else {
        timerSpan.textContent = `Acknowledge in: ${secs}s`;
        timerSpan.className = 'unit-alert-timer ' + (secs < 30 ? 'unit-alert-timer-urgent' : 'unit-alert-timer-warn');
      }
    } else {
      timerSpan.textContent = `State: ${openA.state}`;
      timerSpan.className = 'unit-alert-timer unit-alert-timer-normal';
    }
  }

  function renderActionButtons(btnRow, card, openA, kind) {
    const sendResp = async (action, reason = '') => {
      inFlight = true;
      disableActionButtons(btnRow);
      const r = await callBridge('unit_alert_respond', { alertId: openA.id, action, reason });
      if (!r.ok && r.error) {
        showInlineError(card, r.error);
      }
      inFlight = false;
      await fetchState();
    };

    if (openA.state === 'Pending') {
      const btnAccept = document.createElement('button');
      btnAccept.textContent = 'Accept Dispatch';
      btnAccept.className = 'unit-btn unit-btn-accept unit-btn-success';
      btnAccept.onclick = () => sendResp('accept');

      const btnDecline = document.createElement('button');
      btnDecline.textContent = 'Decline Dispatch';
      btnDecline.className = 'unit-btn unit-btn-decline unit-btn-danger';
      btnDecline.onclick = () => {
        const reason = prompt('Optional reason for declining dispatch:') || '';
        sendResp('decline', reason);
      };

      btnRow.appendChild(btnAccept);
      btnRow.appendChild(btnDecline);
    } else if (openA.state === 'Accepted') {
      if (kind === 'ambulance' || kind === 'fire_truck' || kind === 'police_unit') {
        const btnEnRoute = document.createElement('button');
        btnEnRoute.textContent = 'Mark En Route';
        btnEnRoute.className = 'unit-btn unit-btn-enroute unit-btn-primary';
        btnEnRoute.onclick = () => sendResp('en_route');
        btnRow.appendChild(btnEnRoute);
      }
      const btnClear = document.createElement('button');
      btnClear.textContent = 'Clear Alert';
      btnClear.className = 'unit-btn unit-btn-clear';
      btnClear.onclick = () => sendResp('clear');
      btnRow.appendChild(btnClear);
    } else if (openA.state === 'EnRoute') {
      const btnOnScene = document.createElement('button');
      btnOnScene.textContent = 'Mark On Scene';
      btnOnScene.className = 'unit-btn unit-btn-onscene unit-btn-primary';
      btnOnScene.onclick = () => sendResp('on_scene');
      btnRow.appendChild(btnOnScene);

      const btnClear = document.createElement('button');
      btnClear.textContent = 'Clear Alert';
      btnClear.className = 'unit-btn unit-btn-clear';
      btnClear.onclick = () => sendResp('clear');
      btnRow.appendChild(btnClear);
    } else if (openA.state === 'OnScene') {
      if (kind === 'ambulance') {
        const btnTrans = document.createElement('button');
        btnTrans.textContent = 'Transporting Patient';
        btnTrans.className = 'unit-btn unit-btn-transporting unit-btn-primary';
        btnTrans.onclick = () => sendResp('transporting');
        btnRow.appendChild(btnTrans);
      }
      const btnClear = document.createElement('button');
      btnClear.textContent = 'Clear Alert';
      btnClear.className = 'unit-btn unit-btn-clear';
      btnClear.onclick = () => sendResp('clear');
      btnRow.appendChild(btnClear);
    } else if (openA.state === 'Transporting') {
      const btnClear = document.createElement('button');
      btnClear.textContent = 'Clear Alert';
      btnClear.className = 'unit-btn unit-btn-clear';
      btnClear.onclick = () => sendResp('clear');
      btnRow.appendChild(btnClear);
    } else {
      const btnClear = document.createElement('button');
      btnClear.textContent = 'Clear Alert';
      btnClear.className = 'unit-btn unit-btn-clear';
      btnClear.onclick = () => sendResp('clear');
      btnRow.appendChild(btnClear);
    }
  }

  function renderHistoryTable(slot) {
    slot.textContent = '';
    const h4 = document.createElement('h4');
    h4.className = 'unit-res-title';
    h4.textContent = 'Recent Closed Dispatches (Last 5)';
    slot.appendChild(h4);

    const recents = (unitState && unitState.recentAlerts) ? unitState.recentAlerts : [];
    if (recents.length === 0) {
      const p = document.createElement('p');
      p.className = 'unit-empty-text';
      p.textContent = 'No past closed alerts.';
      slot.appendChild(p);
    } else {
      const tableWrapper = document.createElement('div');
      tableWrapper.className = 'unit-table-responsive';

      const table = document.createElement('table');
      table.className = 'unit-table';

      const thead = document.createElement('thead');
      const trHead = document.createElement('tr');
      ['ID', 'Type', 'Severity', 'Location', 'Final State', 'Reason'].forEach(txt => {
        const th = document.createElement('th');
        th.className = 'unit-th';
        th.textContent = txt;
        trHead.appendChild(th);
      });
      thead.appendChild(trHead);
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      recents.forEach(a => {
        const tr = document.createElement('tr');

        const addTd = (val, isState = false) => {
          const td = document.createElement('td');
          td.className = 'unit-td';
          if (isState) {
            const spState = document.createElement('span');
            spState.textContent = val !== undefined && val !== null ? val : '';
            const cleanState = String(val || '').toLowerCase().replace(/[\s_]+/g, '');
            spState.className = 'status-chip status-chip-' + cleanState;
            td.appendChild(spState);
          } else {
            td.textContent = val !== undefined && val !== null ? val : '';
          }
          tr.appendChild(td);
        };

        addTd(a.id);
        addTd(a.type);
        addTd(a.severity);
        addTd(a.locationText);
        addTd(a.state, true);
        addTd(a.reason || 'N/A');
        tbody.appendChild(tr);
      });
      table.appendChild(tbody);
      tableWrapper.appendChild(table);
      slot.appendChild(tableWrapper);
    }
  }

  function disableActionButtons(container) {
    const btns = container.querySelectorAll('button');
    btns.forEach(b => { b.disabled = true; });
  }

  function showInlineError(parent, message) {
    let errDiv = parent.querySelector('.unit-inline-error');
    if (!errDiv) {
      errDiv = document.createElement('div');
      errDiv.className = 'unit-inline-error unit-feedback-err';
      parent.appendChild(errDiv);
    }
    errDiv.textContent = message;
    setTimeout(() => {
      if (errDiv.parentNode) errDiv.parentNode.removeChild(errDiv);
    }, 4000);
  }

  return {
    init,
    stop,
    fetchState
  };
})();
