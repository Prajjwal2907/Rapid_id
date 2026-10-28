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
 * Strict textContent usage for all dynamic text to prevent XSS.
 */

window.RapidAidUnit = (function() {
  let pollInterval = null;
  let audioCtx = null;
  let beepInterval = null;
  let activeTab = 'resources';
  let unitState = null;
  let inFlight = false;

  function getToken() {
    return sessionStorage.getItem('rapidaid_token') || '';
  }

  async function callBridge(action, payload = {}) {
    const token = getToken();
    try {
      const res = await fetch('/api/bridge', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, token, payload })
      });
      return await res.json();
    } catch (err) {
      return { ok: false, error: 'Network error communicating with server' };
    }
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
        tabRes.style.fontWeight = 'bold';
        if (tabAlerts) tabAlerts.style.fontWeight = 'normal';
        document.getElementById('view-unit-resources').style.display = 'block';
        document.getElementById('view-unit-alerts').style.display = 'none';
      };
    }

    if (tabAlerts) {
      tabAlerts.onclick = () => {
        activeTab = 'alerts';
        tabAlerts.style.fontWeight = 'bold';
        if (tabRes) tabRes.style.fontWeight = 'normal';
        document.getElementById('view-unit-alerts').style.display = 'block';
        document.getElementById('view-unit-resources').style.display = 'none';
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
    statusBox.style.padding = '12px';
    statusBox.style.marginBottom = '15px';
    statusBox.style.border = '1px solid #ccc';
    statusBox.style.background = '#f9f9f9';

    const statusLine = document.createElement('div');
    statusLine.style.marginBottom = '8px';
    const stLabel = document.createElement('strong');
    stLabel.textContent = 'Current Status: ';
    statusLine.appendChild(stLabel);

    const stVal = document.createElement('span');
    stVal.textContent = status;
    stVal.style.fontWeight = 'bold';
    stVal.style.color = (status === 'Available') ? '#28a745' :
                        (status === 'Dispatched') ? '#0066cc' : '#dc3545';
    statusLine.appendChild(stVal);
    statusBox.appendChild(statusLine);

    if (isMobile) {
      const toggleBtn = document.createElement('button');
      toggleBtn.style.padding = '6px 14px';
      toggleBtn.style.cursor = 'pointer';

      if (status === 'Dispatched') {
        toggleBtn.disabled = true;
        toggleBtn.textContent = 'Status Locked: Dispatched to Incident';
        const expl = document.createElement('span');
        expl.style.marginLeft = '10px';
        expl.style.color = '#666';
        expl.style.fontSize = '12px';
        expl.textContent = '(Availability cannot be changed while actively dispatched)';
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
    locBox.style.padding = '12px';
    locBox.style.marginBottom = '15px';
    locBox.style.border = '1px solid #ccc';
    locBox.style.background = '#f9f9f9';

    const locH4 = document.createElement('h4');
    locH4.style.margin = '0 0 10px 0';
    locH4.textContent = 'Unit Geographic Coordinates';
    locBox.appendChild(locH4);

    const locInputs = document.createElement('div');
    locInputs.style.display = 'flex';
    locInputs.style.gap = '10px';
    locInputs.style.alignItems = 'center';
    locInputs.style.flexWrap = 'wrap';

    const latLabel = document.createElement('label');
    latLabel.textContent = 'Lat: ';
    const latIn = document.createElement('input');
    latIn.type = 'number';
    latIn.step = 'any';
    latIn.value = prof.lat !== undefined ? prof.lat : '';
    latIn.style.width = '120px';
    latIn.style.padding = '4px';
    latLabel.appendChild(latIn);

    const lonLabel = document.createElement('label');
    lonLabel.textContent = 'Lon: ';
    const lonIn = document.createElement('input');
    lonIn.type = 'number';
    lonIn.step = 'any';
    lonIn.value = prof.lon !== undefined ? prof.lon : '';
    lonIn.style.width = '120px';
    lonIn.style.padding = '4px';
    lonLabel.appendChild(lonIn);

    const btnUpdateLoc = document.createElement('button');
    btnUpdateLoc.textContent = 'Update Position';
    btnUpdateLoc.style.padding = '6px 12px';
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
    btnGeo.style.padding = '6px 12px';
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
    resBox.style.padding = '12px';
    resBox.style.marginBottom = '15px';
    resBox.style.border = '1px solid #ccc';
    resBox.style.background = '#f9f9f9';

    const resources = unitState.resources || {};

    if (kind === 'ambulance' || kind === 'fire_truck' || kind === 'police_unit') {
      const resH4 = document.createElement('h4');
      resH4.style.margin = '0 0 10px 0';
      const countName = (kind === 'ambulance') ? 'Paramedic Crew Count' :
                         (kind === 'fire_truck') ? 'Water Capacity (Litres)' : 'Officer Staffing Count';
      resH4.textContent = countName;
      resBox.appendChild(resH4);

      const countVal = (kind === 'ambulance') ? (resources.paramedics || 0) :
                       (kind === 'fire_truck') ? (resources.waterLitres || 0) : (resources.officers || 0);

      const d = document.createElement('div');
      d.style.display = 'flex';
      d.style.gap = '10px';
      d.style.alignItems = 'center';

      const inVal = document.createElement('input');
      inVal.type = 'number';
      inVal.min = '0';
      inVal.value = countVal;
      inVal.style.width = '100px';
      inVal.style.padding = '4px';

      const btnUpdRes = document.createElement('button');
      btnUpdRes.textContent = 'Update Resource Count';
      btnUpdRes.style.padding = '6px 12px';
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
      resH4.style.margin = '0 0 10px 0';
      resH4.textContent = 'Hospital Bed Occupancy & Capacity';
      resBox.appendChild(resH4);

      const stats = document.createElement('div');
      stats.style.marginBottom = '10px';
      stats.textContent = `Total Beds: ${resources.totalBeds || 0} | Occupied: ${resources.occupiedBeds || 0} | Available: ${resources.availableBeds || 0}`;
      resBox.appendChild(stats);

      const bedControls = document.createElement('div');
      bedControls.style.display = 'flex';
      bedControls.style.gap = '10px';
      bedControls.style.alignItems = 'center';
      bedControls.style.flexWrap = 'wrap';

      const bedIn = document.createElement('input');
      bedIn.type = 'number';
      bedIn.min = '1';
      bedIn.value = '1';
      bedIn.style.width = '80px';
      bedIn.style.padding = '4px';

      const btnAdmit = document.createElement('button');
      btnAdmit.textContent = 'Admit Patients';
      btnAdmit.style.padding = '6px 12px';
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
      btnDischarge.style.padding = '6px 12px';
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
      resH4.style.margin = '0 0 10px 0';
      resH4.textContent = 'Shelter Capacity & Occupancy';
      resBox.appendChild(resH4);

      const stats = document.createElement('div');
      stats.style.marginBottom = '10px';
      stats.textContent = `Capacity: ${resources.capacity || 0} | Occupied: ${resources.occupied || 0} | Available Space: ${resources.availableSpace || 0} | Full: ${resources.isFull ? 'YES' : 'NO'}`;
      if (resources.isFull) stats.style.color = '#d9534f';
      resBox.appendChild(stats);

      const shelterControls = document.createElement('div');
      shelterControls.style.display = 'flex';
      shelterControls.style.gap = '10px';
      shelterControls.style.alignItems = 'center';
      shelterControls.style.flexWrap = 'wrap';

      const sIn = document.createElement('input');
      sIn.type = 'number';
      sIn.min = '1';
      sIn.value = '1';
      sIn.style.width = '80px';
      sIn.style.padding = '4px';

      const btnAdmit = document.createElement('button');
      btnAdmit.textContent = 'Admit People';
      btnAdmit.style.padding = '6px 12px';
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
      btnLeave.style.padding = '6px 12px';
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
    box.style.padding = '12px';
    box.style.marginBottom = '15px';
    box.style.border = '1px solid #ccc';
    box.style.background = '#f9f9f9';

    const h4 = document.createElement('h4');
    h4.style.margin = '0 0 10px 0';
    h4.textContent = title;
    box.appendChild(h4);

    const subunits = unitState.subunits || [];
    if (subunits.length === 0) {
      const p = document.createElement('p');
      p.style.color = '#777';
      p.textContent = 'No child sub-units registered yet.';
      box.appendChild(p);
    } else {
      const table = document.createElement('table');
      table.style.width = '100%';
      table.style.borderCollapse = 'collapse';
      table.style.marginBottom = '12px';
      table.style.fontSize = '13px';

      const thead = document.createElement('thead');
      thead.innerHTML = '<tr style="border-bottom: 2px solid #ccc; text-align: left;"><th style="padding: 6px;">ID</th><th style="padding: 6px;">Name</th><th style="padding: 6px;">' + countLabel + '</th><th style="padding: 6px;">Status</th><th style="padding: 6px;">Action</th></tr>';
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      subunits.forEach(sub => {
        const tr = document.createElement('tr');
        tr.style.borderBottom = '1px solid #eee';

        const tdId = document.createElement('td');
        tdId.style.padding = '6px';
        tdId.textContent = sub.id;

        const tdName = document.createElement('td');
        tdName.style.padding = '6px';
        tdName.textContent = sub.name;

        const tdCount = document.createElement('td');
        tdCount.style.padding = '6px';
        tdCount.textContent = sub.count;

        const tdStat = document.createElement('td');
        tdStat.style.padding = '6px';
        tdStat.textContent = sub.status || (sub.available ? 'Available' : 'Unavailable');
        tdStat.style.fontWeight = 'bold';
        tdStat.style.color = sub.available ? '#28a745' : '#dc3545';

        const tdAct = document.createElement('td');
        tdAct.style.padding = '6px';
        const btnToggle = document.createElement('button');
        btnToggle.textContent = sub.available ? 'Make Unavailable' : 'Make Available';
        btnToggle.style.padding = '3px 8px';
        btnToggle.style.fontSize = '12px';
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
      box.appendChild(table);
    }

    // Add Form
    const addContainer = document.createElement('div');
    addContainer.style.display = 'flex';
    addContainer.style.gap = '8px';
    addContainer.style.alignItems = 'center';
    addContainer.style.flexWrap = 'wrap';

    const nameIn = document.createElement('input');
    nameIn.type = 'text';
    nameIn.placeholder = 'Subunit Name (e.g. Unit 1)';
    nameIn.style.padding = '4px 8px';

    const countIn = document.createElement('input');
    countIn.type = 'number';
    countIn.placeholder = countLabel;
    countIn.min = '0';
    countIn.value = '1';
    countIn.style.width = '100px';
    countIn.style.padding = '4px 8px';

    const btnAdd = document.createElement('button');
    btnAdd.textContent = addTitle;
    btnAdd.style.padding = '5px 12px';
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
  function renderAlertsTab() {
    const container = document.getElementById('view-unit-alerts');
    if (!container || !unitState) return;

    container.textContent = ''; // Clear cleanly

    const openA = unitState.openAlert;
    const kind = (unitState.profile && unitState.profile.kind) ? unitState.profile.kind : '';

    if (!openA) {
      const noBox = document.createElement('div');
      noBox.style.padding = '25px';
      noBox.style.textAlign = 'center';
      noBox.style.background = '#f0f9ff';
      noBox.style.border = '1px solid #b8daff';
      noBox.style.borderRadius = '4px';
      noBox.style.marginBottom = '20px';

      const noH3 = document.createElement('h3');
      noH3.style.color = '#004085';
      noH3.style.margin = '0 0 5px 0';
      noH3.textContent = 'No Active Dispatch Alert';
      noBox.appendChild(noH3);

      const noP = document.createElement('p');
      noP.style.margin = '0';
      noP.style.color = '#555';
      noP.textContent = 'Unit is on standby. Incoming dispatches will sound an audible alarm and appear here.';
      noBox.appendChild(noP);
      container.appendChild(noBox);
    } else {
      // Open Alert Card
      const card = document.createElement('div');
      card.style.padding = '20px';
      card.style.border = '2px solid #d9534f';
      card.style.borderRadius = '6px';
      card.style.background = (openA.state === 'Pending') ? '#fff3cd' : '#ffffff';
      card.style.boxShadow = '0 2px 6px rgba(0,0,0,0.1)';
      card.style.marginBottom = '25px';

      const cardHead = document.createElement('div');
      cardHead.style.display = 'flex';
      cardHead.style.justifyContent = 'space-between';
      cardHead.style.alignItems = 'center';
      cardHead.style.marginBottom = '12px';

      const cTitle = document.createElement('h3');
      cTitle.style.margin = '0';
      cTitle.style.color = '#a71d2a';
      cTitle.textContent = `Active Dispatch Alert #${openA.id} [${openA.state}]`;
      cardHead.appendChild(cTitle);

      const timerSpan = document.createElement('span');
      timerSpan.style.fontWeight = 'bold';
      timerSpan.style.fontSize = '14px';
      if (openA.state === 'Pending') {
        const secs = openA.secondsLeft !== undefined ? openA.secondsLeft : 60;
        if (secs <= 0) {
          timerSpan.textContent = 'Acknowledge: OVERDUE (0s)';
          timerSpan.style.color = '#dc3545';
        } else {
          timerSpan.textContent = `Acknowledge in: ${secs}s`;
          timerSpan.style.color = '#d39e00';
        }
      } else {
        timerSpan.textContent = `State: ${openA.state}`;
        timerSpan.style.color = '#28a745';
      }
      cardHead.appendChild(timerSpan);
      card.appendChild(cardHead);

      // Card Body Details
      const detailsGrid = document.createElement('div');
      detailsGrid.style.display = 'grid';
      detailsGrid.style.gridTemplateColumns = 'repeat(auto-fit, minmax(200px, 1fr))';
      detailsGrid.style.gap = '10px';
      detailsGrid.style.marginBottom = '15px';

      const addField = (lbl, val) => {
        const p = document.createElement('p');
        p.style.margin = '0';
        const st = document.createElement('strong');
        st.textContent = lbl + ': ';
        p.appendChild(st);
        const sp = document.createElement('span');
        sp.textContent = val !== undefined && val !== null ? val : 'N/A';
        p.appendChild(sp);
        detailsGrid.appendChild(p);
      };

      addField('Emergency Type', openA.type);
      addField('Severity Level', `${openA.severity} / 5`);
      addField('People Affected', openA.peopleAffected);
      addField('Location', openA.locationText);
      if (openA.lat !== undefined && openA.lon !== undefined) {
        addField('Coordinates', `${Number(openA.lat).toFixed(5)}, ${Number(openA.lon).toFixed(5)}`);
      }
      addField('Dispatcher Note', openA.note || 'None');
      card.appendChild(detailsGrid);

      // Action Buttons Container
      const btnRow = document.createElement('div');
      btnRow.style.display = 'flex';
      btnRow.style.gap = '10px';
      btnRow.style.flexWrap = 'wrap';
      btnRow.style.alignItems = 'center';

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
        btnAccept.style.padding = '8px 18px';
        btnAccept.style.fontWeight = 'bold';
        btnAccept.style.background = '#28a745';
        btnAccept.style.color = '#ffffff';
        btnAccept.style.border = 'none';
        btnAccept.style.borderRadius = '3px';
        btnAccept.style.cursor = 'pointer';
        btnAccept.onclick = () => sendResp('accept');

        const btnDecline = document.createElement('button');
        btnDecline.textContent = 'Decline Dispatch';
        btnDecline.style.padding = '8px 14px';
        btnDecline.style.background = '#dc3545';
        btnDecline.style.color = '#ffffff';
        btnDecline.style.border = 'none';
        btnDecline.style.borderRadius = '3px';
        btnDecline.style.cursor = 'pointer';
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
          btnEnRoute.style.padding = '8px 16px';
          btnEnRoute.style.fontWeight = 'bold';
          btnEnRoute.onclick = () => sendResp('en_route');
          btnRow.appendChild(btnEnRoute);
        }
        const btnClear = document.createElement('button');
        btnClear.textContent = 'Clear Alert';
        btnClear.style.padding = '8px 16px';
        btnClear.onclick = () => sendResp('clear');
        btnRow.appendChild(btnClear);
      } else if (openA.state === 'EnRoute') {
        const btnOnScene = document.createElement('button');
        btnOnScene.textContent = 'Mark On Scene';
        btnOnScene.style.padding = '8px 16px';
        btnOnScene.style.fontWeight = 'bold';
        btnOnScene.onclick = () => sendResp('on_scene');
        btnRow.appendChild(btnOnScene);

        const btnClear = document.createElement('button');
        btnClear.textContent = 'Clear Alert';
        btnClear.style.padding = '8px 16px';
        btnClear.onclick = () => sendResp('clear');
        btnRow.appendChild(btnClear);
      } else if (openA.state === 'OnScene') {
        if (kind === 'ambulance') {
          const btnTrans = document.createElement('button');
          btnTrans.textContent = 'Transporting Patient';
          btnTrans.style.padding = '8px 16px';
          btnTrans.style.fontWeight = 'bold';
          btnTrans.onclick = () => sendResp('transporting');
          btnRow.appendChild(btnTrans);
        }
        const btnClear = document.createElement('button');
        btnClear.textContent = 'Clear Alert';
        btnClear.style.padding = '8px 16px';
        btnClear.onclick = () => sendResp('clear');
        btnRow.appendChild(btnClear);
      } else if (openA.state === 'Transporting') {
        const btnClear = document.createElement('button');
        btnClear.textContent = 'Clear Alert';
        btnClear.style.padding = '8px 16px';
        btnClear.onclick = () => sendResp('clear');
        btnRow.appendChild(btnClear);
      }

      card.appendChild(btnRow);
      container.appendChild(card);
    }

    // Historical 5 Closed Alerts Table
    const histBox = document.createElement('div');
    histBox.style.marginTop = '25px';

    const h4 = document.createElement('h4');
    h4.textContent = 'Recent Closed Dispatches (Last 5)';
    histBox.appendChild(h4);

    const recents = unitState.recentAlerts || [];
    if (recents.length === 0) {
      const p = document.createElement('p');
      p.style.color = '#777';
      p.textContent = 'No past closed alerts.';
      histBox.appendChild(p);
    } else {
      const table = document.createElement('table');
      table.style.width = '100%';
      table.style.borderCollapse = 'collapse';
      table.style.fontSize = '13px';

      const thead = document.createElement('thead');
      thead.innerHTML = '<tr style="border-bottom: 2px solid #ccc; text-align: left;"><th style="padding: 6px;">ID</th><th style="padding: 6px;">Type</th><th style="padding: 6px;">Severity</th><th style="padding: 6px;">Location</th><th style="padding: 6px;">Final State</th><th style="padding: 6px;">Reason</th></tr>';
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      recents.forEach(a => {
        const tr = document.createElement('tr');
        tr.style.borderBottom = '1px solid #eee';

        const addTd = val => {
          const td = document.createElement('td');
          td.style.padding = '6px';
          td.textContent = val !== undefined && val !== null ? val : '';
          tr.appendChild(td);
        };

        addTd(a.id);
        addTd(a.type);
        addTd(a.severity);
        addTd(a.locationText);
        addTd(a.state);
        addTd(a.reason || 'N/A');
        tbody.appendChild(tr);
      });
      table.appendChild(tbody);
      histBox.appendChild(table);
    }

    container.appendChild(histBox);
  }

  function disableActionButtons(container) {
    const btns = container.querySelectorAll('button');
    btns.forEach(b => { b.disabled = true; });
  }

  function showInlineError(parent, message) {
    let errDiv = parent.querySelector('.unit-inline-error');
    if (!errDiv) {
      errDiv = document.createElement('div');
      errDiv.className = 'unit-inline-error';
      errDiv.style.marginTop = '8px';
      errDiv.style.color = '#dc3545';
      errDiv.style.fontWeight = 'bold';
      errDiv.style.fontSize = '13px';
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
