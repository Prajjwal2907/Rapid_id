/**
 * RapidAid Incident Board Module (board.js)
 * Implements window.RapidAidBoard = { init(mountEl), stop() };
 *
 * Responsibilities:
 * (a) New-incident form:
 *     - Type: select [Medical, Fire, Police, Other]
 *     - Severity: select or input 1-5 (default 3)
 *     - People affected: number input (>= 0, default 0)
 *     - Description: text input / textarea
 *     - Caller name: text input
 *     - Caller phone: text input
 *     - Location picker:
 *       - Search box calling RapidAid.callBridge('search_location', {query, localOnly: false})
 *       - Dropdown/list of candidate labels ({label, lat, lon, source})
 *       - Choosing one places marker and updates location text/coordinates
 *       - Click on shared map (RapidAid.getBoardMap()) places marker
 *       - Draggable marker calling RapidAid.callBridge('snap_point', {lat, lon})
 *       - Snapped point display, snapMeters, and red "outside mapped area" warning when withinArea is false
 *       - "Create incident" disabled until location is resolved (and withinArea is true)
 *       - Clicking "Create incident" calls create_incident then clears form on success
 * (b) Queue table:
 *     - Polled every 2s via list_incidents
 *     - Renders in returned order: priority, type, severity, peopleAffected, location text, waiting minutes, status, callerName/phone
 *     - Row click highlights / centers marker on shared map
 * (c) Per-row buttons:
 *     - "Propose": calls RapidAidDispatch.open(incident.id) or shows "Dispatch panel not installed"
 *     - "Cancel": confirmation then calls cancel_incident
 *     - "Edit" (for New or Proposed): calls update_incident, displays "Press Propose again to re-propose" after editing Proposed
 *     - Server errors shown next to the causing control
 *
 * Strict Rule: All server or user text is inserted with textContent, NEVER innerHTML.
 * Styling: Uses modules.css classes (prefixed board-) with CSS variables for seamless night mode.
 */

(function () {
  'use strict';

  // Lifecycle state
  let mountElement = null;
  let pollTimer = null;
  let searchDebounceTimer = null;
  let isFetchingIncidents = false;

  // Map and Layer state
  let currentMap = null;
  let boardLayerGroup = null;
  let pickerMarker = null;
  let snappedCircle = null;
  let incidentMarkers = {}; // incidentId -> L.marker/L.circleMarker
  let highlightedRow = null;

  // Location Picker state
  let resolvedLocation = null; // { text, lat, lon, source }
  let isResolvingLocation = false;

  // Incident Queue & Edit state
  let currentIncidents = [];
  let activeEditIncidentId = null;
  const reproposeNotices = {}; // incidentId -> boolean

  // UI Element References
  let dom = {};

  // ==========================================
  // Session Expiry & Bridge Access Helpers
  // ==========================================
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

  async function callBridge(action, payload) {
    let res = null;
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.callBridge === 'function') {
      try {
        res = await window.RapidAid.callBridge(action, payload || {});
        checkSessionExpired(res);
        return res;
      } catch (err) {
        return { ok: false, error: err.message || 'Error communicating with bridge' };
      }
    }
    if (typeof window !== 'undefined' && typeof window.callBridge === 'function') {
      try {
        res = await window.callBridge(action, payload || {});
        checkSessionExpired(res);
        return res;
      } catch (err) {
        return { ok: false, error: err.message || 'Error communicating with bridge' };
      }
    }
    const token = (typeof sessionStorage !== 'undefined' && sessionStorage.getItem('rapidaid_token')) || '';
    try {
      const httpRes = await fetch('/api/bridge', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, token, payload: payload || {} })
      });
      res = await httpRes.json();
      checkSessionExpired(res);
      return res;
    } catch (err) {
      return { ok: false, error: err.message || 'Network error communicating with server' };
    }
  }

  // Listen for global session-expired events
  if (typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
    window.addEventListener('rapidaid:session-expired', function () {
      stop();
    });
  }

  function getBoardMap() {
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.getBoardMap === 'function') {
      return window.RapidAid.getBoardMap();
    }
    if (typeof window !== 'undefined' && window.RapidAidMap) {
      return window.RapidAidMap;
    }
    if (typeof window !== 'undefined' && window.leafletMap) {
      return window.leafletMap;
    }
    return null;
  }

  function ensureMap() {
    const map = getBoardMap();
    if (map && map !== currentMap) {
      if (currentMap && boardLayerGroup) {
        try { currentMap.off('click', onMapClick); } catch (e) {}
        try { currentMap.removeLayer(boardLayerGroup); } catch (e) {}
      }
      currentMap = map;
      if (typeof window !== 'undefined' && window.L && !boardLayerGroup) {
        boardLayerGroup = window.L.layerGroup();
      }
      if (boardLayerGroup) {
        boardLayerGroup.addTo(currentMap);
      }
      currentMap.on('click', onMapClick);
    }
    return currentMap;
  }

  function onMapClick(e) {
    if (!e || !e.latlng) return;
    const lat = e.latlng.lat;
    const lon = e.latlng.lng;
    const label = 'Map Point (' + lat.toFixed(5) + ', ' + lon.toFixed(5) + ')';
    if (dom.locSearchInput) {
      dom.locSearchInput.value = label;
    }
    if (dom.candidatesList) {
      dom.candidatesList.textContent = '';
      dom.candidatesList.classList.remove('board-candidates-list-open');
    }
    resolveLocation(lat, lon, label, 'map_click');
  }

  // ==========================================
  // Location Picker & Snap Logic
  // ==========================================
  function extractCandidates(res) {
    if (Array.isArray(res)) return res;
    if (!res || typeof res !== 'object') return [];
    if (Array.isArray(res.candidates)) return res.candidates;
    if (Array.isArray(res.results)) return res.results;
    if (Array.isArray(res.locations)) return res.locations;
    if (Array.isArray(res.places)) return res.places;
    if (Array.isArray(res.data)) return res.data;
    return [];
  }

  function handleSearchInput() {
    if (searchDebounceTimer) {
      clearTimeout(searchDebounceTimer);
    }
    searchDebounceTimer = setTimeout(() => {
      triggerSearch();
    }, 300);
  }

  async function triggerSearch() {
    if (!dom.locSearchInput) return;
    const query = dom.locSearchInput.value.trim();
    if (!query) {
      if (dom.candidatesList) {
        dom.candidatesList.textContent = '';
        dom.candidatesList.classList.remove('board-candidates-list-open');
      }
      return;
    }

    if (dom.candidatesList) {
      dom.candidatesList.textContent = '';
      const loadingItem = document.createElement('div');
      loadingItem.className = 'board-candidate-feedback';
      loadingItem.textContent = 'Searching locations...';
      dom.candidatesList.appendChild(loadingItem);
      dom.candidatesList.classList.add('board-candidates-list-open');
    }

    try {
      const res = await callBridge('search_location', { query: query, localOnly: false });
      const candidates = extractCandidates(res);
      renderCandidates(candidates);
    } catch (err) {
      if (dom.candidatesList) {
        dom.candidatesList.textContent = '';
        const errItem = document.createElement('div');
        errItem.className = 'board-candidate-feedback board-candidate-feedback-error';
        errItem.textContent = 'Search failed: ' + (err.message || 'Server error');
        dom.candidatesList.appendChild(errItem);
      }
    }
  }

  function renderCandidates(candidates) {
    if (!dom.candidatesList) return;
    dom.candidatesList.textContent = '';

    if (!candidates || candidates.length === 0) {
      const emptyItem = document.createElement('div');
      emptyItem.className = 'board-candidate-feedback';
      emptyItem.textContent = 'No matching locations found';
      dom.candidatesList.appendChild(emptyItem);
      dom.candidatesList.classList.add('board-candidates-list-open');
      return;
    }

    candidates.forEach((cand) => {
      const label = cand.label || cand.name || cand.display_name || cand.text || 'Location';
      const lat = Number(cand.lat !== undefined ? cand.lat : cand.latitude);
      const lon = Number(cand.lon !== undefined ? cand.lon : (cand.longitude !== undefined ? cand.longitude : cand.lng));
      const source = cand.source || 'search';

      const item = document.createElement('div');
      item.className = 'board-candidate-item';
      item.textContent = label;

      item.addEventListener('click', () => {
        if (dom.locSearchInput) dom.locSearchInput.value = label;
        dom.candidatesList.textContent = '';
        dom.candidatesList.classList.remove('board-candidates-list-open');
        resolveLocation(lat, lon, label, source);
      });

      dom.candidatesList.appendChild(item);
    });

    dom.candidatesList.classList.add('board-candidates-list-open');
  }

  async function resolveLocation(lat, lon, labelText, source) {
    if (isNaN(lat) || isNaN(lon)) return;
    isResolvingLocation = true;

    if (dom.btnCreate) {
      dom.btnCreate.disabled = true;
    }

    if (dom.locStatus) {
      dom.locStatus.textContent = 'Snapping location to road network...';
      dom.locStatus.className = 'board-loc-status';
    }

    ensureMap();
    if (currentMap && typeof window !== 'undefined' && window.L && boardLayerGroup) {
      if (pickerMarker) {
        pickerMarker.setLatLng([lat, lon]);
      } else {
        pickerMarker = window.L.marker([lat, lon], { draggable: true });
        pickerMarker.on('dragend', (e) => {
          const pos = e.target.getLatLng();
          const dragLabel = 'Map Point (' + pos.lat.toFixed(5) + ', ' + pos.lng.toFixed(5) + ')';
          if (dom.locSearchInput) dom.locSearchInput.value = dragLabel;
          resolveLocation(pos.lat, pos.lng, dragLabel, 'marker_drag');
        });
        boardLayerGroup.addLayer(pickerMarker);
      }
      try {
        currentMap.panTo([lat, lon]);
      } catch (e) {}
    }

    try {
      const snapRes = await callBridge('snap_point', { lat: Number(lat), lon: Number(lon) });
      applySnapResult(lat, lon, labelText, source, snapRes);
    } catch (err) {
      if (dom.locStatus) {
        dom.locStatus.textContent = 'Error checking location: ' + (err.message || 'Server error');
        dom.locStatus.className = 'board-loc-status board-loc-status-warn';
      }
      if (dom.btnCreate) {
        dom.btnCreate.disabled = true;
      }
    } finally {
      isResolvingLocation = false;
    }
  }

  function applySnapResult(lat, lon, labelText, source, snapRes) {
    if (!dom.locStatus) return;
    dom.locStatus.textContent = '';

    if (!snapRes || snapRes.ok === false) {
      const errSpan = document.createElement('span');
      errSpan.textContent = (snapRes && snapRes.error) ? snapRes.error : 'Location verification failed';
      errSpan.className = 'board-loc-status-warn';
      dom.locStatus.appendChild(errSpan);

      if (dom.btnCreate) {
        dom.btnCreate.disabled = true;
      }
      if (snappedCircle && boardLayerGroup) {
        boardLayerGroup.removeLayer(snappedCircle);
        snappedCircle = null;
      }
      return;
    }

    const withinArea = (snapRes.withinArea !== undefined) ? Boolean(snapRes.withinArea) :
                       (snapRes.within_area !== undefined) ? Boolean(snapRes.within_area) :
                       (snapRes.snapped !== undefined ? Boolean(snapRes.snapped) : true);

    const snapMeters = (snapRes.snapMeters !== undefined) ? snapRes.snapMeters :
                       (snapRes.snap_meters !== undefined ? snapRes.snap_meters :
                       (snapRes.snapDistanceMeters !== undefined ? snapRes.snapDistanceMeters :
                       (snapRes.distanceMeters !== undefined ? snapRes.distanceMeters : null)));

    const snappedLat = (snapRes.snappedLat !== undefined) ? snapRes.snappedLat :
                       (snapRes.snapped_lat !== undefined ? snapRes.snapped_lat :
                       (snapRes.snappedPoint && snapRes.snappedPoint.lat !== undefined ? snapRes.snappedPoint.lat : null));

    const snappedLon = (snapRes.snappedLon !== undefined) ? snapRes.snappedLon :
                       (snapRes.snapped_lon !== undefined ? snapRes.snapped_lon :
                       (snapRes.snappedPoint && snapRes.snappedPoint.lon !== undefined ? snapRes.snappedPoint.lon : null));

    if (!withinArea) {
      const warnSpan = document.createElement('span');
      warnSpan.textContent = 'Outside mapped area';
      warnSpan.className = 'board-loc-status-warn';
      dom.locStatus.appendChild(warnSpan);

      if (snapMeters !== null) {
        const distSpan = document.createElement('span');
        distSpan.textContent = ' (distance to network: ' + Math.round(snapMeters) + 'm)';
        distSpan.className = 'board-loc-status';
        dom.locStatus.appendChild(distSpan);
      }

      if (dom.btnCreate) {
        dom.btnCreate.disabled = true;
      }
      if (snappedCircle && boardLayerGroup) {
        boardLayerGroup.removeLayer(snappedCircle);
        snappedCircle = null;
      }
      resolvedLocation = null;
    } else {
      const okSpan = document.createElement('span');
      let msg = 'Snapped location: (' + Number(lat).toFixed(5) + ', ' + Number(lon).toFixed(5) + ')';
      if (snapMeters !== null) {
        msg += ' | Snap distance: ' + Math.round(snapMeters) + 'm';
      }
      if (snappedLat !== null && snappedLon !== null) {
        msg += ' -> node [' + Number(snappedLat).toFixed(5) + ', ' + Number(snappedLon).toFixed(5) + ']';
      }
      okSpan.textContent = msg;
      okSpan.className = 'board-loc-status-ok';
      dom.locStatus.appendChild(okSpan);

      ensureMap();
      if (currentMap && typeof window !== 'undefined' && window.L && boardLayerGroup) {
        const sLat = (snappedLat !== null) ? Number(snappedLat) : Number(lat);
        const sLon = (snappedLon !== null) ? Number(snappedLon) : Number(lon);
        if (snappedCircle) {
          snappedCircle.setLatLng([sLat, sLon]);
        } else {
          snappedCircle = window.L.circleMarker([sLat, sLon], {
            radius: 6,
            color: '#287a54',
            fillColor: '#287a54',
            fillOpacity: 0.85
          });
          boardLayerGroup.addLayer(snappedCircle);
        }
      }

      resolvedLocation = {
        text: labelText || ('(' + Number(lat).toFixed(5) + ', ' + Number(lon).toFixed(5) + ')'),
        lat: Number(lat),
        lon: Number(lon),
        snappedLat: (snappedLat !== null) ? Number(snappedLat) : Number(lat),
        snappedLon: (snappedLon !== null) ? Number(snappedLon) : Number(lon),
        source: source || 'resolved'
      };

      if (dom.btnCreate) {
        dom.btnCreate.disabled = false;
      }
    }
  }

  // ==========================================
  // Form Submission
  // ==========================================
  async function handleCreateIncident() {
    if (!resolvedLocation) {
      if (dom.createFeedback) {
        dom.createFeedback.textContent = 'Please choose a location within the mapped area.';
        dom.createFeedback.className = 'board-create-feedback board-create-feedback-err';
      }
      return;
    }

    if (dom.btnCreate) {
      dom.btnCreate.disabled = true;
    }
    if (dom.createFeedback) {
      dom.createFeedback.textContent = 'Creating incident...';
      dom.createFeedback.className = 'board-create-feedback';
    }

    const type = dom.typeSelect ? dom.typeSelect.value : 'Medical';
    const severity = dom.sevInput ? parseInt(dom.sevInput.value, 10) || 3 : 3;
    const peopleAffected = dom.peopleInput ? parseInt(dom.peopleInput.value, 10) || 0 : 0;
    const description = dom.descInput ? dom.descInput.value.trim() : '';
    const callerName = dom.callerNameInput ? dom.callerNameInput.value.trim() : '';
    const callerPhone = dom.callerPhoneInput ? dom.callerPhoneInput.value.trim() : '';

    const payload = {
      type: type,
      severity: severity,
      peopleAffected: peopleAffected,
      people_affected: peopleAffected,
      description: description,
      callerName: callerName,
      caller_name: callerName,
      callerPhone: callerPhone,
      caller_phone: callerPhone,
      locationText: resolvedLocation.text,
      location_text: resolvedLocation.text,
      lat: resolvedLocation.lat,
      lon: resolvedLocation.lon,
      location: {
        text: resolvedLocation.text,
        lat: resolvedLocation.lat,
        lon: resolvedLocation.lon,
        snappedLat: resolvedLocation.snappedLat,
        snappedLon: resolvedLocation.snappedLon,
        source: resolvedLocation.source
      }
    };

    try {
      const res = await callBridge('create_incident', payload);
      if (res && res.ok) {
        resetForm();
        if (dom.createFeedback) {
          dom.createFeedback.textContent = 'Incident created successfully';
          dom.createFeedback.className = 'board-create-feedback board-create-feedback-ok';
          setTimeout(() => {
            if (dom.createFeedback && dom.createFeedback.textContent === 'Incident created successfully') {
              dom.createFeedback.textContent = '';
            }
          }, 3500);
        }
        await fetchIncidents();
      } else {
        if (dom.createFeedback) {
          dom.createFeedback.textContent = (res && res.error) ? res.error : 'Failed to create incident';
          dom.createFeedback.className = 'board-create-feedback board-create-feedback-err';
        }
        if (dom.btnCreate) {
          dom.btnCreate.disabled = false;
        }
      }
    } catch (err) {
      if (dom.createFeedback) {
        dom.createFeedback.textContent = 'Error: ' + (err.message || 'Server error');
        dom.createFeedback.className = 'board-create-feedback board-create-feedback-err';
      }
      if (dom.btnCreate) {
        dom.btnCreate.disabled = false;
      }
    }
  }

  function resetForm() {
    if (dom.typeSelect) dom.typeSelect.value = 'Medical';
    if (dom.sevInput) dom.sevInput.value = '3';
    if (dom.peopleInput) dom.peopleInput.value = '0';
    if (dom.descInput) dom.descInput.value = '';
    if (dom.callerNameInput) dom.callerNameInput.value = '';
    if (dom.callerPhoneInput) dom.callerPhoneInput.value = '';
    if (dom.locSearchInput) dom.locSearchInput.value = '';
    if (dom.candidatesList) {
      dom.candidatesList.textContent = '';
      dom.candidatesList.classList.remove('board-candidates-list-open');
    }
    if (dom.locStatus) dom.locStatus.textContent = '';
    resolvedLocation = null;

    if (dom.btnCreate) {
      dom.btnCreate.disabled = true;
    }

    if (pickerMarker && boardLayerGroup) {
      try { boardLayerGroup.removeLayer(pickerMarker); } catch (e) {}
      pickerMarker = null;
    }
    if (snappedCircle && boardLayerGroup) {
      try { boardLayerGroup.removeLayer(snappedCircle); } catch (e) {}
      snappedCircle = null;
    }
  }

  // ==========================================
  // Incident Queue Polling & Rendering
  // ==========================================
  function getLocationText(inc) {
    if (!inc) return 'Unknown';
    if (typeof inc.location === 'string') return inc.location;
    if (inc.location && typeof inc.location.text === 'string') return inc.location.text;
    if (inc.locationText) return inc.locationText;
    if (inc.location_text) return inc.location_text;
    return 'Unknown';
  }

  function getCoordinates(inc) {
    if (!inc) return null;
    if (inc.lat !== undefined && inc.lon !== undefined && !isNaN(Number(inc.lat)) && !isNaN(Number(inc.lon))) {
      return [Number(inc.lat), Number(inc.lon)];
    }
    if (inc.location && inc.location.lat !== undefined && inc.location.lon !== undefined) {
      return [Number(inc.location.lat), Number(inc.location.lon)];
    }
    return null;
  }

  function getWaitingMinutes(inc) {
    if (!inc) return 0;
    if (inc.waitingMinutes !== undefined) return Math.max(0, Math.floor(inc.waitingMinutes));
    if (inc.waiting_minutes !== undefined) return Math.max(0, Math.floor(inc.waiting_minutes));
    const now = Math.floor(Date.now() / 1000);
    const created = inc.createdAt || inc.created_at || inc.createdTime || inc.created_time || now;
    return Math.max(0, Math.floor((now - Number(created)) / 60));
  }

  async function fetchIncidents() {
    if (isFetchingIncidents) return;
    isFetchingIncidents = true;

    try {
      const res = await callBridge('list_incidents');
      if (dom.queueError) {
        dom.queueError.textContent = '';
        dom.queueError.classList.remove('board-queue-error-visible');
      }

      let incidents = [];
      if (res && res.ok && Array.isArray(res.incidents)) {
        incidents = res.incidents;
      } else if (Array.isArray(res)) {
        incidents = res;
      } else if (res && Array.isArray(res.data)) {
        incidents = res.data;
      } else if (res && !res.ok) {
        if (dom.queueError) {
          dom.queueError.textContent = (res.error) ? res.error : 'Failed to fetch incident queue';
          dom.queueError.classList.add('board-queue-error-visible');
        }
      }

      currentIncidents = incidents;
      renderIncidentQueue(incidents);
      updateIncidentMapMarkers(incidents);

      if (dom.queueStatus) {
        dom.queueStatus.textContent = 'Queue updated: ' + (new Date()).toLocaleTimeString() + ' (' + incidents.length + ' incidents)';
      }
    } catch (err) {
      if (dom.queueError) {
        dom.queueError.textContent = 'Network error fetching incidents: ' + (err.message || 'Server error');
        dom.queueError.classList.add('board-queue-error-visible');
      }
    } finally {
      isFetchingIncidents = false;
    }
  }

  function renderIncidentQueue(incidents) {
    if (!dom.queueTbody) return;
    dom.queueTbody.textContent = '';

    if (!incidents || incidents.length === 0) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.setAttribute('colspan', '9');
      td.className = 'board-td';
      td.textContent = 'No active incidents in queue.';
      tr.appendChild(td);
      dom.queueTbody.appendChild(tr);
      return;
    }

    incidents.forEach((inc) => {
      const tr = document.createElement('tr');
      tr.id = 'incident-row-' + inc.id;
      tr.className = 'board-row';

      // 1. Priority
      const tdPrio = document.createElement('td');
      tdPrio.className = 'board-td';
      const spPrio = document.createElement('span');
      const prioVal = String(inc.priority !== undefined ? inc.priority : 'Normal');
      spPrio.textContent = prioVal;
      const pLower = prioVal.toLowerCase();
      let prioClass = 'board-prio-normal';
      if (pLower.includes('critical') || prioVal === '1') {
        prioClass = 'board-prio-critical';
      } else if (pLower.includes('high') || prioVal === '2') {
        prioClass = 'board-prio-high';
      }
      spPrio.className = 'board-prio ' + prioClass;
      tdPrio.appendChild(spPrio);
      tr.appendChild(tdPrio);

      // 2. Type
      const tdType = document.createElement('td');
      tdType.className = 'board-td';
      tdType.textContent = inc.type || '-';
      tr.appendChild(tdType);

      // 3. Severity
      const tdSev = document.createElement('td');
      tdSev.className = 'board-td';
      tdSev.id = 'cell-severity-' + inc.id;
      tdSev.textContent = String(inc.severity !== undefined ? inc.severity : '-');
      tr.appendChild(tdSev);

      // 4. People Affected
      const tdPeople = document.createElement('td');
      tdPeople.className = 'board-td';
      tdPeople.id = 'cell-people-' + inc.id;
      tdPeople.textContent = String(inc.peopleAffected !== undefined ? inc.peopleAffected : (inc.people_affected !== undefined ? inc.people_affected : 0));
      tr.appendChild(tdPeople);

      // 5. Location Text
      const tdLoc = document.createElement('td');
      tdLoc.className = 'board-td';
      tdLoc.textContent = getLocationText(inc);
      tr.appendChild(tdLoc);

      // 6. Waiting Minutes
      const tdWait = document.createElement('td');
      tdWait.className = 'board-td';
      tdWait.textContent = String(getWaitingMinutes(inc)) + ' min';
      tr.appendChild(tdWait);

      // 7. Status
      const tdStat = document.createElement('td');
      tdStat.className = 'board-td';
      const spStat = document.createElement('span');
      const statVal = String(inc.status || 'New');
      spStat.textContent = statVal;
      spStat.className = 'status-chip status-chip-' + statVal.toLowerCase();
      tdStat.appendChild(spStat);
      tr.appendChild(tdStat);

      // 8. Caller Name / Phone
      const tdCaller = document.createElement('td');
      tdCaller.className = 'board-td';
      const cName = inc.callerName || inc.caller_name || '';
      const cPhone = inc.callerPhone || inc.caller_phone || '';
      if (cName && cPhone) {
        tdCaller.textContent = cName + ' (' + cPhone + ')';
      } else if (cName || cPhone) {
        tdCaller.textContent = cName || cPhone;
      } else {
        tdCaller.textContent = '-';
      }
      tr.appendChild(tdCaller);

      // 9. Actions Column
      const tdActions = document.createElement('td');
      tdActions.className = 'board-td';
      buildActionButtons(tdActions, inc, tr);
      tr.appendChild(tdActions);

      // Row Click: highlight / center on shared map
      tr.addEventListener('click', (e) => {
        if (e.target && e.target.closest('button, input, select, textarea')) return;
        highlightAndCenterIncident(inc, tr);
      });

      dom.queueTbody.appendChild(tr);
    });
  }

  function buildActionButtons(tdActions, inc, mainRow) {
    const errorNoticeSpan = document.createElement('span');
    errorNoticeSpan.className = 'board-repropose-notice';

    // Show repropose notice if flagged
    if (reproposeNotices[inc.id]) {
      errorNoticeSpan.textContent = 'Press Propose again to re-propose';
    }

    // "Propose" Button
    const btnPropose = document.createElement('button');
    btnPropose.textContent = 'Propose';
    btnPropose.className = 'board-btn-action board-btn-propose';

    btnPropose.addEventListener('click', (e) => {
      e.stopPropagation();
      errorNoticeSpan.textContent = '';
      if (typeof window !== 'undefined' && window.RapidAidDispatch && typeof window.RapidAidDispatch.open === 'function') {
        if (reproposeNotices[inc.id]) {
          delete reproposeNotices[inc.id];
        }
        window.RapidAidDispatch.open(inc.id);
      } else {
        errorNoticeSpan.textContent = 'Dispatch panel not installed';
        errorNoticeSpan.className = 'board-edit-error';
      }
    });
    tdActions.appendChild(btnPropose);

    // "Cancel" Button
    const btnCancel = document.createElement('button');
    btnCancel.textContent = 'Cancel';
    btnCancel.className = 'board-btn-action board-btn-cancel';

    btnCancel.addEventListener('click', async (e) => {
      e.stopPropagation();
      errorNoticeSpan.textContent = '';
      const ok = (typeof window !== 'undefined' && typeof window.confirm === 'function') ?
                 window.confirm('Are you sure you want to cancel incident #' + inc.id + '?') : true;
      if (!ok) return;

      btnCancel.disabled = true;
      try {
        const res = await callBridge('cancel_incident', { incidentId: inc.id, id: inc.id });
        if (res && res.ok) {
          await fetchIncidents();
        } else {
          errorNoticeSpan.textContent = (res && res.error) ? res.error : 'Failed to cancel';
          errorNoticeSpan.className = 'board-edit-error';
          btnCancel.disabled = false;
        }
      } catch (err) {
        errorNoticeSpan.textContent = 'Cancel error: ' + (err.message || 'Server error');
        errorNoticeSpan.className = 'board-edit-error';
        btnCancel.disabled = false;
      }
    });
    tdActions.appendChild(btnCancel);

    // "Edit" Button (only for New or Proposed incidents)
    const statusLower = String(inc.status || '').trim().toLowerCase();
    const canEdit = (statusLower === 'new' || statusLower === 'proposed');
    if (canEdit) {
      const btnEdit = document.createElement('button');
      btnEdit.textContent = 'Edit';
      btnEdit.className = 'board-btn-action board-btn-edit';

      btnEdit.addEventListener('click', (e) => {
        e.stopPropagation();
        errorNoticeSpan.textContent = '';
        toggleEditSubRow(inc, mainRow, tdActions);
      });
      tdActions.appendChild(btnEdit);
    }

    tdActions.appendChild(errorNoticeSpan);
  }

  function toggleEditSubRow(inc, mainRow, tdActions) {
    const existingEditRow = document.getElementById('edit-subrow-' + inc.id);
    if (existingEditRow) {
      existingEditRow.remove();
      activeEditIncidentId = null;
      return;
    }

    activeEditIncidentId = inc.id;

    const editRow = document.createElement('tr');
    editRow.id = 'edit-subrow-' + inc.id;
    editRow.className = 'board-edit-row';

    const editTd = document.createElement('td');
    editTd.setAttribute('colspan', '9');
    editTd.className = 'board-td';

    const editContainer = document.createElement('div');
    editContainer.className = 'board-edit-container';

    const titleLabel = document.createElement('strong');
    titleLabel.textContent = 'Edit Incident #' + inc.id + ':';
    titleLabel.className = 'board-edit-label';
    editContainer.appendChild(titleLabel);

    // Severity Edit
    const sevLabel = document.createElement('label');
    sevLabel.className = 'board-edit-label';
    sevLabel.textContent = 'Severity (1-5): ';
    const sevInput = document.createElement('input');
    sevInput.type = 'number';
    sevInput.min = '1';
    sevInput.max = '5';
    sevInput.value = String(inc.severity !== undefined ? inc.severity : 3);
    sevInput.className = 'board-input board-edit-input-sev';
    sevLabel.appendChild(sevInput);
    editContainer.appendChild(sevLabel);

    // People Affected Edit
    const peopleLabel = document.createElement('label');
    peopleLabel.className = 'board-edit-label';
    peopleLabel.textContent = 'People Affected: ';
    const peopleInput = document.createElement('input');
    peopleInput.type = 'number';
    peopleInput.min = '0';
    peopleInput.value = String(inc.peopleAffected !== undefined ? inc.peopleAffected : (inc.people_affected !== undefined ? inc.people_affected : 0));
    peopleInput.className = 'board-input board-edit-input-people';
    peopleLabel.appendChild(peopleInput);
    editContainer.appendChild(peopleLabel);

    // Description Edit
    const descLabel = document.createElement('label');
    descLabel.className = 'board-edit-label';
    descLabel.textContent = 'Description: ';
    const descInput = document.createElement('input');
    descInput.type = 'text';
    descInput.value = inc.description || '';
    descInput.className = 'board-input board-edit-input-desc';
    descLabel.appendChild(descInput);
    editContainer.appendChild(descLabel);

    // Save & Cancel Buttons
    const btnSave = document.createElement('button');
    btnSave.textContent = 'Save';
    btnSave.className = 'board-btn-save';

    const btnClose = document.createElement('button');
    btnClose.textContent = 'Close';
    btnClose.className = 'board-btn-action board-btn-close';

    const editErrorSpan = document.createElement('span');
    editErrorSpan.className = 'board-edit-error';

    btnSave.addEventListener('click', async (e) => {
      e.stopPropagation();
      editErrorSpan.textContent = '';
      btnSave.disabled = true;

      const newSev = parseInt(sevInput.value, 10) || 3;
      const newPeople = parseInt(peopleInput.value, 10) || 0;
      const newDesc = descInput.value.trim();

      try {
        const res = await callBridge('update_incident', {
          incidentId: inc.id,
          id: inc.id,
          severity: newSev,
          peopleAffected: newPeople,
          description: newDesc
        });

        if (res && res.ok) {
          const sLower = String(inc.status || '').trim().toLowerCase();
          if (sLower === 'proposed') {
            reproposeNotices[inc.id] = true;
          }
          activeEditIncidentId = null;
          editRow.remove();
          await fetchIncidents();
        } else {
          editErrorSpan.textContent = (res && res.error) ? res.error : 'Update failed';
          btnSave.disabled = false;
        }
      } catch (err) {
        editErrorSpan.textContent = 'Update error: ' + (err.message || 'Server error');
        btnSave.disabled = false;
      }
    });

    btnClose.addEventListener('click', (e) => {
      e.stopPropagation();
      activeEditIncidentId = null;
      editRow.remove();
    });

    editContainer.appendChild(btnSave);
    editContainer.appendChild(btnClose);
    editContainer.appendChild(editErrorSpan);

    editTd.appendChild(editContainer);
    editRow.appendChild(editTd);

    if (mainRow.parentNode) {
      mainRow.parentNode.insertBefore(editRow, mainRow.nextSibling);
    }
  }

  function highlightAndCenterIncident(inc, tr) {
    if (highlightedRow && highlightedRow !== tr) {
      highlightedRow.classList.remove('board-row-highlighted');
    }
    highlightedRow = tr;
    tr.classList.add('board-row-highlighted');

    const coords = getCoordinates(inc);
    if (!coords) return;

    ensureMap();
    if (currentMap) {
      try {
        currentMap.setView(coords, Math.max(currentMap.getZoom ? currentMap.getZoom() : 15, 15));
      } catch (e) {}
    }

    if (incidentMarkers[inc.id]) {
      try {
        incidentMarkers[inc.id].openPopup();
      } catch (e) {}
    }
  }

  function updateIncidentMapMarkers(incidents) {
    ensureMap();
    if (!currentMap || typeof window === 'undefined' || !window.L || !boardLayerGroup) return;

    const seenIds = {};
    (incidents || []).forEach((inc) => {
      seenIds[inc.id] = true;
      const coords = getCoordinates(inc);
      if (!coords) return;

      const title = '[' + (inc.priority || 'Normal') + '] ' + (inc.type || 'Incident') + ' - #' + inc.id;
      const popupText = title + '\n' + getLocationText(inc) + '\nStatus: ' + (inc.status || 'New');

      if (incidentMarkers[inc.id]) {
        incidentMarkers[inc.id].setLatLng(coords);
        if (incidentMarkers[inc.id].getPopup()) {
          incidentMarkers[inc.id].getPopup().setContent(popupText);
        }
      } else {
        const m = window.L.marker(coords);
        m.bindPopup(popupText);
        m.on('click', () => {
          const row = document.getElementById('incident-row-' + inc.id);
          if (row) {
            highlightAndCenterIncident(inc, row);
            try { row.scrollIntoView({ behavior: 'smooth', block: 'nearest' }); } catch (e) {}
          }
        });
        boardLayerGroup.addLayer(m);
        incidentMarkers[inc.id] = m;
      }
    });

    Object.keys(incidentMarkers).forEach((id) => {
      if (!seenIds[id]) {
        try { boardLayerGroup.removeLayer(incidentMarkers[id]); } catch (e) {}
        delete incidentMarkers[id];
      }
    });
  }

  // ==========================================
  // DOM Construction (Semantic HTML & Classes)
  // ==========================================
  function buildBoardUI(mountEl) {
    const container = document.createElement('div');
    container.id = 'rapidaid-board-root';
    container.className = 'board-root';

    // ------------------------------------------
    // (a) New Incident Form Section
    // ------------------------------------------
    const formSection = document.createElement('section');
    formSection.className = 'board-form-section';

    const formHeading = document.createElement('h3');
    formHeading.className = 'board-form-heading';
    formHeading.textContent = 'Report New Emergency Incident';
    formSection.appendChild(formHeading);

    const formEl = document.createElement('form');
    formEl.onsubmit = () => false;

    // Row 1: Type, Severity, People Affected
    const row1 = document.createElement('div');
    row1.className = 'board-form-row';

    // Type field
    const typeGroup = document.createElement('div');
    typeGroup.className = 'board-form-group board-form-group-type';
    const typeLabel = document.createElement('label');
    typeLabel.textContent = 'Incident Type:';
    typeLabel.className = 'board-form-label';
    const typeSelect = document.createElement('select');
    typeSelect.id = 'board-incident-type';
    typeSelect.className = 'board-select';
    ['Medical', 'Fire', 'Police', 'Other'].forEach((t) => {
      const opt = document.createElement('option');
      opt.value = t;
      opt.textContent = t;
      if (t === 'Medical') opt.selected = true;
      typeSelect.appendChild(opt);
    });
    typeGroup.appendChild(typeLabel);
    typeGroup.appendChild(typeSelect);
    row1.appendChild(typeGroup);
    dom.typeSelect = typeSelect;

    // Severity field
    const sevGroup = document.createElement('div');
    sevGroup.className = 'board-form-group board-form-group-sev';
    const sevLabel = document.createElement('label');
    sevLabel.textContent = 'Severity (1-5):';
    sevLabel.className = 'board-form-label';
    const sevInput = document.createElement('input');
    sevInput.type = 'number';
    sevInput.id = 'board-incident-severity';
    sevInput.min = '1';
    sevInput.max = '5';
    sevInput.value = '3';
    sevInput.className = 'board-input';
    sevGroup.appendChild(sevLabel);
    sevGroup.appendChild(sevInput);
    row1.appendChild(sevGroup);
    dom.sevInput = sevInput;

    // People Affected field
    const peopleGroup = document.createElement('div');
    peopleGroup.className = 'board-form-group board-form-group-people';
    const peopleLabel = document.createElement('label');
    peopleLabel.textContent = 'People Affected:';
    peopleLabel.className = 'board-form-label';
    const peopleInput = document.createElement('input');
    peopleInput.type = 'number';
    peopleInput.id = 'board-incident-people';
    peopleInput.min = '0';
    peopleInput.value = '0';
    peopleInput.className = 'board-input';
    peopleGroup.appendChild(peopleLabel);
    peopleGroup.appendChild(peopleInput);
    row1.appendChild(peopleGroup);
    dom.peopleInput = peopleInput;

    formEl.appendChild(row1);

    // Row 2: Caller Name & Phone
    const row2 = document.createElement('div');
    row2.className = 'board-form-row';

    const callerNameGroup = document.createElement('div');
    callerNameGroup.className = 'board-form-group board-form-group-name';
    const callerNameLabel = document.createElement('label');
    callerNameLabel.textContent = 'Caller Name:';
    callerNameLabel.className = 'board-form-label';
    const callerNameInput = document.createElement('input');
    callerNameInput.type = 'text';
    callerNameInput.id = 'board-incident-caller-name';
    callerNameInput.placeholder = 'e.g. John Doe';
    callerNameInput.className = 'board-input';
    callerNameGroup.appendChild(callerNameLabel);
    callerNameGroup.appendChild(callerNameInput);
    row2.appendChild(callerNameGroup);
    dom.callerNameInput = callerNameInput;

    const callerPhoneGroup = document.createElement('div');
    callerPhoneGroup.className = 'board-form-group board-form-group-phone';
    const callerPhoneLabel = document.createElement('label');
    callerPhoneLabel.textContent = 'Caller Phone:';
    callerPhoneLabel.className = 'board-form-label';
    const callerPhoneInput = document.createElement('input');
    callerPhoneInput.type = 'text';
    callerPhoneInput.id = 'board-incident-caller-phone';
    callerPhoneInput.placeholder = 'e.g. 555-0144';
    callerPhoneInput.className = 'board-input';
    callerPhoneGroup.appendChild(callerPhoneLabel);
    callerPhoneGroup.appendChild(callerPhoneInput);
    row2.appendChild(callerPhoneGroup);
    dom.callerPhoneInput = callerPhoneInput;

    formEl.appendChild(row2);

    // Row 3: Description
    const row3 = document.createElement('div');
    row3.className = 'board-form-row';
    const descLabel = document.createElement('label');
    descLabel.textContent = 'Incident Description:';
    descLabel.className = 'board-form-label';
    const descInput = document.createElement('textarea');
    descInput.id = 'board-incident-desc';
    descInput.rows = 2;
    descInput.placeholder = 'Describe incident situation, landmarks, or medical symptoms...';
    descInput.className = 'board-textarea';
    row3.appendChild(descLabel);
    row3.appendChild(descInput);
    formEl.appendChild(row3);
    dom.descInput = descInput;

    // Row 4: Location Picker (Search box, Dropdown, Status)
    const row4 = document.createElement('div');
    row4.className = 'board-form-row';
    const locGroup = document.createElement('div');
    locGroup.className = 'board-form-group';

    const locLabel = document.createElement('label');
    locLabel.textContent = 'Location (Search or click on the map):';
    locLabel.className = 'board-form-label';
    locGroup.appendChild(locLabel);

    const searchBoxContainer = document.createElement('div');
    searchBoxContainer.className = 'board-search-container';

    const locSearchInput = document.createElement('input');
    locSearchInput.type = 'text';
    locSearchInput.id = 'board-loc-search';
    locSearchInput.placeholder = 'Type address/landmark or click on the interactive map...';
    locSearchInput.className = 'board-input board-search-input';
    locSearchInput.addEventListener('input', handleSearchInput);
    locSearchInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        triggerSearch();
      }
    });
    searchBoxContainer.appendChild(locSearchInput);
    dom.locSearchInput = locSearchInput;

    const btnSearch = document.createElement('button');
    btnSearch.type = 'button';
    btnSearch.textContent = 'Search';
    btnSearch.className = 'board-btn-search';
    btnSearch.addEventListener('click', triggerSearch);
    searchBoxContainer.appendChild(btnSearch);

    locGroup.appendChild(searchBoxContainer);

    // Candidates dropdown/list container
    const candidatesList = document.createElement('div');
    candidatesList.id = 'board-candidates-list';
    candidatesList.className = 'board-candidates-list';
    locGroup.appendChild(candidatesList);
    dom.candidatesList = candidatesList;

    // Location verification status (coordinates, snap meters, outside mapped area warning)
    const locStatus = document.createElement('div');
    locStatus.id = 'board-loc-status';
    locStatus.className = 'board-loc-status';
    locGroup.appendChild(locStatus);
    dom.locStatus = locStatus;

    row4.appendChild(locGroup);
    formEl.appendChild(row4);

    // Row 5: Submit Button and Feedback
    const row5 = document.createElement('div');
    row5.className = 'board-form-row';

    const btnCreate = document.createElement('button');
    btnCreate.type = 'button';
    btnCreate.id = 'board-btn-create';
    btnCreate.textContent = 'Create Incident';
    btnCreate.disabled = true;
    btnCreate.className = 'board-btn-create';
    btnCreate.addEventListener('click', handleCreateIncident);
    row5.appendChild(btnCreate);
    dom.btnCreate = btnCreate;

    const createFeedback = document.createElement('span');
    createFeedback.id = 'board-create-feedback';
    createFeedback.className = 'board-create-feedback';
    row5.appendChild(createFeedback);
    dom.createFeedback = createFeedback;

    formEl.appendChild(row5);
    formSection.appendChild(formEl);
    container.appendChild(formSection);

    // ------------------------------------------
    // (b) Queue Table Section
    // ------------------------------------------
    const queueSection = document.createElement('section');
    queueSection.id = 'board-queue-section';
    queueSection.className = 'board-queue-section';

    const queueHeader = document.createElement('div');
    queueHeader.className = 'board-queue-header';

    const queueTitle = document.createElement('h3');
    queueTitle.className = 'board-queue-title';
    queueTitle.textContent = 'Incident Queue';
    queueHeader.appendChild(queueTitle);

    const queueStatus = document.createElement('span');
    queueStatus.id = 'board-queue-status';
    queueStatus.className = 'board-queue-status';
    queueStatus.textContent = 'Initializing...';
    queueHeader.appendChild(queueStatus);
    dom.queueStatus = queueStatus;

    queueSection.appendChild(queueHeader);

    const queueError = document.createElement('div');
    queueError.id = 'board-queue-error';
    queueError.className = 'board-queue-error';
    queueSection.appendChild(queueError);
    dom.queueError = queueError;

    // Responsive table wrapper
    const tableWrapper = document.createElement('div');
    tableWrapper.className = 'board-table-responsive';

    const table = document.createElement('table');
    table.id = 'board-incidents-table';
    table.className = 'board-table';

    const thead = document.createElement('thead');
    thead.className = 'board-thead';
    const headerRow = document.createElement('tr');

    const headers = [
      'Priority',
      'Type',
      'Severity',
      'People',
      'Location',
      'Waiting',
      'Status',
      'Caller',
      'Actions'
    ];

    headers.forEach((h) => {
      const th = document.createElement('th');
      th.className = 'board-th';
      th.textContent = h;
      headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    tbody.id = 'board-incidents-tbody';
    table.appendChild(tbody);
    dom.queueTbody = tbody;

    tableWrapper.appendChild(table);
    queueSection.appendChild(tableWrapper);
    container.appendChild(queueSection);

    mountEl.appendChild(container);
  }

  // ==========================================
  // Public Lifecycle API: init & stop
  // ==========================================
  function init(mountEl) {
    if (!mountEl) return;
    stop();

    mountElement = mountEl;
    mountEl.textContent = '';
    dom = {};

    buildBoardUI(mountEl);
    ensureMap();
    fetchIncidents();

    pollTimer = setInterval(() => {
      fetchIncidents();
    }, 2000);
  }

  function stop() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
    if (searchDebounceTimer) {
      clearTimeout(searchDebounceTimer);
      searchDebounceTimer = null;
    }

    if (currentMap) {
      try {
        currentMap.off('click', onMapClick);
      } catch (e) {}
      if (boardLayerGroup) {
        try {
          boardLayerGroup.clearLayers();
          currentMap.removeLayer(boardLayerGroup);
        } catch (e) {}
      }
    }

    boardLayerGroup = null;
    currentMap = null;
    pickerMarker = null;
    snappedCircle = null;
    incidentMarkers = {};
    highlightedRow = null;
    resolvedLocation = null;
    activeEditIncidentId = null;
    isFetchingIncidents = false;
    isResolvingLocation = false;
    dom = {};

    if (mountElement) {
      mountElement.textContent = '';
      mountElement = null;
    }
  }

  // Export to window
  if (typeof window !== 'undefined') {
    window.RapidAidBoard = {
      init: init,
      stop: stop
    };
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
      init: init,
      stop: stop
    };
  }
})();
