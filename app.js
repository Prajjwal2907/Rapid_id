/**
 * RapidAid Frontend Dispatcher Application (app.js)
 * Features:
 * - Real backend authentication & session persistence (sessionStorage)
 * - Role-based UI routing (Dispatcher / Admin vs Service Unit)
 * - Bridge-based OSM Geocoding & Area Pipeline
 * - Shared Leaflet Map Engine for Area & Incident Board
 * - Modular Loader for RapidAidBoard and RapidAidDispatch
 * - Dispatcher Developer Test Alert panel & 3s live alert polling
 * Strict textContent usage for all dynamic text to prevent XSS.
 */

const state = {
  user: null,
  role: null,
  activeSection: 'area',
  hasGeoJson: false,
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

// ==========================================
// API Bridge Helper
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
    return await res.json();
  } catch (err) {
    return { ok: false, error: 'Network error communicating with server: ' + (err.message || err) };
  }
}

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
  showAuthView
};

// ==========================================
// Tolerant Optional Module Loader
// ==========================================
function loadOptionalModules() {
  function appendModuleScript(src) {
    const s = document.createElement('script');
    s.src = src;
    s.async = true;
    s.onload = () => {
      if (state.activeSection === 'incidents') {
        initActiveBoardModules();
      }
    };
    s.onerror = () => {
      // Tolerantly ignore missing module script
    };
    document.head.appendChild(s);
  }

  appendModuleScript('board.js');
  appendModuleScript('dispatch.js');
}

function initActiveBoardModules() {
  const boardMount = document.getElementById('board-mount');
  if (boardMount) {
    if (window.RapidAidBoard && typeof window.RapidAidBoard.init === 'function') {
      if (!boardRunning) {
        boardMount.textContent = '';
        boardMount.style.padding = '';
        boardMount.style.color = '';
        boardMount.style.fontStyle = '';
        try {
          window.RapidAidBoard.init(boardMount);
          boardRunning = true;
        } catch (err) {
          console.error('Error initializing RapidAidBoard:', err);
        }
      }
    } else {
      boardMount.textContent = 'Incident board module not installed';
      boardMount.style.padding = '12px';
      boardMount.style.color = '#777';
      boardMount.style.fontStyle = 'italic';
      boardRunning = false;
    }
  }

  const dispatchMount = document.getElementById('dispatch-mount');
  if (dispatchMount) {
    if (window.RapidAidDispatch && typeof window.RapidAidDispatch.init === 'function') {
      if (!dispatchRunning) {
        dispatchMount.textContent = '';
        dispatchMount.style.padding = '';
        dispatchMount.style.color = '';
        dispatchMount.style.fontStyle = '';
        try {
          window.RapidAidDispatch.init(dispatchMount);
          dispatchRunning = true;
        } catch (err) {
          console.error('Error initializing RapidAidDispatch:', err);
        }
      }
    } else {
      dispatchMount.textContent = 'Dispatch panel not installed';
      dispatchMount.style.padding = '12px';
      dispatchMount.style.color = '#777';
      dispatchMount.style.fontStyle = 'italic';
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

// ==========================================
// 1. Initialization
// ==========================================
document.addEventListener('DOMContentLoaded', async () => {
  initAuthEvents();
  initNavigationEvents();
  initAreaEvents();
  initIncidentEvents();
  initApiKeyEvents();
  initDispatcherAlertTest();
  checkApiKey();
  loadOptionalModules();
  updateStatusBanner('Ready');

  // Session check on page load (A2)
  const token = getToken();
  if (token) {
    const res = await callBridge('session');
    if (res && res.ok) {
      state.user = res.username;
      state.role = res.role;
      routeByRole(res.role, res);
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
    badge.style.background = '#d4edda';
    badge.style.borderColor = '#c3e6cb';
    badge.style.color = '#155724';
  }
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
      badge.style.color = '#28a745';
      badge.style.fontWeight = 'bold';
    }
    if (banner) banner.style.display = 'none';
  } else {
    state.cartoApiKey = '';
    if (badge) {
      badge.textContent = 'Not Set (Click to configure)';
      badge.style.color = '#d9534f';
      badge.style.fontWeight = 'bold';
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
        banner.style.display = (banner.style.display === 'none') ? 'block' : 'none';
      }
    });
  }

  if (btnDismiss) {
    btnDismiss.addEventListener('click', () => {
      if (banner) banner.style.display = 'none';
    });
  }

  if (btnSave) {
    btnSave.addEventListener('click', () => {
      const key = (input.value || '').trim();
      if (!key) {
        if (feedback) {
          feedback.textContent = 'Please enter a valid API key string.';
          feedback.style.color = '#d9534f';
        }
        return;
      }

      state.cartoApiKey = key;
      localStorage.setItem('rapidaid_carto_api_key', key);

      const badge = document.getElementById('apikey-badge');
      if (badge) {
        badge.textContent = 'Configured';
        badge.style.color = '#28a745';
        badge.style.fontWeight = 'bold';
      }

      if (feedback) {
        feedback.textContent = 'API key saved to private local storage and activated!';
        feedback.style.color = '#28a745';
      }

      updateTileLayers();

      setTimeout(() => {
        if (banner) banner.style.display = 'none';
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

      if (feedback) {
        feedback.textContent = 'config.json downloaded! Place it in the project root folder.';
        feedback.style.color = '#17a2b8';
      }
    });
  }
}

function applyTileLayer(mapInstance, tileLayerKey) {
  if (!mapInstance) return;

  if (state[tileLayerKey]) {
    mapInstance.removeLayer(state[tileLayerKey]);
    state[tileLayerKey] = null;
  }

  let tileUrl = 'https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png';
  if (state.cartoApiKey) {
    tileUrl += `?key=${encodeURIComponent(state.cartoApiKey)}`;
  }

  state[tileLayerKey] = L.tileLayer(tileUrl, {
    maxZoom: 19,
    subdomains: 'abcd',
    attribution: '&copy; OpenStreetMap contributors &copy; CARTO'
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
        feedback.textContent = 'Please enter an area name to preview.';
        feedback.style.color = '#d9534f';
        return;
      }

      feedback.textContent = 'Geocoding area coordinates & preparing zone preview...';
      feedback.style.color = '#0056b3';

      ensureLeafletMap();

      const loc = await callBridge('geocode', { query: name });
      if (!loc || !loc.ok) {
        feedback.textContent = (loc && loc.error) ? loc.error : 'Location not found for: ' + name;
        feedback.style.color = '#d9534f';
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

      feedback.textContent = 'Location geocoded! Center marker and radius circle updated.';
      feedback.style.color = '#28a745';
    });
  }

  if (btnGenerate) {
    btnGenerate.addEventListener('click', async () => {
      const name = document.getElementById('area-name').value.trim();
      const radius = parseInt(document.getElementById('area-radius').value, 10) || 3000;

      if (!name) {
        feedback.textContent = 'Please specify an operational area name.';
        feedback.style.color = '#d9534f';
        return;
      }

      feedback.textContent = 'Generating road network and emergency facilities via pipeline...';
      feedback.style.color = '#0056b3';

      ensureLeafletMap();

      let lat = state.centerLat;
      let lon = state.centerLon;
      let displayName = state.centerDisplayName || name;

      if (state.lastGeocodedName !== name) {
        const loc = await callBridge('geocode', { query: name });
        if (!loc || !loc.ok) {
          feedback.textContent = (loc && loc.error) ? loc.error : 'Location not found for: ' + name;
          feedback.style.color = '#d9534f';
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

        document.getElementById('area-stats-box').style.display = 'block';
        document.getElementById('stat-nodes').textContent = pipeData.nodesCount != null ? pipeData.nodesCount : 0;
        document.getElementById('stat-edges').textContent = pipeData.edgesCount != null ? pipeData.edgesCount : 0;
        document.getElementById('stat-places').textContent = pipeData.placesCount != null ? pipeData.placesCount : 0;
        document.getElementById('stat-snapped').textContent = pipeData.snappedCount != null ? pipeData.snappedCount : 0;
        if (pipeData.roadsFile) document.getElementById('stat-roads-file').textContent = pipeData.roadsFile;
        if (pipeData.placesFile) document.getElementById('stat-places-file').textContent = pipeData.placesFile;

        feedback.textContent = `Success! Operational zone pipeline completed.\n${pipeData.placesCount} facilities snapped to ${pipeData.edgesCount} road edges.`;
        feedback.style.color = '#28a745';

        const areaAlert = document.getElementById('area-alert');
        if (areaAlert) areaAlert.style.display = 'none';
      } else {
        feedback.textContent = (pipeData && pipeData.error) ? pipeData.error : 'Pipeline generation failed.';
        feedback.style.color = '#d9534f';
      }
    });
  }
}

// ==========================================
// 5. Authentication & Routing
// ==========================================
function initAuthEvents() {
  const tabLogin = document.getElementById('tab-login');
  const tabSignup = document.getElementById('tab-signup');
  const loginContainer = document.getElementById('login-container');
  const signupContainer = document.getElementById('signup-container');
  const roleSelect = document.getElementById('signup-role');
  const unitFields = document.getElementById('signup-unit-fields');
  const kindSelect = document.getElementById('signup-unit-kind');
  const countContainer = document.getElementById('signup-unit-count-container');
  const countLabel = document.getElementById('signup-unit-count-label');

  tabLogin.addEventListener('click', () => {
    tabLogin.style.fontWeight = 'bold';
    tabSignup.style.fontWeight = 'normal';
    loginContainer.style.display = 'block';
    signupContainer.style.display = 'none';
    showAuthFeedback('', '');
  });

  tabSignup.addEventListener('click', () => {
    tabSignup.style.fontWeight = 'bold';
    tabLogin.style.fontWeight = 'normal';
    signupContainer.style.display = 'block';
    loginContainer.style.display = 'none';
    showAuthFeedback('', '');
  });

  if (roleSelect && unitFields) {
    roleSelect.addEventListener('change', () => {
      unitFields.style.display = (roleSelect.value === 'unit') ? 'block' : 'none';
    });
  }

  if (kindSelect && countLabel && countContainer) {
    kindSelect.addEventListener('change', () => {
      const k = kindSelect.value;
      if (k === 'ambulance') {
        countContainer.style.display = 'block';
        countLabel.textContent = 'Paramedics:';
      } else if (k === 'fire_truck') {
        countContainer.style.display = 'block';
        countLabel.textContent = 'Water Litres:';
      } else if (k === 'police_unit') {
        countContainer.style.display = 'block';
        countLabel.textContent = 'Officers:';
      } else if (k === 'hospital') {
        countContainer.style.display = 'block';
        countLabel.textContent = 'Total Beds:';
      } else if (k === 'shelter') {
        countContainer.style.display = 'block';
        countLabel.textContent = 'Capacity:';
      } else {
        countContainer.style.display = 'none';
      }
    });
  }

  document.getElementById('btn-login').addEventListener('click', async () => {
    const u = document.getElementById('login-username').value.trim();
    const p = document.getElementById('login-password').value;

    if (!u || !p) {
      showAuthFeedback('Please enter both username and password.', '#d9534f');
      return;
    }

    const res = await callBridge('login', { username: u, password: p });
    if (res && res.ok) {
      sessionStorage.setItem('rapidaid_token', res.token);
      state.user = res.username;
      state.role = res.role;
      showAuthFeedback('Login successful!', '#28a745');
      routeByRole(res.role, res);
    } else {
      showAuthFeedback((res && res.error) ? res.error : 'Invalid username or password', '#d9534f');
    }
  });

  document.getElementById('btn-signup').addEventListener('click', async () => {
    const u = document.getElementById('signup-username').value.trim();
    const p = document.getElementById('signup-password').value;
    const r = document.getElementById('signup-role').value;

    if (!u || !p) {
      showAuthFeedback('Please provide a username and password.', '#d9534f');
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
        showAuthFeedback('Please enter a display name for the unit.', '#d9534f');
        return;
      }
      if (isNaN(lat) || isNaN(lon)) {
        showAuthFeedback('Please enter valid coordinates.', '#d9534f');
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
      showAuthFeedback(`Account created for ${u}! Please log in.`, '#28a745');
      tabLogin.click();
      document.getElementById('login-username').value = u;
      document.getElementById('login-password').value = '';
    } else {
      showAuthFeedback((res && res.error) ? res.error : 'Registration failed.', '#d9534f');
    }
  });
}

function showAuthFeedback(msg, color) {
  const el = document.getElementById('auth-feedback');
  if (el) {
    el.textContent = msg;
    el.style.color = color;
  }
}

function showAuthView(msg = '') {
  stopBoardModules();
  document.getElementById('auth-view').style.display = 'block';
  document.getElementById('app-view').style.display = 'none';
  document.getElementById('main-nav').style.display = 'none';
  document.getElementById('session-info').style.display = 'none';
  document.getElementById('unit-view').style.display = 'none';
  const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
  if (apiKeyBtn) apiKeyBtn.style.display = 'inline-block';
  if (msg) showAuthFeedback(msg, '#28a745');
}

function routeByRole(role, sessionData) {
  document.getElementById('auth-view').style.display = 'none';

  if (role === 'unit') {
    document.getElementById('main-nav').style.display = 'none';
    document.getElementById('app-view').style.display = 'none';
    document.getElementById('session-info').style.display = 'none';
    const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
    if (apiKeyBtn) apiKeyBtn.style.display = 'none';
    const apiKeyBanner = document.getElementById('apikey-banner');
    if (apiKeyBanner) apiKeyBanner.style.display = 'none';

    stopBoardModules();
    stopAlertsPolling();
    window.RapidAidUnit.init(state.user, sessionData.unit);
  } else {
    const unitView = document.getElementById('unit-view');
    if (unitView) unitView.style.display = 'none';
    if (window.RapidAidUnit) window.RapidAidUnit.stop();

    const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
    if (apiKeyBtn) apiKeyBtn.style.display = 'inline-block';

    onLoginSuccess();
  }
}

async function checkAreaStatusAndRoute() {
  const statusRes = await callBridge('area_status');
  if (statusRes && statusRes.ok && statusRes.exists) {
    state.hasGeoJson = true;
    showSection('incidents');
    const areaAlert = document.getElementById('area-alert');
    if (areaAlert) areaAlert.style.display = 'none';
    await window.RapidAid.reloadLayers();
  } else {
    state.hasGeoJson = false;
    showSection('area');
    const areaAlert = document.getElementById('area-alert');
    if (areaAlert) areaAlert.style.display = 'block';
  }
}

async function onLoginSuccess() {
  document.getElementById('app-view').style.display = 'block';
  document.getElementById('main-nav').style.display = 'block';
  document.getElementById('session-info').style.display = 'inline';
  document.getElementById('user-display').textContent = state.user;
  document.getElementById('role-display').textContent = state.role;

  await checkAreaStatusAndRoute();
}

// ==========================================
// 6. Navigation
// ==========================================
function initNavigationEvents() {
  const navIncidents = document.getElementById('nav-incidents');
  const navArea = document.getElementById('nav-area');
  const navStats = document.getElementById('nav-stats');
  const navLogout = document.getElementById('nav-logout');

  navIncidents.addEventListener('click', () => showSection('incidents'));
  navArea.addEventListener('click', () => showSection('area'));
  navStats.addEventListener('click', () => showSection('stats'));

  navLogout.addEventListener('click', async () => {
    stopBoardModules();
    await callBridge('logout');
    sessionStorage.removeItem('rapidaid_token');
    state.user = null;
    state.role = null;
    stopAlertsPolling();
    showAuthView('Logged out successfully.');
  });
}

function showSection(sectionId) {
  const prevSection = state.activeSection;
  state.activeSection = sectionId;

  const sections = ['area', 'incidents', 'stats'];
  sections.forEach(s => {
    const el = document.getElementById('section-' + s);
    const nav = document.getElementById('nav-' + s);
    if (el) el.style.display = (s === sectionId) ? 'block' : 'none';
    if (nav) nav.style.fontWeight = (s === sectionId) ? 'bold' : 'normal';
  });

  if (prevSection === 'incidents' && sectionId !== 'incidents') {
    stopBoardModules();
  }

  if (sectionId === 'area') {
    ensureLeafletMap();
    if (state.roadsGeoJson || state.placesGeoJson) {
      renderGeoJsonOnMap(state.leafletMap, state.roadsGeoJson, state.placesGeoJson, state.areaLayers);
    }
  } else if (sectionId === 'incidents') {
    startBoardModules();
    startAlertsPolling();
    refreshUnitSelect();
  }

  if (sectionId !== 'incidents') {
    stopAlertsPolling();
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
        feedback.textContent = 'Please select a recipient unit.';
        feedback.style.color = '#d9534f';
        return;
      }
      if (!location) {
        feedback.textContent = 'Please specify an incident location.';
        feedback.style.color = '#d9534f';
        return;
      }

      feedback.textContent = 'Dispatching alert...';
      feedback.style.color = '#0056b3';

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
        feedback.textContent = `Alert #${res.alertId} dispatched successfully to ${toUser}!`;
        feedback.style.color = '#28a745';
        refreshAlertsTable();
      } else {
        feedback.textContent = (res && res.error) ? res.error : 'Failed to dispatch alert.';
        feedback.style.color = '#d9534f';
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
      tr.style.borderBottom = '1px solid #ddd';

      const tdId = document.createElement('td');
      tdId.style.padding = '8px';
      tdId.textContent = `#${a.id}`;

      const tdUnit = document.createElement('td');
      tdUnit.style.padding = '8px';
      tdUnit.textContent = a.toUser;

      const tdType = document.createElement('td');
      tdType.style.padding = '8px';
      tdType.textContent = a.type;

      const tdSev = document.createElement('td');
      tdSev.style.padding = '8px';
      tdSev.textContent = `${a.severity}/5`;

      const tdState = document.createElement('td');
      tdState.style.padding = '8px';
      tdState.textContent = a.state;
      tdState.style.fontWeight = 'bold';
      tdState.style.color = (a.state === 'Cleared') ? '#28a745' :
                            (a.state === 'Pending') ? '#ffc107' :
                            (a.state === 'Declined') ? '#6c757d' : '#0066cc';

      const tdTime = document.createElement('td');
      tdTime.style.padding = '8px';
      const d = new Date(a.createdAt * 1000);
      tdTime.textContent = d.toLocaleTimeString();

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
    td.style.padding = '12px';
    td.style.textAlign = 'center';
    td.style.color = '#777';
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

// ==========================================
// 8. Incident Event Compatibility Stub
// ==========================================
function initIncidentEvents() {
  // Retained for backward compatibility
}
