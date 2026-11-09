/**
 * RapidAid Emergency Dispatch System - Dispatch View (dispatch.js)
 * Implements window.RapidAidDispatch = { init(mountEl), stop(), open(incidentId) }
 *
 * SPECIFICATION COMPLIANCE:
 * - Plain HTML and JavaScript only, no CSS files, minimal inline styles.
 * - All server or user text inserted with textContent, NEVER innerHTML.
 * - Route coordinates from server ([lon, lat]) swapped to [lat, lon] for Leaflet.
 * - Map layers stored in an independent L.layerGroup removed on stop/close/switch.
 * - Distances strictly formatted in km to 2 decimals: (distanceMeters / 1000).toFixed(2) + " km".
 * - NO ETA stated anywhere.
 */

(function() {
  'use strict';

  // Module state
  let containerEl = null;
  let proposalPanelEl = null;
  let liveSectionEl = null;
  let pollTimer = null;
  let isPolling = false;
  let activeIncidentId = null;
  let routeLayerGroup = null;
  let currentSlots = [];

  // ==========================================
  // 1. Bridge & Map Communication Helpers
  // ==========================================

  async function callBridge(action, payload = {}) {
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.callBridge === 'function') {
      try {
        return await window.RapidAid.callBridge(action, payload);
      } catch (err) {
        return { ok: false, error: err.message || 'Error communicating with bridge' };
      }
    }

    const token = (typeof sessionStorage !== 'undefined' && sessionStorage.getItem('rapidaid_token')) || '';
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

  function getSharedMap() {
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.getBoardMap === 'function') {
      const m = window.RapidAid.getBoardMap();
      if (m) return m;
    }
    if (typeof window !== 'undefined' && window.state && window.state.leafletMap) {
      return window.state.leafletMap;
    }
    return null;
  }

  function getRouteLayerGroup() {
    const map = getSharedMap();
    if (!map || typeof L === 'undefined') return null;
    if (!routeLayerGroup) {
      routeLayerGroup = L.layerGroup();
    }
    if (typeof map.hasLayer === 'function' && !map.hasLayer(routeLayerGroup)) {
      routeLayerGroup.addTo(map);
    }
    return routeLayerGroup;
  }

  function clearRouteLayers() {
    if (routeLayerGroup) {
      routeLayerGroup.clearLayers();
      const map = getSharedMap();
      if (map && typeof map.hasLayer === 'function' && map.hasLayer(routeLayerGroup)) {
        map.removeLayer(routeLayerGroup);
      }
    }
    if (currentSlots && currentSlots.length > 0) {
      currentSlots.forEach(s => {
        s.polyline = null;
        s.showRoute = false;
      });
    }
  }

  // ==========================================
  // 2. Formatting Helpers (Strictly NO ETA)
  // ==========================================

  function formatDistance(distanceMeters, distanceKm) {
    if (distanceMeters !== undefined && distanceMeters !== null && !isNaN(Number(distanceMeters))) {
      const km = Number(distanceMeters) / 1000;
      return km.toFixed(2) + ' km';
    }
    if (distanceKm !== undefined && distanceKm !== null && !isNaN(Number(distanceKm))) {
      return Number(distanceKm).toFixed(2) + ' km';
    }
    return '';
  }

  function parseRouteCoordinates(coords) {
    if (!Array.isArray(coords)) return [];
    const latLngs = [];
    for (let i = 0; i < coords.length; i++) {
      const pt = coords[i];
      if (Array.isArray(pt) && pt.length >= 2) {
        // Server sends [lon, lat] pairs: swap to [lat, lon] for Leaflet
        const lon = Number(pt[0]);
        const lat = Number(pt[1]);
        if (!isNaN(lat) && !isNaN(lon)) {
          latLngs.push([lat, lon]);
        }
      } else if (pt && typeof pt === 'object') {
        const lat = Number(pt.lat !== undefined ? pt.lat : pt.latitude);
        const lon = Number(pt.lon !== undefined ? pt.lon : (pt.lng !== undefined ? pt.lng : pt.longitude));
        if (!isNaN(lat) && !isNaN(lon)) {
          latLngs.push([lat, lon]);
        }
      }
    }
    return latLngs;
  }

  function formatTimestamp(ts) {
    if (!ts) return '';
    if (typeof ts === 'number') {
      const ms = ts < 1e11 ? ts * 1000 : ts;
      const d = new Date(ms);
      if (!isNaN(d.getTime())) {
        return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      }
    }
    if (typeof ts === 'string') {
      const d = new Date(ts);
      if (!isNaN(d.getTime())) {
        return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
      }
      return ts;
    }
    return String(ts);
  }

  // ==========================================
  // 3. Slot Route Drawing on Map
  // ==========================================

  function updateSlotRoute(slotIndex) {
    const slotState = currentSlots[slotIndex];
    if (!slotState) return;

    const layerGroup = getRouteLayerGroup();

    // Remove existing polyline for this slot
    if (slotState.polyline) {
      if (layerGroup && typeof layerGroup.hasLayer === 'function' && layerGroup.hasLayer(slotState.polyline)) {
        layerGroup.removeLayer(slotState.polyline);
      }
      slotState.polyline = null;
    }

    if (!slotState.showRoute) return;

    const unit = slotState.selectedUnit;
    const rawCoords = (unit && Array.isArray(unit.routeCoordinates) && unit.routeCoordinates.length > 0)
      ? unit.routeCoordinates
      : (slotState.rawSlot && Array.isArray(slotState.rawSlot.routeCoordinates) ? slotState.rawSlot.routeCoordinates : null);

    if (!rawCoords || rawCoords.length === 0) return;

    const latLngs = parseRouteCoordinates(rawCoords);
    if (latLngs.length === 0) return;

    if (!layerGroup || typeof L === 'undefined') return;

    const routeColors = ['#0066cc', '#d9534f', '#28a745', '#e65100', '#6f42c1', '#17a2b8'];
    const color = routeColors[slotIndex % routeColors.length];

    const polyline = L.polyline(latLngs, {
      color: color,
      weight: 4,
      opacity: 0.85,
      lineJoin: 'round'
    });

    polyline.addTo(layerGroup);
    slotState.polyline = polyline;

    const map = getSharedMap();
    if (map && typeof map.fitBounds === 'function') {
      try {
        map.fitBounds(polyline.getBounds(), { padding: [30, 30] });
      } catch (e) {
        // Map may not be ready or detached
      }
    }
  }

  // ==========================================
  // 4. Slot Candidates Normalization
  // ==========================================

  function normalizeSlotCandidates(slot, slotIndex) {
    let chosen = null;
    if (slot.candidate && typeof slot.candidate === 'object') {
      chosen = slot.candidate;
    } else if (slot.unit && typeof slot.unit === 'object') {
      chosen = slot.unit;
    } else if (slot.username) {
      chosen = {
        username: slot.username,
        name: slot.name || slot.unitName || slot.username,
        distanceMeters: slot.distanceMeters !== undefined ? slot.distanceMeters : (slot.distance !== undefined ? slot.distance : null),
        distanceKm: slot.distanceKm,
        routeCoordinates: slot.routeCoordinates || slot.route || []
      };
    }

    const rawAlts = Array.isArray(slot.alternatives) ? slot.alternatives : (Array.isArray(slot.candidates) ? slot.candidates : (Array.isArray(slot.units) ? slot.units : []));

    const allCandidates = [];
    const seenUsernames = new Set();

    if (chosen && chosen.username) {
      allCandidates.push({
        username: chosen.username,
        name: chosen.name || chosen.unitName || chosen.username,
        distanceMeters: chosen.distanceMeters !== undefined ? chosen.distanceMeters : (chosen.distance !== undefined ? chosen.distance : null),
        distanceKm: chosen.distanceKm,
        routeCoordinates: chosen.routeCoordinates || chosen.route || slot.routeCoordinates || []
      });
      seenUsernames.add(chosen.username);
    }

    for (let i = 0; i < rawAlts.length; i++) {
      const alt = rawAlts[i];
      if (!alt || !alt.username) continue;
      if (!seenUsernames.has(alt.username)) {
        seenUsernames.add(alt.username);
        allCandidates.push({
          username: alt.username,
          name: alt.name || alt.unitName || alt.username,
          distanceMeters: alt.distanceMeters !== undefined ? alt.distanceMeters : (alt.distance !== undefined ? alt.distance : null),
          distanceKm: alt.distanceKm,
          routeCoordinates: alt.routeCoordinates || alt.route || slot.routeCoordinates || []
        });
      }
    }

    const isFound = (chosen !== null && Boolean(chosen.username)) || (slot.found !== false && allCandidates.length > 0);

    return {
      slotIndex: slotIndex,
      rawSlot: slot,
      role: slot.role || 'primary',
      kind: slot.kind || slot.unitKind || 'Unit',
      found: Boolean(isFound),
      candidates: allCandidates,
      selectedUnit: allCandidates.length > 0 ? allCandidates[0] : null,
      showRoute: false,
      polyline: null
    };
  }

  // ==========================================
  // 5. Render Slot Row
  // ==========================================

  function renderSlotRow(slotState, slotIndex) {
    const slotRow = document.createElement('div');
    slotRow.style.display = 'flex';
    slotRow.style.alignItems = 'center';
    slotRow.style.justifyContent = 'space-between';
    slotRow.style.padding = '10px 14px';
    slotRow.style.marginBottom = '8px';
    slotRow.style.border = '1px solid #e0e0e0';
    slotRow.style.borderRadius = '4px';
    slotRow.style.backgroundColor = '#fafafa';
    slotRow.style.flexWrap = 'wrap';
    slotRow.style.gap = '8px';

    // Left container: Role badge and Kind
    const leftDiv = document.createElement('div');
    leftDiv.style.display = 'flex';
    leftDiv.style.alignItems = 'center';
    leftDiv.style.gap = '8px';

    const roleBadge = document.createElement('span');
    roleBadge.textContent = String(slotState.role).toUpperCase();
    roleBadge.style.fontWeight = 'bold';
    roleBadge.style.fontSize = '11px';
    roleBadge.style.padding = '3px 7px';
    roleBadge.style.borderRadius = '3px';
    roleBadge.style.color = '#fff';
    const roleLower = String(slotState.role).toLowerCase();
    if (roleLower === 'primary') {
      roleBadge.style.backgroundColor = '#0066cc';
    } else if (roleLower === 'standby') {
      roleBadge.style.backgroundColor = '#6c757d';
    } else {
      roleBadge.style.backgroundColor = '#17a2b8';
    }
    leftDiv.appendChild(roleBadge);

    const kindSpan = document.createElement('span');
    kindSpan.textContent = slotState.kind;
    kindSpan.style.fontWeight = 'bold';
    kindSpan.style.color = '#333';
    leftDiv.appendChild(kindSpan);

    slotRow.appendChild(leftDiv);

    // Right container: Candidate details, Select dropdown, Show route
    const rightDiv = document.createElement('div');
    rightDiv.style.display = 'flex';
    rightDiv.style.alignItems = 'center';
    rightDiv.style.gap = '12px';
    rightDiv.style.flexWrap = 'wrap';

    if (!slotState.found || !slotState.selectedUnit) {
      // If not found: red text indicating no unit available
      const notFoundSpan = document.createElement('span');
      notFoundSpan.textContent = 'No unit available';
      notFoundSpan.style.color = '#d9534f';
      notFoundSpan.style.fontWeight = 'bold';
      rightDiv.appendChild(notFoundSpan);
    } else {
      // Display candidate name/username and distance in km
      const candInfoSpan = document.createElement('span');
      candInfoSpan.style.fontSize = '13px';
      candInfoSpan.style.color = '#444';

      function updateCandText(unit) {
        const nameStr = unit.name ? (unit.name + ' (' + unit.username + ')') : unit.username;
        const distStr = formatDistance(unit.distanceMeters, unit.distanceKm);
        candInfoSpan.textContent = nameStr + (distStr ? ' - ' + distStr : '');
      }
      updateCandText(slotState.selectedUnit);
      rightDiv.appendChild(candInfoSpan);

      // <select> listing chosen unit and alternatives with distances in km
      const selectEl = document.createElement('select');
      selectEl.style.padding = '4px 8px';
      selectEl.style.borderRadius = '3px';
      selectEl.style.border = '1px solid #ccc';
      selectEl.style.fontSize = '13px';
      selectEl.style.cursor = 'pointer';

      slotState.candidates.forEach(cand => {
        const opt = document.createElement('option');
        opt.value = cand.username;
        const nameStr = cand.name ? (cand.name + ' (' + cand.username + ')') : cand.username;
        const distStr = formatDistance(cand.distanceMeters, cand.distanceKm);
        opt.textContent = nameStr + (distStr ? ' [' + distStr + ']' : '');
        if (cand.username === slotState.selectedUnit.username) {
          opt.selected = true;
        }
        selectEl.appendChild(opt);
      });

      selectEl.addEventListener('change', () => {
        const chosenUsername = selectEl.value;
        const nextUnit = slotState.candidates.find(c => c.username === chosenUsername);
        if (nextUnit) {
          slotState.selectedUnit = nextUnit;
          updateCandText(nextUnit);
          if (slotState.showRoute) {
            updateSlotRoute(slotIndex);
          }
        }
      });
      rightDiv.appendChild(selectEl);

      // "Show route" toggle checkbox
      const routeLabel = document.createElement('label');
      routeLabel.style.display = 'inline-flex';
      routeLabel.style.alignItems = 'center';
      routeLabel.style.gap = '5px';
      routeLabel.style.cursor = 'pointer';
      routeLabel.style.fontSize = '13px';
      routeLabel.style.userSelect = 'none';

      const routeCheckbox = document.createElement('input');
      routeCheckbox.type = 'checkbox';
      routeCheckbox.checked = slotState.showRoute;
      routeCheckbox.style.cursor = 'pointer';

      routeCheckbox.addEventListener('change', () => {
        slotState.showRoute = routeCheckbox.checked;
        updateSlotRoute(slotIndex);
      });

      const routeLabelText = document.createElement('span');
      routeLabelText.textContent = 'Show route';

      routeLabel.appendChild(routeCheckbox);
      routeLabel.appendChild(routeLabelText);
      rightDiv.appendChild(routeLabel);
    }

    slotRow.appendChild(rightDiv);
    return slotRow;
  }

  // ==========================================
  // 6. Proposal Panel Rendering
  // ==========================================

  function renderProposalError(errorMsg) {
    if (!proposalPanelEl) return;
    proposalPanelEl.replaceChildren();

    const errorCard = document.createElement('div');
    errorCard.style.border = '1px solid #dc3545';
    errorCard.style.borderRadius = '5px';
    errorCard.style.padding = '16px';
    errorCard.style.backgroundColor = '#fff5f5';

    const errTitle = document.createElement('h4');
    errTitle.textContent = 'Error Generating Dispatch Proposal (Incident: ' + (activeIncidentId || 'N/A') + ')';
    errTitle.style.color = '#dc3545';
    errTitle.style.marginTop = '0';
    errorCard.appendChild(errTitle);

    const errText = document.createElement('p');
    errText.textContent = errorMsg;
    errText.style.color = '#721c24';
    errorCard.appendChild(errText);

    const btnDiv = document.createElement('div');
    btnDiv.style.display = 'flex';
    btnDiv.style.gap = '10px';
    btnDiv.style.marginTop = '12px';

    const btnRetry = document.createElement('button');
    btnRetry.textContent = 'Re-propose';
    btnRetry.style.padding = '6px 14px';
    btnRetry.style.backgroundColor = '#0066cc';
    btnRetry.style.color = '#fff';
    btnRetry.style.border = 'none';
    btnRetry.style.borderRadius = '3px';
    btnRetry.style.cursor = 'pointer';
    btnRetry.style.fontWeight = 'bold';
    btnRetry.addEventListener('click', () => open(activeIncidentId));
    btnDiv.appendChild(btnRetry);

    const btnDismiss = document.createElement('button');
    btnDismiss.textContent = 'Dismiss';
    btnDismiss.style.padding = '6px 14px';
    btnDismiss.style.backgroundColor = '#6c757d';
    btnDismiss.style.color = '#fff';
    btnDismiss.style.border = 'none';
    btnDismiss.style.borderRadius = '3px';
    btnDismiss.style.cursor = 'pointer';
    btnDismiss.addEventListener('click', () => {
      clearRouteLayers();
      proposalPanelEl.replaceChildren();
      proposalPanelEl.style.display = 'none';
    });
    btnDiv.appendChild(btnDismiss);

    errorCard.appendChild(btnDiv);
    proposalPanelEl.appendChild(errorCard);
  }

  function renderProposalPanel(proposal) {
    if (!proposalPanelEl) return;
    proposalPanelEl.replaceChildren();

    const panelCard = document.createElement('div');
    panelCard.style.border = '2px solid #0066cc';
    panelCard.style.borderRadius = '6px';
    panelCard.style.padding = '16px';
    panelCard.style.backgroundColor = '#fff';
    panelCard.style.boxShadow = '0 2px 6px rgba(0,0,0,0.08)';

    // Header with Incident ID
    const header = document.createElement('div');
    header.style.display = 'flex';
    header.style.justifyContent = 'space-between';
    header.style.alignItems = 'center';
    header.style.borderBottom = '1px solid #e0e0e0';
    header.style.paddingBottom = '10px';
    header.style.marginBottom = '14px';

    const title = document.createElement('h3');
    title.textContent = 'Dispatch Proposal: Incident ' + (proposal.incidentId || activeIncidentId || '');
    title.style.margin = '0';
    title.style.color = '#0066cc';
    header.appendChild(title);

    const btnClose = document.createElement('button');
    btnClose.textContent = '✕ Close';
    btnClose.style.background = 'none';
    btnClose.style.border = 'none';
    btnClose.style.fontSize = '14px';
    btnClose.style.cursor = 'pointer';
    btnClose.style.color = '#777';
    btnClose.style.fontWeight = 'bold';
    btnClose.addEventListener('click', () => {
      clearRouteLayers();
      proposalPanelEl.replaceChildren();
      proposalPanelEl.style.display = 'none';
    });
    header.appendChild(btnClose);

    panelCard.appendChild(header);

    // Slots List
    const rawSlots = Array.isArray(proposal.slots) ? proposal.slots : [];
    currentSlots = rawSlots.map((s, idx) => normalizeSlotCandidates(s, idx));

    if (currentSlots.length === 0) {
      const emptySlots = document.createElement('p');
      emptySlots.textContent = 'No response unit slots found for this incident proposal.';
      emptySlots.style.color = '#777';
      emptySlots.style.fontStyle = 'italic';
      panelCard.appendChild(emptySlots);
    } else {
      const slotsContainer = document.createElement('div');
      slotsContainer.style.marginBottom = '14px';

      currentSlots.forEach((slotState, idx) => {
        const slotRow = renderSlotRow(slotState, idx);
        slotsContainer.appendChild(slotRow);
      });
      panelCard.appendChild(slotsContainer);
    }

    // Red list of warnings (if any)
    const warnings = Array.isArray(proposal.warnings) ? proposal.warnings : (proposal.warning ? [proposal.warning] : []);
    if (warnings.length > 0) {
      const warningsBox = document.createElement('div');
      warningsBox.style.backgroundColor = '#fff5f5';
      warningsBox.style.border = '1px solid #dc3545';
      warningsBox.style.borderRadius = '4px';
      warningsBox.style.padding = '10px 14px';
      warningsBox.style.marginBottom = '14px';

      const wTitle = document.createElement('div');
      wTitle.textContent = 'Dispatch Warnings:';
      wTitle.style.fontWeight = 'bold';
      wTitle.style.color = '#dc3545';
      wTitle.style.marginBottom = '6px';
      warningsBox.appendChild(wTitle);

      const wList = document.createElement('ul');
      wList.style.margin = '0';
      wList.style.paddingLeft = '20px';
      wList.style.color = '#dc3545';

      warnings.forEach(w => {
        const li = document.createElement('li');
        li.textContent = String(w);
        wList.appendChild(li);
      });
      warningsBox.appendChild(wList);
      panelCard.appendChild(warningsBox);
    }

    // Buttons: "Approve and alert all", "Re-propose"
    const btnRow = document.createElement('div');
    btnRow.style.display = 'flex';
    btnRow.style.gap = '10px';
    btnRow.style.alignItems = 'center';
    btnRow.style.flexWrap = 'wrap';
    btnRow.style.marginTop = '12px';

    const btnApprove = document.createElement('button');
    btnApprove.textContent = 'Approve and alert all';
    btnApprove.style.padding = '8px 20px';
    btnApprove.style.backgroundColor = '#28a745';
    btnApprove.style.color = '#fff';
    btnApprove.style.border = 'none';
    btnApprove.style.borderRadius = '4px';
    btnApprove.style.fontWeight = 'bold';
    btnApprove.style.cursor = 'pointer';

    const btnRepropose = document.createElement('button');
    btnRepropose.textContent = 'Re-propose';
    btnRepropose.style.padding = '8px 18px';
    btnRepropose.style.backgroundColor = '#0066cc';
    btnRepropose.style.color = '#fff';
    btnRepropose.style.border = 'none';
    btnRepropose.style.borderRadius = '4px';
    btnRepropose.style.fontWeight = 'bold';
    btnRepropose.style.cursor = 'pointer';

    const feedbackEl = document.createElement('div');
    feedbackEl.style.width = '100%';
    feedbackEl.style.marginTop = '8px';
    feedbackEl.style.fontSize = '13px';
    feedbackEl.style.fontWeight = 'bold';

    btnApprove.addEventListener('click', async () => {
      const unitsToAlert = [];
      currentSlots.forEach(s => {
        if (s.selectedUnit && s.selectedUnit.username) {
          unitsToAlert.push({
            username: s.selectedUnit.username,
            role: s.role
          });
        }
      });

      if (unitsToAlert.length === 0) {
        feedbackEl.textContent = 'No units available to dispatch.';
        feedbackEl.style.color = '#dc3545';
        return;
      }

      btnApprove.disabled = true;
      btnRepropose.disabled = true;
      btnApprove.textContent = 'Approving...';
      feedbackEl.textContent = '';

      const res = await callBridge('approve_dispatch', {
        incidentId: activeIncidentId,
        units: unitsToAlert
      });

      btnApprove.disabled = false;
      btnRepropose.disabled = false;
      btnApprove.textContent = 'Approve and alert all';

      if (res && res.ok) {
        feedbackEl.style.color = '#28a745';
        feedbackEl.textContent = 'Dispatch approved and all units alerted successfully.';
        fetchAndRenderLiveIncidents();
      } else {
        feedbackEl.style.color = '#dc3545';
        feedbackEl.textContent = (res && res.error) ? res.error : 'Failed to approve dispatch.';
      }
    });

    btnRepropose.addEventListener('click', () => {
      open(activeIncidentId);
    });

    btnRow.appendChild(btnApprove);
    btnRow.appendChild(btnRepropose);
    panelCard.appendChild(btnRow);
    panelCard.appendChild(feedbackEl);

    proposalPanelEl.replaceChildren(panelCard);
  }

  // ==========================================
  // 7. Live Section: Polling & Rendering
  // ==========================================

  async function fetchAndRenderLiveIncidents() {
    if (isPolling) return;
    isPolling = true;

    try {
      const res = await callBridge('list_incidents');
      let incidents = [];
      if (Array.isArray(res)) {
        incidents = res;
      } else if (res && Array.isArray(res.incidents)) {
        incidents = res.incidents;
      } else if (res && res.data && Array.isArray(res.data.incidents)) {
        incidents = res.data.incidents;
      } else if (res && Array.isArray(res.data)) {
        incidents = res.data;
      }

      renderLiveIncidents(incidents);
    } catch (err) {
      // Ignore network hiccup during background polling
    } finally {
      isPolling = false;
    }
  }

  function renderLiveIncidents(incidents) {
    if (!liveSectionEl) return;

    const contentArea = liveSectionEl.querySelector('.dispatch-live-content');
    if (!contentArea) return;

    // Filter for every Dispatched incident
    const dispatched = (Array.isArray(incidents) ? incidents : []).filter(inc => {
      if (!inc) return false;
      const s = String(inc.status || inc.state || '').trim().toLowerCase();
      return s === 'dispatched';
    });

    if (dispatched.length === 0) {
      contentArea.replaceChildren();
      const emptyMsg = document.createElement('p');
      emptyMsg.textContent = 'No active dispatched incidents.';
      emptyMsg.style.color = '#777';
      emptyMsg.style.fontStyle = 'italic';
      emptyMsg.style.margin = '10px 0';
      contentArea.appendChild(emptyMsg);
      return;
    }

    const newFragment = document.createDocumentFragment();

    dispatched.forEach(inc => {
      const card = document.createElement('div');
      card.style.border = '1px solid #dcdcdc';
      card.style.borderRadius = '5px';
      card.style.padding = '14px';
      card.style.marginBottom = '14px';
      card.style.backgroundColor = '#fff';
      card.style.boxShadow = '0 1px 3px rgba(0,0,0,0.06)';

      // Incident Card Header
      const cardHeader = document.createElement('div');
      cardHeader.style.display = 'flex';
      cardHeader.style.justifyContent = 'space-between';
      cardHeader.style.alignItems = 'center';
      cardHeader.style.marginBottom = '10px';
      cardHeader.style.borderBottom = '1px solid #eee';
      cardHeader.style.paddingBottom = '8px';
      cardHeader.style.flexWrap = 'wrap';
      cardHeader.style.gap = '8px';

      const titleDiv = document.createElement('div');
      titleDiv.style.display = 'flex';
      titleDiv.style.alignItems = 'center';
      titleDiv.style.gap = '10px';

      const incBadge = document.createElement('span');
      incBadge.textContent = 'DISPATCHED';
      incBadge.style.backgroundColor = '#28a745';
      incBadge.style.color = '#fff';
      incBadge.style.fontSize = '11px';
      incBadge.style.fontWeight = 'bold';
      incBadge.style.padding = '2px 8px';
      incBadge.style.borderRadius = '3px';
      titleDiv.appendChild(incBadge);

      const incTitle = document.createElement('h4');
      incTitle.textContent = (inc.id || inc.incidentId || 'Incident') + (inc.title ? (' - ' + inc.title) : '');
      incTitle.style.margin = '0';
      incTitle.style.color = '#222';
      titleDiv.appendChild(incTitle);

      cardHeader.appendChild(titleDiv);

      const btnInspect = document.createElement('button');
      btnInspect.textContent = 'Inspect / Re-dispatch';
      btnInspect.style.padding = '4px 10px';
      btnInspect.style.fontSize = '12px';
      btnInspect.style.backgroundColor = '#f0f0f0';
      btnInspect.style.border = '1px solid #ccc';
      btnInspect.style.borderRadius = '3px';
      btnInspect.style.cursor = 'pointer';
      btnInspect.addEventListener('click', () => {
        open(inc.id || inc.incidentId);
      });
      cardHeader.appendChild(btnInspect);

      card.appendChild(cardHeader);

      // Visible banner when warnings exist (escalation or no available unit)
      const rawWarnings = Array.isArray(inc.warnings) ? inc.warnings : (inc.warning ? [inc.warning] : []);
      const isEscalated = inc.escalated || inc.isEscalated || inc.escalation || false;
      const hasNoUnitWarning = inc.hasNoAvailableUnit || false;

      const assignments = Array.isArray(inc.assignments) ? inc.assignments : (Array.isArray(inc.units) ? inc.units : (Array.isArray(inc.dispatchedUnits) ? inc.dispatchedUnits : []));

      let assignmentWarningFound = false;
      assignments.forEach(a => {
        const state = String(a.alertState || a.state || a.status || '').toLowerCase();
        if (state === 'declined' || state === 'timeout' || state === 'escalated') {
          assignmentWarningFound = true;
        }
      });

      const hasWarnings = rawWarnings.length > 0 || isEscalated || hasNoUnitWarning || assignmentWarningFound;

      if (hasWarnings) {
        const banner = document.createElement('div');
        banner.style.backgroundColor = '#fff3cd';
        banner.style.color = '#856404';
        banner.style.border = '1px solid #ffeeba';
        banner.style.borderLeft = '5px solid #dc3545';
        banner.style.borderRadius = '4px';
        banner.style.padding = '8px 12px';
        banner.style.marginBottom = '12px';
        banner.style.fontWeight = 'bold';
        banner.style.fontSize = '13px';

        const bannerTitle = document.createElement('div');
        bannerTitle.textContent = 'WARNING: Escalation or unit alert condition active!';
        bannerTitle.style.marginBottom = rawWarnings.length > 0 ? '4px' : '0';
        banner.appendChild(bannerTitle);

        if (rawWarnings.length > 0) {
          const wList = document.createElement('ul');
          wList.style.margin = '4px 0 0 0';
          wList.style.paddingLeft = '18px';
          wList.style.fontWeight = 'normal';
          rawWarnings.forEach(w => {
            const li = document.createElement('li');
            li.textContent = String(w);
            wList.appendChild(li);
          });
          banner.appendChild(wList);
        } else if (assignmentWarningFound) {
          const wDetail = document.createElement('div');
          wDetail.textContent = 'One or more units timed out, declined, or escalated.';
          wDetail.style.fontWeight = 'normal';
          wDetail.style.fontSize = '12px';
          banner.appendChild(wDetail);
        }

        card.appendChild(banner);
      }

      // Display each assignment
      const assignHeader = document.createElement('h5');
      assignHeader.textContent = 'Assignments:';
      assignHeader.style.margin = '8px 0 6px 0';
      assignHeader.style.color = '#444';
      card.appendChild(assignHeader);

      if (assignments.length === 0) {
        const noAssign = document.createElement('p');
        noAssign.textContent = 'No unit assignments on record.';
        noAssign.style.color = '#888';
        noAssign.style.fontSize = '13px';
        noAssign.style.margin = '4px 0 10px 0';
        card.appendChild(noAssign);
      } else {
        const table = document.createElement('table');
        table.style.width = '100%';
        table.style.borderCollapse = 'collapse';
        table.style.fontSize = '13px';
        table.style.marginBottom = '12px';

        const thead = document.createElement('thead');
        const trh = document.createElement('tr');
        trh.style.backgroundColor = '#f1f1f1';
        trh.style.textAlign = 'left';

        const colHeaders = ['Unit', 'Kind', 'Role', 'Attempt', 'Alert State', 'Distance'];
        colHeaders.forEach(h => {
          const th = document.createElement('th');
          th.textContent = h;
          th.style.padding = '6px 8px';
          th.style.border = '1px solid #ddd';
          trh.appendChild(th);
        });
        thead.appendChild(trh);
        table.appendChild(thead);

        const tbody = document.createElement('tbody');
        assignments.forEach(a => {
          const tr = document.createElement('tr');
          tr.style.borderBottom = '1px solid #eee';

          // Unit username/name
          const tdUnit = document.createElement('td');
          tdUnit.style.padding = '6px 8px';
          tdUnit.style.border = '1px solid #ddd';
          const uname = a.username || (a.unit && a.unit.username) || '';
          const name = a.name || (a.unit && a.unit.name) || '';
          tdUnit.textContent = name ? (name + ' (' + uname + ')') : (uname || 'N/A');
          tr.appendChild(tdUnit);

          // Kind
          const tdKind = document.createElement('td');
          tdKind.style.padding = '6px 8px';
          tdKind.style.border = '1px solid #ddd';
          tdKind.textContent = a.kind || (a.unit && a.unit.kind) || a.type || 'N/A';
          tr.appendChild(tdKind);

          // Role
          const tdRole = document.createElement('td');
          tdRole.style.padding = '6px 8px';
          tdRole.style.border = '1px solid #ddd';
          tdRole.textContent = a.role || 'N/A';
          tr.appendChild(tdRole);

          // Attempt number
          const tdAttempt = document.createElement('td');
          tdAttempt.style.padding = '6px 8px';
          tdAttempt.style.border = '1px solid #ddd';
          const attemptVal = a.attempt !== undefined ? a.attempt : (a.attemptNumber !== undefined ? a.attemptNumber : (a.attempts !== undefined ? a.attempts : 1));
          tdAttempt.textContent = String(attemptVal);
          tr.appendChild(tdAttempt);

          // Alert state
          const tdState = document.createElement('td');
          tdState.style.padding = '6px 8px';
          tdState.style.border = '1px solid #ddd';
          const alertState = a.alertState || a.state || a.status || 'Pending';
          tdState.textContent = alertState;
          tdState.style.fontWeight = 'bold';
          const stateLower = String(alertState).toLowerCase();
          if (stateLower === 'accepted') {
            tdState.style.color = '#28a745';
          } else if (stateLower === 'pending') {
            tdState.style.color = '#e67e22';
          } else if (stateLower === 'declined' || stateLower === 'timeout') {
            tdState.style.color = '#dc3545';
          } else if (stateLower === 'en route' || stateLower === 'en_route') {
            tdState.style.color = '#0066cc';
          } else if (stateLower === 'on scene' || stateLower === 'on_scene') {
            tdState.style.color = '#6f42c1';
          }
          tr.appendChild(tdState);

          // Distance in km
          const tdDist = document.createElement('td');
          tdDist.style.padding = '6px 8px';
          tdDist.style.border = '1px solid #ddd';
          const distM = a.distanceMeters !== undefined ? a.distanceMeters : (a.distance !== undefined ? a.distance : (a.unit && a.unit.distanceMeters));
          const distK = a.distanceKm !== undefined ? a.distanceKm : (a.unit && a.unit.distanceKm);
          tdDist.textContent = formatDistance(distM, distK) || 'N/A';
          tr.appendChild(tdDist);

          tbody.appendChild(tr);
        });
        table.appendChild(tbody);
        card.appendChild(table);
      }

      // Incident Log Entries
      const logHeader = document.createElement('h5');
      logHeader.textContent = 'Incident Log:';
      logHeader.style.margin = '8px 0 6px 0';
      logHeader.style.color = '#444';
      card.appendChild(logHeader);

      const logs = Array.isArray(inc.log) ? inc.log : (Array.isArray(inc.logs) ? inc.logs : (Array.isArray(inc.history) ? inc.history : (Array.isArray(inc.timeline) ? inc.timeline : [])));

      if (logs.length === 0) {
        const noLogs = document.createElement('p');
        noLogs.textContent = 'No log entries recorded.';
        noLogs.style.color = '#888';
        noLogs.style.fontSize = '12px';
        noLogs.style.margin = '4px 0 0 0';
        card.appendChild(noLogs);
      } else {
        const logBox = document.createElement('div');
        logBox.style.backgroundColor = '#f8f9fa';
        logBox.style.border = '1px solid #e9ecef';
        logBox.style.borderRadius = '4px';
        logBox.style.padding = '8px 10px';
        logBox.style.maxHeight = '140px';
        logBox.style.overflowY = 'auto';

        logs.forEach(entry => {
          const entryRow = document.createElement('div');
          entryRow.style.fontSize = '12px';
          entryRow.style.marginBottom = '4px';
          entryRow.style.paddingBottom = '3px';
          entryRow.style.borderBottom = '1px dashed #e2e2e2';
          entryRow.style.display = 'flex';
          entryRow.style.gap = '8px';

          const tsVal = entry.timestamp || entry.time || entry.date || entry.ts || entry.createdAt;
          const textVal = entry.text || entry.message || entry.msg || entry.entry || entry.action || (typeof entry === 'string' ? entry : '');

          const tsSpan = document.createElement('span');
          tsSpan.textContent = tsVal ? ('[' + formatTimestamp(tsVal) + ']') : '';
          tsSpan.style.color = '#6c757d';
          tsSpan.style.whiteSpace = 'nowrap';
          entryRow.appendChild(tsSpan);

          const textSpan = document.createElement('span');
          textSpan.textContent = textVal;
          textSpan.style.color = '#333';
          entryRow.appendChild(textSpan);

          logBox.appendChild(entryRow);
        });
        card.appendChild(logBox);
      }

      newFragment.appendChild(card);
    });

    contentArea.replaceChildren(newFragment);
  }

  function startPolling() {
    if (pollTimer) return;
    fetchAndRenderLiveIncidents();
    pollTimer = setInterval(fetchAndRenderLiveIncidents, 2000);
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  // ==========================================
  // 8. Public Interface
  // ==========================================

  function init(mountEl) {
    stop();

    if (!mountEl) return;
    containerEl = mountEl;
    containerEl.replaceChildren();

    const root = document.createElement('div');
    root.style.fontFamily = '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif';
    root.style.maxWidth = '1000px';
    root.style.margin = '0 auto';
    root.style.padding = '10px 0';

    // Proposal Panel container (hidden until open(incidentId) is called)
    proposalPanelEl = document.createElement('div');
    proposalPanelEl.id = 'dispatch-proposal-panel';
    proposalPanelEl.style.display = 'none';
    proposalPanelEl.style.marginBottom = '20px';
    root.appendChild(proposalPanelEl);

    // Live Section container
    liveSectionEl = document.createElement('div');
    liveSectionEl.id = 'dispatch-live-section';

    const liveHeader = document.createElement('div');
    liveHeader.style.display = 'flex';
    liveHeader.style.justifyContent = 'space-between';
    liveHeader.style.alignItems = 'center';
    liveHeader.style.borderBottom = '2px solid #0066cc';
    liveHeader.style.paddingBottom = '8px';
    liveHeader.style.marginBottom = '12px';

    const liveTitle = document.createElement('h3');
    liveTitle.textContent = 'Active Dispatches (Live Updates Every 2s)';
    liveTitle.style.margin = '0';
    liveTitle.style.color = '#0066cc';
    liveHeader.appendChild(liveTitle);

    liveSectionEl.appendChild(liveHeader);

    const liveContent = document.createElement('div');
    liveContent.className = 'dispatch-live-content';
    liveSectionEl.appendChild(liveContent);

    root.appendChild(liveSectionEl);
    containerEl.appendChild(root);

    // Start polling list_incidents every 2 seconds
    startPolling();
  }

  function stop() {
    stopPolling();
    clearRouteLayers();
  }

  async function open(incidentId) {
    if (!proposalPanelEl) {
      if (containerEl) {
        init(containerEl);
      } else {
        const fallback = document.getElementById('view-dispatch') || document.getElementById('dispatch-container') || document.body;
        if (fallback) init(fallback);
      }
    }

    activeIncidentId = incidentId;
    clearRouteLayers();

    if (!proposalPanelEl) return;

    proposalPanelEl.style.display = 'block';
    proposalPanelEl.replaceChildren();

    const loadingDiv = document.createElement('div');
    loadingDiv.style.padding = '16px';
    loadingDiv.style.border = '1px solid #ccc';
    loadingDiv.style.borderRadius = '4px';
    loadingDiv.style.backgroundColor = '#f9f9f9';
    loadingDiv.style.textAlign = 'center';
    loadingDiv.style.color = '#666';
    loadingDiv.textContent = 'Generating dispatch proposal for incident: ' + incidentId + '...';
    proposalPanelEl.appendChild(loadingDiv);

    const res = await callBridge('propose_dispatch', { incidentId });

    if (!res || !res.ok) {
      renderProposalError((res && res.error) ? res.error : 'Failed to generate dispatch proposal from server.');
      return;
    }

    const proposalData = res.proposal || res.data || res;
    renderProposalPanel(proposalData);
  }

  // Export module to window
  const RapidAidDispatch = {
    init: init,
    stop: stop,
    open: open
  };

  if (typeof window !== 'undefined') {
    window.RapidAidDispatch = RapidAidDispatch;
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = RapidAidDispatch;
  }
})();
