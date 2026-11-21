/**
 * RapidAid Emergency Dispatch System - Dispatch View (dispatch.js)
 * Implements window.RapidAidDispatch = { init(mountEl), stop(), open(incidentId) }
 *
 * SPECIFICATION COMPLIANCE:
 * - Uses modules.css classes (prefixed dispatch-) using CSS variables for dark/night mode.
 * - All server or user text inserted with textContent, NEVER innerHTML.
 * - Route coordinates from server ([lon, lat]) swapped to [lat, lon] for Leaflet.
 * - Map layers stored in an independent L.layerGroup removed on stop/close/switch.
 * - Distances strictly formatted in km to 2 decimals: (distanceMeters / 1000).toFixed(2) + " km".
 * - NO ETA stated anywhere.
 * - Dispatches rapidaid:session-expired CustomEvent and stops timers on not_logged_in / session_expired.
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
  // 1. Session Expiry & Bridge Communication Helpers
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

  async function callBridge(action, payload = {}) {
    let res = null;
    if (typeof window !== 'undefined' && window.RapidAid && typeof window.RapidAid.callBridge === 'function') {
      try {
        res = await window.RapidAid.callBridge(action, payload);
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
        body: JSON.stringify({ action, token, payload })
      });
      res = await httpRes.json();
      checkSessionExpired(res);
      return res;
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

  // ==========================================
  // 3. Coordinate Swapping & Route Drawing
  // ==========================================

  function convertGeoJsonCoordsToLatLngs(coords) {
    if (!Array.isArray(coords)) return [];
    return coords.map(pt => {
      if (Array.isArray(pt) && pt.length >= 2) {
        return [Number(pt[1]), Number(pt[0])]; // swap [lon, lat] -> [lat, lon]
      }
      return null;
    }).filter(pt => pt !== null && !isNaN(pt[0]) && !isNaN(pt[1]));
  }

  function updateSlotRoute(slotIndex) {
    const layerGroup = getRouteLayerGroup();
    if (!layerGroup) return;

    const slotState = currentSlots[slotIndex];
    if (!slotState) return;

    if (slotState.polyline) {
      layerGroup.removeLayer(slotState.polyline);
      slotState.polyline = null;
    }

    if (!slotState.showRoute || !slotState.selectedUnit) {
      return;
    }

    const routeData = slotState.selectedUnit.route;
    if (!routeData) return;

    let latLngs = [];
    if (Array.isArray(routeData.coordinates)) {
      latLngs = convertGeoJsonCoordsToLatLngs(routeData.coordinates);
    } else if (routeData.geometry && Array.isArray(routeData.geometry.coordinates)) {
      latLngs = convertGeoJsonCoordsToLatLngs(routeData.geometry.coordinates);
    } else if (Array.isArray(routeData)) {
      latLngs = convertGeoJsonCoordsToLatLngs(routeData);
    }

    if (latLngs.length < 2) return;

    const colors = ['#0066cc', '#e67e22', '#28a745', '#6f42c1', '#d9534f'];
    const strokeColor = colors[slotIndex % colors.length];

    const polyline = L.polyline(latLngs, {
      color: strokeColor,
      weight: 5,
      opacity: 0.8,
      dashArray: (slotState.role && String(slotState.role).toLowerCase() === 'standby') ? '6, 8' : null
    });

    const unitName = slotState.selectedUnit.name || slotState.selectedUnit.username;
    const distStr = formatDistance(slotState.selectedUnit.distanceMeters, slotState.selectedUnit.distanceKm);
    polyline.bindPopup(
      '[' + (slotState.role || 'Unit') + '] ' + slotState.kind + ': ' + unitName +
      (distStr ? '<br>Distance: ' + distStr : '')
    );

    layerGroup.addLayer(polyline);
    slotState.polyline = polyline;

    const map = getSharedMap();
    if (map && typeof map.fitBounds === 'function') {
      try {
        map.fitBounds(polyline.getBounds(), { padding: [30, 30] });
      } catch (e) {}
    }
  }

  // ==========================================
  // 4. Candidate Slot Normalization
  // ==========================================

  function normalizeSlotCandidates(rawSlot, slotIndex) {
    const role = rawSlot.role || rawSlot.slotRole || 'primary';
    const kind = rawSlot.kind || rawSlot.unitKind || rawSlot.type || 'Unit';
    const found = rawSlot.found !== undefined ? Boolean(rawSlot.found) : true;

    let candidates = [];
    if (Array.isArray(rawSlot.candidates)) {
      candidates = rawSlot.candidates;
    } else if (rawSlot.candidate) {
      candidates = [rawSlot.candidate];
    } else if (rawSlot.unit) {
      candidates = [rawSlot.unit];
    }

    let selectedUnit = null;
    if (rawSlot.selectedUnit) {
      selectedUnit = rawSlot.selectedUnit;
    } else if (rawSlot.assignedUnit) {
      selectedUnit = rawSlot.assignedUnit;
    } else if (candidates.length > 0) {
      selectedUnit = candidates[0];
    }

    return {
      slotIndex,
      role,
      kind,
      found,
      candidates,
      selectedUnit,
      showRoute: false,
      polyline: null
    };
  }

  // ==========================================
  // 5. DOM Generation: Slot Rows
  // ==========================================

  function renderSlotRow(slotState, slotIndex) {
    const slotRow = document.createElement('div');
    slotRow.className = 'dispatch-slot-row';

    // Left container: Role badge, kind
    const leftDiv = document.createElement('div');
    leftDiv.className = 'dispatch-slot-left';

    const roleBadge = document.createElement('span');
    roleBadge.textContent = String(slotState.role).toUpperCase();
    const roleLower = String(slotState.role).toLowerCase();
    roleBadge.className = 'dispatch-slot-badge dispatch-slot-badge-' + roleLower;
    leftDiv.appendChild(roleBadge);

    const kindSpan = document.createElement('span');
    kindSpan.textContent = slotState.kind;
    kindSpan.className = 'dispatch-slot-label';
    leftDiv.appendChild(kindSpan);

    slotRow.appendChild(leftDiv);

    // Right container: Candidate details, Select dropdown, Show route
    const rightDiv = document.createElement('div');
    rightDiv.className = 'dispatch-slot-right';

    if (!slotState.found || !slotState.selectedUnit) {
      const notFoundSpan = document.createElement('span');
      notFoundSpan.textContent = 'No unit available';
      notFoundSpan.className = 'dispatch-feedback-err';
      rightDiv.appendChild(notFoundSpan);
    } else {
      const candInfoSpan = document.createElement('span');
      candInfoSpan.className = 'dispatch-slot-dist';

      function updateCandText(unit) {
        const nameStr = unit.name ? (unit.name + ' (' + unit.username + ')') : unit.username;
        const distStr = formatDistance(unit.distanceMeters, unit.distanceKm);
        candInfoSpan.textContent = nameStr + (distStr ? ' - ' + distStr : '');
      }
      updateCandText(slotState.selectedUnit);
      rightDiv.appendChild(candInfoSpan);

      const selectEl = document.createElement('select');
      selectEl.className = 'dispatch-slot-select';

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

      const routeLabel = document.createElement('label');
      routeLabel.className = 'dispatch-route-label';

      const routeCheckbox = document.createElement('input');
      routeCheckbox.type = 'checkbox';
      routeCheckbox.checked = slotState.showRoute;
      routeCheckbox.className = 'dispatch-route-checkbox';

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
    errorCard.className = 'dispatch-error-card';

    const errTitle = document.createElement('h4');
    errTitle.textContent = 'Error Generating Dispatch Proposal (Incident: ' + (activeIncidentId || 'N/A') + ')';
    errTitle.className = 'dispatch-error-title';
    errorCard.appendChild(errTitle);

    const errText = document.createElement('p');
    errText.textContent = errorMsg;
    errText.className = 'dispatch-error-text';
    errorCard.appendChild(errText);

    const btnDiv = document.createElement('div');
    btnDiv.className = 'dispatch-btn-row';

    const btnRetry = document.createElement('button');
    btnRetry.textContent = 'Re-propose';
    btnRetry.className = 'dispatch-btn-repropose';
    btnRetry.addEventListener('click', () => open(activeIncidentId));
    btnDiv.appendChild(btnRetry);

    const btnDismiss = document.createElement('button');
    btnDismiss.textContent = 'Dismiss';
    btnDismiss.className = 'dispatch-btn-dismiss';
    btnDismiss.addEventListener('click', () => {
      clearRouteLayers();
      proposalPanelEl.replaceChildren();
      proposalPanelEl.classList.remove('dispatch-proposal-panel-open');
    });
    btnDiv.appendChild(btnDismiss);

    errorCard.appendChild(btnDiv);
    proposalPanelEl.appendChild(errorCard);
  }

  function renderProposalPanel(proposal) {
    if (!proposalPanelEl) return;
    proposalPanelEl.replaceChildren();

    const panelCard = document.createElement('div');
    panelCard.className = 'dispatch-panel-card';

    // Header with Incident ID
    const header = document.createElement('div');
    header.className = 'dispatch-panel-header';

    const title = document.createElement('h3');
    title.textContent = 'Dispatch Proposal: Incident ' + (proposal.incidentId || activeIncidentId || '');
    title.className = 'dispatch-panel-title';
    header.appendChild(title);

    const btnClose = document.createElement('button');
    btnClose.textContent = '✕ Close';
    btnClose.className = 'dispatch-btn-close';
    btnClose.addEventListener('click', () => {
      clearRouteLayers();
      proposalPanelEl.replaceChildren();
      proposalPanelEl.classList.remove('dispatch-proposal-panel-open');
    });
    header.appendChild(btnClose);

    panelCard.appendChild(header);

    // Slots List
    const rawSlots = Array.isArray(proposal.slots) ? proposal.slots : [];
    currentSlots = rawSlots.map((s, idx) => normalizeSlotCandidates(s, idx));

    if (currentSlots.length === 0) {
      const emptySlots = document.createElement('p');
      emptySlots.textContent = 'No response unit slots found for this incident proposal.';
      emptySlots.className = 'dispatch-empty-msg';
      panelCard.appendChild(emptySlots);
    } else {
      const slotsContainer = document.createElement('div');

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
      warningsBox.className = 'dispatch-warnings-box';

      const wTitle = document.createElement('div');
      wTitle.textContent = 'Dispatch Warnings:';
      wTitle.className = 'dispatch-warnings-title';
      warningsBox.appendChild(wTitle);

      const wList = document.createElement('ul');
      wList.className = 'dispatch-warnings-list';

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
    btnRow.className = 'dispatch-btn-row';

    const btnApprove = document.createElement('button');
    btnApprove.textContent = 'Approve and alert all';
    btnApprove.className = 'dispatch-btn-approve';

    const btnRepropose = document.createElement('button');
    btnRepropose.textContent = 'Re-propose';
    btnRepropose.className = 'dispatch-btn-repropose';

    const feedbackEl = document.createElement('div');
    feedbackEl.className = 'dispatch-feedback';

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
        feedbackEl.className = 'dispatch-feedback dispatch-feedback-err';
        return;
      }

      btnApprove.disabled = true;
      btnRepropose.disabled = true;
      btnApprove.textContent = 'Approving...';
      feedbackEl.textContent = '';
      feedbackEl.className = 'dispatch-feedback';

      const res = await callBridge('approve_dispatch', {
        incidentId: activeIncidentId,
        units: unitsToAlert
      });

      btnApprove.disabled = false;
      btnRepropose.disabled = false;
      btnApprove.textContent = 'Approve and alert all';

      if (res && res.ok) {
        feedbackEl.textContent = 'Dispatch approved and all units alerted successfully.';
        feedbackEl.className = 'dispatch-feedback dispatch-feedback-ok';
        fetchAndRenderLiveIncidents();
      } else {
        feedbackEl.textContent = (res && res.error) ? res.error : 'Failed to approve dispatch.';
        feedbackEl.className = 'dispatch-feedback dispatch-feedback-err';
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
  // 7. Live Incidents Polling & Rendering
  // ==========================================

  async function fetchAndRenderLiveIncidents() {
    if (!liveSectionEl) return;
    const contentEl = liveSectionEl.querySelector('.dispatch-live-content');
    if (!contentEl) return;

    try {
      const res = await callBridge('list_incidents');
      if (!res || !res.ok) {
        return;
      }

      const incidents = Array.isArray(res.incidents) ? res.incidents : (Array.isArray(res) ? res : []);
      const activeIncidents = incidents.filter(inc => {
        const status = String(inc.status || '').toLowerCase();
        return status === 'dispatched' || status === 'proposed' || status === 'active';
      });

      contentEl.replaceChildren();

      if (activeIncidents.length === 0) {
        const emptyMsg = document.createElement('p');
        emptyMsg.textContent = 'No active dispatches at this time.';
        emptyMsg.className = 'dispatch-empty-msg';
        contentEl.appendChild(emptyMsg);
        return;
      }

      activeIncidents.forEach(inc => {
        const card = document.createElement('div');
        card.className = 'dispatch-card';

        // Card Header
        const cardHeader = document.createElement('div');
        cardHeader.className = 'dispatch-card-header';

        const titleDiv = document.createElement('div');
        titleDiv.className = 'dispatch-title-group';

        const incBadge = document.createElement('span');
        incBadge.textContent = inc.type || 'Incident';
        incBadge.className = 'dispatch-badge';
        titleDiv.appendChild(incBadge);

        const incTitle = document.createElement('h4');
        incTitle.textContent = '#' + inc.id + ' - ' + (inc.locationText || inc.location_text || inc.location || 'Unknown Location');
        incTitle.className = 'dispatch-inc-title';
        titleDiv.appendChild(incTitle);

        cardHeader.appendChild(titleDiv);

        // "Inspect on map" button
        const btnInspect = document.createElement('button');
        btnInspect.textContent = 'Inspect on Map';
        btnInspect.className = 'dispatch-btn-inspect';
        btnInspect.addEventListener('click', () => {
          const map = getSharedMap();
          if (map) {
            let lat = inc.lat;
            let lon = inc.lon;
            if (inc.location && inc.location.lat) {
              lat = inc.location.lat;
              lon = inc.location.lon;
            }
            if (lat !== undefined && lon !== undefined && !isNaN(lat) && !isNaN(lon)) {
              map.setView([lat, lon], 16);
            }
          }
        });
        cardHeader.appendChild(btnInspect);
        card.appendChild(cardHeader);

        // Warning banner on active incident card (if any)
        const rawWarnings = Array.isArray(inc.warnings) ? inc.warnings : (inc.warning ? [inc.warning] : []);
        const hasWarning = inc.hasWarning || inc.has_warning || rawWarnings.length > 0;
        if (hasWarning) {
          const banner = document.createElement('div');
          banner.className = 'dispatch-banner-warning';

          const bannerTitle = document.createElement('div');
          bannerTitle.textContent = '⚠ Warning: Needs Attention';
          bannerTitle.className = 'dispatch-banner-title';
          banner.appendChild(bannerTitle);

          if (rawWarnings.length > 0) {
            const wList = document.createElement('ul');
            wList.className = 'dispatch-banner-list';
            rawWarnings.forEach(w => {
              const li = document.createElement('li');
              if (typeof w === 'object' && w !== null) {
                const wText = document.createElement('span');
                wText.textContent = w.message || w.text || 'Warning';
                li.appendChild(wText);
                if (w.details) {
                  const wDetail = document.createElement('div');
                  wDetail.textContent = String(w.details);
                  li.appendChild(wDetail);
                }
              } else {
                li.textContent = String(w);
              }
              wList.appendChild(li);
            });
            banner.appendChild(wList);
          }
          card.appendChild(banner);
        }

        // Assigned Units Section
        const assignHeader = document.createElement('h5');
        assignHeader.textContent = 'Assigned Units:';
        assignHeader.className = 'dispatch-section-header';
        card.appendChild(assignHeader);

        const assignments = Array.isArray(inc.assignments) ? inc.assignments : (Array.isArray(inc.units) ? inc.units : []);
        if (assignments.length === 0) {
          const noAssign = document.createElement('p');
          noAssign.textContent = 'No unit assignments on record.';
          noAssign.className = 'dispatch-empty-msg';
          card.appendChild(noAssign);
        } else {
          const tableWrapper = document.createElement('div');
          tableWrapper.className = 'dispatch-table-responsive';

          const table = document.createElement('table');
          table.className = 'dispatch-table';

          const thead = document.createElement('thead');
          const trh = document.createElement('tr');

          const colHeaders = ['Unit', 'Kind', 'Role', 'Attempt', 'Alert State', 'Distance'];
          colHeaders.forEach(h => {
            const th = document.createElement('th');
            th.textContent = h;
            th.className = 'dispatch-th';
            trh.appendChild(th);
          });
          thead.appendChild(trh);
          table.appendChild(thead);

          const tbody = document.createElement('tbody');
          assignments.forEach(a => {
            const tr = document.createElement('tr');

            // Unit username/name
            const tdUnit = document.createElement('td');
            tdUnit.className = 'dispatch-td';
            const uname = a.username || (a.unit && a.unit.username) || '';
            const name = a.name || (a.unit && a.unit.name) || '';
            tdUnit.textContent = name ? (name + ' (' + uname + ')') : (uname || 'N/A');
            tr.appendChild(tdUnit);

            // Kind
            const tdKind = document.createElement('td');
            tdKind.className = 'dispatch-td';
            tdKind.textContent = a.kind || (a.unit && a.unit.kind) || a.type || 'N/A';
            tr.appendChild(tdKind);

            // Role
            const tdRole = document.createElement('td');
            tdRole.className = 'dispatch-td';
            tdRole.textContent = a.role || 'N/A';
            tr.appendChild(tdRole);

            // Attempt number
            const tdAttempt = document.createElement('td');
            tdAttempt.className = 'dispatch-td';
            const attemptVal = a.attempt !== undefined ? a.attempt : (a.attemptNumber !== undefined ? a.attemptNumber : (a.attempts !== undefined ? a.attempts : 1));
            tdAttempt.textContent = String(attemptVal);
            tr.appendChild(tdAttempt);

            // Alert state
            const tdState = document.createElement('td');
            tdState.className = 'dispatch-td';
            const alertState = a.alertState || a.state || a.status || 'Pending';
            const spState = document.createElement('span');
            spState.textContent = alertState;
            const cleanState = String(alertState).toLowerCase().replace(/[\s_]+/g, '');
            spState.className = 'status-chip status-chip-' + cleanState;
            tdState.appendChild(spState);
            tr.appendChild(tdState);

            // Distance in km
            const tdDist = document.createElement('td');
            tdDist.className = 'dispatch-td';
            const distM = a.distanceMeters !== undefined ? a.distanceMeters : (a.distance !== undefined ? a.distance : (a.unit && a.unit.distanceMeters));
            const distK = a.distanceKm !== undefined ? a.distanceKm : (a.unit && a.unit.distanceKm);
            tdDist.textContent = formatDistance(distM, distK) || 'N/A';
            tr.appendChild(tdDist);

            tbody.appendChild(tr);
          });
          table.appendChild(tbody);
          tableWrapper.appendChild(table);
          card.appendChild(tableWrapper);
        }

        // Event Log Section
        const logHeader = document.createElement('h5');
        logHeader.textContent = 'Recent Events:';
        logHeader.className = 'dispatch-section-header';
        card.appendChild(logHeader);

        const logs = Array.isArray(inc.logs) ? inc.logs : (Array.isArray(inc.eventLogs) ? inc.eventLogs : []);
        if (logs.length === 0) {
          const noLogs = document.createElement('p');
          noLogs.textContent = 'No event logs recorded yet.';
          noLogs.className = 'dispatch-empty-msg';
          card.appendChild(noLogs);
        } else {
          const logBox = document.createElement('div');
          logBox.className = 'dispatch-log-box';

          logs.slice(-10).reverse().forEach(entry => {
            const entryRow = document.createElement('div');
            entryRow.className = 'dispatch-log-entry';

            const tsSpan = document.createElement('span');
            const tsVal = entry.timestamp || entry.time || '';
            tsSpan.textContent = tsVal ? ('[' + new Date(typeof tsVal === 'number' && tsVal < 10000000000 ? tsVal * 1000 : tsVal).toLocaleTimeString() + ']') : '';
            tsSpan.className = 'dispatch-log-ts';
            entryRow.appendChild(tsSpan);

            const textSpan = document.createElement('span');
            textSpan.textContent = entry.text || entry.message || (typeof entry === 'string' ? entry : JSON.stringify(entry));
            textSpan.className = 'dispatch-log-text';
            entryRow.appendChild(textSpan);

            logBox.appendChild(entryRow);
          });
          card.appendChild(logBox);
        }

        contentEl.appendChild(card);
      });
    } catch (e) {
      console.error('Error fetching live dispatches:', e);
    }
  }

  function startPolling() {
    stopPolling();
    fetchAndRenderLiveIncidents();
    pollTimer = setInterval(fetchAndRenderLiveIncidents, 2000);
    isPolling = true;
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
    isPolling = false;
  }

  // ==========================================
  // 8. Public Lifecycle API: init, stop, open
  // ==========================================

  function init(mountEl) {
    if (!mountEl) return;
    containerEl = mountEl;
    containerEl.replaceChildren();

    const root = document.createElement('div');
    root.className = 'dispatch-root';

    // Proposal Panel container (hidden until open(incidentId) is called)
    proposalPanelEl = document.createElement('div');
    proposalPanelEl.id = 'dispatch-proposal-panel';
    proposalPanelEl.className = 'dispatch-proposal-panel';
    root.appendChild(proposalPanelEl);

    // Live Section container
    liveSectionEl = document.createElement('div');
    liveSectionEl.id = 'dispatch-live-section';

    const liveHeader = document.createElement('div');
    liveHeader.className = 'dispatch-live-header';

    const liveTitle = document.createElement('h3');
    liveTitle.textContent = 'Active Dispatches (Live Updates Every 2s)';
    liveTitle.className = 'dispatch-live-title';
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

    proposalPanelEl.classList.add('dispatch-proposal-panel-open');
    proposalPanelEl.replaceChildren();

    const loadingDiv = document.createElement('div');
    loadingDiv.className = 'dispatch-loading';
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

  // Export to window
  if (typeof window !== 'undefined') {
    window.RapidAidDispatch = {
      init: init,
      stop: stop,
      open: open
    };
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
      init: init,
      stop: stop,
      open: open
    };
  }

})();
