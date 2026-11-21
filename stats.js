/**
 * RapidAid Dispatch Statistics & Analytics Module (stats.js)
 * Implements window.RapidAidStats = { mount(rootEl), refresh(), stop() } per CONTRACT E.
 *
 * Responsibilities:
 * - Calls bridge action get_stats
 * - Summary cards:
 *     - total incidents
 *     - completed incidents
 *     - avg severity
 *     - accept rate
 *     - avg ack seconds
 *     - avg minutes to dispatch
 *     - avg minutes to complete
 *     ("n/a" for null / missing)
 * - CSS bar charts for byStatus and byType (no external libraries)
 * - Table of the 20 recent incidents
 * - Refresh button. Auto-refresh every 10 s while mounted. stop() clears the timer.
 * - All dynamic text inserted via textContent / DOM nodes (NO innerHTML).
 * - Styled exclusively using modules.css (prefixed stats-) and style.css CSS variables.
 */

(function () {
  'use strict';

  let mountElement = null;
  let pollTimer = null;
  let isFetching = false;
  let dom = {};

  // ==========================================
  // Session Expiry & Bridge Communication Helpers
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
    window.addEventListener('rapidaid:session-expired', function () {
      stop();
    });
  }

  // ==========================================
  // Formatting Helpers
  // ==========================================

  function formatValue(val, suffix = '', decimals = -1) {
    if (val === null || val === undefined || val === '') return 'n/a';
    const num = Number(val);
    if (isNaN(num)) return String(val);
    if (decimals >= 0) {
      return num.toFixed(decimals) + suffix;
    }
    return String(num) + suffix;
  }

  function formatRate(val) {
    if (val === null || val === undefined || val === '') return 'n/a';
    const num = Number(val);
    if (isNaN(num)) return 'n/a';
    if (num <= 1 && num >= 0) {
      return (num * 100).toFixed(1) + '%';
    }
    return num.toFixed(1) + '%';
  }

  // ==========================================
  // DOM Construction
  // ==========================================

  function buildStatsUI(rootEl) {
    rootEl.textContent = '';
    dom = {};

    const root = document.createElement('div');
    root.id = 'stats-root-container';
    root.className = 'stats-root';

    // 1. Header
    const header = document.createElement('div');
    header.className = 'stats-header';

    const title = document.createElement('h2');
    title.className = 'stats-title';
    title.textContent = 'Dispatch Statistics & Performance';
    header.appendChild(title);

    const actions = document.createElement('div');
    actions.className = 'stats-header-actions';

    const lastUpdated = document.createElement('span');
    lastUpdated.className = 'stats-last-updated';
    lastUpdated.textContent = 'Initializing...';
    actions.appendChild(lastUpdated);
    dom.lastUpdated = lastUpdated;

    const btnRefresh = document.createElement('button');
    btnRefresh.type = 'button';
    btnRefresh.className = 'stats-btn-refresh';
    btnRefresh.textContent = 'Refresh';
    btnRefresh.addEventListener('click', () => refresh());
    actions.appendChild(btnRefresh);
    dom.btnRefresh = btnRefresh;

    header.appendChild(actions);
    root.appendChild(header);

    // Error banner (hidden by default)
    const errorBox = document.createElement('div');
    errorBox.className = 'stats-error';
    root.appendChild(errorBox);
    dom.errorBox = errorBox;

    // 2. Summary Cards Grid
    const cardsGrid = document.createElement('div');
    cardsGrid.className = 'stats-cards-grid';

    const cardDefinitions = [
      { key: 'total', label: 'Total Incidents' },
      { key: 'completed', label: 'Completed' },
      { key: 'avgSeverity', label: 'Avg Severity' },
      { key: 'acceptRate', label: 'Accept Rate' },
      { key: 'avgAckSeconds', label: 'Avg Ack Seconds' },
      { key: 'avgDispatchMin', label: 'Avg Min to Dispatch' },
      { key: 'avgCompleteMin', label: 'Avg Min to Complete' }
    ];

    dom.cards = {};
    cardDefinitions.forEach(def => {
      const card = document.createElement('div');
      card.className = 'stats-card';

      const label = document.createElement('span');
      label.className = 'stats-card-label';
      label.textContent = def.label;
      card.appendChild(label);

      const val = document.createElement('span');
      val.className = 'stats-card-value';
      val.textContent = 'n/a';
      card.appendChild(val);
      dom.cards[def.key] = val;

      cardsGrid.appendChild(card);
    });
    root.appendChild(cardsGrid);

    // 3. Charts Grid (byStatus & byType)
    const chartsGrid = document.createElement('div');
    chartsGrid.className = 'stats-charts-grid';

    // Chart: By Status
    const statusChartCard = document.createElement('div');
    statusChartCard.className = 'stats-chart-card';
    const statusChartTitle = document.createElement('h3');
    statusChartTitle.className = 'stats-chart-title';
    statusChartTitle.textContent = 'Incidents by Status';
    statusChartCard.appendChild(statusChartTitle);

    const statusChartBody = document.createElement('div');
    statusChartBody.className = 'stats-chart-body';
    statusChartCard.appendChild(statusChartBody);
    dom.statusChartBody = statusChartBody;
    chartsGrid.appendChild(statusChartCard);

    // Chart: By Type
    const typeChartCard = document.createElement('div');
    typeChartCard.className = 'stats-chart-card';
    const typeChartTitle = document.createElement('h3');
    typeChartTitle.className = 'stats-chart-title';
    typeChartTitle.textContent = 'Incidents by Emergency Type';
    typeChartCard.appendChild(typeChartTitle);

    const typeChartBody = document.createElement('div');
    typeChartBody.className = 'stats-chart-body';
    typeChartCard.appendChild(typeChartBody);
    dom.typeChartBody = typeChartBody;
    chartsGrid.appendChild(typeChartCard);

    root.appendChild(chartsGrid);

    // 4. Recent Incidents Table Section
    const recentSection = document.createElement('section');
    recentSection.className = 'stats-recent-section';

    const recentTitle = document.createElement('h3');
    recentTitle.className = 'stats-recent-title';
    recentTitle.textContent = 'Recent Incidents (Last 20)';
    recentSection.appendChild(recentTitle);

    const tableWrapper = document.createElement('div');
    tableWrapper.className = 'stats-table-responsive';

    const table = document.createElement('table');
    table.className = 'stats-table';

    const thead = document.createElement('thead');
    const headerRow = document.createElement('tr');
    ['ID', 'Type', 'Severity', 'People', 'Location', 'Status', 'Time'].forEach(h => {
      const th = document.createElement('th');
      th.className = 'stats-th';
      th.textContent = h;
      headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    tbody.className = 'stats-tbody';
    table.appendChild(tbody);
    dom.recentTbody = tbody;

    tableWrapper.appendChild(table);
    recentSection.appendChild(tableWrapper);
    root.appendChild(recentSection);

    rootEl.appendChild(root);
  }

  // ==========================================
  // Render Bar Charts
  // ==========================================

  function renderBarChart(container, dataMap, isStatus = false) {
    container.textContent = '';
    const entries = Object.entries(dataMap || {});
    if (entries.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'stats-empty';
      empty.textContent = 'No data available';
      container.appendChild(empty);
      return;
    }

    let maxVal = 1;
    entries.forEach(([, val]) => {
      const num = Number(val) || 0;
      if (num > maxVal) maxVal = num;
    });

    entries.forEach(([key, val]) => {
      const count = Number(val) || 0;
      const pct = Math.max(0, Math.min(100, Math.round((count / maxVal) * 100)));

      const row = document.createElement('div');
      row.className = 'stats-bar-row';

      // Label
      const label = document.createElement('span');
      label.className = 'stats-bar-label';
      if (isStatus) {
        const chip = document.createElement('span');
        chip.textContent = key;
        const cleanStatus = String(key).toLowerCase().replace(/[\s_]+/g, '');
        chip.className = 'status-chip status-chip-' + cleanStatus;
        label.appendChild(chip);
      } else {
        label.textContent = key;
      }
      row.appendChild(label);

      // Bar Track with SVG rect
      const track = document.createElement('div');
      track.className = 'stats-bar-track';

      const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      svg.setAttribute('class', 'stats-bar-svg');
      svg.setAttribute('viewBox', '0 0 100 14');
      svg.setAttribute('preserveAspectRatio', 'none');

      const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
      rect.setAttribute('class', 'stats-bar-rect');
      rect.setAttribute('x', '0');
      rect.setAttribute('y', '0');
      rect.setAttribute('width', String(pct));
      rect.setAttribute('height', '14');
      rect.setAttribute('rx', '3');
      svg.appendChild(rect);
      track.appendChild(svg);
      row.appendChild(track);

      // Value
      const valSpan = document.createElement('span');
      valSpan.className = 'stats-bar-value';
      valSpan.textContent = String(count);
      row.appendChild(valSpan);

      container.appendChild(row);
    });
  }

  // ==========================================
  // Render Recent Incidents Table
  // ==========================================

  function renderRecentTable(incidents) {
    if (!dom.recentTbody) return;
    dom.recentTbody.textContent = '';

    const list = Array.isArray(incidents) ? incidents.slice(0, 20) : [];
    if (list.length === 0) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.setAttribute('colspan', '7');
      td.className = 'stats-empty';
      td.textContent = 'No recent incidents recorded.';
      tr.appendChild(td);
      dom.recentTbody.appendChild(tr);
      return;
    }

    list.forEach(inc => {
      const tr = document.createElement('tr');

      // ID
      const tdId = document.createElement('td');
      tdId.className = 'stats-td';
      tdId.textContent = '#' + (inc.id !== undefined ? inc.id : 'N/A');
      tr.appendChild(tdId);

      // Type
      const tdType = document.createElement('td');
      tdType.className = 'stats-td';
      tdType.textContent = inc.type || 'Incident';
      tr.appendChild(tdType);

      // Severity
      const tdSev = document.createElement('td');
      tdSev.className = 'stats-td';
      tdSev.textContent = String(inc.severity !== undefined ? inc.severity : 'N/A');
      tr.appendChild(tdSev);

      // People
      const tdPeople = document.createElement('td');
      tdPeople.className = 'stats-td';
      const pCount = inc.peopleAffected !== undefined ? inc.peopleAffected :
                     (inc.people_affected !== undefined ? inc.people_affected : 0);
      tdPeople.textContent = String(pCount);
      tr.appendChild(tdPeople);

      // Location
      const tdLoc = document.createElement('td');
      tdLoc.className = 'stats-td';
      const locText = inc.locationText || inc.location_text || (inc.location && inc.location.text) || inc.location || 'Unknown';
      tdLoc.textContent = typeof locText === 'string' ? locText : 'Unknown';
      tr.appendChild(tdLoc);

      // Status
      const tdStatus = document.createElement('td');
      tdStatus.className = 'stats-td';
      const statusVal = inc.status || 'New';
      const chip = document.createElement('span');
      chip.textContent = statusVal;
      const cleanStat = String(statusVal).toLowerCase().replace(/[\s_]+/g, '');
      chip.className = 'status-chip status-chip-' + cleanStat;
      tdStatus.appendChild(chip);
      tr.appendChild(tdStatus);

      // Time / Created
      const tdTime = document.createElement('td');
      tdTime.className = 'stats-td';
      const tVal = inc.createdAt || inc.created_at || inc.createdTime || inc.created_time || inc.timestamp;
      if (tVal) {
        const d = new Date(typeof tVal === 'number' && tVal < 10000000000 ? tVal * 1000 : tVal);
        tdTime.textContent = isNaN(d.getTime()) ? String(tVal) : d.toLocaleTimeString();
      } else {
        tdTime.textContent = '-';
      }
      tr.appendChild(tdTime);

      dom.recentTbody.appendChild(tr);
    });
  }

  // ==========================================
  // Public Lifecycle API: mount, refresh, stop
  // ==========================================

  function mount(rootEl) {
    if (typeof rootEl === 'string') {
      rootEl = document.querySelector(rootEl);
    }
    if (!rootEl) {
      rootEl = document.getElementById('stats-root') || document.getElementById('section-stats');
    }
    if (!rootEl) return;

    stop();
    mountElement = rootEl;
    buildStatsUI(rootEl);
    refresh();

    // Auto-refresh every 10 s while mounted
    pollTimer = setInterval(() => {
      refresh();
    }, 10000);
  }

  async function refresh() {
    if (isFetching) return;
    isFetching = true;

    if (dom.btnRefresh) {
      dom.btnRefresh.disabled = true;
    }

    try {
      const res = await callBridge('get_stats');

      if (dom.errorBox) {
        dom.errorBox.textContent = '';
        dom.errorBox.classList.remove('stats-error-visible');
      }

      if (!res || !res.ok) {
        if (dom.errorBox) {
          dom.errorBox.textContent = (res && res.error) ? res.error : 'Failed to retrieve dispatch statistics.';
          dom.errorBox.classList.add('stats-error-visible');
        }
        return;
      }

      const data = res.stats || res.data || res;

      // 1. Populate Summary Cards
      if (dom.cards) {
        // Total Incidents
        const total = data.totalIncidents !== undefined ? data.totalIncidents : (data.total !== undefined ? data.total : null);
        dom.cards.total.textContent = formatValue(total);

        // Completed
        const completed = data.completedIncidents !== undefined ? data.completedIncidents : (data.completed !== undefined ? data.completed : null);
        dom.cards.completed.textContent = formatValue(completed);

        // Avg Severity
        const avgSev = data.avgSeverity !== undefined ? data.avgSeverity : (data.averageSeverity !== undefined ? data.averageSeverity : null);
        dom.cards.avgSeverity.textContent = formatValue(avgSev, '', 1);

        // Accept Rate
        const acceptRate = data.acceptRate !== undefined ? data.acceptRate : (data.acceptanceRate !== undefined ? data.acceptanceRate : null);
        dom.cards.acceptRate.textContent = formatRate(acceptRate);

        // Avg Ack Seconds
        const avgAck = data.avgAckSeconds !== undefined ? data.avgAckSeconds : (data.averageAckSeconds !== undefined ? data.averageAckSeconds : null);
        dom.cards.avgAckSeconds.textContent = formatValue(avgAck, ' s', 1);

        // Avg Minutes to Dispatch
        const avgDisp = data.avgMinutesToDispatch !== undefined ? data.avgMinutesToDispatch : (data.avgDispatchMinutes !== undefined ? data.avgDispatchMinutes : null);
        dom.cards.avgDispatchMin.textContent = formatValue(avgDisp, ' min', 1);

        // Avg Minutes to Complete
        const avgComp = data.avgMinutesToComplete !== undefined ? data.avgMinutesToComplete : (data.avgCompletionMinutes !== undefined ? data.avgCompletionMinutes : null);
        dom.cards.avgCompleteMin.textContent = formatValue(avgComp, ' min', 1);
      }

      // 2. Bar Charts: byStatus & byType
      let byStatus = data.byStatus || data.statusCounts || null;
      let byType = data.byType || data.typeCounts || null;
      let recentIncidents = data.recentIncidents || data.recent || data.incidents || null;

      // If charts or incidents are not embedded in get_stats, fetch via list_incidents to populate
      if (!byStatus || !byType || !recentIncidents) {
        try {
          const incRes = await callBridge('list_incidents');
          if (incRes && incRes.ok && Array.isArray(incRes.incidents)) {
            const incs = incRes.incidents;
            if (!recentIncidents) recentIncidents = incs;
            if (!byStatus) {
              byStatus = {};
              incs.forEach(i => {
                const s = i.status || 'New';
                byStatus[s] = (byStatus[s] || 0) + 1;
              });
            }
            if (!byType) {
              byType = {};
              incs.forEach(i => {
                const t = i.type || 'Other';
                byType[t] = (byType[t] || 0) + 1;
              });
            }
          }
        } catch (e) {}
      }

      if (dom.statusChartBody) {
        renderBarChart(dom.statusChartBody, byStatus || {}, true);
      }
      if (dom.typeChartBody) {
        renderBarChart(dom.typeChartBody, byType || {}, false);
      }

      // 3. Recent Incidents Table
      renderRecentTable(recentIncidents || []);

      // 4. Update Timestamp
      if (dom.lastUpdated) {
        dom.lastUpdated.textContent = 'Updated: ' + (new Date()).toLocaleTimeString();
      }
    } catch (err) {
      if (dom.errorBox) {
        dom.errorBox.textContent = 'Error loading stats: ' + (err.message || 'Server error');
        dom.errorBox.classList.add('stats-error-visible');
      }
    } finally {
      isFetching = false;
      if (dom.btnRefresh) {
        dom.btnRefresh.disabled = false;
      }
    }
  }

  function stop() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
    isFetching = false;
  }

  // Export to window
  if (typeof window !== 'undefined') {
    window.RapidAidStats = {
      mount: mount,
      refresh: refresh,
      stop: stop,
      init: mount // alias for compatibility
    };
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
      mount: mount,
      refresh: refresh,
      stop: stop,
      init: mount
    };
  }
})();
