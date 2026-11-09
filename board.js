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
  // Bridge & Map Access Helpers
  // ==========================================
  async function callBridge(action, payload) {
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.callBridge === 'function') {
      return window.RapidAid.callBridge(action, payload || {});
    }
    if (typeof window !== 'undefined' && typeof window.callBridge === 'function') {
      return window.callBridge(action, payload || {});
    }
    const token = (typeof sessionStorage !== 'undefined' && sessionStorage.getItem('rapidaid_token')) || '';
    try {
      const res = await fetch('/api/bridge', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action, token, payload: payload || {} })
      });
      return await res.json();
    } catch (err) {
      return { ok: false, error: err.message || 'Network error communicating with server' };
    }
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
      dom.candidatesList.style.display = 'none';
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
        dom.candidatesList.style.display = 'none';
      }
      return;
    }

    if (dom.candidatesList) {
      dom.candidatesList.textContent = '';
      const loadingItem = document.createElement('div');
      loadingItem.style.padding = '8px 10px';
      loadingItem.style.color = '#777';
      loadingItem.style.fontSize = '12px';
      loadingItem.textContent = 'Searching locations...';
      dom.candidatesList.appendChild(loadingItem);
      dom.candidatesList.style.display = 'block';
    }

    try {
      const res = await callBridge('search_location', { query: query, localOnly: false });
      const candidates = extractCandidates(res);
      renderCandidates(candidates);
    } catch (err) {
      if (dom.candidatesList) {
        dom.candidatesList.textContent = '';
        const errItem = document.createElement('div');
        errItem.style.padding = '8px 10px';
        errItem.style.color = '#d9534f';
        errItem.style.fontSize = '12px';
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
      emptyItem.style.padding = '8px 10px';
      emptyItem.style.color = '#777';
      emptyItem.style.fontSize = '12px';
      emptyItem.textContent = 'No matching locations found';
      dom.candidatesList.appendChild(emptyItem);
      dom.candidatesList.style.display = 'block';
      return;
    }

    candidates.forEach((cand) => {
      const label = cand.label || cand.name || cand.display_name || cand.text || 'Location';
      const lat = Number(cand.lat !== undefined ? cand.lat : cand.latitude);
      const lon = Number(cand.lon !== undefined ? cand.lon : (cand.longitude !== undefined ? cand.longitude : cand.lng));
      const source = cand.source || 'search';

      const item = document.createElement('div');
      item.style.padding = '7px 10px';
      item.style.cursor = 'pointer';
      item.style.fontSize = '13px';
      item.style.borderBottom = '1px solid #eee';
      item.textContent = label;

      item.addEventListener('mouseenter', () => { item.style.background = '#f0f4f8'; });
      item.addEventListener('mouseleave', () => { item.style.background = '#ffffff'; });
      item.addEventListener('click', () => {
        if (dom.locSearchInput) dom.locSearchInput.value = label;
        dom.candidatesList.textContent = '';
        dom.candidatesList.style.display = 'none';
        resolveLocation(lat, lon, label, source);
      });

      dom.candidatesList.appendChild(item);
    });

    dom.candidatesList.style.display = 'block';
  }

  async function resolveLocation(lat, lon, labelText, source) {
    if (isNaN(lat) || isNaN(lon)) return;
    isResolvingLocation = true;
    resolvedLocation = null;

    if (dom.btnCreate) {
      dom.btnCreate.disabled = true;
      dom.btnCreate.style.opacity = '0.6';
      dom.btnCreate.style.cursor = 'not-allowed';
    }

    if (dom.locStatus) {
      dom.locStatus.textContent = 'Snapping location to road network...';
      dom.locStatus.style.color = '#555';
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
        dom.locStatus.style.color = '#d9534f';
      }
      if (dom.btnCreate) {
        dom.btnCreate.disabled = true;
        dom.btnCreate.style.opacity = '0.6';
        dom.btnCreate.style.cursor = 'not-allowed';
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
      errSpan.style.color = '#d9534f';
      dom.locStatus.appendChild(errSpan);

      if (dom.btnCreate) {
        dom.btnCreate.disabled = true;
        dom.btnCreate.style.opacity = '0.6';
        dom.btnCreate.style.cursor = 'not-allowed';
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
      warnSpan.style.color = '#d9534f';
      warnSpan.style.fontWeight = 'bold';
      dom.locStatus.appendChild(warnSpan);

      if (snapMeters !== null) {
        const distSpan = document.createElement('span');
        distSpan.textContent = ' (distance to network: ' + Math.round(snapMeters) + 'm)';
        distSpan.style.color = '#666';
        distSpan.style.marginLeft = '6px';
        dom.locStatus.appendChild(distSpan);
      }

      if (dom.btnCreate) {
        dom.btnCreate.disabled = true;
        dom.btnCreate.style.opacity = '0.6';
        dom.btnCreate.style.cursor = 'not-allowed';
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
      okSpan.style.color = '#28a745';
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
            color: '#28a745',
            fillColor: '#28a745',
            fillOpacity: 0.85
          });
          boardLayerGroup.addLayer(snappedCircle);
        }
      }

      resolvedLocation = {
        text: labelText || (lat.toFixed(5) + ', ' + lon.toFixed(5)),
        lat: Number(lat),
        lon: Number(lon),
        source: source || 'map'
      };

      if (dom.btnCreate) {
        dom.btnCreate.disabled = false;
        dom.btnCreate.style.opacity = '1.0';
        dom.btnCreate.style.cursor = 'pointer';
      }
    }
  }

  // ==========================================
  // Incident Creation
  // ==========================================
  async function handleCreateIncident() {
    if (!resolvedLocation) {
      if (dom.createFeedback) {
        dom.createFeedback.textContent = 'Please choose a valid location within the operational area';
        dom.createFeedback.style.color = '#d9534f';
      }
      return;
    }

    if (dom.btnCreate) {
      dom.btnCreate.disabled = true;
    }
    if (dom.createFeedback) {
      dom.createFeedback.textContent = 'Creating incident...';
      dom.createFeedback.style.color = '#555';
    }

    const type = dom.typeSelect ? dom.typeSelect.value : 'Medical';
    const severity = dom.sevInput ? (parseInt(dom.sevInput.value, 10) || 3) : 3;
    const peopleAffected = dom.peopleInput ? (parseInt(dom.peopleInput.value, 10) || 0) : 0;
    const description = dom.descInput ? dom.descInput.value.trim() : '';
    const callerName = dom.callerNameInput ? dom.callerNameInput.value.trim() : '';
    const callerPhone = dom.callerPhoneInput ? dom.callerPhoneInput.value.trim() : '';

    const payload = {
      type: type,
      severity: severity,
      peopleAffected: peopleAffected,
      description: description,
      callerName: callerName,
      callerPhone: callerPhone,
      location: {
        text: resolvedLocation.text,
        lat: resolvedLocation.lat,
        lon: resolvedLocation.lon,
        source: resolvedLocation.source
      }
    };

    try {
      const res = await callBridge('create_incident', payload);
      if (res && res.ok) {
        resetForm();
        if (dom.createFeedback) {
          dom.createFeedback.textContent = 'Incident created successfully';
          dom.createFeedback.style.color = '#28a745';
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
          dom.createFeedback.style.color = '#d9534f';
        }
        if (dom.btnCreate) {
          dom.btnCreate.disabled = false;
        }
      }
    } catch (err) {
      if (dom.createFeedback) {
        dom.createFeedback.textContent = 'Error: ' + (err.message || 'Server error');
        dom.createFeedback.style.color = '#d9534f';
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
      dom.candidatesList.style.display = 'none';
    }
    if (dom.locStatus) dom.locStatus.textContent = '';
    resolvedLocation = null;

    if (dom.btnCreate) {
      dom.btnCreate.disabled = true;
      dom.btnCreate.style.opacity = '0.6';
      dom.btnCreate.style.cursor = 'not-allowed';
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
    if (inc.location && typeof inc.location.lat === 'number' && typeof inc.location.lon === 'number') {
      return [inc.location.lat, inc.location.lon];
    }
    if (inc.location && typeof inc.location.lat === 'number' && typeof inc.location.lng === 'number') {
      return [inc.location.lat, inc.location.lng];
    }
    if (typeof inc.lat === 'number' && typeof inc.lon === 'number') {
      return [inc.lat, inc.lon];
    }
    if (typeof inc.lat === 'number' && typeof inc.lng === 'number') {
      return [inc.lat, inc.lng];
    }
    if (typeof inc.latitude === 'number' && typeof inc.longitude === 'number') {
      return [inc.latitude, inc.longitude];
    }
    return null;
  }

  function getWaitingMinutes(inc) {
    if (!inc) return 0;
    if (inc.waitingMinutes !== undefined && inc.waitingMinutes !== null) return inc.waitingMinutes;
    if (inc.waiting_minutes !== undefined && inc.waiting_minutes !== null) return inc.waiting_minutes;
    const ts = inc.createdAt || inc.created_at || inc.timestamp || inc.time;
    if (ts) {
      const timeMs = (typeof ts === 'number') ? (ts > 1e11 ? ts : ts * 1000) : new Date(ts).getTime();
      if (!isNaN(timeMs)) {
        return Math.max(0, Math.floor((Date.now() - timeMs) / 60000));
      }
    }
    return 0;
  }

  async function fetchIncidents() {
    if (isFetchingIncidents) return;
    if (activeEditIncidentId !== null) {
      // Defer queue DOM replacement while user is actively typing in edit controls
      return;
    }
    isFetchingIncidents = true;

    try {
      const res = await callBridge('list_incidents', {});
      if (res && res.ok === false) {
        if (dom.queueError) {
          dom.queueError.textContent = res.error || 'Failed to list incidents';
          dom.queueError.style.display = 'block';
        }
        return;
      }
      if (dom.queueError) {
        dom.queueError.textContent = '';
        dom.queueError.style.display = 'none';
      }

      let incidents = [];
      if (Array.isArray(res)) {
        incidents = res;
      } else if (res && typeof res === 'object') {
        incidents = res.incidents || res.results || res.data || [];
      }

      currentIncidents = incidents;
      renderQueueTable(incidents);
      updateIncidentMapMarkers(incidents);

      if (dom.queueStatus) {
        const d = new Date();
        dom.queueStatus.textContent = 'Polled at ' + d.toLocaleTimeString();
      }
    } catch (err) {
      if (dom.queueError) {
        dom.queueError.textContent = 'Queue poll error: ' + (err.message || 'Server error');
        dom.queueError.style.display = 'block';
      }
    } finally {
      isFetchingIncidents = false;
    }
  }

  function renderQueueTable(incidents) {
    if (!dom.queueTbody) return;
    dom.queueTbody.textContent = '';

    if (!incidents || incidents.length === 0) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.setAttribute('colspan', '9');
      td.style.padding = '14px';
      td.style.textAlign = 'center';
      td.style.color = '#777';
      td.textContent = 'No active incidents in queue.';
      tr.appendChild(td);
      dom.queueTbody.appendChild(tr);
      return;
    }

    incidents.forEach((inc) => {
      const tr = document.createElement('tr');
      tr.id = 'incident-row-' + inc.id;
      tr.style.borderBottom = '1px solid #ddd';
      tr.style.cursor = 'pointer';

      // 1. Priority
      const tdPrio = document.createElement('td');
      tdPrio.style.padding = '8px';
      const spPrio = document.createElement('span');
      const prioVal = String(inc.priority !== undefined ? inc.priority : 'Normal');
      spPrio.textContent = prioVal;
      spPrio.style.fontWeight = 'bold';
      const pLower = prioVal.toLowerCase();
      if (pLower.includes('critical') || prioVal === '1') {
        spPrio.style.color = '#d9534f';
      } else if (pLower.includes('high') || prioVal === '2') {
        spPrio.style.color = '#e67e22';
      } else {
        spPrio.style.color = '#0066cc';
      }
      tdPrio.appendChild(spPrio);
      tr.appendChild(tdPrio);

      // 2. Type
      const tdType = document.createElement('td');
      tdType.style.padding = '8px';
      tdType.textContent = inc.type || '-';
      tr.appendChild(tdType);

      // 3. Severity
      const tdSev = document.createElement('td');
      tdSev.style.padding = '8px';
      tdSev.id = 'cell-severity-' + inc.id;
      tdSev.textContent = String(inc.severity !== undefined ? inc.severity : '-');
      tr.appendChild(tdSev);

      // 4. People Affected
      const tdPeople = document.createElement('td');
      tdPeople.style.padding = '8px';
      tdPeople.id = 'cell-people-' + inc.id;
      tdPeople.textContent = String(inc.peopleAffected !== undefined ? inc.peopleAffected : (inc.people_affected !== undefined ? inc.people_affected : 0));
      tr.appendChild(tdPeople);

      // 5. Location Text
      const tdLoc = document.createElement('td');
      tdLoc.style.padding = '8px';
      tdLoc.textContent = getLocationText(inc);
      tr.appendChild(tdLoc);

      // 6. Waiting Minutes
      const tdWait = document.createElement('td');
      tdWait.style.padding = '8px';
      tdWait.textContent = String(getWaitingMinutes(inc)) + ' min';
      tr.appendChild(tdWait);

      // 7. Status
      const tdStat = document.createElement('td');
      tdStat.style.padding = '8px';
      const spStat = document.createElement('span');
      const statVal = String(inc.status || 'New');
      spStat.textContent = statVal;
      spStat.style.padding = '2px 6px';
      spStat.style.borderRadius = '3px';
      spStat.style.fontSize = '12px';
      spStat.style.fontWeight = 'bold';
      const sLower = statVal.toLowerCase();
      if (sLower === 'new') {
        spStat.style.background = '#e2f0d9';
        spStat.style.color = '#385723';
      } else if (sLower === 'proposed') {
        spStat.style.background = '#fff2cc';
        spStat.style.color = '#806000';
      } else if (sLower.includes('cancel')) {
        spStat.style.background = '#fce4d6';
        spStat.style.color = '#c65911';
      } else {
        spStat.style.background = '#e8f4fd';
        spStat.style.color = '#0066cc';
      }
      tdStat.appendChild(spStat);
      tr.appendChild(tdStat);

      // 8. Caller Name / Phone
      const tdCaller = document.createElement('td');
      tdCaller.style.padding = '8px';
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
      tdActions.style.padding = '8px';
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
    errorNoticeSpan.style.marginLeft = '6px';
    errorNoticeSpan.style.fontSize = '12px';

    // Show repropose notice if flagged
    if (reproposeNotices[inc.id]) {
      errorNoticeSpan.textContent = 'Press Propose again to re-propose';
      errorNoticeSpan.style.color = '#e67e22';
      errorNoticeSpan.style.fontWeight = 'bold';
    }

    // "Propose" Button
    const btnPropose = document.createElement('button');
    btnPropose.textContent = 'Propose';
    btnPropose.style.marginRight = '4px';
    btnPropose.style.padding = '4px 8px';
    btnPropose.style.fontSize = '12px';
    btnPropose.style.cursor = 'pointer';

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
        errorNoticeSpan.style.color = '#d9534f';
        errorNoticeSpan.style.fontWeight = 'normal';
      }
    });
    tdActions.appendChild(btnPropose);

    // "Cancel" Button
    const btnCancel = document.createElement('button');
    btnCancel.textContent = 'Cancel';
    btnCancel.style.marginRight = '4px';
    btnCancel.style.padding = '4px 8px';
    btnCancel.style.fontSize = '12px';
    btnCancel.style.cursor = 'pointer';
    btnCancel.style.background = '#fff0f0';
    btnCancel.style.borderColor = '#d9534f';
    btnCancel.style.color = '#c00';

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
          errorNoticeSpan.style.color = '#d9534f';
          btnCancel.disabled = false;
        }
      } catch (err) {
        errorNoticeSpan.textContent = 'Cancel error: ' + (err.message || 'Server error');
        errorNoticeSpan.style.color = '#d9534f';
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
      btnEdit.style.marginRight = '4px';
      btnEdit.style.padding = '4px 8px';
      btnEdit.style.fontSize = '12px';
      btnEdit.style.cursor = 'pointer';

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
    editRow.style.background = '#f9fafb';
    editRow.style.borderBottom = '2px solid #ccc';

    const editTd = document.createElement('td');
    editTd.setAttribute('colspan', '9');
    editTd.style.padding = '12px 16px';

    const editContainer = document.createElement('div');
    editContainer.style.display = 'flex';
    editContainer.style.flexWrap = 'wrap';
    editContainer.style.gap = '12px';
    editContainer.style.alignItems = 'center';

    const titleLabel = document.createElement('strong');
    titleLabel.textContent = 'Edit Incident #' + inc.id + ':';
    titleLabel.style.marginRight = '8px';
    editContainer.appendChild(titleLabel);

    // Severity Edit
    const sevLabel = document.createElement('label');
    sevLabel.textContent = 'Severity (1-5): ';
    const sevInput = document.createElement('input');
    sevInput.type = 'number';
    sevInput.min = '1';
    sevInput.max = '5';
    sevInput.value = String(inc.severity !== undefined ? inc.severity : 3);
    sevInput.style.width = '55px';
    sevInput.style.padding = '3px';
    sevLabel.appendChild(sevInput);
    editContainer.appendChild(sevLabel);

    // People Affected Edit
    const peopleLabel = document.createElement('label');
    peopleLabel.textContent = 'People Affected: ';
    const peopleInput = document.createElement('input');
    peopleInput.type = 'number';
    peopleInput.min = '0';
    peopleInput.value = String(inc.peopleAffected !== undefined ? inc.peopleAffected : (inc.people_affected !== undefined ? inc.people_affected : 0));
    peopleInput.style.width = '65px';
    peopleInput.style.padding = '3px';
    peopleLabel.appendChild(peopleInput);
    editContainer.appendChild(peopleLabel);

    // Description Edit
    const descLabel = document.createElement('label');
    descLabel.textContent = 'Description: ';
    const descInput = document.createElement('input');
    descInput.type = 'text';
    descInput.value = inc.description || '';
    descInput.style.width = '240px';
    descInput.style.padding = '3px';
    descLabel.appendChild(descInput);
    editContainer.appendChild(descLabel);

    // Save & Cancel Buttons
    const btnSave = document.createElement('button');
    btnSave.textContent = 'Save';
    btnSave.style.padding = '4px 12px';
    btnSave.style.background = '#28a745';
    btnSave.style.color = '#fff';
    btnSave.style.border = 'none';
    btnSave.style.borderRadius = '3px';
    btnSave.style.cursor = 'pointer';
    btnSave.style.fontWeight = 'bold';

    const btnClose = document.createElement('button');
    btnClose.textContent = 'Close';
    btnClose.style.padding = '4px 10px';
    btnClose.style.marginLeft = '4px';
    btnClose.style.cursor = 'pointer';

    const editErrorSpan = document.createElement('span');
    editErrorSpan.style.color = '#d9534f';
    editErrorSpan.style.fontSize = '12px';
    editErrorSpan.style.marginLeft = '8px';

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
      highlightedRow.style.background = '';
    }
    highlightedRow = tr;
    tr.style.background = '#e8f4fd';

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
        m.bindTooltip('#' + inc.id + ' ' + (inc.type || 'Incident'));
        boardLayerGroup.addLayer(m);
        incidentMarkers[inc.id] = m;
      }
    });

    // Remove stale markers
    Object.keys(incidentMarkers).forEach((id) => {
      if (!seenIds[id]) {
        try {
          boardLayerGroup.removeLayer(incidentMarkers[id]);
        } catch (e) {}
        delete incidentMarkers[id];
      }
    });
  }

  // ==========================================
  // DOM Construction (Plain HTML & Inline Styles)
  // ==========================================
  function buildBoardUI(mountEl) {
    const container = document.createElement('div');
    container.id = 'rapidaid-board-root';
    container.style.fontFamily = 'sans-serif';
    container.style.lineHeight = '1.5';
    container.style.color = '#333';

    // ------------------------------------------
    // (a) New Incident Form Section
    // ------------------------------------------
    const formSection = document.createElement('section');
    formSection.style.background = '#fafafa';
    formSection.style.border = '1px solid #ccc';
    formSection.style.borderRadius = '4px';
    formSection.style.padding = '18px';
    formSection.style.marginBottom = '24px';

    const formHeading = document.createElement('h3');
    formHeading.style.marginTop = '0';
    formHeading.style.marginBottom = '14px';
    formHeading.style.color = '#222';
    formHeading.textContent = 'Report New Emergency Incident';
    formSection.appendChild(formHeading);

    const formEl = document.createElement('form');
    formEl.onsubmit = () => false;

    // Row 1: Type, Severity, People Affected
    const row1 = document.createElement('div');
    row1.style.display = 'flex';
    row1.style.gap = '15px';
    row1.style.flexWrap = 'wrap';
    row1.style.marginBottom = '12px';

    // Type field
    const typeGroup = document.createElement('div');
    typeGroup.style.flex = '1.2';
    typeGroup.style.minWidth = '140px';
    const typeLabel = document.createElement('label');
    typeLabel.textContent = 'Incident Type:';
    typeLabel.style.fontWeight = 'bold';
    typeLabel.style.display = 'block';
    typeLabel.style.marginBottom = '4px';
    const typeSelect = document.createElement('select');
    typeSelect.id = 'board-incident-type';
    typeSelect.style.width = '100%';
    typeSelect.style.padding = '6px';
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
    sevGroup.style.flex = '0.8';
    sevGroup.style.minWidth = '110px';
    const sevLabel = document.createElement('label');
    sevLabel.textContent = 'Severity (1-5):';
    sevLabel.style.fontWeight = 'bold';
    sevLabel.style.display = 'block';
    sevLabel.style.marginBottom = '4px';
    const sevInput = document.createElement('input');
    sevInput.type = 'number';
    sevInput.id = 'board-incident-severity';
    sevInput.min = '1';
    sevInput.max = '5';
    sevInput.value = '3';
    sevInput.style.width = '100%';
    sevInput.style.padding = '6px';
    sevInput.style.boxSizing = 'border-box';
    sevGroup.appendChild(sevLabel);
    sevGroup.appendChild(sevInput);
    row1.appendChild(sevGroup);
    dom.sevInput = sevInput;

    // People Affected field
    const peopleGroup = document.createElement('div');
    peopleGroup.style.flex = '1';
    peopleGroup.style.minWidth = '130px';
    const peopleLabel = document.createElement('label');
    peopleLabel.textContent = 'People Affected:';
    peopleLabel.style.fontWeight = 'bold';
    peopleLabel.style.display = 'block';
    peopleLabel.style.marginBottom = '4px';
    const peopleInput = document.createElement('input');
    peopleInput.type = 'number';
    peopleInput.id = 'board-incident-people';
    peopleInput.min = '0';
    peopleInput.value = '0';
    peopleInput.style.width = '100%';
    peopleInput.style.padding = '6px';
    peopleInput.style.boxSizing = 'border-box';
    peopleGroup.appendChild(peopleLabel);
    peopleGroup.appendChild(peopleInput);
    row1.appendChild(peopleGroup);
    dom.peopleInput = peopleInput;

    formEl.appendChild(row1);

    // Row 2: Caller Name & Phone
    const row2 = document.createElement('div');
    row2.style.display = 'flex';
    row2.style.gap = '15px';
    row2.style.flexWrap = 'wrap';
    row2.style.marginBottom = '12px';

    const callerNameGroup = document.createElement('div');
    callerNameGroup.style.flex = '1';
    callerNameGroup.style.minWidth = '180px';
    const callerNameLabel = document.createElement('label');
    callerNameLabel.textContent = 'Caller Name:';
    callerNameLabel.style.fontWeight = 'bold';
    callerNameLabel.style.display = 'block';
    callerNameLabel.style.marginBottom = '4px';
    const callerNameInput = document.createElement('input');
    callerNameInput.type = 'text';
    callerNameInput.id = 'board-incident-caller-name';
    callerNameInput.placeholder = 'e.g. John Doe';
    callerNameInput.style.width = '100%';
    callerNameInput.style.padding = '6px';
    callerNameInput.style.boxSizing = 'border-box';
    callerNameGroup.appendChild(callerNameLabel);
    callerNameGroup.appendChild(callerNameInput);
    row2.appendChild(callerNameGroup);
    dom.callerNameInput = callerNameInput;

    const callerPhoneGroup = document.createElement('div');
    callerPhoneGroup.style.flex = '1';
    callerPhoneGroup.style.minWidth = '180px';
    const callerPhoneLabel = document.createElement('label');
    callerPhoneLabel.textContent = 'Caller Phone:';
    callerPhoneLabel.style.fontWeight = 'bold';
    callerPhoneLabel.style.display = 'block';
    callerPhoneLabel.style.marginBottom = '4px';
    const callerPhoneInput = document.createElement('input');
    callerPhoneInput.type = 'text';
    callerPhoneInput.id = 'board-incident-caller-phone';
    callerPhoneInput.placeholder = 'e.g. 555-0144';
    callerPhoneInput.style.width = '100%';
    callerPhoneInput.style.padding = '6px';
    callerPhoneInput.style.boxSizing = 'border-box';
    callerPhoneGroup.appendChild(callerPhoneLabel);
    callerPhoneGroup.appendChild(callerPhoneInput);
    row2.appendChild(callerPhoneGroup);
    dom.callerPhoneInput = callerPhoneInput;

    formEl.appendChild(row2);

    // Row 3: Description
    const row3 = document.createElement('div');
    row3.style.marginBottom = '12px';
    const descLabel = document.createElement('label');
    descLabel.textContent = 'Incident Description:';
    descLabel.style.fontWeight = 'bold';
    descLabel.style.display = 'block';
    descLabel.style.marginBottom = '4px';
    const descInput = document.createElement('textarea');
    descInput.id = 'board-incident-desc';
    descInput.rows = 2;
    descInput.placeholder = 'Describe incident situation, landmarks, or medical symptoms...';
    descInput.style.width = '100%';
    descInput.style.padding = '6px';
    descInput.style.boxSizing = 'border-box';
    descInput.style.fontFamily = 'sans-serif';
    row3.appendChild(descLabel);
    row3.appendChild(descInput);
    formEl.appendChild(row3);
    dom.descInput = descInput;

    // Row 4: Location Picker (Search box, Dropdown, Status)
    const row4 = document.createElement('div');
    row4.style.marginBottom = '14px';

    const locLabel = document.createElement('label');
    locLabel.textContent = 'Location (Search or click on the map):';
    locLabel.style.fontWeight = 'bold';
    locLabel.style.display = 'block';
    locLabel.style.marginBottom = '4px';
    row4.appendChild(locLabel);

    const searchBoxContainer = document.createElement('div');
    searchBoxContainer.style.display = 'flex';
    searchBoxContainer.style.gap = '8px';
    searchBoxContainer.style.alignItems = 'center';

    const locSearchInput = document.createElement('input');
    locSearchInput.type = 'text';
    locSearchInput.id = 'board-loc-search';
    locSearchInput.placeholder = 'Type address/landmark or click on the interactive map...';
    locSearchInput.style.flex = '1';
    locSearchInput.style.padding = '7px';
    locSearchInput.style.boxSizing = 'border-box';
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
    btnSearch.style.padding = '7px 15px';
    btnSearch.style.cursor = 'pointer';
    btnSearch.addEventListener('click', triggerSearch);
    searchBoxContainer.appendChild(btnSearch);

    row4.appendChild(searchBoxContainer);

    // Candidates dropdown/list container
    const candidatesList = document.createElement('div');
    candidatesList.id = 'board-candidates-list';
    candidatesList.style.display = 'none';
    candidatesList.style.background = '#ffffff';
    candidatesList.style.border = '1px solid #ccc';
    candidatesList.style.borderRadius = '3px';
    candidatesList.style.marginTop = '4px';
    candidatesList.style.maxHeight = '180px';
    candidatesList.style.overflowY = 'auto';
    candidatesList.style.boxShadow = '0 3px 6px rgba(0,0,0,0.1)';
    row4.appendChild(candidatesList);
    dom.candidatesList = candidatesList;

    // Location verification status (coordinates, snap meters, outside mapped area warning)
    const locStatus = document.createElement('div');
    locStatus.id = 'board-loc-status';
    locStatus.style.marginTop = '6px';
    locStatus.style.fontSize = '13px';
    locStatus.style.minHeight = '18px';
    row4.appendChild(locStatus);
    dom.locStatus = locStatus;

    formEl.appendChild(row4);

    // Row 5: Submit Button and Feedback
    const row5 = document.createElement('div');
    row5.style.display = 'flex';
    row5.style.alignItems = 'center';
    row5.style.gap = '15px';

    const btnCreate = document.createElement('button');
    btnCreate.type = 'button';
    btnCreate.id = 'board-btn-create';
    btnCreate.textContent = 'Create Incident';
    btnCreate.disabled = true;
    btnCreate.style.padding = '8px 22px';
    btnCreate.style.fontWeight = 'bold';
    btnCreate.style.background = '#0066cc';
    btnCreate.style.color = '#ffffff';
    btnCreate.style.border = 'none';
    btnCreate.style.borderRadius = '3px';
    btnCreate.style.cursor = 'not-allowed';
    btnCreate.style.opacity = '0.6';
    btnCreate.addEventListener('click', handleCreateIncident);
    row5.appendChild(btnCreate);
    dom.btnCreate = btnCreate;

    const createFeedback = document.createElement('span');
    createFeedback.id = 'board-create-feedback';
    createFeedback.style.fontSize = '13px';
    createFeedback.style.fontWeight = 'bold';
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

    const queueHeader = document.createElement('div');
    queueHeader.style.display = 'flex';
    queueHeader.style.justifyContent = 'space-between';
    queueHeader.style.alignItems = 'center';
    queueHeader.style.marginBottom = '10px';

    const queueTitle = document.createElement('h3');
    queueTitle.style.margin = '0';
    queueTitle.style.color = '#222';
    queueTitle.textContent = 'Incident Queue';
    queueHeader.appendChild(queueTitle);

    const queueStatus = document.createElement('span');
    queueStatus.id = 'board-queue-status';
    queueStatus.style.fontSize = '12px';
    queueStatus.style.color = '#666';
    queueStatus.textContent = 'Initializing...';
    queueHeader.appendChild(queueStatus);
    dom.queueStatus = queueStatus;

    queueSection.appendChild(queueHeader);

    const queueError = document.createElement('div');
    queueError.id = 'board-queue-error';
    queueError.style.display = 'none';
    queueError.style.color = '#d9534f';
    queueError.style.fontWeight = 'bold';
    queueError.style.fontSize = '13px';
    queueError.style.marginBottom = '8px';
    queueSection.appendChild(queueError);
    dom.queueError = queueError;

    const table = document.createElement('table');
    table.id = 'board-incidents-table';
    table.style.width = '100%';
    table.style.borderCollapse = 'collapse';
    table.style.fontSize = '13px';
    table.style.textAlign = 'left';

    const thead = document.createElement('thead');
    const headerRow = document.createElement('tr');
    headerRow.style.background = '#e9ecef';
    headerRow.style.borderBottom = '2px solid #ccc';

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
      th.style.padding = '8px';
      th.textContent = h;
      headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    tbody.id = 'board-incidents-tbody';
    table.appendChild(tbody);
    dom.queueTbody = tbody;

    queueSection.appendChild(table);
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
