/**
 * RapidAid Frontend Dispatcher Application (app.js)
 * Features:
 * - Real backend authentication & session persistence (sessionStorage)
 * - Role-based UI routing (Dispatcher / Admin vs Service Unit)
 * - Bridge-based OSM Geocoding & Area Pipeline
 * - Shared Leaflet Map Engine for Area & Incident Board
 * - Modular Loader for RapidAidBoard, RapidAidDispatch, RapidAidStats, RapidAidAdmin
 * - Dispatcher Developer Test Alert panel & 3s live alert polling
 * Strict DOM nodes and textContent usage for all dynamic text to prevent XSS.
 */

const state = {
  user: null,
  role: null,
  activeSection: 'incidents',
  hasGeoJson: false,
  mustChangePassword: false,
  sessionData: null,
  previousSection: null,
  centerLat: 48.8583,
  centerLon: 2.2945,
  centerDisplayName: 'Eiffel Tower, Paris',
  lastGeocodedName: '',
  radiusMeters: 3000,
  cartoApiKey: '',
  areaTileLayer: null,
  boardTileLayer: null,
  leafletMap: null, // Area map (#leaflet-map)
  boardMap: null,   // Shared Incident Board map (#board-map)
  centerMarker: null,
  radiusCircle: null,
  areaLayers: { roadsLayer: null, placesLayer: null },
  boardLayers: { roadsLayer: null, placesLayer: null },
  roadsGeoJson: null,
  placesGeoJson: null
};

let dispatcherAlertsTimer = null;
let boardRunning = false;
let dispatchRunning = false;
let statsRunning = false;
let adminRunning = false;

// ==========================================
// API Bridge Helper & Session Handling
// ==========================================
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
    const data = await res.json();

    // Session expiry detection for authenticated requests
    if (token && data && !data.ok && action !== 'login' && action !== 'signup' && action !== 'health') {
      const errCode = (data.code || data.error || '').toString().toLowerCase();
      if (errCode === 'not_logged_in' || errCode === 'session_expired' || errCode === 'not logged in') {
        handleSessionExpired();
      }
    }

    return data;
  } catch (err) {
    return { ok: false, error: 'Network error communicating with server: ' + (err.message || err) };
  }
}

function handleSessionExpired() {
  sessionStorage.removeItem('rapidaid_token');
  state.user = null;
  state.role = null;
  state.mustChangePassword = false;
  state.sessionData = null;
  clearAuthForms();
  stopAllModules();
  showAuthView('Your session expired. Please log in again.', 'error');
}

// Window event listener for CONTRACT E
window.addEventListener('rapidaid:session-expired', () => {
  handleSessionExpired();
});

// ==========================================
// Public RapidAid Bridge & Module Contracts
// ==========================================
window.RapidAid = {
  callBridge: async function(action, payload = {}) {
    return await callBridge(action, payload);
  },
  getBoardMap: function() {
    return ensureBoardMap();
  },
  reloadLayers: async function() {
    const roadsRes = await callBridge('get_geojson', { type: 'roads' });
    const placesRes = await callBridge('get_geojson', { type: 'places' });
    let roads = null;
    let places = null;
    if (roadsRes && roadsRes.ok && roadsRes.geojson) {
      try {
        roads = typeof roadsRes.geojson === 'string' ? JSON.parse(roadsRes.geojson) : roadsRes.geojson;
      } catch (e) {
        console.error('Failed to parse roads geojson', e);
      }
    }
    if (placesRes && placesRes.ok && placesRes.geojson) {
      try {
        places = typeof placesRes.geojson === 'string' ? JSON.parse(placesRes.geojson) : placesRes.geojson;
      } catch (e) {
        console.error('Failed to parse places geojson', e);
      }
    }
    state.roadsGeoJson = roads;
    state.placesGeoJson = places;

    const bMap = ensureBoardMap();
    if (bMap && (roads || places)) {
      renderGeoJsonOnMap(bMap, roads, places, state.boardLayers);
    }
    if (state.leafletMap && (roads || places)) {
      renderGeoJsonOnMap(state.leafletMap, roads, places, state.areaLayers);
    }
    return { ok: true, roads, places };
  },
  getUsername: function() {
    return state.user || '';
  },
  getRole: function() {
    return state.role || '';
  }
};

// Global hook for unit.js
window.RapidAidApp = {
  showAuthView,
  clearAuthForms
};

// ==========================================
// Modular Board & Dispatch Lifecycle
// ==========================================
function initActiveBoardModules() {
  const boardMount = document.getElementById('board-mount');
  if (boardMount) {
    if (window.RapidAidBoard && typeof window.RapidAidBoard.init === 'function') {
      if (!boardRunning) {
        boardMount.textContent = '';
        try {
          window.RapidAidBoard.init(boardMount);
          boardRunning = true;
        } catch (err) {
          console.error('Error initializing RapidAidBoard:', err);
        }
      }
    } else {
      boardMount.textContent = 'Incident board module not installed';
      boardRunning = false;
    }
  }

  const dispatchMount = document.getElementById('dispatch-mount');
  if (dispatchMount) {
    if (window.RapidAidDispatch && typeof window.RapidAidDispatch.init === 'function') {
      if (!dispatchRunning) {
        dispatchMount.textContent = '';
        try {
          window.RapidAidDispatch.init(dispatchMount);
          dispatchRunning = true;
        } catch (err) {
          console.error('Error initializing RapidAidDispatch:', err);
        }
      }
    } else {
      dispatchMount.textContent = 'Dispatch panel not installed';
      dispatchRunning = false;
    }
  }
}

function startBoardModules() {
  const bMap = window.RapidAid.getBoardMap();
  if (bMap) {
    setTimeout(() => bMap.invalidateSize(), 150);
  }
  initActiveBoardModules();
}

function stopBoardModules() {
  if (boardRunning) {
    if (window.RapidAidBoard && typeof window.RapidAidBoard.stop === 'function') {
      try {
        window.RapidAidBoard.stop();
      } catch (err) {
        console.error('Error stopping RapidAidBoard:', err);
      }
    }
    boardRunning = false;
  }
  if (dispatchRunning) {
    if (window.RapidAidDispatch && typeof window.RapidAidDispatch.stop === 'function') {
      try {
        window.RapidAidDispatch.stop();
      } catch (err) {
        console.error('Error stopping RapidAidDispatch:', err);
      }
    }
    dispatchRunning = false;
  }
}

// Stats & Admin module mounting
function startStatsModule() {
  const root = document.getElementById('stats-root');
  if (!root) return;
  const mod = window.RapidAidStats || window.Stats;
  if (mod && !statsRunning) {
    try {
      if (typeof mod.mount === 'function') {
        mod.mount(root);
        statsRunning = true;
      } else if (typeof mod.init === 'function') {
        mod.init(root);
        statsRunning = true;
      }
    } catch (err) {
      console.error('Error mounting Stats module:', err);
    }
  }
}

function stopStatsModule() {
  if (statsRunning) {
    const mod = window.RapidAidStats || window.Stats;
    if (mod && typeof mod.stop === 'function') {
      try {
        mod.stop();
      } catch (err) {
        console.error('Error stopping Stats module:', err);
      }
    }
    statsRunning = false;
  }
}

function startAdminModule() {
  const root = document.getElementById('admin-root');
  if (!root) return;
  const mod = window.RapidAidAdmin || window.Admin;
  if (mod && !adminRunning) {
    try {
      if (typeof mod.mount === 'function') {
        mod.mount(root);
        adminRunning = true;
      } else if (typeof mod.init === 'function') {
        mod.init(root);
        adminRunning = true;
      }
    } catch (err) {
      console.error('Error mounting Admin module:', err);
    }
  }
}

function stopAdminModule() {
  if (adminRunning) {
    const mod = window.RapidAidAdmin || window.Admin;
    if (mod && typeof mod.stop === 'function') {
      try {
        mod.stop();
      } catch (err) {
        console.error('Error stopping Admin module:', err);
      }
    }
    adminRunning = false;
  }
}

function stopAllModules() {
  stopBoardModules();
  stopAlertsPolling();
  stopStatsModule();
  stopAdminModule();
  if (window.RapidAidUnit && typeof window.RapidAidUnit.stop === 'function') {
    try {
      window.RapidAidUnit.stop();
    } catch (e) {
      console.error('Error stopping unit module:', e);
    }
  }
}

// ==========================================
// 1. Initialization
// ==========================================
document.addEventListener('DOMContentLoaded', async () => {
  initThemeToggle();
  initProfileMenu();
  initAuthEvents();
  initNavigationEvents();
  initAreaEvents();
  initApiKeyEvents();
  initDispatcherAlertTest();
  checkApiKey();
  updateStatusBanner('Ready');

  // Session check on page load
  const token = getToken();
  if (token) {
    const res = await callBridge('session');
    if (res && res.ok) {
      state.user = res.username;
      state.role = res.role;
      state.sessionData = res;
      if (res.mustChangePassword) {
        showChangePasswordView(true);
      } else {
        routeByRole(res.role, res);
      }
    } else {
      sessionStorage.removeItem('rapidaid_token');
      showAuthView();
    }
  } else {
    showAuthView();
  }
});

function updateStatusBanner(text) {
  const badge = document.getElementById('backend-status');
  if (badge) {
    badge.textContent = 'System: ' + text;
  }
}

// ==========================================
// Theme Management
// ==========================================
function initThemeToggle() {
  const toggle = document.getElementById('theme-toggle');
  const label = document.getElementById('theme-toggle-label');
  const icon = toggle ? toggle.querySelector('.theme-toggle-icon') : null;

  function updateThemeUI() {
    const isNight = document.documentElement.dataset.theme === 'night';
    if (label) label.textContent = isNight ? 'Night' : 'Day';
    if (icon) icon.textContent = isNight ? '🌙' : '☀';
  }

  updateThemeUI();

  if (toggle) {
    toggle.addEventListener('click', () => {
      const isNight = document.documentElement.dataset.theme === 'night';
      if (isNight) {
        delete document.documentElement.dataset.theme;
        localStorage.setItem('rapidaid_theme', 'day');
      } else {
        document.documentElement.dataset.theme = 'night';
        localStorage.setItem('rapidaid_theme', 'night');
      }
      updateThemeUI();
    });
  }
}

// ==========================================
// Profile Menu Management
// ==========================================
function getInitials(name) {
  if (!name) return 'RA';
  const parts = name.trim().split(/[\s_\-]+/).filter(Boolean);
  if (parts.length >= 2) {
    return (parts[0][0] + parts[1][0]).toUpperCase();
  }
  return name.slice(0, 2).toUpperCase();
}

function updateProfileUI() {
  const username = state.user || 'Guest';
  const role = state.role || 'Guest';
  const initials = getInitials(username);

  const avatarInitials = document.getElementById('profile-avatar-initials');
  if (avatarInitials) avatarInitials.textContent = initials;

  const profileName = document.getElementById('profile-name');
  if (profileName) profileName.textContent = username;

  const menuAvatar = document.getElementById('profile-menu-avatar');
  if (menuAvatar) menuAvatar.textContent = initials;

  const userDisplay = document.getElementById('user-display');
  if (userDisplay) userDisplay.textContent = username;

  const roleDisplay = document.getElementById('role-display');
  if (roleDisplay) roleDisplay.textContent = role;
}

function initProfileMenu() {
  const toggle = document.getElementById('profile-toggle');
  const menu = document.getElementById('profile-menu');
  const btnLogout = document.getElementById('profile-logout');
  const btnChangePw = document.getElementById('profile-change-password');

  if (toggle && menu) {
    toggle.addEventListener('click', (e) => {
      e.stopPropagation();
      const isHidden = menu.hidden;
      menu.hidden = !isHidden;
      toggle.setAttribute('aria-expanded', isHidden ? 'true' : 'false');
    });

    document.addEventListener('click', (e) => {
      if (!menu.hidden && !e.target.closest('.profile-wrap')) {
        menu.hidden = true;
        toggle.setAttribute('aria-expanded', 'false');
      }
    });

    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && !menu.hidden) {
        menu.hidden = true;
        toggle.setAttribute('aria-expanded', 'false');
        toggle.focus();
      }
    });
  }

  if (btnLogout) {
    btnLogout.addEventListener('click', () => performLogout());
  }

  if (btnChangePw) {
    btnChangePw.addEventListener('click', () => {
      if (menu) {
        menu.hidden = true;
        if (toggle) toggle.setAttribute('aria-expanded', 'false');
      }
      showChangePasswordView(false);
    });
  }
}

async function performLogout() {
  stopAllModules();
  await callBridge('logout');
  sessionStorage.removeItem('rapidaid_token');
  state.user = null;
  state.role = null;
  state.mustChangePassword = false;
  state.sessionData = null;
  clearAuthForms();
  showAuthView('Logged out successfully.', 'success');
}

// ==========================================
// 2. CARTO API Key Management
// ==========================================
async function checkApiKey() {
  let key = localStorage.getItem('rapidaid_carto_api_key') || '';

  if (!key) {
    try {
      const res = await fetch('config.json');
      if (res.ok) {
        const cfg = await res.json();
        const cand = (cfg && (cfg.key || cfg.cartoApiKey)) ? (cfg.key || cfg.cartoApiKey).trim() : '';
        if (cand && cand !== 'YOUR_KEY' && cand !== 'YOUR_CARTO_API_KEY_HERE') {
          key = cand;
          localStorage.setItem('rapidaid_carto_api_key', key);
        }
      }
    } catch (e) {}
  }

  const badge = document.getElementById('apikey-badge');
  const banner = document.getElementById('apikey-banner');
  const input = document.getElementById('apikey-input');

  if (key) {
    state.cartoApiKey = key;
    if (badge) {
      badge.textContent = 'Configured';
    }
    if (banner) banner.hidden = true;
  } else {
    state.cartoApiKey = '';
    if (badge) {
      badge.textContent = 'Not Set (Click to configure)';
    }
  }

  if (input && key) {
    input.value = key;
  }
}

function initApiKeyEvents() {
  const btnToggle = document.getElementById('btn-toggle-apikey-settings');
  const banner = document.getElementById('apikey-banner');
  const btnSave = document.getElementById('btn-save-apikey');
  const btnDownload = document.getElementById('btn-download-config');
  const btnDismiss = document.getElementById('btn-dismiss-apikey');
  const input = document.getElementById('apikey-input');
  const feedback = document.getElementById('apikey-feedback');

  if (btnToggle) {
    btnToggle.addEventListener('click', () => {
      if (banner) {
        banner.hidden = !banner.hidden;
      }
    });
  }

  if (btnDismiss) {
    btnDismiss.addEventListener('click', () => {
      if (banner) banner.hidden = true;
    });
  }

  if (btnSave) {
    btnSave.addEventListener('click', () => {
      const key = (input.value || '').trim();
      if (!key) {
        setFeedback(feedback, 'Please enter a valid API key string.', 'error');
        return;
      }

      state.cartoApiKey = key;
      localStorage.setItem('rapidaid_carto_api_key', key);

      const badge = document.getElementById('apikey-badge');
      if (badge) badge.textContent = 'Configured';

      setFeedback(feedback, 'API key saved to private local storage and activated!', 'success');
      updateTileLayers();

      setTimeout(() => {
        if (banner) banner.hidden = true;
      }, 1500);
    });
  }

  if (btnDownload) {
    btnDownload.addEventListener('click', () => {
      const key = (input.value || state.cartoApiKey || '').trim();
      const configData = {
        _comment: 'Private CARTO API key configuration. This file is git-ignored.',
        cartoApiKey: key || 'YOUR_CARTO_API_KEY_HERE'
      };
      const blob = new Blob([JSON.stringify(configData, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'config.json';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      setFeedback(feedback, 'config.json downloaded! Place it in the project root folder.', 'info');
    });
  }
}

function applyTileLayer(mapInstance, tileLayerKey) {
  if (!mapInstance) return;

  if (state[tileLayerKey]) {
    mapInstance.removeLayer(state[tileLayerKey]);
    state[tileLayerKey] = null;
  }

  let tileUrl = 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png';
  let tileAttribution = '&copy; OpenStreetMap contributors';
  if (state.cartoApiKey) {
    tileUrl = 'https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png';
    tileUrl += `?key=${encodeURIComponent(state.cartoApiKey)}`;
    tileAttribution += ' &copy; CARTO';
  }

  state[tileLayerKey] = L.tileLayer(tileUrl, {
    maxZoom: 19,
    subdomains: 'abcd',
    attribution: tileAttribution
  }).addTo(mapInstance);
}

function updateTileLayers() {
  if (state.leafletMap) {
    applyTileLayer(state.leafletMap, 'areaTileLayer');
  }
  if (state.boardMap) {
    applyTileLayer(state.boardMap, 'boardTileLayer');
  }
}

// ==========================================
// 3. Leaflet Map Engine
// ==========================================
function formatAmenityName(cat) {
  if (cat === 'hospital') return 'Hospital';
  if (cat === 'fire_station') return 'Fire Station';
  if (cat === 'police') return 'Police Station';
  if (cat === 'clinic') return 'Clinic';
  if (cat === 'pharmacy') return 'Pharmacy';
  if (cat === 'shelter') return 'Emergency Shelter';
  return cat || 'Facility';
}

function renderGeoJsonOnMap(mapInstance, roadsGeoJson, placesGeoJson, layerHolder) {
  if (!mapInstance) return;

  if (layerHolder.roadsLayer) {
    mapInstance.removeLayer(layerHolder.roadsLayer);
    layerHolder.roadsLayer = null;
  }
  if (layerHolder.placesLayer) {
    mapInstance.removeLayer(layerHolder.placesLayer);
    layerHolder.placesLayer = null;
  }

  if (roadsGeoJson && roadsGeoJson.features && roadsGeoJson.features.length > 0) {
    layerHolder.roadsLayer = L.geoJSON(roadsGeoJson, {
      style: { color: '#0066cc', weight: 3.5, opacity: 0.85 }
    }).addTo(mapInstance);
  }

  const categoryColors = {
    hospital: '#e74c3c',
    clinic: '#27ae60',
    pharmacy: '#9b59b6',
    fire_station: '#e67e22',
    police: '#2980b9',
    shelter: '#16a085'
  };

  if (placesGeoJson && placesGeoJson.features && placesGeoJson.features.length > 0) {
    layerHolder.placesLayer = L.geoJSON(placesGeoJson, {
      pointToLayer: (feature, latlng) => {
        const cat = (feature.properties && feature.properties.category) ? feature.properties.category : 'hospital';
        const markerColor = categoryColors[cat] || '#28a745';
        return L.circleMarker(latlng, {
          radius: 8,
          fillColor: markerColor,
          color: '#ffffff',
          weight: 2,
          opacity: 1,
          fillOpacity: 0.95
        });
      },
      onEachFeature: (feature, layer) => {
        const name = (feature.properties && feature.properties.name) ? feature.properties.name : 'Facility';
        const cat = (feature.properties && feature.properties.category) ? feature.properties.category : 'facility';
        const container = document.createElement('div');
        const b = document.createElement('b');
        b.textContent = name;
        container.appendChild(b);
        container.appendChild(document.createElement('br'));
        const span = document.createElement('span');
        span.textContent = 'Category: ' + formatAmenityName(cat);
        container.appendChild(span);
        layer.bindPopup(container);
      }
    }).addTo(mapInstance);
  }

  try {
    const group = new L.featureGroup([layerHolder.roadsLayer, layerHolder.placesLayer].filter(Boolean));
    if (group.getLayers().length > 0) {
      mapInstance.fitBounds(group.getBounds().pad(0.1));
    }
  } catch (e) {}
}

function ensureLeafletMap() {
  const container = document.getElementById('leaflet-map');
  if (!container) return;

  if (state.leafletMap) {
    setTimeout(() => state.leafletMap.invalidateSize(), 150);
    return;
  }

  state.leafletMap = L.map('leaflet-map', { preferCanvas: true }).setView([state.centerLat, state.centerLon], 14);
  applyTileLayer(state.leafletMap, 'areaTileLayer');

  updateMapVisuals(state.centerDisplayName, state.centerLat, state.centerLon, state.radiusMeters);

  if (state.roadsGeoJson || state.placesGeoJson) {
    renderGeoJsonOnMap(state.leafletMap, state.roadsGeoJson, state.placesGeoJson, state.areaLayers);
  }

  setTimeout(() => state.leafletMap.invalidateSize(), 200);
}

function ensureBoardMap() {
  const container = document.getElementById('board-map');
  if (!container) return null;

  if (state.boardMap) {
    setTimeout(() => {
      if (state.boardMap) state.boardMap.invalidateSize();
    }, 100);
    return state.boardMap;
  }

  state.boardMap = L.map('board-map', { preferCanvas: true }).setView([state.centerLat, state.centerLon], 14);
  applyTileLayer(state.boardMap, 'boardTileLayer');

  if (state.roadsGeoJson || state.placesGeoJson) {
    renderGeoJsonOnMap(state.boardMap, state.roadsGeoJson, state.placesGeoJson, state.boardLayers);
  }

  setTimeout(() => {
    if (state.boardMap) state.boardMap.invalidateSize();
  }, 150);

  return state.boardMap;
}

function updateMapVisuals(displayName, lat, lon, radiusMeters) {
  if (!state.leafletMap) return;

  state.centerLat = lat;
  state.centerLon = lon;
  state.radiusMeters = radiusMeters;
  state.centerDisplayName = displayName;

  if (state.centerMarker) {
    state.leafletMap.removeLayer(state.centerMarker);
  }
  state.centerMarker = L.marker([lat, lon]).addTo(state.leafletMap);

  const popupDiv = document.createElement('div');
  const b = document.createElement('b');
  b.textContent = displayName;
  popupDiv.appendChild(b);
  popupDiv.appendChild(document.createElement('br'));
  const cSpan = document.createElement('span');
  cSpan.textContent = `Coordinates: ${lat.toFixed(5)}, ${lon.toFixed(5)}`;
  popupDiv.appendChild(cSpan);
  state.centerMarker.bindPopup(popupDiv).openPopup();

  if (state.radiusCircle) {
    state.leafletMap.removeLayer(state.radiusCircle);
  }
  state.radiusCircle = L.circle([lat, lon], {
    radius: radiusMeters,
    color: '#ff7800',
    weight: 2,
    dashArray: '5, 5',
    fillColor: '#ffa500',
    fillOpacity: 0.15
  }).addTo(state.leafletMap);

  state.leafletMap.flyTo([lat, lon], 14);

  const statusOverlay = document.getElementById('map-status-overlay');
  if (statusOverlay) {
    statusOverlay.textContent = `Center: (${lat.toFixed(4)}, ${lon.toFixed(4)}) | Radius: ${radiusMeters}m`;
  }
}

// ==========================================
// 4. Area Setup Events (Real Bridge Pipeline)
// ==========================================
function initAreaEvents() {
  const btnPreview = document.getElementById('btn-preview');
  const btnGenerate = document.getElementById('btn-generate');
  const feedback = document.getElementById('area-pipeline-feedback');

  if (btnPreview) {
    btnPreview.addEventListener('click', async () => {
      const name = document.getElementById('area-name').value.trim();
      const radius = parseInt(document.getElementById('area-radius').value, 10) || 3000;

      if (!name) {
        setFeedback(feedback, 'Please enter an area name to preview.', 'error');
        return;
      }

      setFeedback(feedback, 'Geocoding area coordinates & preparing zone preview...', 'info');
      ensureLeafletMap();

      const loc = await callBridge('geocode', { query: name });
      if (!loc || !loc.ok) {
        setFeedback(feedback, (loc && loc.error) ? loc.error : 'Location not found for: ' + name, 'error');
        return;
      }

      state.centerLat = loc.lat;
      state.centerLon = loc.lon;
      state.centerDisplayName = loc.displayName || name;
      state.lastGeocodedName = name;
      state.radiusMeters = radius;

      document.getElementById('preview-coords').textContent = `${loc.lat.toFixed(5)}, ${loc.lon.toFixed(5)}`;
      document.getElementById('preview-displayname').textContent = loc.displayName;

      updateMapVisuals(loc.displayName, loc.lat, loc.lon, radius);
      setFeedback(feedback, 'Location geocoded! Center marker and radius circle updated.', 'success');
    });
  }

  if (btnGenerate) {
    btnGenerate.addEventListener('click', async () => {
      const name = document.getElementById('area-name').value.trim();
      const radius = parseInt(document.getElementById('area-radius').value, 10) || 3000;

      if (!name) {
        setFeedback(feedback, 'Please specify an operational area name.', 'error');
        return;
      }

      setFeedback(feedback, 'Generating road network and emergency facilities via pipeline...', 'info');
      ensureLeafletMap();

      let lat = state.centerLat;
      let lon = state.centerLon;
      let displayName = state.centerDisplayName || name;

      if (state.lastGeocodedName !== name) {
        const loc = await callBridge('geocode', { query: name });
        if (!loc || !loc.ok) {
          setFeedback(feedback, (loc && loc.error) ? loc.error : 'Location not found for: ' + name, 'error');
          return;
        }
        lat = loc.lat;
        lon = loc.lon;
        displayName = loc.displayName || name;
        state.centerLat = lat;
        state.centerLon = lon;
        state.centerDisplayName = displayName;
        state.lastGeocodedName = name;
        document.getElementById('preview-coords').textContent = `${lat.toFixed(5)}, ${lon.toFixed(5)}`;
        document.getElementById('preview-displayname').textContent = displayName;
      }

      updateMapVisuals(displayName, lat, lon, radius);

      const pipeData = await callBridge('create_area', {
        name,
        radius,
        lat,
        lon,
        displayName
      });

      if (pipeData && pipeData.ok) {
        state.hasGeoJson = true;

        let roads = null;
        let places = null;
        try {
          roads = typeof pipeData.roadsGeoJson === 'string' ? JSON.parse(pipeData.roadsGeoJson) : pipeData.roadsGeoJson;
        } catch (e) {}
        try {
          places = typeof pipeData.placesGeoJson === 'string' ? JSON.parse(pipeData.placesGeoJson) : pipeData.placesGeoJson;
        } catch (e) {}

        state.roadsGeoJson = roads;
        state.placesGeoJson = places;

        renderGeoJsonOnMap(state.leafletMap, roads, places, state.areaLayers);
        if (state.boardMap) {
          renderGeoJsonOnMap(state.boardMap, roads, places, state.boardLayers);
        }

        const statsBox = document.getElementById('area-stats-box');
        if (statsBox) statsBox.hidden = false;
        document.getElementById('stat-nodes').textContent = pipeData.nodesCount != null ? pipeData.nodesCount : 0;
        document.getElementById('stat-edges').textContent = pipeData.edgesCount != null ? pipeData.edgesCount : 0;
        document.getElementById('stat-places').textContent = pipeData.placesCount != null ? pipeData.placesCount : 0;
        document.getElementById('stat-snapped').textContent = pipeData.snappedCount != null ? pipeData.snappedCount : 0;
        if (pipeData.roadsFile) document.getElementById('stat-roads-file').textContent = pipeData.roadsFile;
        if (pipeData.placesFile) document.getElementById('stat-places-file').textContent = pipeData.placesFile;

        setFeedback(feedback, `Success! Operational zone pipeline completed.\n${pipeData.placesCount} facilities snapped to ${pipeData.edgesCount} road edges.`, 'success');

        const areaAlert = document.getElementById('area-alert');
        if (areaAlert) areaAlert.hidden = true;
      } else {
        setFeedback(feedback, (pipeData && pipeData.error) ? pipeData.error : 'Pipeline generation failed.', 'error');
      }
    });
  }
}

// ==========================================
// 5. Authentication, Forms & Routing
// ==========================================
function clearAuthForms() {
  const ids = [
    'login-username',
    'login-password',
    'signup-username',
    'signup-password',
    'change-old-password',
    'change-new-password',
    'change-confirm-password'
  ];
  ids.forEach(id => {
    const el = document.getElementById(id);
    if (el) el.value = '';
  });
}

function initAuthEvents() {
  const tabLogin = document.getElementById('tab-login');
  const tabSignup = document.getElementById('tab-signup');
  const loginContainer = document.getElementById('login-container');
  const signupContainer = document.getElementById('signup-container');
  const changePasswordContainer = document.getElementById('change-password-container');
  const roleSelect = document.getElementById('signup-role');
  const unitFields = document.getElementById('signup-unit-fields');
  const kindSelect = document.getElementById('signup-unit-kind');
  const countContainer = document.getElementById('signup-unit-count-container');
  const countLabel = document.getElementById('signup-unit-count-label');

  tabLogin.addEventListener('click', () => {
    tabLogin.classList.add('active');
    tabSignup.classList.remove('active');
    loginContainer.hidden = false;
    signupContainer.hidden = true;
    if (changePasswordContainer) changePasswordContainer.hidden = true;
    showAuthFeedback('');
  });

  tabSignup.addEventListener('click', () => {
    tabSignup.classList.add('active');
    tabLogin.classList.remove('active');
    signupContainer.hidden = false;
    loginContainer.hidden = true;
    if (changePasswordContainer) changePasswordContainer.hidden = true;
    showAuthFeedback('');
  });

  if (roleSelect && unitFields) {
    roleSelect.addEventListener('change', () => {
      unitFields.hidden = (roleSelect.value !== 'unit');
    });
  }

  if (kindSelect && countLabel && countContainer) {
    kindSelect.addEventListener('change', () => {
      const k = kindSelect.value;
      if (k === 'ambulance') {
        countContainer.hidden = false;
        countLabel.textContent = 'Paramedics:';
      } else if (k === 'fire_truck') {
        countContainer.hidden = false;
        countLabel.textContent = 'Water Litres:';
      } else if (k === 'police_unit') {
        countContainer.hidden = false;
        countLabel.textContent = 'Officers:';
      } else if (k === 'hospital') {
        countContainer.hidden = false;
        countLabel.textContent = 'Total Beds:';
      } else if (k === 'shelter') {
        countContainer.hidden = false;
        countLabel.textContent = 'Capacity:';
      } else {
        countContainer.hidden = true;
      }
    });
  }

  // Login form submit handler
  const formLogin = document.getElementById('form-login');
  if (formLogin) {
    formLogin.addEventListener('submit', async (e) => {
      e.preventDefault();
      const u = document.getElementById('login-username').value.trim();
      const p = document.getElementById('login-password').value;

      if (!u || !p) {
        showAuthFeedback('Please enter both username and password.', 'error');
        return;
      }

      const res = await callBridge('login', { username: u, password: p });
      if (res && res.ok) {
        sessionStorage.setItem('rapidaid_token', res.token);
        state.user = res.username;
        state.role = res.role;
        clearAuthForms();

        if (res.mustChangePassword) {
          state.sessionData = res;
          showChangePasswordView(true);
          return;
        }

        showAuthFeedback('Login successful!', 'success');
        routeByRole(res.role, res);
      } else {
        const code = (res && (res.code || res.error || '')).toString().toLowerCase();
        if (code.includes('locked')) {
          const n = (res && (res.lockoutSeconds || res.retryAfter || res.seconds)) || 30;
          showAuthFeedback(`Too many attempts. Try again in ${n} seconds.`, 'error');
        } else if (code.includes('account_pending')) {
          showAuthFeedback((res && res.message) || (res && res.error) || 'Account pending approval.', 'error');
        } else if (code.includes('account_disabled')) {
          showAuthFeedback((res && res.message) || (res && res.error) || 'Account disabled.', 'error');
        } else {
          showAuthFeedback((res && res.error) ? res.error : 'Invalid username or password', 'error');
        }
      }
    });
  }

  // Signup form submit handler
  const formSignup = document.getElementById('form-signup');
  if (formSignup) {
    formSignup.addEventListener('submit', async (e) => {
      e.preventDefault();
      const u = document.getElementById('signup-username').value.trim();
      const p = document.getElementById('signup-password').value;
      const r = document.getElementById('signup-role').value;

      if (!u || !p) {
        showAuthFeedback('Please provide a username and password.', 'error');
        return;
      }

      const payload = { username: u, password: p, role: r };

      if (r === 'unit') {
        const kind = document.getElementById('signup-unit-kind').value;
        const name = document.getElementById('signup-unit-name').value.trim();
        const lat = parseFloat(document.getElementById('signup-unit-lat').value);
        const lon = parseFloat(document.getElementById('signup-unit-lon').value);
        const countVal = parseInt(document.getElementById('signup-unit-count').value, 10) || 0;

        if (!name) {
          showAuthFeedback('Please enter a display name for the unit.', 'error');
          return;
        }
        if (isNaN(lat) || isNaN(lon)) {
          showAuthFeedback('Please enter valid coordinates.', 'error');
          return;
        }

        payload.unit = {
          kind,
          name,
          lat,
          lon,
          count: countVal
        };
      }

      const res = await callBridge('signup', payload);
      if (res && res.ok) {
        clearAuthForms();
        let successMsg = `Account created for ${u}! Please log in.`;
        if (res.pending === true) {
          successMsg = 'Account created. An administrator must approve it before you can log in.';
        }
        tabLogin.click();
        showAuthFeedback(successMsg, 'success');
      } else {
        showAuthFeedback((res && res.error) ? res.error : 'Registration failed.', 'error');
      }
    });
  }

  // Change password form submit handler
  const formChangePassword = document.getElementById('form-change-password');
  if (formChangePassword) {
    formChangePassword.addEventListener('submit', async (e) => {
      e.preventDefault();
      const oldP = (document.getElementById('change-old-password').value || '');
      const newP = (document.getElementById('change-new-password').value || '');
      const confP = (document.getElementById('change-confirm-password').value || '');
      const feedback = document.getElementById('change-password-feedback');

      if (!oldP) {
        setFeedback(feedback, 'Please enter your current password.', 'error');
        return;
      }
      if (newP.length < 10 || newP.length > 128) {
        setFeedback(feedback, 'Password must be between 10 and 128 characters.', 'error');
        return;
      }
      if (newP !== confP) {
        setFeedback(feedback, 'New passwords do not match.', 'error');
        return;
      }

      setFeedback(feedback, 'Updating password...', 'info');

      const res = await callBridge('change_password', {
        oldPassword: oldP,
        newPassword: newP,
        confirmPassword: confP,
        old_password: oldP,
        new_password: newP
      });

      if (res && res.ok) {
        const msg = res.message || 'Password changed successfully!';
        setFeedback(feedback, msg, 'success');
        clearAuthForms();

        if (state.mustChangePassword) {
          state.mustChangePassword = false;
          setTimeout(() => {
            if (state.role) {
              routeByRole(state.role, state.sessionData || {});
            } else {
              showAuthView('Password changed successfully. Please log in with your new password.', 'success');
            }
          }, 800);
        } else {
          setTimeout(() => {
            if (state.role === 'unit') {
              document.getElementById('auth-view').hidden = true;
              document.getElementById('unit-view').hidden = false;
            } else if (state.role) {
              document.getElementById('auth-view').hidden = true;
              document.getElementById('app-view').hidden = false;
              const mainNav = document.getElementById('main-nav');
              if (mainNav) {
                mainNav.hidden = false;
                mainNav.style.display = 'flex';
              }
              showSection(state.previousSection || 'incidents');
            }
          }, 800);
        }
      } else {
        const err = (res && res.error) || (res && res.message) || 'Failed to change password.';
        setFeedback(feedback, err, 'error');
      }
    });
  }

  const btnCancelChange = document.getElementById('btn-cancel-change-password');
  if (btnCancelChange) {
    btnCancelChange.addEventListener('click', () => {
      clearAuthForms();
      if (state.role === 'unit') {
        document.getElementById('auth-view').hidden = true;
        document.getElementById('unit-view').hidden = false;
      } else if (state.role) {
        document.getElementById('auth-view').hidden = true;
        document.getElementById('app-view').hidden = false;
        const mainNav = document.getElementById('main-nav');
        if (mainNav) {
          mainNav.hidden = false;
          mainNav.style.display = 'flex';
        }
        showSection(state.previousSection || 'incidents');
      } else {
        showAuthView();
      }
    });
  }
}

function showChangePasswordView(isForced) {
  state.mustChangePassword = isForced;
  const authTabs = document.getElementById('auth-tabs-bar');
  const loginCont = document.getElementById('login-container');
  const signupCont = document.getElementById('signup-container');
  const changeCont = document.getElementById('change-password-container');
  const heading = document.getElementById('change-password-heading');
  const desc = document.getElementById('change-password-desc');
  const cancelBtn = document.getElementById('btn-cancel-change-password');
  const feedback = document.getElementById('change-password-feedback');

  if (feedback) {
    feedback.textContent = '';
    feedback.className = 'form-feedback';
  }

  document.getElementById('auth-view').hidden = false;
  document.getElementById('app-view').hidden = true;
  document.getElementById('unit-view').hidden = true;

  if (isForced) {
    const mainNav = document.getElementById('main-nav');
    if (mainNav) {
      mainNav.hidden = true;
      mainNav.style.display = 'none';
    }
    if (authTabs) authTabs.hidden = true;
    if (loginCont) loginCont.hidden = true;
    if (signupCont) signupCont.hidden = true;
    if (changeCont) changeCont.hidden = false;
    if (heading) heading.textContent = 'Change Password Required';
    if (desc) desc.textContent = 'You must change your password before continuing.';
    if (cancelBtn) cancelBtn.hidden = true;
  } else {
    state.previousSection = state.activeSection;
    if (authTabs) authTabs.hidden = true;
    if (loginCont) loginCont.hidden = true;
    if (signupCont) signupCont.hidden = true;
    if (changeCont) changeCont.hidden = false;
    if (heading) heading.textContent = 'Change Password';
    if (desc) desc.textContent = 'Enter your current password and choose a new password.';
    if (cancelBtn) cancelBtn.hidden = false;
  }
}

function setFeedback(el, msg, type = '') {
  if (!el) return;
  el.textContent = msg;
  el.className = 'form-feedback';
  if (type === 'success') {
    el.classList.add('form-success');
  } else if (type === 'error') {
    el.classList.add('form-error');
  }
}

function showAuthFeedback(msg, type = '') {
  const el = document.getElementById('auth-feedback');
  setFeedback(el, msg, type);
}

function showAuthView(msg = '', type = 'info') {
  stopAllModules();
  document.getElementById('auth-view').hidden = false;
  document.getElementById('app-view').hidden = true;
  document.getElementById('unit-view').hidden = true;

  const mainNav = document.getElementById('main-nav');
  if (mainNav) {
    mainNav.hidden = true;
    mainNav.style.display = 'none';
  }

  const authTabs = document.getElementById('auth-tabs-bar');
  if (authTabs) authTabs.hidden = false;

  const tabLogin = document.getElementById('tab-login');
  if (tabLogin) tabLogin.click();

  const changeCont = document.getElementById('change-password-container');
  if (changeCont) changeCont.hidden = true;

  const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
  if (apiKeyBtn) apiKeyBtn.hidden = false;

  updateProfileUI();
  if (msg) showAuthFeedback(msg, type);
}

function routeByRole(role, sessionData = {}) {
  document.getElementById('auth-view').hidden = true;
  updateProfileUI();

  if (role === 'unit') {
    const mainNav = document.getElementById('main-nav');
    if (mainNav) {
      mainNav.hidden = true;
      mainNav.style.display = 'none';
    }
    document.getElementById('app-view').hidden = true;
    const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
    if (apiKeyBtn) apiKeyBtn.hidden = true;
    const apiKeyBanner = document.getElementById('apikey-banner');
    if (apiKeyBanner) apiKeyBanner.hidden = true;

    stopBoardModules();
    stopAlertsPolling();
    stopStatsModule();
    stopAdminModule();
    if (window.RapidAidUnit && typeof window.RapidAidUnit.init === 'function') {
      window.RapidAidUnit.init(state.user, sessionData.unit);
    }
  } else {
    const unitView = document.getElementById('unit-view');
    if (unitView) unitView.hidden = true;
    if (window.RapidAidUnit && typeof window.RapidAidUnit.stop === 'function') {
      window.RapidAidUnit.stop();
    }

    const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
    if (apiKeyBtn) apiKeyBtn.hidden = false;

    onLoginSuccess();
  }
}

async function checkAreaStatusAndRoute() {
  const statusRes = await callBridge('area_status');
  if (statusRes && statusRes.ok && statusRes.exists) {
    state.hasGeoJson = true;
    showSection('incidents');
    const areaAlert = document.getElementById('area-alert');
    if (areaAlert) areaAlert.hidden = true;
    await window.RapidAid.reloadLayers();
  } else {
    state.hasGeoJson = false;
    showSection('area');
    const areaAlert = document.getElementById('area-alert');
    if (areaAlert) areaAlert.hidden = false;
  }
}

async function onLoginSuccess() {
  document.getElementById('app-view').hidden = false;
  const mainNav = document.getElementById('main-nav');
  if (mainNav) {
    mainNav.hidden = false;
    mainNav.style.display = 'flex';
  }

  // Admin section nav visibility
  const navUsers = document.getElementById('nav-users');
  if (navUsers) {
    navUsers.hidden = (state.role !== 'admin');
  }

  updateProfileUI();
  await checkAreaStatusAndRoute();
}

// ==========================================
// 6. Navigation
// ==========================================
function initNavigationEvents() {
  const navIncidents = document.getElementById('nav-incidents');
  const navArea = document.getElementById('nav-area');
  const navStats = document.getElementById('nav-stats');
  const navUsers = document.getElementById('nav-users');
  const navLogout = document.getElementById('nav-logout');

  if (navIncidents) navIncidents.addEventListener('click', () => showSection('incidents'));
  if (navArea) navArea.addEventListener('click', () => showSection('area'));
  if (navStats) navStats.addEventListener('click', () => showSection('stats'));
  if (navUsers) navUsers.addEventListener('click', () => showSection('admin'));
  if (navLogout) navLogout.addEventListener('click', () => performLogout());
}

function showSection(sectionId) {
  if (sectionId === 'admin' && state.role !== 'admin') {
    sectionId = 'incidents';
  }

  const prevSection = state.activeSection;
  state.activeSection = sectionId;

  if (prevSection === 'incidents' && sectionId !== 'incidents') {
    stopBoardModules();
    stopAlertsPolling();
  } else if (prevSection === 'stats' && sectionId !== 'stats') {
    stopStatsModule();
  } else if (prevSection === 'admin' && sectionId !== 'admin') {
    stopAdminModule();
  }

  const sections = ['area', 'incidents', 'stats', 'admin'];
  sections.forEach(s => {
    const el = document.getElementById('section-' + s);
    const nav = document.getElementById(s === 'admin' ? 'nav-users' : ('nav-' + s));
    const isActive = (s === sectionId);
    if (el) {
      el.hidden = !isActive;
    }
    if (nav) {
      if (isActive) {
        nav.setAttribute('aria-current', 'page');
      } else {
        nav.removeAttribute('aria-current');
      }
    }
  });

  if (sectionId === 'area') {
    ensureLeafletMap();
    if (state.roadsGeoJson || state.placesGeoJson) {
      renderGeoJsonOnMap(state.leafletMap, state.roadsGeoJson, state.placesGeoJson, state.areaLayers);
    }
  } else if (sectionId === 'incidents') {
    startBoardModules();
    startAlertsPolling();
    refreshUnitSelect();
  } else if (sectionId === 'stats') {
    startStatsModule();
  } else if (sectionId === 'admin') {
    startAdminModule();
  }
}

// ==========================================
// 7. Dispatcher Developer Test Alert Box
// ==========================================
function initDispatcherAlertTest() {
  const btnRefresh = document.getElementById('btn-refresh-units');
  const btnSend = document.getElementById('btn-send-alert');

  if (btnRefresh) {
    btnRefresh.addEventListener('click', refreshUnitSelect);
  }

  if (btnSend) {
    btnSend.addEventListener('click', async () => {
      const unitSelect = document.getElementById('alert-unit-select');
      const toUser = unitSelect ? unitSelect.value : '';
      const type = document.getElementById('alert-type').value;
      const severity = parseInt(document.getElementById('alert-severity').value, 10) || 3;
      const people = parseInt(document.getElementById('alert-people').value, 10) || 1;
      const location = document.getElementById('alert-location').value.trim();
      const note = document.getElementById('alert-note').value.trim();
      const latVal = parseFloat(document.getElementById('alert-lat').value);
      const lonVal = parseFloat(document.getElementById('alert-lon').value);
      const feedback = document.getElementById('alert-send-feedback');

      if (!toUser) {
        setFeedback(feedback, 'Please select a recipient unit.', 'error');
        return;
      }
      if (!location) {
        setFeedback(feedback, 'Please specify an incident location.', 'error');
        return;
      }

      setFeedback(feedback, 'Dispatching alert...', 'info');

      const payload = {
        toUser,
        type,
        severity,
        peopleAffected: people,
        locationText: location,
        note
      };
      if (!isNaN(latVal)) payload.lat = latVal;
      if (!isNaN(lonVal)) payload.lon = lonVal;

      const res = await callBridge('send_test_alert', payload);
      if (res && res.ok) {
        setFeedback(feedback, `Alert #${res.alertId} dispatched successfully to ${toUser}!`, 'success');
        refreshAlertsTable();
      } else {
        setFeedback(feedback, (res && res.error) ? res.error : 'Failed to dispatch alert.', 'error');
      }
    });
  }
}

async function refreshUnitSelect() {
  const select = document.getElementById('alert-unit-select');
  if (!select) return;

  const res = await callBridge('list_units');
  select.textContent = ''; // Clear options safely

  if (res && res.ok && res.units && res.units.length > 0) {
    res.units.forEach(u => {
      const opt = document.createElement('option');
      opt.value = u.username;
      opt.textContent = `${u.name} (${u.kind}) - Status: ${u.status}`;
      select.appendChild(opt);
    });
  } else {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = 'No service units registered';
    select.appendChild(opt);
  }
}

async function refreshAlertsTable() {
  const tbody = document.getElementById('alerts-table-body');
  if (!tbody) return;

  const res = await callBridge('list_alerts');
  tbody.textContent = ''; // Clear rows safely

  if (res && res.ok && res.alerts && res.alerts.length > 0) {
    const list = [...res.alerts].reverse();
    list.forEach(a => {
      const tr = document.createElement('tr');

      const tdId = document.createElement('td');
      tdId.textContent = `#${a.id}`;

      const tdUnit = document.createElement('td');
      tdUnit.textContent = a.toUser || '';

      const tdType = document.createElement('td');
      tdType.textContent = a.type || '';

      const tdSev = document.createElement('td');
      tdSev.textContent = `${a.severity}/5`;

      const tdState = document.createElement('td');
      tdState.textContent = a.state || '';

      const tdTime = document.createElement('td');
      const d = new Date((a.createdAt || 0) * 1000);
      tdTime.textContent = isNaN(d.getTime()) ? '' : d.toLocaleTimeString();

      tr.appendChild(tdId);
      tr.appendChild(tdUnit);
      tr.appendChild(tdType);
      tr.appendChild(tdSev);
      tr.appendChild(tdState);
      tr.appendChild(tdTime);
      tbody.appendChild(tr);
    });
  } else {
    const tr = document.createElement('tr');
    const td = document.createElement('td');
    td.colSpan = 6;
    td.textContent = 'No dispatch alerts recorded yet.';
    tr.appendChild(td);
    tbody.appendChild(tr);
  }
}

function startAlertsPolling() {
  stopAlertsPolling();
  refreshAlertsTable();
  dispatcherAlertsTimer = setInterval(refreshAlertsTable, 3000);
}

function stopAlertsPolling() {
  if (dispatcherAlertsTimer) {
    clearInterval(dispatcherAlertsTimer);
    dispatcherAlertsTimer = null;
  }
}
