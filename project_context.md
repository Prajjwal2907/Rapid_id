# Project context

## app.js

```javascript
/**
 * RapidAid Frontend Dispatcher Application (app.js)
 * Features:
 * - Real backend authentication & session persistence (token via sessionStorage)
 * - Role-based UI routing (Dispatcher / Admin vs Service Unit)
 * - Live OpenStreetMap Geocoding (Photon & Nominatim fallback)
 * - Live Leaflet Map Integration with real OSM street tiles & CARTO
 * - GeoJSON Generation and Pipeline Trigger with authenticated tokens
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
  radiusMeters: 3000,
  cartoApiKey: '',
  tileLayer: null,
  leafletMap: null,
  centerMarker: null,
  radiusCircle: null,
  roadsLayer: null,
  placesLayer: null,
  roadsGeoJson: null,
  placesGeoJson: null
};

let dispatcherAlertsTimer = null;

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
    return { ok: false, error: 'Network error communicating with server' };
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

      updateTileLayer();

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

// ==========================================
// 3. Leaflet Map Engine
// ==========================================
function updateTileLayer() {
  if (!state.leafletMap) return;

  if (state.tileLayer) {
    state.leafletMap.removeLayer(state.tileLayer);
  }

  let tileUrl = 'https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png';
  if (state.cartoApiKey) {
    tileUrl += `?key=${encodeURIComponent(state.cartoApiKey)}`;
  }

  state.tileLayer = L.tileLayer(tileUrl, {
    maxZoom: 19,
    subdomains: 'abcd',
    attribution: '&copy; OpenStreetMap contributors &copy; CARTO'
  }).addTo(state.leafletMap);
}

function getPipelineGeoJson() {
  if (window.RAPIDAID_ROADS && window.RAPIDAID_PLACES) {
    return {
      roads: window.RAPIDAID_ROADS,
      places: window.RAPIDAID_PLACES
    };
  }
  return null;
}

function ensureLeafletMap() {
  if (state.leafletMap) {
    setTimeout(() => state.leafletMap.invalidateSize(), 150);
    return;
  }

  const container = document.getElementById('leaflet-map');
  if (!container) return;

  state.leafletMap = L.map('leaflet-map', { preferCanvas: true }).setView([state.centerLat, state.centerLon], 14);
  updateTileLayer();

  updateMapVisuals('Eiffel Tower, Paris', state.centerLat, state.centerLon, state.radiusMeters);

  const pData = getPipelineGeoJson();
  if (pData) {
    state.roadsGeoJson = pData.roads;
    state.placesGeoJson = pData.places;
    renderGeoJsonOnLeaflet(pData.roads, pData.places);
  }

  setTimeout(() => state.leafletMap.invalidateSize(), 200);
}

function updateMapVisuals(displayName, lat, lon, radiusMeters) {
  if (!state.leafletMap) return;

  state.centerLat = lat;
  state.centerLon = lon;
  state.radiusMeters = radiusMeters;

  if (state.centerMarker) {
    state.leafletMap.removeLayer(state.centerMarker);
  }
  state.centerMarker = L.marker([lat, lon]).addTo(state.leafletMap);
  state.centerMarker.bindPopup(`<b>${displayName}</b><br>Coordinates: ${lat.toFixed(5)}, ${lon.toFixed(5)}`).openPopup();

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
// 4. Geocoding & Area Pipeline
// ==========================================
async function fetchLiveGeocode(query) {
  // Bridge backend geocoder first (if cache/token available)
  const bRes = await callBridge('geocode', { query });
  if (bRes && bRes.ok) {
    return { lat: bRes.lat, lon: bRes.lon, displayName: bRes.displayName };
  }

  try {
    const photonUrl = `https://photon.komoot.io/api/?q=${encodeURIComponent(query)}&limit=1`;
    const res = await fetch(photonUrl);
    if (res.ok) {
      const data = await res.json();
      if (data && data.features && data.features.length > 0) {
        const feat = data.features[0];
        const [lon, lat] = feat.geometry.coordinates;
        const name = feat.properties.name || feat.properties.city || query;
        const country = feat.properties.country || '';
        const displayName = country ? `${name}, ${country}` : name;
        return { lat, lon, displayName };
      }
    }
  } catch (err) {}

  return {
    lat: 48.8583,
    lon: 2.2945,
    displayName: `${query} (Default Grid Reference)`
  };
}

function formatAmenityName(cat) {
  if (cat === 'hospital') return 'Hospital';
  if (cat === 'fire_station') return 'Fire Station';
  if (cat === 'police') return 'Police Station';
  if (cat === 'clinic') return 'Clinic';
  if (cat === 'pharmacy') return 'Pharmacy';
  if (cat === 'shelter') return 'Emergency Shelter';
  return cat || 'Facility';
}

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

      const loc = await fetchLiveGeocode(name);
      document.getElementById('preview-coords').textContent = `${loc.lat.toFixed(5)}, ${loc.lon.toFixed(5)}`;
      document.getElementById('preview-displayname').textContent = loc.displayName;

      updateMapVisuals(loc.displayName, loc.lat, loc.lon, radius);

      const pipelineData = getPipelineGeoJson();
      if (pipelineData) {
        state.roadsGeoJson = pipelineData.roads;
        state.placesGeoJson = pipelineData.places;
        renderGeoJsonOnLeaflet(pipelineData.roads, pipelineData.places);
        feedback.textContent = `Location preview updated with pipeline network.`;
        feedback.style.color = '#28a745';
      } else {
        feedback.textContent = 'Location geocoded! Center marker and radius circle updated.';
        feedback.style.color = '#28a745';
      }
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

      const loc = await fetchLiveGeocode(name);
      updateMapVisuals(loc.displayName, loc.lat, loc.lon, radius);

      // Authenticated pipeline request to /api/create_area (B2)
      try {
        const res = await fetch('/api/create_area', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            token: getToken(),
            name,
            radius,
            lat: loc.lat,
            lon: loc.lon,
            displayName: loc.displayName
          })
        });
        const pipeData = await res.json();

        if (pipeData && pipeData.ok) {
          state.hasGeoJson = true;
          if (pipeData.roadsGeoJson && pipeData.placesGeoJson) {
            state.roadsGeoJson = JSON.parse(pipeData.roadsGeoJson);
            state.placesGeoJson = JSON.parse(pipeData.placesGeoJson);
            renderGeoJsonOnLeaflet(state.roadsGeoJson, state.placesGeoJson);
          }

          document.getElementById('area-stats-box').style.display = 'block';
          document.getElementById('stat-nodes').textContent = pipeData.nodesCount || 0;
          document.getElementById('stat-edges').textContent = pipeData.edgesCount || 0;
          document.getElementById('stat-places').textContent = pipeData.placesCount || 0;
          document.getElementById('stat-snapped').textContent = pipeData.snappedCount || 0;

          feedback.textContent = `Success! Operational zone pipeline completed.\n${pipeData.placesCount} facilities snapped to ${pipeData.edgesCount} road edges.`;
          feedback.style.color = '#28a745';

          document.getElementById('area-alert').style.display = 'none';
          localStorage.setItem('rapidaid_has_geojson', 'true');
        } else {
          feedback.textContent = (pipeData && pipeData.error) ? pipeData.error : 'Pipeline generation failed.';
          feedback.style.color = '#d9534f';
        }
      } catch (err) {
        feedback.textContent = 'Network error contacting pipeline server.';
        feedback.style.color = '#d9534f';
      }
    });
  }
}

function renderGeoJsonOnLeaflet(roadsGeoJson, placesGeoJson) {
  if (!state.leafletMap) return;

  if (state.roadsLayer) state.leafletMap.removeLayer(state.roadsLayer);
  if (state.placesLayer) state.leafletMap.removeLayer(state.placesLayer);

  if (roadsGeoJson && roadsGeoJson.features && roadsGeoJson.features.length > 0) {
    state.roadsLayer = L.geoJSON(roadsGeoJson, {
      style: { color: '#0066cc', weight: 3.5, opacity: 0.85 }
    }).addTo(state.leafletMap);
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
    state.placesLayer = L.geoJSON(placesGeoJson, {
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
        layer.bindPopup(`<b>${name}</b><br>Category: <b>${formatAmenityName(cat)}</b>`);
      }
    }).addTo(state.leafletMap);
  }

  try {
    const group = new L.featureGroup([state.roadsLayer, state.placesLayer].filter(Boolean));
    if (group.getLayers().length > 0) {
      state.leafletMap.fitBounds(group.getBounds().pad(0.1));
    }
  } catch (e) {}
}

// ==========================================
// 5. Authentication (A1, A2, A3, A4)
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

  // Dynamic role fields toggle (A3)
  if (roleSelect && unitFields) {
    roleSelect.addEventListener('change', () => {
      unitFields.style.display = (roleSelect.value === 'unit') ? 'block' : 'none';
    });
  }

  // Dynamic unit type count label toggle (C1)
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

  // Real Login via POST /api/bridge (A1, A2)
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

  // Real Signup via POST /api/bridge (A1, A3, C1)
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
  document.getElementById('auth-view').style.display = 'block';
  document.getElementById('app-view').style.display = 'none';
  document.getElementById('main-nav').style.display = 'none';
  document.getElementById('session-info').style.display = 'none';
  document.getElementById('unit-view').style.display = 'none';
  const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
  if (apiKeyBtn) apiKeyBtn.style.display = 'inline-block';
  if (msg) showAuthFeedback(msg, '#28a745');
}

// Role-based UI Routing (A4)
function routeByRole(role, sessionData) {
  document.getElementById('auth-view').style.display = 'none';

  if (role === 'unit') {
    // Unit role gets ONLY the unit view (A4)
    document.getElementById('main-nav').style.display = 'none';
    document.getElementById('app-view').style.display = 'none';
    document.getElementById('session-info').style.display = 'none';
    const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
    if (apiKeyBtn) apiKeyBtn.style.display = 'none';
    const apiKeyBanner = document.getElementById('apikey-banner');
    if (apiKeyBanner) apiKeyBanner.style.display = 'none';

    stopAlertsPolling();
    window.RapidAidUnit.init(state.user, sessionData.unit);
  } else {
    // Dispatcher or Admin role
    const unitView = document.getElementById('unit-view');
    if (unitView) unitView.style.display = 'none';
    if (window.RapidAidUnit) window.RapidAidUnit.stop();

    const apiKeyBtn = document.getElementById('btn-toggle-apikey-settings');
    if (apiKeyBtn) apiKeyBtn.style.display = 'inline-block';

    onLoginSuccess();
  }
}

function onLoginSuccess() {
  document.getElementById('app-view').style.display = 'block';
  document.getElementById('main-nav').style.display = 'block';
  document.getElementById('session-info').style.display = 'inline';
  document.getElementById('user-display').textContent = state.user;
  document.getElementById('role-display').textContent = state.role;

  const savedGeoJson = localStorage.getItem('rapidaid_has_geojson');
  state.hasGeoJson = (savedGeoJson === 'true');

  if (!state.hasGeoJson) {
    showSection('area');
    document.getElementById('area-alert').style.display = 'block';
  } else {
    showSection('incidents');
    document.getElementById('area-alert').style.display = 'none';

    try {
      const r = JSON.parse(localStorage.getItem('rapidaid_roads_geojson') || '{}');
      const p = JSON.parse(localStorage.getItem('rapidaid_places_geojson') || '{}');
      state.roadsGeoJson = r;
      state.placesGeoJson = p;
    } catch (e) {}
  }

  ensureLeafletMap();
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
    await callBridge('logout');
    sessionStorage.removeItem('rapidaid_token');
    state.user = null;
    state.role = null;
    stopAlertsPolling();
    showAuthView('Logged out successfully.');
  });
}

function showSection(sectionId) {
  state.activeSection = sectionId;
  const sections = ['area', 'incidents', 'stats'];
  sections.forEach(s => {
    const el = document.getElementById('section-' + s);
    const nav = document.getElementById('nav-' + s);
    if (el) el.style.display = (s === sectionId) ? 'block' : 'none';
    if (nav) nav.style.fontWeight = (s === sectionId) ? 'bold' : 'normal';
  });

  if (sectionId === 'area') {
    ensureLeafletMap();
    if (state.roadsGeoJson && state.placesGeoJson) {
      renderGeoJsonOnLeaflet(state.roadsGeoJson, state.placesGeoJson);
    }
  }

  // Manage dispatcher alert polling (C3)
  if (sectionId === 'incidents') {
    startAlertsPolling();
    refreshUnitSelect();
  } else {
    stopAlertsPolling();
  }
}

// ==========================================
// 7. Dispatcher Developer Test Alert (C3)
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
  select.textContent = ''; // Clear options

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
  tbody.textContent = ''; // Clear rows

  if (res && res.ok && res.alerts && res.alerts.length > 0) {
    // Show newest first
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
// 8. Incident Report Queue (Mock/Legacy)
// ==========================================
const incidentStore = [];

function initIncidentEvents() {
  const btnSubmit = document.getElementById('btn-submit-incident');
  if (!btnSubmit) return;
  const form = document.getElementById('form-incident');
  const btnRefresh = document.getElementById('btn-refresh-incidents');
  const feedback = document.getElementById('incident-feedback');

  btnSubmit.addEventListener('click', () => {
    const title = document.getElementById('inc-title').value.trim();
    const location = document.getElementById('inc-location').value.trim();
    const type = document.getElementById('inc-type').value;
    const priority = document.getElementById('inc-priority').value;
    const notes = document.getElementById('inc-notes').value.trim();

    if (!title || !location) {
      feedback.textContent = 'Please fill out both Incident Title and Location.';
      feedback.style.color = '#d9534f';
      return;
    }

    const inc = {
      id: 'INC-' + (incidentStore.length + 101),
      title,
      location,
      type,
      priority,
      timestamp: new Date().toISOString().replace('T', ' ').substring(0, 19),
      status: 'Pending Dispatch',
      notes
    };

    incidentStore.unshift(inc);
    renderIncidentsTable();

    feedback.textContent = `Incident ${inc.id} recorded in queue (Backend pending)`;
    feedback.style.color = '#28a745';
    form.reset();
  });

  btnRefresh.addEventListener('click', renderIncidentsTable);
}

function renderIncidentsTable() {
  const tbody = document.getElementById('incidents-body');
  if (!tbody) return;

  tbody.textContent = ''; // Clear cleanly with DOM

  if (incidentStore.length === 0) {
    const tr = document.createElement('tr');
    const td = document.createElement('td');
    td.colSpan = 8;
    td.style.textAlign = 'center';
    td.style.color = '#888';
    td.textContent = 'No active emergency incidents logged. Use the form to file a report.';
    tr.appendChild(td);
    tbody.appendChild(tr);
    return;
  }

  incidentStore.forEach(inc => {
    const tr = document.createElement('tr');

    const tdId = document.createElement('td');
    const stId = document.createElement('strong');
    stId.textContent = inc.id;
    tdId.appendChild(stId);

    const tdTime = document.createElement('td');
    tdTime.textContent = inc.timestamp;

    const tdTitle = document.createElement('td');
    tdTitle.textContent = inc.title;

    const tdLoc = document.createElement('td');
    tdLoc.textContent = inc.location;

    const tdType = document.createElement('td');
    tdType.textContent = inc.type;

    const tdPrio = document.createElement('td');
    const spPrio = document.createElement('span');
    spPrio.textContent = inc.priority;
    spPrio.style.fontWeight = 'bold';
    spPrio.style.color = (inc.priority === 'Critical') ? '#d9534f' :
                         (inc.priority === 'High') ? '#f0ad4e' : '#5bc0de';
    tdPrio.appendChild(spPrio);

    const tdStat = document.createElement('td');
    tdStat.textContent = inc.status;

    const tdAct = document.createElement('td');
    const btnDisp = document.createElement('button');
    btnDisp.style.fontSize = '11px';
    btnDisp.textContent = 'Dispatch Unit';
    btnDisp.onclick = () => alert('Dispatch routing engine is under active development.');
    tdAct.appendChild(btnDisp);

    tr.appendChild(tdId);
    tr.appendChild(tdTime);
    tr.appendChild(tdTitle);
    tr.appendChild(tdLoc);
    tr.appendChild(tdType);
    tr.appendChild(tdPrio);
    tr.appendChild(tdStat);
    tr.appendChild(tdAct);
    tbody.appendChild(tr);
  });

  const totalMetric = document.getElementById('metric-total');
  if (totalMetric) totalMetric.textContent = incidentStore.length;
}

// Global hook for unit.js
window.RapidAidApp = {
  showAuthView
};

```

## include/Ambulance.hpp

```cpp
#pragma once

#include <string>

namespace rapidaid::service {

class Ambulance {
public:
    enum class Status { Available, Dispatched, Unavailable };

    Ambulance(int id, std::string name, double latitude, double longitude,
              int homeStationId, int paramedicCount);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    Status getStatus() const;
    int getCurrentIncidentId() const;
    int getHomeStationId() const;
    int getParamedicCount() const;
    bool isAvailable() const;
    bool assign(int incidentId);
    void release();
    void markUnavailable();
    static std::string statusToString(Status status);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    Status status = Status::Available;
    int currentIncidentId = -1;
    int homeStationId;
    int paramedicCount;
};

}

```

## include/Config.example.hpp

```cpp
// Copy this file to Config.local.hpp, set kContact to your own contact,
// and never commit Config.local.hpp.
// The contact is sent to OpenStreetMap services in the User-Agent header.

#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace rapidaid::config {
    constexpr const char* kContact = "<your project URL or contact>";
}

#endif // CONFIG_HPP


```

## include/DataCache.hpp

```cpp
#ifndef RAPIDAID_DATA_DATACACHE_HPP
#define RAPIDAID_DATA_DATACACHE_HPP

#include "Geocoder.hpp"
#include "OverpassClient.hpp"
#include "RoadClient.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rapidaid::data {

class DataCache {
public:
    // Constructs a DataCache instance targeting the given directory and refresh policy.
    // Failure behavior: Never throws.
    explicit DataCache(std::string dir = "data", bool refresh = false);

    // Geocodes a free-text query via Nominatim, using cache if valid and refresh is false.
    // Failure behavior: Returns std::nullopt on empty query, network failure, or parse failure. Never throws.
    std::optional<rapidaid::geo::Location> geocode(const std::string& query);

    // Finds places of category c within radiusMeters of (lat, lon) via Overpass, using cache if valid and refresh is false.
    // Failure behavior: Returns an empty vector on invalid input, network failure, or parse failure. Never throws.
    std::vector<rapidaid::osm::Place> places(rapidaid::osm::Category c, double lat, double lon, int radiusMeters);

    // Fetches drivable road network within radiusMeters of (lat, lon) via Overpass, using cache if valid and refresh is false.
    // Failure behavior: Returns an empty RoadData on invalid input, network failure, or parse failure. Never throws.
    rapidaid::osm::RoadData roads(double lat, double lon, int radiusMeters);

    // Computes the cache file path for a geocode query:
    // dir + "/geocode_" + FileStore::sanitizeFileName(query) + ".json"
    // Failure behavior: Falls back to sanitized name if query is empty; never throws.
    std::string geocodePath(const std::string& query) const;

    // Computes the cache file path for a places query:
    // dir + "/places_<categoryName lowercase>_<lat4>_<lon4>_<radius>.json"
    // Failure behavior: Formats coordinates with 4 decimals in classic locale; never throws.
    std::string placesPath(rapidaid::osm::Category c, double lat, double lon, int radiusMeters) const;

    // Computes the cache file path for a roads query:
    // dir + "/roads_<lat4>_<lon4>_<radius>.json"
    // Failure behavior: Formats coordinates with 4 decimals in classic locale; never throws.
    std::string roadsPath(double lat, double lon, int radiusMeters) const;

private:
    std::string m_dir;
    bool m_refresh;

    template <typename Result, typename Parser, typename Fetcher, typename IsEmpty>
    Result getCachedOrFetch(const std::string& path,
                            Parser&& parser,
                            Fetcher&& fetcher,
                            IsEmpty&& isEmpty);
};

} // namespace rapidaid::data

#endif // RAPIDAID_DATA_DATACACHE_HPP

```

## include/FileStore.hpp

```cpp
#ifndef RAPIDAID_IO_FILESTORE_HPP
#define RAPIDAID_IO_FILESTORE_HPP

#include <string>

namespace rapidaid::io {

class FileStore {
public:
    // Saves text to path atomically via a temporary file (path + ".tmp").
    // Creates parent directories if the parent path exists and is not empty.
    // Overwrites existing target file if present.
    // Failure behavior: returns false on any failure (e.g. directory creation, write, rename). Never throws.
    static bool save(const std::string& path, const std::string& text);

    // Reads the entire file as a binary string.
    // Failure behavior: returns an empty string ("") if the file is missing, unreadable, or on any error. Never throws.
    static std::string load(const std::string& path);

    // Checks if path exists and is a regular file.
    // Failure behavior: returns false if the file does not exist, is a directory, or on any filesystem error. Never throws.
    static bool exists(const std::string& path);

    // Pure function to sanitize arbitrary text into a safe filename.
    // Converts to lowercase, replaces characters outside [a-z0-9] with '_',
    // collapses repeated underscores into a single '_', trims leading/trailing '_',
    // and caps the result at 80 characters.
    // Failure behavior: returns "unnamed" if the sanitized string would be empty. Never throws.
    static std::string sanitizeFileName(const std::string& text);
};

} // namespace rapidaid::io

#endif // RAPIDAID_IO_FILESTORE_HPP

```

## include/Fire_station.hpp

```cpp
#pragma once

#include "Fire_truck.hpp"

#include <string>
#include <vector>

namespace rapidaid::service {

class FireStation {
public:
    FireStation(int id, std::string name, double latitude, double longitude);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    void addTruck(FireTruck truck);
    std::vector<FireTruck*> getAvailableTrucks();
    FireTruck* findTruckById(int id);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    std::vector<FireTruck> trucks;
};

}

```

## include/Fire_truck.hpp

```cpp
#pragma once

#include <string>

namespace rapidaid::service {

class FireTruck {
public:
    enum class Status { Available, Dispatched, Unavailable };

    FireTruck(int id, std::string name, double latitude, double longitude,
              int homeStationId, int waterCapacityLitres);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    Status getStatus() const;
    int getCurrentIncidentId() const;
    int getHomeStationId() const;
    int getWaterCapacityLitres() const;
    bool isAvailable() const;
    bool assign(int incidentId);
    void release();
    void markUnavailable();
    static std::string statusToString(Status status);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    Status status = Status::Available;
    int currentIncidentId = -1;
    int homeStationId;
    int waterCapacityLitres;
};

}
```

## include/Geocoder.hpp

```cpp
#ifndef RAPIDAID_GEO_GEOCODER_HPP
#define RAPIDAID_GEO_GEOCODER_HPP

#include <string>
#include <optional>

namespace rapidaid::geo {
    struct Location {
        double lat;
        double lon;
        std::string displayName;
    };

    class Geocoder {
    public:
        // Geocodes a free-text query via Nominatim. Returns std::nullopt on network failure, parse failure, or no results.
        static std::optional<Location> geocode(const std::string& query);

        // Fetches Nominatim JSON body for a query. Returns the raw Nominatim JSON body, or "" on failure.
        // Rejects empty or whitespace-only queries without making a network call.
        static std::string fetchRaw(const std::string& query);

        // Pure function, no network. Parses a Nominatim format=json response body (a JSON array).
        // Returns nullopt on empty string, invalid JSON, empty array, or missing/non-numeric lat/lon.
        static std::optional<Location> parseResponse(const std::string& body);

        // Pure function. Builds the full request URL for a query (uses HttpClient::urlEncode), with format=json and limit=1.
        static std::string buildUrl(const std::string& query);

    private:
        // Sleeps so >= 1 second passes between geocode() calls; uses a function-local static std::chrono::steady_clock time_point.
        static void enforceRateLimit();
    };
}

#endif // RAPIDAID_GEO_GEOCODER_HPP

```

## include/GeoJsonWriter.hpp

```cpp
#ifndef RAPIDAID_GEOJSON_GEOJSONWRITER_HPP
#define RAPIDAID_GEOJSON_GEOJSONWRITER_HPP

#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "OverpassClient.hpp"

#include <string>
#include <vector>

namespace rapidaid::geojson {

class GeoJsonWriter {
public:
    // Serializes a vector of places into an RFC 7946 GeoJSON FeatureCollection string.
    // Each place is converted to a Point feature with coordinates [lon, lat] rounded to 7 decimals,
    // and properties: id, osmType, name, category, and tags.
    // Failure behavior: Returns a FeatureCollection with an empty features array if places is empty. Never throws.
    static std::string placesCollection(const std::vector<rapidaid::osm::Place>& places);

    // Serializes a vector of snapped places into an RFC 7946 GeoJSON FeatureCollection string.
    // Each snapped place is converted to a Point feature with coordinates [lon, lat] rounded to 7 decimals,
    // and properties: id, osmType, name, category, tags, snapped, and snapDistanceMeters.
    // Failure behavior: Returns a FeatureCollection with an empty features array if places is empty. Never throws.
    static std::string snappedPlacesCollection(const std::vector<rapidaid::graph::SnappedPlace>& places);

    // Serializes a path on graph g into an RFC 7946 GeoJSON FeatureCollection string.
    // Emits a single LineString feature through the path nodes in order, with properties distanceMeters and nodeCount.
    // Failure behavior: Returns a FeatureCollection with an empty features array if path.found is false,
    // the path has fewer than 2 nodes, or any node index is out of range for g. Never throws.
    static std::string pathCollection(const rapidaid::graph::Graph& g, const rapidaid::graph::PathResult& path);

    // Serializes all undirected edges in graph g into an RFC 7946 GeoJSON FeatureCollection string.
    // Emits every undirected edge exactly once as a 2-point LineString feature with property weightMeters.
    // Failure behavior: Returns a FeatureCollection with an empty features array if g is empty or has no edges. Never throws.
    static std::string roadsCollection(const rapidaid::graph::Graph& g);

    // Writes a GeoJSON string to path atomically via FileStore::save.
    // Creates parent directories if needed.
    // Failure behavior: Returns false if saving fails (e.g. invalid path or write error). Never throws.
    static bool writeFile(const std::string& path, const std::string& geojson);
};

} // namespace rapidaid::geojson

#endif // RAPIDAID_GEOJSON_GEOJSONWRITER_HPP

```

## include/GeoMath.hpp

```cpp
#ifndef RAPIDAID_GEO_GEOMATH_HPP
#define RAPIDAID_GEO_GEOMATH_HPP

namespace rapidaid::geo {

// Computes the great-circle distance in meters between two coordinates (lat1, lon1) and (lat2, lon2)
// using the Haversine formula with Earth radius R = 6371000.0 meters.
// Handles antimeridian crossings by wrapping longitude differences to [-pi, pi].
// Failure behavior: returns 0.0 for identical coordinates. Never throws.
double haversineMeters(double lat1, double lon1, double lat2, double lon2);

} // namespace rapidaid::geo

#endif // RAPIDAID_GEO_GEOMATH_HPP

```

## include/Graph.hpp

```cpp
#ifndef RAPIDAID_GRAPH_GRAPH_HPP
#define RAPIDAID_GRAPH_GRAPH_HPP

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace rapidaid::graph {

struct Edge {
    int to;
    double weightMeters;
};

struct PathResult {
    bool found = false;
    double distanceMeters = 0.0;
    std::vector<int> nodes;
};

class Graph {
public:
    Graph() = default;

    // Adds a node with the given osmId and coordinates.
    // Failure behavior: If osmId already exists in the graph, returns the existing
    // node index without updating coordinates or adding a duplicate.
    int addNode(long long osmId, double lat, double lon);

    // Adds an undirected edge between node indices a and b with the given weight in meters.
    // Failure behavior: Ignores negative weights, self-loops (a == b), and out-of-range
    // node indices (does nothing). If an edge between a and b already exists, retains
    // the smaller weight.
    void addEdge(int a, int b, double weightMeters);

    // Returns the total number of nodes in the graph. Never fails (returns 0 if empty).
    int nodeCount() const;

    // Returns the total number of undirected edges in the graph, counting each edge once.
    // Never fails (returns 0 if empty).
    size_t edgeCount() const;

    // Returns the outgoing edges from the node at index.
    // Failure behavior: Returns a reference to an empty vector if index is out of range.
    const std::vector<Edge>& neighbors(int index) const;

    // Returns the OSM ID of the node at index.
    // Failure behavior: Returns 0 if index is out of range.
    long long osmId(int index) const;

    // Returns the latitude of the node at index.
    // Failure behavior: Returns 0.0 if index is out of range.
    double lat(int index) const;

    // Returns the longitude of the node at index.
    // Failure behavior: Returns 0.0 if index is out of range.
    double lon(int index) const;

    // Returns the dense node index for the given OSM ID.
    // Failure behavior: Returns -1 if osmId does not exist in the graph.
    int indexOf(long long osmId) const;

    // Finds the nearest node to (lat, lon) using haversine distance.
    // Failure behavior: Returns -1 if the graph is empty (distanceMetersOut is unchanged).
    // If distanceMetersOut is non-null and graph is non-empty, stores distance in meters.
    int nearestNode(double lat, double lon, double* distanceMetersOut = nullptr) const;

    // Computes the shortest path from node 'from' to node 'to' using Dijkstra's algorithm.
    // Failure behavior: Returns found=false, distanceMeters=0.0, and empty nodes vector
    // if from or to is out of range or if no path exists between them.
    // If from == to and index is valid, returns found=true, distanceMeters=0.0, nodes={from}.
    PathResult shortestPath(int from, int to) const;

    // Returns a new Graph containing only the largest connected component, preserving
    // OSM IDs and coordinates. Uses an iterative traversal.
    // Failure behavior: Returns an empty Graph if this graph is empty.
    // In case of a size tie, selects the component containing the lowest node index.
    Graph largestComponent() const;

private:
    struct NodeData {
        long long osmId = 0;
        double lat = 0.0;
        double lon = 0.0;
        std::vector<Edge> edges;
    };

    std::vector<NodeData> m_nodes;
    std::unordered_map<long long, int> m_osmIdToIndex;
    size_t m_edgeCount = 0;
};

} // namespace rapidaid::graph

#endif // RAPIDAID_GRAPH_GRAPH_HPP

```

## include/GraphBuilder.hpp

```cpp
#ifndef RAPIDAID_GRAPH_GRAPH_BUILDER_HPP
#define RAPIDAID_GRAPH_GRAPH_BUILDER_HPP

#include "Graph.hpp"
#include "RoadClient.hpp"
#include "OverpassClient.hpp"

#include <vector>

namespace rapidaid::graph {

struct SnappedPlace {
    rapidaid::osm::Place place;
    int nodeIndex;
    double snapDistanceMeters;
    bool snapped;
};

class GraphBuilder {
public:
    // Builds a Graph from RoadData.
    // For every way, for each consecutive node id pair (a, b): if both exist in roads.nodes,
    // adds both nodes and an undirected edge weighted by haversineMeters.
    // Failure behavior: If either id is missing, skips only that segment and continues with the rest of the way.
    // Returns an empty Graph if roads is empty or contains no valid drivable segments.
    // If keepLargestComponent is true, retains only the largest connected component;
    // if false, retains all components.
    static Graph build(const rapidaid::osm::RoadData& roads, bool keepLargestComponent = true);

    // Snaps each place to its nearest node in the graph within maxSnapMeters.
    // Failure behavior: If the graph is empty or no node is within maxSnapMeters,
    // sets snapped = false and nodeIndex = -1. Otherwise sets snapped = true,
    // nodeIndex to the nearest node index, and snapDistanceMeters to the distance in meters.
    // Preserves the input order of places.
    static std::vector<SnappedPlace> snapPlaces(const Graph& graph,
                                                const std::vector<rapidaid::osm::Place>& places,
                                                double maxSnapMeters = 500.0);
};

} // namespace rapidaid::graph

#endif // RAPIDAID_GRAPH_GRAPH_BUILDER_HPP

```

## include/Hospital.hpp

```cpp
#pragma once

#include "Ambulance.hpp"

#include <string>
#include <vector>

namespace rapidaid::service {

class Hospital {
public:
    Hospital(int id, std::string name, double latitude, double longitude,
             int totalBeds, int occupiedBeds = 0);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);
    int getTotalBeds() const;
    int getOccupiedBeds() const;

    void addAmbulance(Ambulance ambulance);
    std::vector<Ambulance*> getAvailableAmbulances();
    int availableBeds() const;
    bool admitPatients(int count);
    void dischargePatients(int count);
    Ambulance* findAmbulanceById(int id);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    int totalBeds;
    int occupiedBeds;
    std::vector<Ambulance> ambulances;
};

}
```

## include/HttpClient.hpp

```cpp
#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <cstddef>

namespace rapidaid::net{
    class HttpClient{
        public:
            // returns the percent-encoded form of text, or "" on failure.
            static std::string urlEncode(const std::string& text);
            static std::string get(const std::string& url);
            static std::string post(const std::string& url, const std::string& data);
        private:
            static std::string performRequest(const std::string& url, const std::string* body = nullptr);
            static std::size_t writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp);
    };
}

#endif
```

## include/Login.hpp

```cpp
#ifndef LOGIN_HPP
#define LOGIN_HPP

#include <cstddef>
#include <string>
#include <unordered_map>

/**
 * @class Logger
 * @brief Simple timestamped event logger for login/logout actions.
 */
class Logger {
    std::string filename;
public:
    /**
     * @brief Constructs a Logger with a target output file.
     * @param f File path to append log records to (defaults to "log.txt").
     * Failure behavior: Does not fail; initializes filename member with given path.
     */
    Logger(std::string f = "log.txt");

    /**
     * @brief Appends a timestamped log entry in the format: "<timestamp> | <type> | <details>\n".
     * @param type Event category (e.g., "LOGIN_SUCCESS", "LOGIN_FAILED", "LOGOUT").
     * @param details Additional context or username associated with the event.
     * Failure behavior: If the file cannot be opened or written to, the write fails silently
     *                    without throwing exceptions.
     */
    void log(std::string type, std::string details);
};

/**
 * @struct User
 * @brief Internal user record storing password hash and role designation.
 */
struct User {
    std::size_t passHash;
    std::string role;
};

/**
 * @class Auth
 * @brief Manages authentication, user storage, and credential validation.
 */
class Auth {
    std::unordered_map<std::string, User> users;
    std::string filename;

    /**
     * @brief Computes a std::hash for the given password string.
     * Failure behavior: Does not fail; returns std::size_t hash code.
     */
    std::size_t hashPass(const std::string& p);

public:
    /**
     * @brief Constructs an Auth instance and loads users from disk.
     *        If no users are found after loading, automatically creates default "admin" account.
     * @param f File path for user credentials storage (defaults to "users.txt").
     * Failure behavior: If the file cannot be opened or read, users remains empty,
     *                    and the default admin account ("admin", "admin123", "admin") is created and saved.
     */
    Auth(std::string f = "users.txt");

    /**
     * @brief Loads user records from the configured file.
     * Failure behavior: If the file cannot be opened or data is malformed, reading ceases;
     *                    any users loaded prior to the error remain in memory.
     */
    void load();

    /**
     * @brief Persists all user records to the configured file.
     * Failure behavior: If the file cannot be opened for writing, no data is written;
     *                    fails silently without throwing exceptions.
     */
    void save();

    /**
     * @brief Registers a new user with the specified credentials and role.
     * @param u Desired username.
     * @param p Plaintext password.
     * @param role User role designation (e.g., "admin", "dispatcher").
     * @return true if the user was successfully registered; false if the username already exists.
     * Failure behavior: Returns false if the username is already registered in the users map.
     */
    bool signUp(std::string u, std::string p, std::string role);

    /**
     * @brief Validates login credentials against existing user records.
     * @param u Username to verify.
     * @param p Plaintext password to verify against stored hash.
     * @return true if credentials match; false otherwise.
     * Failure behavior: Returns false if username is not found or password hash does not match.
     */
    bool login(std::string u, std::string p);

    /**
     * @brief Retrieves the role associated with the specified username.
     * @param u Username to query.
     * @return The user's role string.
     * Failure behavior: If the username does not exist in the map, accessing users[u] default-inserts
     *                    an entry with hash 0 and returns an empty string ("").
     */
    std::string getRole(std::string u);
};

/**
 * @brief Interactive dispatcher console menu.
 * @param auth Reference to active Auth instance.
 * @param logger Reference to active Logger instance.
 * @param user Name of the authenticated user.
 * Failure behavior: On invalid input choices, prints an error message to stdout and reprompts.
 */
void dispatcherMenu(Auth& auth, Logger& logger, std::string user);

/**
 * @brief Processes a JSON-formatted login request and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param requestJson JSON payload formatted as: {"username":"..","password":".."}
 * @return JSON response string:
 *         Success: {"ok":true,"username":"..","role":".."}
 *         Auth Failure: {"ok":false,"error":"Invalid username or password"}
 *         Bad Request:  {"ok":false,"error":"Bad request"}
 * Failure behavior: Never throws. Returns {"ok":false,"error":"Bad request"} on malformed JSON,
 *                    non-object JSON, missing "username"/"password", or non-string values.
 *                    Returns {"ok":false,"error":"Invalid username or password"} if credentials
 *                    do not match. Logs attempts via Logger without logging passwords.
 */
std::string handleLoginRequest(Auth& auth, const std::string& requestJson);

/**
 * @brief Processes a logout notification for a user and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param username Username of the logging-out user.
 * @return JSON response string: {"ok":true}
 * Failure behavior: Never throws. Always returns {"ok":true}. Logs the logout event via Logger.
 */
std::string handleLogoutRequest(Auth& auth, const std::string& username);

/**
 * @brief Health check probe for web UI / HTTP server integration.
 * @return JSON response string: {"ok":true,"service":"login"}
 * Failure behavior: Never throws. Always returns {"ok":true,"service":"login"}.
 */
std::string handleHealthRequest();

/**
 * @brief Processes a JSON-formatted signup request and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param requestJson JSON payload formatted as: {"username":"..","password":"..","role":".."}
 * @return JSON response string:
 *         Success: {"ok":true,"username":"..","role":".."}
 *         Username exists: {"ok":false,"error":"Username already exists"}
 *         Bad Request:  {"ok":false,"error":"Bad request"}
 * Failure behavior: Never throws. Returns {"ok":false,"error":"Bad request"} on malformed JSON,
 *                    non-object JSON, missing "username"/"password", empty strings, or non-string values.
 *                    Returns {"ok":false,"error":"Username already exists"} if user already exists.
 *                    Logs attempts via Logger without logging passwords.
 */
std::string handleSignUpRequest(Auth& auth, const std::string& requestJson);

#endif // LOGIN_HPP

```

## include/OverpassClient.hpp

```cpp
#ifndef OVERPASS_CLIENT_HPP
#define OVERPASS_CLIENT_HPP

#include <string>
#include <vector>
#include <map>

namespace rapidaid::osm {

enum class Category {
    Hospital,
    Clinic,
    Pharmacy,
    FireStation,
    Police,
    Shelter,
    Water
};

struct Place {
    long long id;
    std::string osmType;
    double lat;
    double lon;
    std::string name;
    std::map<std::string, std::string> tags;
    Category category;
};

class OverpassClient {
public:
    // Finds places of a category within radiusMeters of (lat, lon).
    // Returns an empty vector on failure (invalid input, network failure, or JSON parse error).
    // Validates radius (1..50000), lat ([-90, 90]), and lon ([-180, 180]).
    // Retries up to 3 times total with exponential backoff (2s, 4s) when HttpClient returns "" (504/timeouts).
    static std::vector<Place> findNearby(Category category, double lat, double lon, int radiusMeters);

    // Fetches raw Overpass JSON response body.
    // Validates radius (1..50000), lat ([-90, 90]), and lon ([-180, 180]); returns "" on invalid input.
    // Retries up to 3 times total with exponential backoff (2s, 4s) when HttpClient returns "".
    // Returns the first non-empty body, or "" if all attempts fail.
    static std::string fetchRaw(Category category, double lat, double lon, int radiusMeters);

    // Pure functions, no network:

    // Builds the Overpass QL query string for the requested category, location, and radius.
    // Query format uses [out:json][timeout:25] and around filters for node, way, relation.
    static std::string buildQuery(Category category, double lat, double lon, int radiusMeters);

    // Formats query into form-encoded POST body: "data=" + HttpClient::urlEncode(query).
    // Returns "" if urlEncode fails.
    static std::string buildRequestBody(const std::string& query);

    // Parses Overpass JSON response body.
    // Returns an empty vector on empty string, invalid JSON, missing elements, or parse error.
    static std::vector<Place> parseResponse(const std::string& body, Category category);

    // Returns the canonical name string of the category (e.g. "hospital", "clinic").
    static std::string categoryName(Category category);
};

} // namespace rapidaid::osm

#endif // OVERPASS_CLIENT_HPP

```

## include/Police_station.hpp

```cpp
#pragma once

#include "Police_unit.hpp"

#include <string>
#include <vector>

namespace rapidaid::service {

class PoliceStation {
public:
    PoliceStation(int id, std::string name, double latitude, double longitude);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    void addUnit(PoliceUnit unit);
    std::vector<PoliceUnit*> getAvailableUnits();
    PoliceUnit* findUnitById(int id);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    std::vector<PoliceUnit> units;
};

}

```

## include/Police_unit.hpp

```cpp
#pragma once

#include <string>

namespace rapidaid::service {

class PoliceUnit {
public:
    enum class Status { Available, Dispatched, Unavailable };

    PoliceUnit(int id, std::string name, double latitude, double longitude,
               int homeStationId, int officerCount);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    Status getStatus() const;
    int getCurrentIncidentId() const;
    int getHomeStationId() const;
    int getOfficerCount() const;
    bool isAvailable() const;
    bool assign(int incidentId);
    void release();
    void markUnavailable();
    static std::string statusToString(Status status);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    Status status = Status::Available;
    int currentIncidentId = -1;
    int homeStationId;
    int officerCount;
};

}
```

## include/RoadClient.hpp

```cpp
#ifndef RAPIDAID_OSM_ROAD_CLIENT_HPP
#define RAPIDAID_OSM_ROAD_CLIENT_HPP

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace rapidaid::osm {

struct RoadNode {
    long long id;
    double lat;
    double lon;
};

struct RoadWay {
    long long id;
    std::vector<long long> nodeIds;
    std::string highway;
    std::string name;
    std::map<std::string, std::string> tags;
};

struct RoadData {
    std::unordered_map<long long, RoadNode> nodes;
    std::vector<RoadWay> ways;
    bool empty() const; // empty() true when there are no ways (i.e. ways.empty())
};

class RoadClient {
public:
    // Builds Overpass QL query string for drivable roads within radiusMeters of (lat, lon).
    // Validates radius (1..5000), lat ([-90, 90]), lon ([-180, 180]).
    // Returns "" on invalid input.
    static std::string buildRoadQuery(double lat, double lon, int radiusMeters);

    // Formats query into form-encoded POST body: "data=" + HttpClient::urlEncode(query).
    // Returns "" if urlEncode fails on non-empty query.
    static std::string buildRequestBody(const std::string& query);

    // Fetches raw response from Overpass API.
    // Validates radius (1..5000), lat ([-90, 90]), lon ([-180, 180]); invalid -> "" with no network call.
    // Retries up to 3 attempts with 2s and 4s sleep between attempts.
    // Returns the first non-empty response body, or "" if all attempts fail.
    static std::string fetchRaw(double lat, double lon, int radiusMeters);

    // Parses Overpass JSON response body.
    // Returns an empty RoadData on empty string, invalid JSON, or missing elements.
    static RoadData parseRoads(const std::string& body);

    // Checks whether tags describe a drivable road according to highway and access restrictions.
    // Returns false if highway tag is missing, not drivable, or restricted.
    static bool isDrivable(const std::map<std::string, std::string>& tags);
};

} // namespace rapidaid::osm

#endif // RAPIDAID_OSM_ROAD_CLIENT_HPP

```

## include/Shelter.hpp

```cpp
#pragma once

#include <string>

namespace rapidaid::service {

class Shelter {
public:
    Shelter(int id, std::string name, double latitude, double longitude,
            int capacity, int occupied = 0);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);
    int getCapacity() const;
    int getOccupied() const;

    int availableSpace() const;
    bool admit(int count);
    void leave(int count);
    bool isFull() const;

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    int capacity;
    int occupied;
};

}
```

## include/UiBridge.hpp

```cpp
#ifndef RAPIDAID_UI_UIBRIDGE_HPP
#define RAPIDAID_UI_UIBRIDGE_HPP

#include "DataCache.hpp"
#include "Login.hpp"

#include <string>

namespace rapidaid::ui {

/**
 * @brief Checks if permanent GeoJSON files exist in the specified output directory.
 * @param outDir Directory path where permanent GeoJSON files are stored (default "data/out").
 * @return true if either roads.geojson or places.geojson exists and is non-empty; false otherwise.
 * Failure behavior: Returns false if the directory does not exist or files cannot be accessed. Never throws.
 */
bool checkGeoJsonExists(const std::string& outDir = "data/out");

/**
 * @brief Returns JSON status of permanent GeoJSON files.
 * @param outDir Directory path where permanent GeoJSON files are stored (default "data/out").
 * @return JSON response string: {"ok":true,"exists":bool,"roadsExists":bool,"placesExists":bool,"roadsFile":string,"placesFile":string}
 * Failure behavior: Never throws; returns valid JSON even if paths do not exist.
 */
std::string handleAreaStatusRequest(const std::string& outDir = "data/out");

/**
 * @brief Geocodes an area query string into coordinates and display name.
 * @param cache Reference to initialized DataCache.
 * @param requestJson JSON payload: {"query":"Paris"} or {"name":"Paris"}
 * @return JSON response string:
 *         Success: {"ok":true,"query":"..","lat":..,"lon":..,"displayName":".."}
 *         Failure: {"ok":false,"error":"Location not found" or "Bad request"}
 * Failure behavior: Never throws; returns error JSON on malformed JSON or geocoding miss.
 */
std::string handleGeocodeRequest(rapidaid::data::DataCache& cache, const std::string& requestJson);

/**
 * @brief Executes the full area pipeline: geocode, fetch places, fetch roads, build graph, snap places, and save GeoJSON.
 * @param cache Reference to initialized DataCache.
 * @param requestJson JSON payload: {"name":"..", "radius":..., "category":"...", optional "lat":..., "lon":...}
 * @param outDir Directory path where output GeoJSON files will be permanently saved (default "data/out").
 * @return JSON response string with operation stats and generated GeoJSON.
 * Failure behavior: Never throws; returns {"ok":false,"error":"..."} on invalid inputs or pipeline failures.
 */
std::string handleCreateAreaRequest(rapidaid::data::DataCache& cache, const std::string& requestJson, const std::string& outDir = "data/out");

/**
 * @brief Reads and returns stored GeoJSON file content.
 * @param type Layer type ("roads" or "places").
 * @param outDir Directory path where output GeoJSON files are stored.
 * @return Raw GeoJSON string or error JSON.
 * Failure behavior: Never throws; returns {"ok":false,"error":"File not found"} if missing.
 */
std::string handleGetGeoJsonRequest(const std::string& type, const std::string& outDir = "data/out");

/**
 * @brief Returns incident report records and under-development notice.
 * @return JSON response string.
 * Failure behavior: Never throws.
 */
std::string handleGetIncidentsRequest();

/**
 * @brief Records a new incident report (mock backend with in-memory persistence).
 * @param requestJson JSON payload describing the incident.
 * @return JSON response string confirming receipt.
 * Failure behavior: Never throws; returns {"ok":false,"error":"Bad request"} on malformed JSON.
 */
std::string handleAddIncidentRequest(const std::string& requestJson);

/**
 * @brief Returns past dispatch statistics and under-development notice.
 * @return JSON response string.
 * Failure behavior: Never throws.
 */
std::string handleGetStatsRequest();

/**
 * @brief Unified router processing any frontend bridge command.
 * @param auth Reference to active Auth instance.
 * @param cache Reference to active DataCache instance.
 * @param messageJson JSON payload formatted as: {"action":"login"|"signup"|"logout"|"health"|"area_status"|"geocode"|"create_area"|"get_geojson"|"get_incidents"|"add_incident"|"get_stats", "payload":{...}}
 * @param outDir Output directory for permanent GeoJSON.
 * @return JSON response string for the requested action.
 * Failure behavior: Never throws; returns {"ok":false,"error":"Unknown action"} if action is unhandled.
 */
std::string handleBridgeMessage(Auth& auth, rapidaid::data::DataCache& cache, const std::string& messageJson, const std::string& outDir = "data/out");

/**
 * @brief Checks if a token is valid and possesses the required role privilege.
 * @param token Session token string.
 * @param requiredRole Required role name (e.g. "dispatcher", "admin", "unit").
 * @return true if token is valid and user has appropriate role; false otherwise.
 * Failure behavior: Returns false if token is unknown, empty, or role is insufficient. Never throws.
 */
bool isTokenAuthorized(const std::string& token, const std::string& requiredRole);

/**
 * @brief Resets all in-memory bridge state including sessions, alerts, and unit registry.
 * Failure behavior: Never throws.
 */
void resetBridgeState();

} // namespace rapidaid::ui

#endif // RAPIDAID_UI_UIBRIDGE_HPP

```

## include/UnitRegistry.hpp

```cpp
#ifndef RAPIDAID_UI_UNITREGISTRY_HPP
#define RAPIDAID_UI_UNITREGISTRY_HPP

#include "Ambulance.hpp"
#include "Fire_station.hpp"
#include "Fire_truck.hpp"
#include "Hospital.hpp"
#include "Police_station.hpp"
#include "Police_unit.hpp"
#include "Shelter.hpp"
#include "json.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rapidaid::ui {

/**
 * @struct UnitProfile
 * @brief Core profile configuration of a registered emergency service unit.
 */
struct UnitProfile {
    std::string username;
    std::string kind;
    std::string name;
    double lat{0.0};
    double lon{0.0};
    int count{0};
};

/**
 * @struct SubUnitInfo
 * @brief Metadata tracking child units (e.g., ambulances owned by hospitals, trucks by fire stations).
 */
struct SubUnitInfo {
    int id{0};
    std::string name;
    int count{0};
    bool available{true};
};

/**
 * @struct Alert
 * @brief Represents an emergency dispatch alert assigned to a unit.
 */
struct Alert {
    int id{0};
    std::string toUser;
    std::string type{"Medical"};
    int severity{3};
    int peopleAffected{0};
    std::string locationText;
    std::optional<double> lat;
    std::optional<double> lon;
    std::string note;
    int64_t createdAt{0};
    int ackSeconds{60};
    std::string state{"Pending"}; // Pending, Accepted, Declined, EnRoute, OnScene, Transporting, Cleared
    std::string reason;
};

/**
 * @struct RegisteredUnitRecord
 * @brief Encapsulates a unit's profile, domain service instance, owned sub-units, and alert pointer.
 */
struct RegisteredUnitRecord {
    UnitProfile profile;
    std::unique_ptr<rapidaid::service::Ambulance> ambulance;
    std::unique_ptr<rapidaid::service::FireTruck> fireTruck;
    std::unique_ptr<rapidaid::service::PoliceUnit> policeUnit;
    std::unique_ptr<rapidaid::service::Hospital> hospital;
    std::unique_ptr<rapidaid::service::FireStation> fireStation;
    std::unique_ptr<rapidaid::service::PoliceStation> policeStation;
    std::unique_ptr<rapidaid::service::Shelter> shelter;

    std::vector<SubUnitInfo> subunits;
    int nextSubunitId{1};
    int openAlertId{-1};

    /**
     * @brief Computes human-readable operational status string.
     * @return "Available", "Dispatched", or "Unavailable". Never throws.
     */
    std::string getStatusString() const;

    /**
     * @brief Checks if unit is currently capable of receiving alerts.
     * @return true if status is Available and no open alert exists; false otherwise. Never throws.
     */
    bool isAvailable() const;
};

/**
 * @class UnitRegistry
 * @brief Manages unit accounts, domain service model instances, and dispatch alerts.
 */
class UnitRegistry {
public:
    /**
     * @brief Constructs registry with target persistence JSON path.
     * @param dataPath File path to store unit configurations (default "data/units.json").
     * Failure behavior: Does not throw; lazy-loads on first query.
     */
    explicit UnitRegistry(std::string dataPath = "data/units.json");

    /**
     * @brief Sets file path for persistence.
     * @param path New target path.
     * Failure behavior: Does not throw; resets loaded state.
     */
    void setDataPath(const std::string& path);

    /**
     * @brief Ensures unit profiles are loaded from disk if not yet initialized.
     * Failure behavior: Tolerates missing or corrupted JSON files gracefully without throwing.
     */
    void ensureLoaded();

    /**
     * @brief Deserializes registered units from disk.
     * @param path Optional explicit file path.
     * @return true on successful load or benign missing file; false if file corrupt or unreadable. Never throws.
     */
    bool load(const std::string& path = "");

    /**
     * @brief Serializes registered units to disk using an atomic temp-file rename.
     * @param path Optional explicit file path.
     * @return true on success; false on write failure. Never throws.
     */
    bool save(const std::string& path = "") const;

    /**
     * @brief Clears all registered units and alerts from memory.
     * Failure behavior: Never throws.
     */
    void clear();

    /**
     * @brief Checks if a username has an active unit registration.
     * @param username Username to check.
     * @return true if unit exists; false otherwise. Never throws.
     */
    bool hasUnit(const std::string& username);

    /**
     * @brief Registers a new unit profile, initializes service model, and persists to disk.
     * @param profile Unit profile details.
     * @param errorMsg Output error message on validation or model failure.
     * @return true on successful registration; false on failure. Never throws.
     */
    bool registerUnit(const UnitProfile& profile, std::string& errorMsg);

    /**
     * @brief Retrieves unit profile by username.
     * @param username Unit username.
     * @return Optional UnitProfile containing details, or std::nullopt if not found. Never throws.
     */
    std::optional<UnitProfile> getProfile(const std::string& username);

    /**
     * @brief Toggles availability for a mobile service unit.
     * @param username Unit username.
     * @param available Desired availability state.
     * @param errorMsg Output error message on failure.
     * @return true on success; false if unit not mobile, dispatched, or not found. Never throws.
     */
    bool setAvailability(const std::string& username, bool available, std::string& errorMsg);

    /**
     * @brief Updates geographic location coordinates of a unit.
     * @param username Unit username.
     * @param lat Latitude in [-90, 90].
     * @param lon Longitude in [-180, 180].
     * @param errorMsg Output error message on failure.
     * @return true on success; false on invalid coordinates or unit not found. Never throws.
     */
    bool updateLocation(const std::string& username, double lat, double lon, std::string& errorMsg);

    /**
     * @brief Updates capacity or counts for facilities and mobile units.
     * @param username Unit username.
     * @param action Resource action (e.g., "admit", "discharge", "leave", or empty).
     * @param value Resource delta count or absolute value.
     * @param errorMsg Output error message on failure.
     * @return true on success; false on failure. Never throws.
     */
    bool updateResources(const std::string& username, const std::string& action, int value, std::string& errorMsg);

    /**
     * @brief Adds a child sub-unit to a facility (Hospital, FireStation, PoliceStation).
     * @param username Facility unit username.
     * @param name Name of sub-unit.
     * @param count Capacity or staffing of sub-unit.
     * @param errorMsg Output error message on failure.
     * @return Assigned sub-unit ID on success, or -1 on failure. Never throws.
     */
    int addSubunit(const std::string& username, const std::string& name, int count, std::string& errorMsg);

    /**
     * @brief Toggles availability of an owned sub-unit.
     * @param username Facility unit username.
     * @param subunitId ID of sub-unit.
     * @param available Desired availability state.
     * @param errorMsg Output error message on failure.
     * @return true on success; false on failure. Never throws.
     */
    bool setSubunitAvailability(const std::string& username, int subunitId, bool available, std::string& errorMsg);

    /**
     * @brief Produces JSON representation of unit's state, resources, sub-units, and alerts.
     * @param username Unit username.
     * @return JSON response object. Never throws.
     */
    nlohmann::ordered_json getUnitStateJson(const std::string& username);

    /**
     * @brief Lists all registered units with summary metrics for dispatchers.
     * @return JSON response object containing array of units. Never throws.
     */
    nlohmann::ordered_json listUnitsJson();

    /**
     * @brief Issues a test dispatch alert to an eligible unit.
     * @param alert Alert parameters.
     * @param errorMsg Output error message on failure.
     * @return Alert ID on success, or -1 on failure. Never throws.
     */
    int sendAlert(const Alert& alert, std::string& errorMsg);

    /**
     * @brief Processes an alert state transition from the receiving unit.
     * @param alertId Alert ID.
     * @param username Responding unit username.
     * @param action Requested state transition action.
     * @param reason Optional decline explanation.
     * @param errorMsg Output error message on failure.
     * @return true on success; false on invalid transition or unauthorized unit. Never throws.
     */
    bool respondToAlert(int alertId, const std::string& username, const std::string& action, const std::string& reason, std::string& errorMsg);

    /**
     * @brief Lists all alerts currently tracked in memory.
     * @return JSON response object containing alerts array. Never throws.
     */
    nlohmann::ordered_json listAlertsJson() const;

private:
    std::string dataPath_;
    std::unordered_map<std::string, RegisteredUnitRecord> units_;
    std::vector<Alert> alerts_;
    int nextAlertId_{1};
    bool loaded_{false};

    void reinstantiateMobile(RegisteredUnitRecord& rec, int newCount);
};

} // namespace rapidaid::ui

#endif // RAPIDAID_UI_UNITREGISTRY_HPP

```

## index.html

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>RapidAid - Emergency Dispatch System</title>
  <!-- Leaflet Map Integration -->
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
</head>
<body style="font-family: sans-serif; margin: 20px; line-height: 1.5;">

  <!-- Header & Navigation -->
  <header style="border-bottom: 2px solid #333; padding-bottom: 10px; margin-bottom: 20px;">
    <h1 style="margin: 0 0 10px 0;">RapidAid Emergency Dispatch System</h1>
    <div id="header-bar" style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap;">
      <div>
        <span id="backend-status" style="padding: 3px 8px; background: #eee; border: 1px solid #ccc; font-size: 13px;">Checking backend...</span>
        <button id="btn-toggle-apikey-settings" style="margin-left: 8px; font-size: 12px; padding: 3px 8px; cursor: pointer;">Map API Key: <span id="apikey-badge">Checking...</span></button>
        <span id="session-info" style="margin-left: 15px; font-weight: bold; display: none;">
          Logged in as: <span id="user-display"></span> (<span id="role-display"></span>)
        </span>
      </div>
      <nav id="main-nav" style="display: none; margin-top: 5px;">
        <button id="nav-incidents" style="margin-right: 8px; font-weight: bold;">Incident Reports</button>
        <button id="nav-area" style="margin-right: 8px;">Area Map Setup</button>
        <button id="nav-stats" style="margin-right: 8px;">Past Dispatch Stats</button>
        <button id="nav-logout" style="background: #fee; border-color: #c00;">Logout</button>
      </nav>
    </div>
  </header>

  <!-- API Key Setup Banner / Dialog -->
  <div id="apikey-banner" style="display: none; background: #eef5fc; border: 2px solid #3366cc; border-radius: 4px; padding: 15px; margin-bottom: 20px;">
    <h3 style="margin-top: 0; color: #003366;">Map Service CARTO API Key Setup</h3>
    <p style="margin: 5px 0 10px 0;">
      A CARTO API key is required to stream live map tiles. When entered, it is saved privately to your local storage and ignored by git (see <code>config.example.json</code>).
    </p>
    <div style="display: flex; gap: 10px; align-items: center; flex-wrap: wrap;">
      <input type="text" id="apikey-input" placeholder="Paste your CARTO API Key here" style="flex: 1; min-width: 260px; padding: 8px; box-sizing: border-box;">
      <button id="btn-save-apikey" style="padding: 8px 18px; font-weight: bold; background: #28a745; color: white; border: none; border-radius: 3px; cursor: pointer;">Save API Key</button>
      <button id="btn-download-config" style="padding: 8px 14px; background: #17a2b8; color: white; border: none; border-radius: 3px; cursor: pointer;">Save to config.json</button>
      <button id="btn-dismiss-apikey" style="padding: 8px 12px; background: #e2e6ea; border: 1px solid #ccc; border-radius: 3px; cursor: pointer;">Hide</button>
    </div>
    <div id="apikey-feedback" style="margin-top: 8px; font-weight: bold; font-size: 13px;"></div>
  </div>

  <!-- ========================================== -->
  <!-- 1. Authentication View (Login / Signup)    -->
  <!-- ========================================== -->
  <main id="auth-view" style="max-width: 480px; margin: 40px auto; border: 1px solid #ccc; padding: 25px;">
    <h2 style="margin-top: 0;">RapidAid Access Portal</h2>
    <div style="margin-bottom: 15px;">
      <button id="tab-login" style="padding: 8px 16px; font-weight: bold;">Login</button>
      <button id="tab-signup" style="padding: 8px 16px;">Sign Up</button>
    </div>

    <!-- Login Form -->
    <div id="login-container">
      <h3>System Login</h3>
      <form id="form-login" onsubmit="return false;">
        <p>
          <label for="login-username">Username:</label><br>
          <input type="text" id="login-username" value="admin" style="width: 100%; padding: 6px; box-sizing: border-box;" required>
        </p>
        <p>
          <label for="login-password">Password:</label><br>
          <input type="password" id="login-password" value="admin123" style="width: 100%; padding: 6px; box-sizing: border-box;" required>
        </p>
        <p>
          <button id="btn-login" style="padding: 8px 20px; font-size: 15px;">Log In</button>
        </p>
        <p style="font-size: 12px; color: #666;">
          Default credentials: <b>admin</b> / <b>admin123</b>
        </p>
      </form>
    </div>

    <!-- Signup Form -->
    <div id="signup-container" style="display: none;">
      <h3>Register New Account</h3>
      <form id="form-signup" onsubmit="return false;">
        <p>
          <label for="signup-username">New Username:</label><br>
          <input type="text" id="signup-username" style="width: 100%; padding: 6px; box-sizing: border-box;" required>
        </p>
        <p>
          <label for="signup-password">New Password:</label><br>
          <input type="password" id="signup-password" style="width: 100%; padding: 6px; box-sizing: border-box;" required>
        </p>
        <p>
          <label for="signup-role">Role:</label><br>
          <select id="signup-role" style="width: 100%; padding: 6px;">
            <option value="dispatcher" selected>Dispatcher</option>
            <option value="unit">Service Unit</option>
          </select>
        </p>

        <!-- Service Unit Details -->
        <div id="signup-unit-fields" style="display: none; border-top: 1px solid #ddd; margin-top: 12px; padding-top: 10px;">
          <p>
            <label for="signup-unit-kind">Unit Type:</label><br>
            <select id="signup-unit-kind" style="width: 100%; padding: 6px;">
              <option value="ambulance" selected>Ambulance</option>
              <option value="fire_truck">Fire Truck</option>
              <option value="police_unit">Police Unit</option>
              <option value="hospital">Hospital</option>
              <option value="fire_station">Fire Station</option>
              <option value="police_station">Police Station</option>
              <option value="shelter">Shelter</option>
            </select>
          </p>
          <p>
            <label for="signup-unit-name">Display Name:</label><br>
            <input type="text" id="signup-unit-name" placeholder="e.g. City Ambulance 1" style="width: 100%; padding: 6px; box-sizing: border-box;">
          </p>
          <div style="display: flex; gap: 10px;">
            <p style="flex: 1; margin: 0 0 10px 0;">
              <label for="signup-unit-lat">Latitude:</label><br>
              <input type="number" step="any" id="signup-unit-lat" value="48.8584" style="width: 100%; padding: 6px; box-sizing: border-box;">
            </p>
            <p style="flex: 1; margin: 0 0 10px 0;">
              <label for="signup-unit-lon">Longitude:</label><br>
              <input type="number" step="any" id="signup-unit-lon" value="2.2945" style="width: 100%; padding: 6px; box-sizing: border-box;">
            </p>
          </div>
          <p id="signup-unit-count-container" style="margin-top: 0;">
            <label for="signup-unit-count" id="signup-unit-count-label">Paramedics:</label><br>
            <input type="number" id="signup-unit-count" value="2" min="0" style="width: 100%; padding: 6px; box-sizing: border-box;">
          </p>
        </div>

        <p>
          <button id="btn-signup" style="padding: 8px 20px; font-size: 15px;">Create Account</button>
        </p>
      </form>
    </div>

    <!-- Auth Feedback Message -->
    <div id="auth-feedback" style="margin-top: 15px; font-weight: bold; min-height: 20px;"></div>
  </main>

  <!-- ========================================== -->
  <!-- 2. Main App View Container (Dispatcher/Admin) -->
  <!-- ========================================== -->
  <div id="app-view" style="display: none;">

    <!-- ========================================== -->
    <!-- 2A. Area Setup Menu (Side-by-Side Map)     -->
    <!-- ========================================== -->
    <section id="section-area" style="display: none;">
      <div id="area-alert" style="background: #fff3cd; border: 1px solid #ffeeba; padding: 12px; margin-bottom: 20px;">
        <strong>Notice:</strong> <span id="area-alert-text">No permanent area GeoJSON detected. Please configure the operational area under your charge.</span>
      </div>

      <h2>Operational Area &amp; Zone Map Configuration</h2>
      <p>Enter the area details to geocode, preview the boundary and facilities side by side, and generate the permanent GeoJSON network.</p>

      <!-- Side-by-Side Container -->
      <div style="display: flex; flex-direction: row; gap: 20px; align-items: flex-start; flex-wrap: wrap;">
        
        <!-- Left Column: Form & Controls -->
        <div style="flex: 1; min-width: 320px; border: 1px solid #ccc; padding: 18px;">
          <h3>Area Parameters</h3>
          <form id="form-area" onsubmit="return false;">
            <p>
              <label for="area-name"><strong>Area / Region Name:</strong></label><br>
              <input type="text" id="area-name" value="Eiffel Tower, Paris" style="width: 100%; padding: 6px; box-sizing: border-box;" required>
              <small style="color: #666;">e.g. Eiffel Tower, Paris | Paris Center | Manhattan, NY</small>
            </p>

            <p>
              <label for="area-radius"><strong>Radius (meters):</strong></label><br>
              <input type="number" id="area-radius" value="3000" min="500" max="10000" step="500" style="width: 100%; padding: 6px; box-sizing: border-box;" required>
              <small style="color: #666;">Suggested: 1500m to 5000m</small>
            </p>

            <div style="margin: 15px 0; padding: 10px; background: #f8f9fa; border: 1px solid #ddd;">
              <div><strong>Geocoded Center:</strong> <span id="preview-coords">None</span></div>
              <div><strong>Full Name:</strong> <span id="preview-displayname">None</span></div>
              <div style="margin-top: 5px; font-size: 12px; color: #555;"><i>All emergency facilities (Hospitals, Fire Stations, Police, Clinics) will be automatically detected and mapped.</i></div>
            </div>

            <p style="display: flex; gap: 10px; flex-wrap: wrap;">
              <button id="btn-preview" style="padding: 8px 16px;">1. Preview Area on Map</button>
              <button id="btn-generate" style="padding: 8px 16px; font-weight: bold;">2. Generate &amp; Save GeoJSON</button>
            </p>
          </form>

          <div id="area-pipeline-feedback" style="margin-top: 15px; font-size: 14px; white-space: pre-wrap;"></div>

          <div id="area-stats-box" style="display: none; margin-top: 15px; border-top: 1px solid #ccc; padding-top: 10px;">
            <h4>Pipeline Generation Summary</h4>
            <ul style="padding-left: 20px; margin: 5px 0;">
              <li>Road Network Nodes: <b id="stat-nodes">0</b></li>
              <li>Road Network Edges: <b id="stat-edges">0</b></li>
              <li>Facilities Detected: <b id="stat-places">0</b></li>
              <li>Snapped Facilities: <b id="stat-snapped">0</b></li>
              <li>Permanent Roads File: <code id="stat-roads-file">data/out/roads.geojson</code></li>
              <li>Permanent Places File: <code id="stat-places-file">data/out/places.geojson</code></li>
            </ul>
          </div>
        </div>

        <!-- Right Column: Interactive Leaflet Map Viewer -->
        <div style="flex: 1.5; min-width: 380px; border: 1px solid #ccc; padding: 18px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
            <h3 style="margin: 0;">Interactive Zone Map (OpenStreetMap &amp; Leaflet)</h3>
            <span id="map-status-overlay" style="font-size: 12px; color: #555;">Map ready</span>
          </div>

          <!-- Leaflet Map Container with explicit height -->
          <div id="leaflet-map" style="width: 100%; height: 480px; border: 1px solid #999; border-radius: 4px; z-index: 1;"></div>

          <div style="margin-top: 10px; font-size: 13px; color: #444; display: flex; gap: 15px; flex-wrap: wrap;">
            <span><span style="display: inline-block; width: 12px; height: 12px; background: #2A81CB; border-radius: 50%; vertical-align: middle;"></span> Center Position</span>
            <span><span style="display: inline-block; width: 12px; height: 12px; border: 1px dashed #ff8800; background: rgba(255,165,0,0.3); vertical-align: middle;"></span> Coverage Radius</span>
            <span><span style="display: inline-block; width: 16px; height: 3px; background: #0066cc; vertical-align: middle;"></span> Drivable Roads</span>
            <span><span style="display: inline-block; width: 10px; height: 10px; background: #e74c3c; border-radius: 50%; vertical-align: middle;"></span> Hospitals</span>
            <span><span style="display: inline-block; width: 10px; height: 10px; background: #e67e22; border-radius: 50%; vertical-align: middle;"></span> Fire Stations</span>
            <span><span style="display: inline-block; width: 10px; height: 10px; background: #2980b9; border-radius: 50%; vertical-align: middle;"></span> Police</span>
            <span><span style="display: inline-block; width: 10px; height: 10px; background: #27ae60; border-radius: 50%; vertical-align: middle;"></span> Clinics &amp; Others</span>
          </div>
        </div>

      </div>
    </section>

    <!-- ========================================== -->
    <!-- 2B. Incident Report Handling Menu          -->
    <!-- ========================================== -->
    <section id="section-incidents" style="display: none;">
      <div style="display: flex; justify-content: center; align-items: center; min-height: 25vh; text-align: center;">
        <h1 style="font-size: 48px; color: #888; margin: 0; font-weight: 800; letter-spacing: 4px;">UNDER DEVELOPMENT</h1>
      </div>

      <!-- Developer Test Dispatch Alert Box (C3) -->
      <div id="dispatcher-alert-test-box" style="margin: 20px auto; max-width: 850px; border: 1px solid #ccc; padding: 20px; background: #fafafa;">
        <h3 style="margin-top: 0; color: #333;">Developer test: send a dispatch alert to a unit</h3>
        <form id="form-test-alert" onsubmit="return false;">
          <div style="display: flex; gap: 15px; flex-wrap: wrap; margin-bottom: 12px;">
            <div style="flex: 1.5; min-width: 220px;">
              <label for="alert-unit-select"><strong>Target Service Unit:</strong></label>
              <div style="display: flex; gap: 6px; margin-top: 4px;">
                <select id="alert-unit-select" style="flex: 1; padding: 6px;"></select>
                <button id="btn-refresh-units" type="button" style="padding: 6px 12px; cursor: pointer;">↻ Refresh</button>
              </div>
            </div>
            <div style="flex: 1; min-width: 140px;">
              <label for="alert-type"><strong>Alert Type:</strong></label>
              <select id="alert-type" style="width: 100%; padding: 6px; margin-top: 4px;">
                <option value="Medical" selected>Medical</option>
                <option value="Fire">Fire</option>
                <option value="Police">Police</option>
                <option value="Other">Other</option>
              </select>
            </div>
            <div style="flex: 0.7; min-width: 100px;">
              <label for="alert-severity"><strong>Severity (1-5):</strong></label>
              <input type="number" id="alert-severity" min="1" max="5" value="3" style="width: 100%; padding: 6px; margin-top: 4px; box-sizing: border-box;">
            </div>
            <div style="flex: 0.8; min-width: 110px;">
              <label for="alert-people"><strong>People Affected:</strong></label>
              <input type="number" id="alert-people" min="0" value="1" style="width: 100%; padding: 6px; margin-top: 4px; box-sizing: border-box;">
            </div>
          </div>

          <div style="margin-bottom: 12px;">
            <label for="alert-location"><strong>Location Text:</strong></label>
            <input type="text" id="alert-location" value="12 Rue de Rivoli, Paris" style="width: 100%; padding: 6px; margin-top: 4px; box-sizing: border-box;" required>
          </div>

          <div style="display: flex; gap: 15px; margin-bottom: 12px; flex-wrap: wrap;">
            <div style="flex: 1; min-width: 160px;">
              <label for="alert-lat"><strong>Latitude (optional):</strong></label>
              <input type="number" step="any" id="alert-lat" value="48.8584" style="width: 100%; padding: 6px; margin-top: 4px; box-sizing: border-box;">
            </div>
            <div style="flex: 1; min-width: 160px;">
              <label for="alert-lon"><strong>Longitude (optional):</strong></label>
              <input type="number" step="any" id="alert-lon" value="2.2945" style="width: 100%; padding: 6px; margin-top: 4px; box-sizing: border-box;">
            </div>
          </div>

          <div style="margin-bottom: 12px;">
            <label for="alert-note"><strong>Note / Instructions:</strong></label>
            <input type="text" id="alert-note" placeholder="Provide emergency context or notes" style="width: 100%; padding: 6px; margin-top: 4px; box-sizing: border-box;">
          </div>

          <div>
            <button id="btn-send-alert" type="button" style="padding: 8px 22px; font-weight: bold; background: #0066cc; color: white; border: none; border-radius: 3px; cursor: pointer;">Send Dispatch Alert</button>
            <span id="alert-send-feedback" style="margin-left: 12px; font-weight: bold; font-size: 13px;"></span>
          </div>
        </form>

        <hr style="margin: 25px 0;">
        <h4 style="margin: 0 0 10px 0;">Tracked Dispatch Alerts (Live Updates Every 3s)</h4>
        <table style="width: 100%; border-collapse: collapse; font-size: 13px; text-align: left;">
          <thead>
            <tr style="background: #e9ecef; border-bottom: 2px solid #ccc;">
              <th style="padding: 8px;">ID</th>
              <th style="padding: 8px;">Target Unit</th>
              <th style="padding: 8px;">Type</th>
              <th style="padding: 8px;">Severity</th>
              <th style="padding: 8px;">State</th>
              <th style="padding: 8px;">Created At</th>
            </tr>
          </thead>
          <tbody id="alerts-table-body">
            <tr><td colspan="6" style="padding: 10px; text-align: center; color: #777;">Loading alerts...</td></tr>
          </tbody>
        </table>
      </div>
    </section>

    <!-- ========================================== -->
    <!-- 2C. Past Dispatch Statistics               -->
    <!-- ========================================== -->
    <section id="section-stats" style="display: none;">
      <div style="display: flex; justify-content: center; align-items: center; min-height: 60vh; text-align: center;">
        <h1 style="font-size: 56px; color: #888; margin: 0; font-weight: 800; letter-spacing: 4px;">UNDER DEVELOPMENT</h1>
      </div>
    </section>

  </div>

  <!-- ========================================== -->
  <!-- 3. Unit View Container (Role: unit)        -->
  <!-- ========================================== -->
  <div id="unit-view" style="display: none; max-width: 900px; margin: 20px auto;">
    <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #0066cc; padding-bottom: 10px; margin-bottom: 15px; flex-wrap: wrap;">
      <h2 style="margin: 0;">Service Unit Terminal: <span id="unit-header-name">Unit</span> (<span id="unit-header-kind">KIND</span>)</h2>
      <button id="btn-unit-logout" style="background: #fee; border-color: #c00; padding: 6px 14px; cursor: pointer; border-radius: 3px;">Logout</button>
    </div>
    <div style="margin-bottom: 15px;">
      <button id="tab-unit-resources" style="padding: 8px 18px; font-weight: bold; cursor: pointer;">Availability &amp; Resources</button>
      <button id="tab-unit-alerts" style="padding: 8px 18px; cursor: pointer;">Dispatch Alert <span id="badge-unit-pending" style="display: none; background: #d9534f; color: white; border-radius: 10px; padding: 2px 7px; font-size: 11px;">NEW</span></button>
    </div>
    <div id="view-unit-resources"></div>
    <div id="view-unit-alerts" style="display: none;"></div>
  </div>

  <script src="data/out/places.js"></script>
  <script src="data/out/roads.js"></script>
  <script src="unit.js"></script>
  <script src="app.js"></script>
</body>
</html>

```

## Login.cpp

```cpp
#include "Login.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <ctime>
using namespace std;

Logger::Logger(string f) : filename(f) {}

void Logger::log(string type, string details) {
    ofstream out(filename, ios::app);
    time_t t = time(nullptr);
    string ts = ctime(&t);
    ts.pop_back();
    out << ts << " | " << type << " | " << details << "\n";
}

size_t Auth::hashPass(const string& p) {
    return hash<string>{}(p);
}

Auth::Auth(string f) : filename(f) {
    load();
    if (users.empty()) {
        signUp("admin", "admin123", "admin");
    }
}

void Auth::load() {
    ifstream in(filename);
    string name, role;
    size_t h;
    while (in >> name >> h >> role) {
        users[name] = {h, role};
    }
}

void Auth::save() {
    ofstream out(filename);
    for (auto& p : users) {
        out << p.first << " " << p.second.passHash
            << " " << p.second.role << "\n";
    }
}

bool Auth::signUp(string u, string p, string role) {
    if (users.count(u)) return false;
    users[u] = {hashPass(p), role};
    save();
    return true;
}

bool Auth::login(string u, string p) {
    auto it = users.find(u);
    if (it == users.end()) return false;
    return it->second.passHash == hashPass(p);
}

string Auth::getRole(string u) { return users[u].role; }

void dispatcherMenu(Auth& auth, Logger& logger, string user) {
    bool isAdmin = (auth.getRole(user) == "admin");
    int choice;

    while (true) {
        cout << "\n--- Rapid Aid Menu (" << user << ") ---\n"
             << "1. New incident report\n"
             << "2. Active incidents\n"
             << "3. Past reports\n"
             << "4. Statistics\n";
        if (isAdmin) cout << "5. Add new dispatcher\n";
        cout << "0. Logout\nChoice: ";
        cin >> choice;

        if (choice == 0) {
            logger.log("LOGOUT", user);
            return;
        }
        else if (choice == 5 && isAdmin) {
            string u, p;
            cout << "New dispatcher username: ";
            cin >> u;
            cout << "New dispatcher password: ";
            cin >> p;
            if (auth.signUp(u, p, "dispatcher")) {
                cout << "Dispatcher added.\n";
                logger.log("USER_ADDED", u + " added by " + user);
            } else {
                cout << "Username already exists.\n";
            }
        }
        else if (choice >= 1 && choice <= 4) {
            cout << "Coming soon (teammates' modules connect here)\n";
        }
        else {
            cout << "Invalid choice.\n";
        }
    }
}

#ifdef LOGIN_STANDALONE
int main() {
    Auth auth;
    Logger logger;

    cout << "=== Rapid Aid - Dispatcher Login ===\n";

    for (int attempt = 1; attempt <= 3; attempt++) {
        string u, p;
        cout << "\nUsername: ";
        cin >> u;
        cout << "Password: ";
        cin >> p;

        if (auth.login(u, p)) {
            logger.log("LOGIN_SUCCESS", u);
            cout << "Welcome, " << u << "!\n";
            dispatcherMenu(auth, logger, u);
            return 0;
        }

        logger.log("LOGIN_FAILED", u);
        cout << "Wrong username or password. "
             << 3 - attempt << " attempts left.\n";
    }

    logger.log("LOGIN_LOCKED", "3 failed attempts");
    cout << "Too many failed attempts. Exiting.\n";
    return 0;
}
#endif

std::string handleLoginRequest(Auth& auth, const std::string& requestJson) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (const nlohmann::json::exception&) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("username") || !j.contains("password")) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j["username"].is_string() || !j["password"].is_string()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string username = j["username"].get<std::string>();
        std::string password = j["password"].get<std::string>();

        if (auth.login(username, password)) {
            Logger logger;
            logger.log("LOGIN_SUCCESS", username);
            std::string role = auth.getRole(username);
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = username;
            resp["role"] = role;
            return resp.dump();
        } else {
            Logger logger;
            logger.log("LOGIN_FAILED", username);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Invalid username or password";
            return resp.dump();
        }
    } catch (const nlohmann::json::exception&) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}

std::string handleLogoutRequest(Auth& auth, const std::string& username) {
    (void)auth;
    try {
        Logger logger;
        logger.log("LOGOUT", username);
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        return resp.dump();
    }
}

std::string handleHealthRequest() {
    try {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["service"] = "login";
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"service\":\"login\"}";
    }
}

std::string handleSignUpRequest(Auth& auth, const std::string& requestJson) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (const nlohmann::json::exception&) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("username") || !j.contains("password")) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j["username"].is_string() || !j["password"].is_string()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string username = j["username"].get<std::string>();
        std::string password = j["password"].get<std::string>();
        if (username.empty() || password.empty()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string role = "dispatcher";
        if (j.contains("role") && j["role"].is_string()) {
            role = j["role"].get<std::string>();
        }

        if (auth.signUp(username, password, role)) {
            Logger logger;
            logger.log("USER_SIGNUP", username + " (" + role + ")");
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = username;
            resp["role"] = role;
            return resp.dump();
        } else {
            Logger logger;
            logger.log("SIGNUP_FAILED", username + " already exists");
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Username already exists";
            return resp.dump();
        }
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}
```

## main.cpp

```cpp
#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET -1
#endif

namespace {

void sendHttpResponse(SOCKET client, const std::string& status, const std::string& contentType, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    std::string resp = oss.str();
    send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
}

void handleClient(SOCKET client, Auth& auth, rapidaid::data::DataCache& cache) {
    std::string req;
    char buf[4096];
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;
    bool hasContentLength = false;

    // Read full request headers and Content-Length body
    while (true) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<std::size_t>(n));

        if (headerEnd == std::string::npos) {
            headerEnd = req.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                std::string headers = req.substr(0, headerEnd);
                std::string clTag = "content-length:";
                std::string lowerHeaders = headers;
                std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                auto pos = lowerHeaders.find(clTag);
                if (pos != std::string::npos) {
                    auto lineEnd = headers.find("\r\n", pos);
                    std::string clVal = headers.substr(pos + clTag.size(),
                        lineEnd == std::string::npos ? std::string::npos : lineEnd - (pos + clTag.size()));
                    contentLength = std::strtoul(clVal.c_str(), nullptr, 10);
                    hasContentLength = true;
                }
            }
        }

        if (hasContentLength) {
            if (contentLength > 1024 * 1024) { // reject bodies > 1 MB
                sendHttpResponse(client, "413 Payload Too Large", "text/plain", "Payload Too Large");
                closesocket(client);
                return;
            }
            if (req.size() >= headerEnd + 4 + contentLength) {
                break;
            }
        } else if (headerEnd != std::string::npos) {
            break;
        }
    }

    if (req.empty() || headerEnd == std::string::npos) {
        closesocket(client);
        return;
    }

    std::istringstream iss(req);
    std::string method, path;
    iss >> method >> path;

    if (method == "OPTIONS") {
        sendHttpResponse(client, "200 OK", "text/plain", "");
        closesocket(client);
        return;
    }

    if (path == "/" || path == "/index.html") {
        std::string html = rapidaid::io::FileStore::load("index.html");
        sendHttpResponse(client, "200 OK", "text/html; charset=utf-8", html);
    } else if (path == "/app.js") {
        std::string js = rapidaid::io::FileStore::load("app.js");
        sendHttpResponse(client, "200 OK", "application/javascript; charset=utf-8", js);
    } else if (path == "/unit.js") {
        std::string js = rapidaid::io::FileStore::load("unit.js");
        sendHttpResponse(client, "200 OK", "application/javascript; charset=utf-8", js);
    } else if (path.rfind("/data/out/", 0) == 0) {
        // Path safety: strip query string and validate path characters
        std::string rawPath = path;
        auto qPos = rawPath.find('?');
        if (qPos != std::string::npos) {
            rawPath = rawPath.substr(0, qPos);
        }

        if (rawPath.find("..") != std::string::npos || rawPath.find('\\') != std::string::npos) {
            sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found");
            closesocket(client);
            return;
        }

        bool isJs = (rawPath.size() >= 3 && rawPath.rfind(".js") == rawPath.size() - 3);
        bool isGeoJson = (rawPath.size() >= 8 && rawPath.rfind(".geojson") == rawPath.size() - 8);
        bool isJson = (rawPath.size() >= 5 && rawPath.rfind(".json") == rawPath.size() - 5);

        if (!isJs && !isGeoJson && !isJson) {
            sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found");
            closesocket(client);
            return;
        }

        std::string filePath = rawPath.substr(1);
        std::string content = rapidaid::io::FileStore::load(filePath);
        if (content.empty()) {
            sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found");
            closesocket(client);
            return;
        }

        std::string contentType = isJs ? "application/javascript; charset=utf-8" : "application/json; charset=utf-8";
        sendHttpResponse(client, "200 OK", contentType, content);
    } else if (path == "/api/bridge") {
        std::string body = req.substr(headerEnd + 4);
        std::string result = rapidaid::ui::handleBridgeMessage(auth, cache, body, "data/out");
        sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", result);
    } else if (path == "/api/create_area" || path == "/api/pipeline") {
        std::string body = req.substr(headerEnd + 4);
        std::string token;
        try {
            nlohmann::json j = nlohmann::json::parse(body);
            if (j.contains("token") && j["token"].is_string()) {
                token = j["token"].get<std::string>();
            }
        } catch (...) {}

        if (!rapidaid::ui::isTokenAuthorized(token, "dispatcher")) {
            sendHttpResponse(client, "403 Forbidden", "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Not allowed\"}");
            closesocket(client);
            return;
        }

        std::string result = rapidaid::ui::handleCreateAreaRequest(cache, body, "data/out");
        sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", result);
    } else {
        sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found");
    }

    closesocket(client);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    bool lanMode = false;
    std::string jsonArg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lan") {
            lanMode = true;
        } else if (!arg.empty() && (arg[0] == '{' || arg[0] == '[')) {
            jsonArg = arg;
        }
    }

    // Mode 1: Command-line single JSON argument
    // Example: ./rapidaid.exe '{"action":"login","payload":{"username":"admin","password":"admin123"}}'
    if (!jsonArg.empty()) {
        Auth auth("users.txt");
        rapidaid::data::DataCache cache("data", false);
        std::cout << rapidaid::ui::handleBridgeMessage(auth, cache, jsonArg, "data/out") << "\n";
        return 0;
    }

    // Sync any pre-existing GeoJSON files to JavaScript wrappers for instantaneous browser loading
    std::string placesGeo = rapidaid::io::FileStore::load("data/out/places.geojson");
    if (!placesGeo.empty()) {
        rapidaid::io::FileStore::save("data/out/places.js", "window.RAPIDAID_PLACES = " + placesGeo + ";");
    }
    std::string roadsGeo = rapidaid::io::FileStore::load("data/out/roads.geojson");
    if (!roadsGeo.empty()) {
        rapidaid::io::FileStore::save("data/out/roads.js", "window.RAPIDAID_ROADS = " + roadsGeo + ";");
    }

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed. Opening index.html directly.\n";
        std::system("start index.html");
        return 1;
    }
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Opening index.html directly.\n";
#if defined(_WIN32)
        std::system("start index.html");
        WSACleanup();
#else
        std::system("xdg-open index.html");
#endif
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    int port = 8080;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (lanMode) {
        std::cout << "Warning: Binding to all network interfaces (--lan). Passwords travel unencrypted over HTTP.\n";
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        // Port 8080 in use; try port 8081
        port = 8081;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::cerr << "Bind failed on ports 8080/8081. Opening index.html directly.\n";
            closesocket(server);
#if defined(_WIN32)
            std::system("start index.html");
            WSACleanup();
#endif
            return 1;
        }
    }

    if (listen(server, 10) != 0) {
        std::cerr << "Listen failed. Opening index.html directly.\n";
        closesocket(server);
#if defined(_WIN32)
        std::system("start index.html");
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "========================================================\n"
              << "  RapidAid Server started at http://localhost:" << port << "\n"
              << "  Bridge & Pipeline API ready for Dispatcher UI\n"
              << "========================================================\n";

    // Launch UI in browser pointing to local server
    std::string launchCmd = "start http://localhost:" + std::to_string(port);
#if defined(_WIN32)
    std::system(launchCmd.c_str());
#elif defined(__APPLE__)
    std::system(("open http://localhost:" + std::to_string(port)).c_str());
#else
    std::system(("xdg-open http://localhost:" + std::to_string(port)).c_str());
#endif

    // Lazy load auth and cache for request processing
    Auth auth("users.txt");
    rapidaid::data::DataCache cache("data", false);

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        std::thread([client, &auth, &cache]() {
            handleClient(client, auth, cache);
        }).detach();
    }

    closesocket(server);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
```

## src/Ambulance.cpp

```cpp
#include "Ambulance.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

Ambulance::Ambulance(int id, std::string name, double latitude, double longitude,
                     int homeStationId, int paramedicCount)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      homeStationId(homeStationId),
      paramedicCount(paramedicCount) {
    if (paramedicCount < 0) {
        throw std::invalid_argument("paramedic count cannot be negative");
    }
}

int Ambulance::getId() const { return id; }
const std::string& Ambulance::getName() const { return name; }
double Ambulance::getLatitude() const { return latitude; }
double Ambulance::getLongitude() const { return longitude; }
long long Ambulance::getGraphNodeId() const { return graphNodeId; }
void Ambulance::setGraphNodeId(long long value) { graphNodeId = value; }
Ambulance::Status Ambulance::getStatus() const { return status; }
int Ambulance::getCurrentIncidentId() const { return currentIncidentId; }
int Ambulance::getHomeStationId() const { return homeStationId; }
int Ambulance::getParamedicCount() const { return paramedicCount; }
bool Ambulance::isAvailable() const { return status == Status::Available; }

bool Ambulance::assign(int incidentId) {
    if (incidentId < 0) {
        throw std::invalid_argument("incident id cannot be negative");
    }
    if (!isAvailable()) {
        return false;
    }
    status = Status::Dispatched;
    currentIncidentId = incidentId;
    return true;
}

void Ambulance::release() {
    status = Status::Available;
    currentIncidentId = -1;
}

void Ambulance::markUnavailable() {
    status = Status::Unavailable;
    currentIncidentId = -1;
}

std::string Ambulance::statusToString(Status status) {
    switch (status) {
    case Status::Available: return "Available";
    case Status::Dispatched: return "Dispatched";
    case Status::Unavailable: return "Unavailable";
    }
    throw std::invalid_argument("invalid ambulance status");
}

}
```

## src/DataCache.cpp

```cpp
#include "DataCache.hpp"
#include "FileStore.hpp"

#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace rapidaid::data {

namespace {

std::string toLower(std::string str) {
    for (char& ch : str) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return str;
}

std::string formatCoord4(double value) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(4) << value;
    return ss.str();
}

} // anonymous namespace

DataCache::DataCache(std::string dir, bool refresh)
    : m_dir(std::move(dir)), m_refresh(refresh) {}

std::string DataCache::geocodePath(const std::string& query) const {
    return m_dir + "/geocode_" + rapidaid::io::FileStore::sanitizeFileName(query) + ".json";
}

std::string DataCache::placesPath(rapidaid::osm::Category c, double lat, double lon, int radiusMeters) const {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << m_dir << "/places_"
       << toLower(rapidaid::osm::OverpassClient::categoryName(c)) << "_"
       << formatCoord4(lat) << "_"
       << formatCoord4(lon) << "_"
       << radiusMeters << ".json";
    return ss.str();
}

std::string DataCache::roadsPath(double lat, double lon, int radiusMeters) const {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << m_dir << "/roads_"
       << formatCoord4(lat) << "_"
       << formatCoord4(lon) << "_"
       << radiusMeters << ".json";
    return ss.str();
}

template <typename Result, typename Parser, typename Fetcher, typename IsEmpty>
Result DataCache::getCachedOrFetch(const std::string& path,
                                   Parser&& parser,
                                   Fetcher&& fetcher,
                                   IsEmpty&& isEmpty) {
    if (!m_refresh && rapidaid::io::FileStore::exists(path)) {
        std::string cached = rapidaid::io::FileStore::load(path);
        if (!cached.empty()) {
            Result result = parser(cached);
            if (!isEmpty(result)) {
                return result;
            }
        }
    }

    std::string raw = fetcher();
    if (raw.empty()) {
        return Result{};
    }

    Result result = parser(raw);
    if (!isEmpty(result)) {
        rapidaid::io::FileStore::save(path, raw);
    }
    return result;
}

std::optional<rapidaid::geo::Location> DataCache::geocode(const std::string& query) {
    return getCachedOrFetch<std::optional<rapidaid::geo::Location>>(
        geocodePath(query),
        [](const std::string& body) {
            return rapidaid::geo::Geocoder::parseResponse(body);
        },
        [&query]() {
            return rapidaid::geo::Geocoder::fetchRaw(query);
        },
        [](const std::optional<rapidaid::geo::Location>& loc) {
            return !loc.has_value();
        }
    );
}

std::vector<rapidaid::osm::Place> DataCache::places(rapidaid::osm::Category c,
                                                   double lat,
                                                   double lon,
                                                   int radiusMeters) {
    return getCachedOrFetch<std::vector<rapidaid::osm::Place>>(
        placesPath(c, lat, lon, radiusMeters),
        [c](const std::string& body) {
            return rapidaid::osm::OverpassClient::parseResponse(body, c);
        },
        [c, lat, lon, radiusMeters]() {
            return rapidaid::osm::OverpassClient::fetchRaw(c, lat, lon, radiusMeters);
        },
        [](const std::vector<rapidaid::osm::Place>& pls) {
            return pls.empty();
        }
    );
}

rapidaid::osm::RoadData DataCache::roads(double lat, double lon, int radiusMeters) {
    return getCachedOrFetch<rapidaid::osm::RoadData>(
        roadsPath(lat, lon, radiusMeters),
        [](const std::string& body) {
            return rapidaid::osm::RoadClient::parseRoads(body);
        },
        [lat, lon, radiusMeters]() {
            return rapidaid::osm::RoadClient::fetchRaw(lat, lon, radiusMeters);
        },
        [](const rapidaid::osm::RoadData& rds) {
            return rds.empty();
        }
    );
}

} // namespace rapidaid::data

```

## src/FileStore.cpp

```cpp
#include "FileStore.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace rapidaid::io {

bool FileStore::save(const std::string& path, const std::string& text) {
    std::string tmpPath = path + ".tmp";
    try {
        std::filesystem::path targetPath(path);
        std::filesystem::path parent = targetPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out.good()) {
                out.close();
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
            out.close();
            if (out.fail()) {
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
        }

        std::filesystem::rename(tmpPath, targetPath);
        return true;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
}

std::string FileStore::load(const std::string& path) {
    try {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            return "";
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        return content;
    } catch (...) {
        return "";
    }
}

bool FileStore::exists(const std::string& path) {
    try {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec) && !ec;
    } catch (...) {
        return false;
    }
}

std::string FileStore::sanitizeFileName(const std::string& text) {
    std::string s;
    s.reserve(text.size());

    // 1. Convert to lowercase; characters not in [a-z0-9] become '_'
    for (unsigned char ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            s.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            s.push_back(static_cast<char>(ch));
        } else {
            s.push_back('_');
        }
    }

    // 2. Collapse repeated '_' into one (e.g. "___" -> "_")
    std::string collapsed;
    collapsed.reserve(s.size());
    bool prevWasUnderscore = false;
    for (char c : s) {
        if (c == '_') {
            if (!prevWasUnderscore) {
                collapsed.push_back('_');
                prevWasUnderscore = true;
            }
        } else {
            collapsed.push_back(c);
            prevWasUnderscore = false;
        }
    }

    // 3. Trim leading and trailing '_'
    size_t start = 0;
    while (start < collapsed.size() && collapsed[start] == '_') {
        ++start;
    }
    size_t end = collapsed.size();
    while (end > start && collapsed[end - 1] == '_') {
        --end;
    }

    std::string trimmed = collapsed.substr(start, end - start);

    // 4. Cap at 80 characters
    if (trimmed.size() > 80) {
        trimmed.resize(80);
    }

    // 5. Return "unnamed" if the result would be empty
    if (trimmed.empty()) {
        return "unnamed";
    }

    return trimmed;
}

} // namespace rapidaid::io

```

## src/Fire_station.cpp

```cpp
#include "Fire_station.hpp"

#include <utility>

namespace rapidaid::service {

FireStation::FireStation(int id, std::string name, double latitude, double longitude)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude) {}

int FireStation::getId() const { return id; }
const std::string& FireStation::getName() const { return name; }
double FireStation::getLatitude() const { return latitude; }
double FireStation::getLongitude() const { return longitude; }
long long FireStation::getGraphNodeId() const { return graphNodeId; }
void FireStation::setGraphNodeId(long long value) { graphNodeId = value; }

void FireStation::addTruck(FireTruck truck) {
    trucks.push_back(std::move(truck));
}

std::vector<FireTruck*> FireStation::getAvailableTrucks() {
    std::vector<FireTruck*> available;
    for (FireTruck& truck : trucks) {
        if (truck.isAvailable()) {
            available.push_back(&truck);
        }
    }
    return available;
}

FireTruck* FireStation::findTruckById(int truckId) {
    for (FireTruck& truck : trucks) {
        if (truck.getId() == truckId) {
            return &truck;
        }
    }
    return nullptr;
}

}

```

## src/Fire_truck.cpp

```cpp
#include "Fire_truck.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

FireTruck::FireTruck(int id, std::string name, double latitude, double longitude,
                     int homeStationId, int waterCapacityLitres)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      homeStationId(homeStationId),
      waterCapacityLitres(waterCapacityLitres) {
    if (waterCapacityLitres < 0) {
        throw std::invalid_argument("water capacity cannot be negative");
    }
}

int FireTruck::getId() const { return id; }
const std::string& FireTruck::getName() const { return name; }
double FireTruck::getLatitude() const { return latitude; }
double FireTruck::getLongitude() const { return longitude; }
long long FireTruck::getGraphNodeId() const { return graphNodeId; }
void FireTruck::setGraphNodeId(long long value) { graphNodeId = value; }
FireTruck::Status FireTruck::getStatus() const { return status; }
int FireTruck::getCurrentIncidentId() const { return currentIncidentId; }
int FireTruck::getHomeStationId() const { return homeStationId; }
int FireTruck::getWaterCapacityLitres() const { return waterCapacityLitres; }
bool FireTruck::isAvailable() const { return status == Status::Available; }

bool FireTruck::assign(int incidentId) {
    if (incidentId < 0) {
        throw std::invalid_argument("incident id cannot be negative");
    }
    if (!isAvailable()) {
        return false;
    }
    status = Status::Dispatched;
    currentIncidentId = incidentId;
    return true;
}

void FireTruck::release() {
    status = Status::Available;
    currentIncidentId = -1;
}

void FireTruck::markUnavailable() {
    status = Status::Unavailable;
    currentIncidentId = -1;
}

std::string FireTruck::statusToString(Status status) {
    switch (status) {
    case Status::Available: return "Available";
    case Status::Dispatched: return "Dispatched";
    case Status::Unavailable: return "Unavailable";
    }
    throw std::invalid_argument("invalid fire truck status");
}

}
```

## src/Geocoder.cpp

```cpp
#include "Geocoder.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <thread>
#include <cctype>
#include <algorithm>

namespace rapidaid::geo {

    static bool isAllWhitespaceOrEmpty(const std::string& str) {
        return std::all_of(str.begin(), str.end(), [](unsigned char c) {
            return std::isspace(c);
        });
    }

    void Geocoder::enforceRateLimit() {
        static std::chrono::steady_clock::time_point lastCall;
        static bool initialized = false;

        auto now = std::chrono::steady_clock::now();
        if (initialized) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCall);
            if (elapsed < std::chrono::milliseconds(1000)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000) - elapsed);
            }
        } else {
            initialized = true;
        }
        lastCall = std::chrono::steady_clock::now();
    }

    std::string Geocoder::buildUrl(const std::string& query) {
        return "https://nominatim.openstreetmap.org/search?q=" +
               rapidaid::net::HttpClient::urlEncode(query) +
               "&format=json&limit=1";
    }

    std::optional<Location> Geocoder::parseResponse(const std::string& body) {
        if (body.empty()) {
            return std::nullopt;
        }

        try {
            auto parsed = nlohmann::json::parse(body);
            if (!parsed.is_array() || parsed.empty()) {
                return std::nullopt;
            }

            const auto& first = parsed[0];
            if (!first.is_object() || !first.contains("lat") || !first.contains("lon")) {
                return std::nullopt;
            }

            const auto& latVal = first["lat"];
            const auto& lonVal = first["lon"];

            if (!latVal.is_string() || !lonVal.is_string()) {
                return std::nullopt;
            }

            std::string latStr = latVal.get<std::string>();
            std::string lonStr = lonVal.get<std::string>();

            std::size_t latIdx = 0;
            std::size_t lonIdx = 0;
            double lat = std::stod(latStr, &latIdx);
            double lon = std::stod(lonStr, &lonIdx);

            if (latIdx != latStr.size() || lonIdx != lonStr.size()) {
                return std::nullopt;
            }

            std::string displayName;
            if (first.contains("display_name") && first["display_name"].is_string()) {
                displayName = first["display_name"].get<std::string>();
            }

            return Location{lat, lon, displayName};
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        } catch (const std::out_of_range&) {
            return std::nullopt;
        }
    }

    std::string Geocoder::fetchRaw(const std::string& query) {
        if (isAllWhitespaceOrEmpty(query)) {
            return "";
        }

        enforceRateLimit();

        std::string url = buildUrl(query);
        return rapidaid::net::HttpClient::get(url);
    }

    std::optional<Location> Geocoder::geocode(const std::string& query) {
        return parseResponse(fetchRaw(query));
    }

} // namespace rapidaid::geo

```

## src/GeoJsonWriter.cpp

```cpp
#include "GeoJsonWriter.hpp"
#include "FileStore.hpp"
#include "json.hpp"

#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rapidaid::geojson {

namespace {

double roundCoord7(double value) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(7) << value;
    std::istringstream iss(ss.str());
    iss.imbue(std::locale::classic());
    double rounded = 0.0;
    iss >> rounded;
    return rounded;
}

nlohmann::json makePointGeometry(double lat, double lon) {
    nlohmann::json geom;
    geom["type"] = "Point";
    geom["coordinates"] = nlohmann::json::array({roundCoord7(lon), roundCoord7(lat)});
    return geom;
}

nlohmann::json makeEmptyFeatureCollection() {
    nlohmann::json fc;
    fc["type"] = "FeatureCollection";
    fc["features"] = nlohmann::json::array();
    return fc;
}

} // anonymous namespace

std::string GeoJsonWriter::placesCollection(const std::vector<rapidaid::osm::Place>& places) {
    nlohmann::json fc = makeEmptyFeatureCollection();

    for (const auto& place : places) {
        nlohmann::json feature;
        feature["type"] = "Feature";
        feature["geometry"] = makePointGeometry(place.lat, place.lon);

        nlohmann::json props;
        props["id"] = place.id;
        props["osmType"] = place.osmType;
        props["name"] = place.name;
        props["category"] = rapidaid::osm::OverpassClient::categoryName(place.category);
        props["tags"] = place.tags;

        feature["properties"] = std::move(props);
        fc["features"].push_back(std::move(feature));
    }

    return fc.dump();
}

std::string GeoJsonWriter::snappedPlacesCollection(const std::vector<rapidaid::graph::SnappedPlace>& places) {
    nlohmann::json fc = makeEmptyFeatureCollection();

    for (const auto& sp : places) {
        nlohmann::json feature;
        feature["type"] = "Feature";
        feature["geometry"] = makePointGeometry(sp.place.lat, sp.place.lon);

        nlohmann::json props;
        props["id"] = sp.place.id;
        props["osmType"] = sp.place.osmType;
        props["name"] = sp.place.name;
        props["category"] = rapidaid::osm::OverpassClient::categoryName(sp.place.category);
        props["tags"] = sp.place.tags;
        props["snapped"] = sp.snapped;
        props["snapDistanceMeters"] = sp.snapDistanceMeters;

        feature["properties"] = std::move(props);
        fc["features"].push_back(std::move(feature));
    }

    return fc.dump();
}

std::string GeoJsonWriter::pathCollection(const rapidaid::graph::Graph& g, const rapidaid::graph::PathResult& path) {
    if (!path.found || path.nodes.size() < 2) {
        return makeEmptyFeatureCollection().dump();
    }

    int nodeCount = g.nodeCount();
    for (int nodeIdx : path.nodes) {
        if (nodeIdx < 0 || nodeIdx >= nodeCount) {
            return makeEmptyFeatureCollection().dump();
        }
    }

    nlohmann::json fc = makeEmptyFeatureCollection();

    nlohmann::json feature;
    feature["type"] = "Feature";

    nlohmann::json geom;
    geom["type"] = "LineString";
    nlohmann::json coords = nlohmann::json::array();
    for (int nodeIdx : path.nodes) {
        coords.push_back(nlohmann::json::array({roundCoord7(g.lon(nodeIdx)), roundCoord7(g.lat(nodeIdx))}));
    }
    geom["coordinates"] = std::move(coords);
    feature["geometry"] = std::move(geom);

    nlohmann::json props;
    props["distanceMeters"] = path.distanceMeters;
    props["nodeCount"] = static_cast<int>(path.nodes.size());
    feature["properties"] = std::move(props);

    fc["features"].push_back(std::move(feature));
    return fc.dump();
}

std::string GeoJsonWriter::roadsCollection(const rapidaid::graph::Graph& g) {
    nlohmann::json fc = makeEmptyFeatureCollection();

    int n = g.nodeCount();
    for (int u = 0; u < n; ++u) {
        const auto& neighbors = g.neighbors(u);
        for (const auto& edge : neighbors) {
            int v = edge.to;
            if (u < v) {
                nlohmann::json feature;
                feature["type"] = "Feature";

                nlohmann::json geom;
                geom["type"] = "LineString";
                geom["coordinates"] = nlohmann::json::array({
                    nlohmann::json::array({roundCoord7(g.lon(u)), roundCoord7(g.lat(u))}),
                    nlohmann::json::array({roundCoord7(g.lon(v)), roundCoord7(g.lat(v))})
                });
                feature["geometry"] = std::move(geom);

                nlohmann::json props;
                props["weightMeters"] = edge.weightMeters;
                feature["properties"] = std::move(props);

                fc["features"].push_back(std::move(feature));
            }
        }
    }

    return fc.dump();
}

bool GeoJsonWriter::writeFile(const std::string& path, const std::string& geojson) {
    return rapidaid::io::FileStore::save(path, geojson);
}

} // namespace rapidaid::geojson

```

## src/GeoMath.cpp

```cpp
#include "GeoMath.hpp"

#include <cmath>

namespace rapidaid::geo {

namespace {
    constexpr double EARTH_RADIUS_METERS = 6371000.0;
    constexpr double PI = 3.14159265358979323846;

    inline double degToRad(double deg) {
        return deg * (PI / 180.0);
    }
} // namespace

double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    if (lat1 == lat2 && lon1 == lon2) {
        return 0.0;
    }

    const double lat1Rad = degToRad(lat1);
    const double lat2Rad = degToRad(lat2);
    const double dlat = degToRad(lat2 - lat1);

    double dlon = degToRad(lon2 - lon1);
    while (dlon > PI) {
        dlon -= 2.0 * PI;
    }
    while (dlon < -PI) {
        dlon += 2.0 * PI;
    }

    const double sinHalfDLat = std::sin(dlat / 2.0);
    const double sinHalfDLon = std::sin(dlon / 2.0);

    double a = sinHalfDLat * sinHalfDLat +
               std::cos(lat1Rad) * std::cos(lat2Rad) * sinHalfDLon * sinHalfDLon;

    if (a < 0.0) {
        a = 0.0;
    } else if (a > 1.0) {
        a = 1.0;
    }

    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return EARTH_RADIUS_METERS * c;
}

} // namespace rapidaid::geo

```

## src/Graph.cpp

```cpp
#include "Graph.hpp"
#include "GeoMath.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace rapidaid::graph {

int Graph::addNode(long long osmId, double lat, double lon) {
    auto it = m_osmIdToIndex.find(osmId);
    if (it != m_osmIdToIndex.end()) {
        return it->second;
    }
    int newIndex = static_cast<int>(m_nodes.size());
    m_nodes.push_back({osmId, lat, lon, {}});
    m_osmIdToIndex[osmId] = newIndex;
    return newIndex;
}

void Graph::addEdge(int a, int b, double weightMeters) {
    if (weightMeters < 0.0) {
        return;
    }
    if (a == b) {
        return;
    }
    int count = static_cast<int>(m_nodes.size());
    if (a < 0 || a >= count || b < 0 || b >= count) {
        return;
    }

    // Check if edge already exists
    for (auto& edge : m_nodes[a].edges) {
        if (edge.to == b) {
            if (weightMeters < edge.weightMeters) {
                edge.weightMeters = weightMeters;
                for (auto& revEdge : m_nodes[b].edges) {
                    if (revEdge.to == a) {
                        revEdge.weightMeters = weightMeters;
                        break;
                    }
                }
            }
            return;
        }
    }

    // New undirected edge
    m_nodes[a].edges.push_back({b, weightMeters});
    m_nodes[b].edges.push_back({a, weightMeters});
    ++m_edgeCount;
}

int Graph::nodeCount() const {
    return static_cast<int>(m_nodes.size());
}

size_t Graph::edgeCount() const {
    return m_edgeCount;
}

const std::vector<Edge>& Graph::neighbors(int index) const {
    static const std::vector<Edge> kEmptyNeighbors;
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return kEmptyNeighbors;
    }
    return m_nodes[index].edges;
}

long long Graph::osmId(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return 0;
    }
    return m_nodes[index].osmId;
}

double Graph::lat(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return 0.0;
    }
    return m_nodes[index].lat;
}

double Graph::lon(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return 0.0;
    }
    return m_nodes[index].lon;
}

int Graph::indexOf(long long osmId) const {
    auto it = m_osmIdToIndex.find(osmId);
    if (it == m_osmIdToIndex.end()) {
        return -1;
    }
    return it->second;
}

int Graph::nearestNode(double lat, double lon, double* distanceMetersOut) const {
    if (m_nodes.empty()) {
        return -1;
    }

    int bestIndex = -1;
    double minDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        double d = rapidaid::geo::haversineMeters(lat, lon, m_nodes[i].lat, m_nodes[i].lon);
        if (d < minDistance) {
            minDistance = d;
            bestIndex = i;
        }
    }

    if (distanceMetersOut != nullptr) {
        *distanceMetersOut = minDistance;
    }
    return bestIndex;
}

PathResult Graph::shortestPath(int from, int to) const {
    int totalNodes = static_cast<int>(m_nodes.size());
    if (from < 0 || from >= totalNodes || to < 0 || to >= totalNodes) {
        return PathResult{false, 0.0, {}};
    }

    if (from == to) {
        return PathResult{true, 0.0, {from}};
    }

    const double kInfinity = std::numeric_limits<double>::infinity();
    std::vector<double> dist(totalNodes, kInfinity);
    std::vector<int> pred(totalNodes, -1);

    using QueueElement = std::pair<double, int>;
    std::priority_queue<QueueElement, std::vector<QueueElement>, std::greater<QueueElement>> pq;

    dist[from] = 0.0;
    pq.push({0.0, from});

    while (!pq.empty()) {
        auto [currentDist, u] = pq.top();
        pq.pop();

        // Skip stale queue entries
        if (currentDist > dist[u]) {
            continue;
        }

        if (u == to) {
            break;
        }

        for (const auto& edge : m_nodes[u].edges) {
            int v = edge.to;
            double nextDist = dist[u] + edge.weightMeters;
            if (nextDist < dist[v]) {
                dist[v] = nextDist;
                pred[v] = u;
                pq.push({nextDist, v});
            }
        }
    }

    if (dist[to] == kInfinity) {
        return PathResult{false, 0.0, {}};
    }

    std::vector<int> path;
    for (int curr = to; curr != -1; curr = pred[curr]) {
        path.push_back(curr);
    }
    std::reverse(path.begin(), path.end());

    return PathResult{true, dist[to], path};
}

Graph Graph::largestComponent() const {
    int count = static_cast<int>(m_nodes.size());
    if (count == 0) {
        return Graph();
    }

    std::vector<bool> visited(count, false);
    std::vector<int> bestComponent;

    for (int i = 0; i < count; ++i) {
        if (visited[i]) {
            continue;
        }

        std::vector<int> currentComponent;
        std::vector<int> queue;
        queue.push_back(i);
        visited[i] = true;
        size_t head = 0;

        while (head < queue.size()) {
            int u = queue[head++];
            currentComponent.push_back(u);

            for (const auto& edge : m_nodes[u].edges) {
                int v = edge.to;
                if (!visited[v]) {
                    visited[v] = true;
                    queue.push_back(v);
                }
            }
        }

        // We iterate i from 0 to count - 1.
        // Therefore, node i is the minimum index in currentComponent.
        // If currentComponent is strictly larger than bestComponent, it becomes bestComponent.
        // In case of a tie in size, the earlier component had a lower root index,
        // so strictly greater preserves the component with the lower index.
        if (currentComponent.size() > bestComponent.size()) {
            bestComponent = std::move(currentComponent);
        }
    }

    Graph componentGraph;
    if (bestComponent.empty()) {
        return componentGraph;
    }

    std::vector<int> oldToNew(count, -1);
    for (int oldIndex : bestComponent) {
        int newIndex = componentGraph.addNode(
            m_nodes[oldIndex].osmId,
            m_nodes[oldIndex].lat,
            m_nodes[oldIndex].lon
        );
        oldToNew[oldIndex] = newIndex;
    }

    for (int oldIndex : bestComponent) {
        int newU = oldToNew[oldIndex];
        for (const auto& edge : m_nodes[oldIndex].edges) {
            int oldV = edge.to;
            if (oldIndex < oldV) {
                int newV = oldToNew[oldV];
                componentGraph.addEdge(newU, newV, edge.weightMeters);
            }
        }
    }

    return componentGraph;
}

} // namespace rapidaid::graph

```

## src/GraphBuilder.cpp

```cpp
#include "GraphBuilder.hpp"
#include "GeoMath.hpp"

namespace rapidaid::graph {

Graph GraphBuilder::build(const rapidaid::osm::RoadData& roads, bool keepLargestComponent) {
    Graph graph;

    for (const auto& way : roads.ways) {
        if (way.nodeIds.size() < 2) {
            continue;
        }

        for (size_t i = 0; i + 1 < way.nodeIds.size(); ++i) {
            long long idA = way.nodeIds[i];
            long long idB = way.nodeIds[i + 1];

            auto itA = roads.nodes.find(idA);
            auto itB = roads.nodes.find(idB);

            if (itA == roads.nodes.end() || itB == roads.nodes.end()) {
                continue;
            }

            const auto& nodeA = itA->second;
            const auto& nodeB = itB->second;

            int idxA = graph.addNode(nodeA.id, nodeA.lat, nodeA.lon);
            int idxB = graph.addNode(nodeB.id, nodeB.lat, nodeB.lon);

            double dist = rapidaid::geo::haversineMeters(nodeA.lat, nodeA.lon, nodeB.lat, nodeB.lon);
            graph.addEdge(idxA, idxB, dist);
        }
    }

    if (keepLargestComponent) {
        return graph.largestComponent();
    }
    return graph;
}

std::vector<SnappedPlace> GraphBuilder::snapPlaces(const Graph& graph,
                                                  const std::vector<rapidaid::osm::Place>& places,
                                                  double maxSnapMeters) {
    std::vector<SnappedPlace> result;
    result.reserve(places.size());

    for (const auto& place : places) {
        SnappedPlace sp;
        sp.place = place;
        sp.nodeIndex = -1;
        sp.snapDistanceMeters = 0.0;
        sp.snapped = false;

        if (graph.nodeCount() > 0) {
            double dist = 0.0;
            int nearest = graph.nearestNode(place.lat, place.lon, &dist);
            if (nearest != -1 && dist <= maxSnapMeters) {
                sp.nodeIndex = nearest;
                sp.snapDistanceMeters = dist;
                sp.snapped = true;
            } else {
                sp.nodeIndex = -1;
                sp.snapDistanceMeters = dist;
                sp.snapped = false;
            }
        }

        result.push_back(sp);
    }

    return result;
}

} // namespace rapidaid::graph

```

## src/Hospital.cpp

```cpp
#include "Hospital.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

Hospital::Hospital(int id, std::string name, double latitude, double longitude,
                   int totalBeds, int occupiedBeds)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      totalBeds(totalBeds),
      occupiedBeds(occupiedBeds) {
    if (totalBeds < 0 || occupiedBeds < 0 || occupiedBeds > totalBeds) {
        throw std::invalid_argument("hospital bed counts are invalid");
    }
}

int Hospital::getId() const { return id; }
const std::string& Hospital::getName() const { return name; }
double Hospital::getLatitude() const { return latitude; }
double Hospital::getLongitude() const { return longitude; }
long long Hospital::getGraphNodeId() const { return graphNodeId; }
void Hospital::setGraphNodeId(long long value) { graphNodeId = value; }
int Hospital::getTotalBeds() const { return totalBeds; }
int Hospital::getOccupiedBeds() const { return occupiedBeds; }

void Hospital::addAmbulance(Ambulance ambulance) {
    ambulances.push_back(std::move(ambulance));
}

std::vector<Ambulance*> Hospital::getAvailableAmbulances() {
    std::vector<Ambulance*> available;
    for (Ambulance& ambulance : ambulances) {
        if (ambulance.isAvailable()) {
            available.push_back(&ambulance);
        }
    }
    return available;
}

int Hospital::availableBeds() const {
    return totalBeds - occupiedBeds;
}

bool Hospital::admitPatients(int count) {
    if (count < 0) {
        throw std::invalid_argument("patient count cannot be negative");
    }
    if (count > availableBeds()) {
        return false;
    }
    occupiedBeds += count;
    return true;
}

void Hospital::dischargePatients(int count) {
    if (count < 0) {
        throw std::invalid_argument("patient count cannot be negative");
    }
    occupiedBeds = count >= occupiedBeds ? 0 : occupiedBeds - count;
}

Ambulance* Hospital::findAmbulanceById(int ambulanceId) {
    for (Ambulance& ambulance : ambulances) {
        if (ambulance.getId() == ambulanceId) {
            return &ambulance;
        }
    }
    return nullptr;
}

}
```

## src/HttpClient.cpp

```cpp
#if __has_include("Config.local.hpp")
    #include "Config.local.hpp"
#else
    #error "Config.local.hpp not found. Copy include/Config.example.hpp to include/Config.local.hpp and set kContact."
#endif

#include "HttpClient.hpp"
#include <iostream>
#include <string>
#include <curl/curl.h>

namespace {
    constexpr const char* USER_AGENT_BASE = "RapidAid/0.1";

    // RAII helper to ensure curl_global_init and curl_global_cleanup are managed properly
    struct CurlGlobalGuard {
        CurlGlobalGuard() {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }
        ~CurlGlobalGuard() {
            curl_global_cleanup();
        }
    };
}

namespace rapidaid::net {

    std::size_t HttpClient::writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
        std::size_t byteSize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<const char*>(contents), byteSize);
        return byteSize;
    }

    std::string HttpClient::performRequest(const std::string& url, const std::string* body) {
        // Function-local static object ensures curl_global_init runs once per process
        static const CurlGlobalGuard curlGlobalGuard;
        // Function-local static string ensures User-Agent is built once per process
        static const std::string userAgent = std::string(USER_AGENT_BASE) + " (" + rapidaid::config::kContact + ")";

        CURL* handle = curl_easy_init();
        if (!handle) {
            std::cerr << "Failed to initialize CURL (" << url << ")\n";
            return "";
        }

        std::string response;
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &HttpClient::writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, userAgent.c_str());

        if (body != nullptr) {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body->c_str());
        }

        CURLcode res = curl_easy_perform(handle);
        long status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        }

        // Cleanup must happen on every path after init succeeds
        curl_easy_cleanup(handle);

        if (res != CURLE_OK) {
            std::cerr << "CURL error: " << curl_easy_strerror(res) << " (" << url << ")\n";
            return "";
        }

        if (status >= 400) {
            std::cerr << "HTTP error " << status << " (" << url << ")\n";
            return "";
        }

        return response;
    }

    std::string HttpClient::urlEncode(const std::string& text) {
        static const CurlGlobalGuard curlGlobalGuard;

        CURL* handle = curl_easy_init();
        if (!handle) {
            return "";
        }

        char* escaped = curl_easy_escape(handle, text.c_str(), static_cast<int>(text.length()));
        if (!escaped) {
            curl_easy_cleanup(handle);
            return "";
        }

        std::string result(escaped);
        curl_free(escaped);
        curl_easy_cleanup(handle);
        return result;
    }

    std::string HttpClient::get(const std::string& url) {
        return performRequest(url);
    }

    std::string HttpClient::post(const std::string& url, const std::string& data) {
        return performRequest(url, &data);
    }

} // namespace rapidaid::net
```

## src/Login.cpp

```cpp
#include "../Login.cpp"

```

## src/OverpassClient.cpp

```cpp
#include "OverpassClient.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace rapidaid::osm {

namespace {

const std::vector<std::pair<std::string, std::string>>& getCategoryTags(Category category) {
    static const std::vector<std::pair<std::string, std::string>> hospitalTags = {
        {"amenity", "hospital"}
    };
    static const std::vector<std::pair<std::string, std::string>> clinicTags = {
        {"amenity", "clinic"},
        {"amenity", "doctors"}
    };
    static const std::vector<std::pair<std::string, std::string>> pharmacyTags = {
        {"amenity", "pharmacy"}
    };
    static const std::vector<std::pair<std::string, std::string>> fireStationTags = {
        {"amenity", "fire_station"}
    };
    static const std::vector<std::pair<std::string, std::string>> policeTags = {
        {"amenity", "police"}
    };
    static const std::vector<std::pair<std::string, std::string>> shelterTags = {
        {"amenity", "shelter"},
        {"emergency", "assembly_point"}
    };
    static const std::vector<std::pair<std::string, std::string>> waterTags = {
        {"amenity", "drinking_water"},
        {"emergency", "water_tank"}
    };
    static const std::vector<std::pair<std::string, std::string>> emptyTags = {};

    switch (category) {
        case Category::Hospital:    return hospitalTags;
        case Category::Clinic:      return clinicTags;
        case Category::Pharmacy:    return pharmacyTags;
        case Category::FireStation: return fireStationTags;
        case Category::Police:      return policeTags;
        case Category::Shelter:     return shelterTags;
        case Category::Water:       return waterTags;
        default:                    return emptyTags;
    }
}

} // anonymous namespace

std::string OverpassClient::categoryName(Category category) {
    switch (category) {
        case Category::Hospital:    return "hospital";
        case Category::Clinic:      return "clinic";
        case Category::Pharmacy:    return "pharmacy";
        case Category::FireStation: return "fire_station";
        case Category::Police:      return "police";
        case Category::Shelter:     return "shelter";
        case Category::Water:       return "water";
        default:                    return "unknown";
    }
}

std::string OverpassClient::buildQuery(Category category, double lat, double lon, int radiusMeters) {
    const auto& tags = getCategoryTags(category);
    if (tags.empty()) {
        return "";
    }

    std::ostringstream ss;
    ss.imbue(std::locale::classic());

    ss << "[out:json][timeout:25]; (";

    static const char* const elementTypes[] = {"node", "way", "relation"};

    for (const char* elem : elementTypes) {
        for (const auto& tag : tags) {
            ss << " " << elem << "[\"" << tag.first << "\"=\"" << tag.second << "\"]"
               << "(around:" << radiusMeters << ","
               << std::fixed << std::setprecision(7) << lat << ","
               << std::fixed << std::setprecision(7) << lon << ");";
        }
    }

    ss << " ); out center tags;";
    return ss.str();
}

std::string OverpassClient::buildRequestBody(const std::string& query) {
    std::string encoded = rapidaid::net::HttpClient::urlEncode(query);
    if (encoded.empty() && !query.empty()) {
        return "";
    }
    return "data=" + encoded;
}

std::vector<Place> OverpassClient::parseResponse(const std::string& body, Category category) {
    std::vector<Place> places;
    if (body.empty()) {
        return places;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(body);
        if (!root.is_object() || !root.contains("elements") || !root["elements"].is_array()) {
            return places;
        }

        for (const auto& element : root["elements"]) {
            if (!element.is_object()) {
                continue;
            }

            if (!element.contains("id") || !element["id"].is_number_integer() ||
                !element.contains("type") || !element["type"].is_string()) {
                continue;
            }

            double lat = 0.0;
            double lon = 0.0;
            bool hasCoords = false;

            std::string osmType = element["type"].get<std::string>();

            if (osmType == "node") {
                if (element.contains("lat") && element["lat"].is_number() &&
                    element.contains("lon") && element["lon"].is_number()) {
                    lat = element["lat"].get<double>();
                    lon = element["lon"].get<double>();
                    hasCoords = true;
                }
            } else if (osmType == "way" || osmType == "relation") {
                if (element.contains("center") && element["center"].is_object()) {
                    const auto& center = element["center"];
                    if (center.contains("lat") && center["lat"].is_number() &&
                        center.contains("lon") && center["lon"].is_number()) {
                        lat = center["lat"].get<double>();
                        lon = center["lon"].get<double>();
                        hasCoords = true;
                    }
                }
            }

            if (!hasCoords) {
                continue;
            }

            Place place;
            place.id = element["id"].get<long long>();
            place.osmType = osmType;
            place.lat = lat;
            place.lon = lon;
            place.category = category;

            if (element.contains("tags") && element["tags"].is_object()) {
                for (auto it = element["tags"].begin(); it != element["tags"].end(); ++it) {
                    if (it.value().is_string()) {
                        place.tags[it.key()] = it.value().get<std::string>();
                    }
                }
                auto nameIt = place.tags.find("name");
                if (nameIt != place.tags.end()) {
                    place.name = nameIt->second;
                }
            }

            places.push_back(std::move(place));
        }
    } catch (const nlohmann::json::exception&) {
        places.clear();
        return places;
    }

    return places;
}

std::string OverpassClient::fetchRaw(Category category, double lat, double lon, int radiusMeters) {
    if (radiusMeters < 1 || radiusMeters > 50000) {
        return "";
    }
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return "";
    }

    std::string query = buildQuery(category, lat, lon, radiusMeters);
    if (query.empty()) {
        return "";
    }

    std::string body = buildRequestBody(query);
    if (body.empty()) {
        return "";
    }

    static const std::string overpassUrl = "https://overpass-api.de/api/interpreter";

    std::string response;
    const int maxAttempts = 3;
    const std::chrono::seconds backoffTimes[] = {
        std::chrono::seconds(2),
        std::chrono::seconds(4)
    };

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        response = rapidaid::net::HttpClient::post(overpassUrl, body);
        if (!response.empty()) {
            break;
        }

        if (attempt < maxAttempts - 1) {
            std::this_thread::sleep_for(backoffTimes[attempt]);
        }
    }

    return response;
}

std::vector<Place> OverpassClient::findNearby(Category category, double lat, double lon, int radiusMeters) {
    return parseResponse(fetchRaw(category, lat, lon, radiusMeters), category);
}

} // namespace rapidaid::osm

```

## src/Police_station.cpp

```cpp
#include "Police_station.hpp"

#include <utility>

namespace rapidaid::service {

PoliceStation::PoliceStation(int id, std::string name, double latitude, double longitude)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude) {}

int PoliceStation::getId() const { return id; }
const std::string& PoliceStation::getName() const { return name; }
double PoliceStation::getLatitude() const { return latitude; }
double PoliceStation::getLongitude() const { return longitude; }
long long PoliceStation::getGraphNodeId() const { return graphNodeId; }
void PoliceStation::setGraphNodeId(long long value) { graphNodeId = value; }

void PoliceStation::addUnit(PoliceUnit unit) {
    units.push_back(std::move(unit));
}

std::vector<PoliceUnit*> PoliceStation::getAvailableUnits() {
    std::vector<PoliceUnit*> available;
    for (PoliceUnit& unit : units) {
        if (unit.isAvailable()) {
            available.push_back(&unit);
        }
    }
    return available;
}

PoliceUnit* PoliceStation::findUnitById(int unitId) {
    for (PoliceUnit& unit : units) {
        if (unit.getId() == unitId) {
            return &unit;
        }
    }
    return nullptr;
}

}

```

## src/Police_unit.cpp

```cpp
#include "Police_unit.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

PoliceUnit::PoliceUnit(int id, std::string name, double latitude, double longitude,
                       int homeStationId, int officerCount)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      homeStationId(homeStationId),
      officerCount(officerCount) {
    if (officerCount < 0) {
        throw std::invalid_argument("officer count cannot be negative");
    }
}

int PoliceUnit::getId() const { return id; }
const std::string& PoliceUnit::getName() const { return name; }
double PoliceUnit::getLatitude() const { return latitude; }
double PoliceUnit::getLongitude() const { return longitude; }
long long PoliceUnit::getGraphNodeId() const { return graphNodeId; }
void PoliceUnit::setGraphNodeId(long long value) { graphNodeId = value; }
PoliceUnit::Status PoliceUnit::getStatus() const { return status; }
int PoliceUnit::getCurrentIncidentId() const { return currentIncidentId; }
int PoliceUnit::getHomeStationId() const { return homeStationId; }
int PoliceUnit::getOfficerCount() const { return officerCount; }
bool PoliceUnit::isAvailable() const { return status == Status::Available; }

bool PoliceUnit::assign(int incidentId) {
    if (incidentId < 0) {
        throw std::invalid_argument("incident id cannot be negative");
    }
    if (!isAvailable()) {
        return false;
    }
    status = Status::Dispatched;
    currentIncidentId = incidentId;
    return true;
}

void PoliceUnit::release() {
    status = Status::Available;
    currentIncidentId = -1;
}

void PoliceUnit::markUnavailable() {
    status = Status::Unavailable;
    currentIncidentId = -1;
}

std::string PoliceUnit::statusToString(Status status) {
    switch (status) {
    case Status::Available: return "Available";
    case Status::Dispatched: return "Dispatched";
    case Status::Unavailable: return "Unavailable";
    }
    throw std::invalid_argument("invalid police unit status");
}

}
```

## src/priority_queue.cpp

```cpp
    #include<iostream>
    #include<string>
    #include<queue>
    #include<vector>
    using namespace std;

    class Incident {
        public:
            int id;
            int severity;
            int peopleAffected;
            int priority;
            int waitingTime;
            string status;
            string location;
            string type;
        
        Incident(){}
        Incident(int id,int severity,int peopleAffected,int waitingTime,string status,string location,string type){
            this->id=id;
            this->severity=severity;
            this->peopleAffected=peopleAffected;
            this->waitingTime=waitingTime;
            this->status=status;
            this->location=location;
            this->type = type;
        };
        void getdata(){
            cout<<"\nEnter Incident id: ";
            cin>>id;
            cout<<"\nEnter severity: ";
            cin>>severity;
            cout<<"\nEnter People Affected: ";
            cin>>peopleAffected;
            cout<<"\nEnter waiting time: ";
            cin>>waitingTime;
            cout<<"\nEnter status: ";
            cin.ignore();
            cin>>status;
            cout<<"\nEnter location: ";
            cin.ignore();
            getline(cin,location);
            cout<<"\nEnter Incident type: ";
            cin.ignore();
            cin>>type;
            calculatePriority();
        }
        void calculatePriority(){
            priority=severity*10 + peopleAffected*2 + waitingTime; 
        }
    };
    struct compareIncident{
        bool operator()(const Incident& s1,const Incident &s2){
            return s1.priority < s2.priority;
        }
    };

    int main(){
        priority_queue<Incident, vector<Incident> ,compareIncident> p;
        int ch;
        do{
            cout<<"1.Enter Incident\n2.Exit\nEnter Choice: ";
            cin>>ch;
            if(ch==1){
                Incident i;
                i.getdata();
                p.push(i);
            }
        }while(ch!=2);

        while(!p.empty()){
            Incident s=p.top();
            p.pop();
            cout << "Incident ID: " << s.id << endl;
            cout << "Priority: " << s.priority << endl;
            cout << "Type: " << s.type << endl;
            cout << "Location: " << s.location << endl;
            cout << "\n";
        }
        return 0;
    }
```

## src/RoadClient.cpp

```cpp
#include "RoadClient.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace rapidaid::osm {

bool RoadData::empty() const {
    return ways.empty();
}

std::string RoadClient::buildRoadQuery(double lat, double lon, int radiusMeters) {
    if (radiusMeters < 1 || radiusMeters > 5000) {
        return "";
    }
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return "";
    }

    std::ostringstream ss;
    ss.imbue(std::locale::classic());

    ss << "[out:json][timeout:60];way[\"highway\"](around:"
       << radiusMeters << ","
       << std::fixed << std::setprecision(7) << lat << ","
       << std::fixed << std::setprecision(7) << lon
       << ");(._;>;);out body;";

    return ss.str();
}

std::string RoadClient::buildRequestBody(const std::string& query) {
    return "data=" + rapidaid::net::HttpClient::urlEncode(query);
}

std::string RoadClient::fetchRaw(double lat, double lon, int radiusMeters) {
    if (radiusMeters < 1 || radiusMeters > 5000) {
        return "";
    }
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return "";
    }

    std::string query = buildRoadQuery(lat, lon, radiusMeters);
    if (query.empty()) {
        return "";
    }

    std::string body = buildRequestBody(query);

    static const std::string overpassUrl = "https://overpass-api.de/api/interpreter";

    std::string response;
    const int maxAttempts = 3;
    const std::chrono::seconds backoffTimes[] = {
        std::chrono::seconds(2),
        std::chrono::seconds(4)
    };

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        response = rapidaid::net::HttpClient::post(overpassUrl, body);
        if (!response.empty()) {
            return response;
        }

        if (attempt < maxAttempts - 1) {
            std::this_thread::sleep_for(backoffTimes[attempt]);
        }
    }

    return "";
}

bool RoadClient::isDrivable(const std::map<std::string, std::string>& tags) {
    auto hwIt = tags.find("highway");
    if (hwIt == tags.end()) {
        return false;
    }

    std::string hw = hwIt->second;
    const std::string suffix = "_link";
    if (hw.length() >= suffix.length() &&
        hw.compare(hw.length() - suffix.length(), suffix.length(), suffix) == 0) {
        hw = hw.substr(0, hw.length() - suffix.length());
    }

    static const std::unordered_set<std::string> drivableTypes = {
        "motorway",
        "trunk",
        "primary",
        "secondary",
        "tertiary",
        "unclassified",
        "residential",
        "living_street",
        "service"
    };

    if (drivableTypes.find(hw) == drivableTypes.end()) {
        return false;
    }

    auto accessIt = tags.find("access");
    if (accessIt != tags.end() && (accessIt->second == "private" || accessIt->second == "no")) {
        return false;
    }

    auto mvIt = tags.find("motor_vehicle");
    if (mvIt != tags.end() && mvIt->second == "no") {
        return false;
    }

    auto vehIt = tags.find("vehicle");
    if (vehIt != tags.end() && vehIt->second == "no") {
        return false;
    }

    auto areaIt = tags.find("area");
    if (areaIt != tags.end() && areaIt->second == "yes") {
        return false;
    }

    return true;
}

RoadData RoadClient::parseRoads(const std::string& body) {
    RoadData result;
    if (body.empty()) {
        return result;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(body);
        if (!root.is_object() || !root.contains("elements") || !root["elements"].is_array()) {
            return result;
        }

        for (const auto& elem : root["elements"]) {
            if (!elem.is_object() || !elem.contains("type") || !elem["type"].is_string()) {
                continue;
            }

            std::string type = elem["type"].get<std::string>();

            if (type == "node") {
                if (!elem.contains("id") || !elem["id"].is_number_integer() ||
                    !elem.contains("lat") || !elem["lat"].is_number() ||
                    !elem.contains("lon") || !elem["lon"].is_number()) {
                    continue;
                }

                RoadNode node;
                node.id = elem["id"].get<long long>();
                node.lat = elem["lat"].get<double>();
                node.lon = elem["lon"].get<double>();
                result.nodes[node.id] = node;
            } else if (type == "way") {
                if (!elem.contains("id") || !elem["id"].is_number_integer()) {
                    continue;
                }

                if (!elem.contains("nodes") || !elem["nodes"].is_array()) {
                    continue;
                }

                std::vector<long long> nodeIds;
                for (const auto& nId : elem["nodes"]) {
                    if (nId.is_number_integer()) {
                        nodeIds.push_back(nId.get<long long>());
                    }
                }

                if (nodeIds.size() < 2) {
                    continue;
                }

                std::map<std::string, std::string> tags;
                if (elem.contains("tags") && elem["tags"].is_object()) {
                    for (auto it = elem["tags"].begin(); it != elem["tags"].end(); ++it) {
                        if (it.value().is_string()) {
                            tags[it.key()] = it.value().get<std::string>();
                        } else {
                            tags[it.key()] = it.value().dump();
                        }
                    }
                }

                if (!isDrivable(tags)) {
                    continue;
                }

                RoadWay way;
                way.id = elem["id"].get<long long>();
                way.nodeIds = std::move(nodeIds);

                auto hwIt = tags.find("highway");
                if (hwIt != tags.end()) {
                    way.highway = hwIt->second;
                }

                auto nameIt = tags.find("name");
                if (nameIt != tags.end()) {
                    way.name = nameIt->second;
                }

                way.tags = std::move(tags);

                result.ways.push_back(std::move(way));
            }
        }
    } catch (const nlohmann::json::exception&) {
        return RoadData{};
    }

    return result;
}

} // namespace rapidaid::osm

```

## src/Shelter.cpp

```cpp
#include "Shelter.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

Shelter::Shelter(int id, std::string name, double latitude, double longitude,
                 int capacity, int occupied)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      capacity(capacity),
      occupied(occupied) {
    if (capacity < 0 || occupied < 0 || occupied > capacity) {
        throw std::invalid_argument("shelter capacity and occupancy are invalid");
    }
}

int Shelter::getId() const { return id; }
const std::string& Shelter::getName() const { return name; }
double Shelter::getLatitude() const { return latitude; }
double Shelter::getLongitude() const { return longitude; }
long long Shelter::getGraphNodeId() const { return graphNodeId; }
void Shelter::setGraphNodeId(long long value) { graphNodeId = value; }
int Shelter::getCapacity() const { return capacity; }
int Shelter::getOccupied() const { return occupied; }

int Shelter::availableSpace() const {
    return capacity - occupied;
}

bool Shelter::admit(int count) {
    if (count < 0) {
        throw std::invalid_argument("admission count cannot be negative");
    }
    if (count > availableSpace()) {
        return false;
    }
    occupied += count;
    return true;
}

void Shelter::leave(int count) {
    if (count < 0) {
        throw std::invalid_argument("departure count cannot be negative");
    }
    occupied = count >= occupied ? 0 : occupied - count;
}

bool Shelter::isFull() const {
    return occupied == capacity;
}

}
```

## src/UiBridge.cpp

```cpp
#include "UiBridge.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "GeoJsonWriter.hpp"
#include "UnitRegistry.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace rapidaid::ui {

namespace {

struct StoredIncident {
    std::string id;
    std::string title;
    std::string location;
    std::string type;
    std::string priority;
    std::string timestamp;
    std::string status;
    std::string notes;
};

struct SessionData {
    std::string username;
    std::string role;
};

// Global mutexes separating domain state from pipeline computation (B7)
std::mutex g_stateMutex;
std::mutex g_pipelineMutex;

// In-memory sessions: token -> SessionData
std::unordered_map<std::string, SessionData> g_sessions;

// Global UnitRegistry instance
UnitRegistry g_registry;

// In-memory mock storage for incidents
std::vector<StoredIncident>& getIncidentStore() {
    static std::vector<StoredIncident> store;
    return store;
}

std::string generateSessionToken() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t p1 = dist(gen);
    uint64_t p2 = dist(gen);
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::hex << std::setfill('0') << std::setw(16) << p1 << std::setw(16) << p2;
    return ss.str();
}

std::string currentTimestampIso() {
    std::time_t t = std::time(nullptr);
    std::tm buf{};
#if defined(_WIN32)
    localtime_s(&buf, &t);
#else
    localtime_r(&t, &buf);
#endif
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

rapidaid::osm::Category parseCategoryString(const std::string& cat) {
    if (cat == "clinic") return rapidaid::osm::Category::Clinic;
    if (cat == "pharmacy") return rapidaid::osm::Category::Pharmacy;
    if (cat == "fire_station" || cat == "firestation") return rapidaid::osm::Category::FireStation;
    if (cat == "police") return rapidaid::osm::Category::Police;
    if (cat == "shelter") return rapidaid::osm::Category::Shelter;
    if (cat == "water") return rapidaid::osm::Category::Water;
    return rapidaid::osm::Category::Hospital;
}

} // namespace

bool isTokenAuthorized(const std::string& token, const std::string& requiredRole) {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) return false;
    if (requiredRole == "dispatcher") {
        return (it->second.role == "dispatcher" || it->second.role == "admin");
    }
    return it->second.role == requiredRole;
}

void resetBridgeState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_sessions.clear();
    g_registry.clear();
    g_registry.setDataPath("data/units.json");
}

bool checkGeoJsonExists(const std::string& outDir) {
    std::error_code ec;
    fs::path dir(outDir);
    fs::path roads = dir / "roads.geojson";
    fs::path places = dir / "places.geojson";

    bool roadsOk = fs::exists(roads, ec) && !ec && fs::file_size(roads, ec) > 0;
    bool placesOk = fs::exists(places, ec) && !ec && fs::file_size(places, ec) > 0;
    return roadsOk || placesOk;
}

std::string handleAreaStatusRequest(const std::string& outDir) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        std::error_code ec;
        fs::path dir(outDir);
        fs::path roads = dir / "roads.geojson";
        fs::path places = dir / "places.geojson";

        bool roadsExists = fs::exists(roads, ec) && !ec && fs::file_size(roads, ec) > 0;
        bool placesExists = fs::exists(places, ec) && !ec && fs::file_size(places, ec) > 0;
        bool exists = roadsExists || placesExists;

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["exists"] = exists;
        resp["roadsExists"] = roadsExists;
        resp["placesExists"] = placesExists;
        resp["roadsFile"] = roads.lexically_normal().string();
        resp["placesFile"] = places.lexically_normal().string();
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["exists"] = false;
        resp["roadsExists"] = false;
        resp["placesExists"] = false;
        resp["roadsFile"] = outDir + "/roads.geojson";
        resp["placesFile"] = outDir + "/places.geojson";
        return resp.dump();
    }
}

std::string handleGeocodeRequest(rapidaid::data::DataCache& cache, const std::string& requestJson) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string query;
        if (j.is_object() && j.contains("query") && j["query"].is_string()) {
            query = j["query"].get<std::string>();
        } else if (j.is_object() && j.contains("name") && j["name"].is_string()) {
            query = j["name"].get<std::string>();
        } else {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (query.empty()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Query cannot be empty";
            return resp.dump();
        }

        auto loc = cache.geocode(query);
        if (!loc.has_value()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Location not found";
            return resp.dump();
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["query"] = query;
        resp["lat"] = loc->lat;
        resp["lon"] = loc->lon;
        resp["displayName"] = loc->displayName;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Geocoding failure";
        return resp.dump();
    }
}

std::string handleCreateAreaRequest(rapidaid::data::DataCache& cache, const std::string& requestJson, const std::string& outDir) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string name;
        if (j.contains("name") && j["name"].is_string()) {
            name = j["name"].get<std::string>();
        } else if (j.contains("query") && j["query"].is_string()) {
            name = j["query"].get<std::string>();
        } else {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Missing area name";
            return resp.dump();
        }

        int radius = 3000;
        if (j.contains("radius") && j["radius"].is_number()) {
            radius = j["radius"].get<int>();
        }
        if (radius <= 0) radius = 3000;

        double lat = 0.0;
        double lon = 0.0;
        std::string displayName = name;

        if (j.contains("lat") && j["lat"].is_number() && j.contains("lon") && j["lon"].is_number()) {
            lat = j["lat"].get<double>();
            lon = j["lon"].get<double>();
            if (j.contains("displayName") && j["displayName"].is_string()) {
                displayName = j["displayName"].get<std::string>();
            }
        } else {
            auto loc = cache.geocode(name);
            if (!loc.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Geocoding failed for: " + name;
                return resp.dump();
            }
            lat = loc->lat;
            lon = loc->lon;
            displayName = loc->displayName;
        }

        // 1. Fetch places & roads
        std::vector<rapidaid::osm::Place> places;
        std::string catStr = "all";
        if (!j.contains("category") || j["category"] == "all") {
            const rapidaid::osm::Category allCats[] = {
                rapidaid::osm::Category::Hospital,
                rapidaid::osm::Category::FireStation,
                rapidaid::osm::Category::Police,
                rapidaid::osm::Category::Clinic,
                rapidaid::osm::Category::Pharmacy,
                rapidaid::osm::Category::Shelter
            };
            for (auto c : allCats) {
                auto p = cache.places(c, lat, lon, radius);
                places.insert(places.end(), p.begin(), p.end());
            }
        } else {
            catStr = "hospital";
            if (j["category"].is_string()) {
                catStr = j["category"].get<std::string>();
            }
            rapidaid::osm::Category cat = parseCategoryString(catStr);
            places = cache.places(cat, lat, lon, radius);
        }
        auto roads = cache.roads(lat, lon, radius);

        // 2. Build graph & snap places
        rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads);
        auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, places);

        int snappedCount = 0;
        for (const auto& sp : snapped) {
            if (sp.snapped) snappedCount++;
        }

        // 3. Serialize GeoJSON
        std::string roadsGeoJson = rapidaid::geojson::GeoJsonWriter::roadsCollection(graph);
        std::string placesGeoJson = rapidaid::geojson::GeoJsonWriter::snappedPlacesCollection(snapped);

        // 4. Save permanently to disk
        fs::path dir(outDir);
        std::error_code ec;
        fs::create_directories(dir, ec);

        std::string roadsPath = (dir / "roads.geojson").lexically_normal().string();
        std::string placesPath = (dir / "places.geojson").lexically_normal().string();

        bool roadsSaved = rapidaid::geojson::GeoJsonWriter::writeFile(roadsPath, roadsGeoJson);
        bool placesSaved = rapidaid::geojson::GeoJsonWriter::writeFile(placesPath, placesGeoJson);
        if (roadsSaved) {
            rapidaid::geojson::GeoJsonWriter::writeFile((dir / "roads.js").lexically_normal().string(), "window.RAPIDAID_ROADS = " + roadsGeoJson + ";");
        }
        if (placesSaved) {
            rapidaid::geojson::GeoJsonWriter::writeFile((dir / "places.js").lexically_normal().string(), "window.RAPIDAID_PLACES = " + placesGeoJson + ";");
        }

        if (!roadsSaved && !placesSaved) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Failed to write GeoJSON files to disk";
            return resp.dump();
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["displayName"] = displayName;
        resp["lat"] = lat;
        resp["lon"] = lon;
        resp["radius"] = radius;
        resp["category"] = catStr;
        resp["nodesCount"] = static_cast<long long>(graph.nodeCount());
        resp["edgesCount"] = static_cast<long long>(graph.edgeCount());
        resp["placesCount"] = static_cast<long long>(places.size());
        resp["snappedCount"] = snappedCount;
        resp["roadsFile"] = roadsPath;
        resp["placesFile"] = placesPath;
        resp["roadsGeoJson"] = roadsGeoJson;
        resp["placesGeoJson"] = placesGeoJson;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Pipeline processing failure";
        return resp.dump();
    }
}

std::string handleGetGeoJsonRequest(const std::string& type, const std::string& outDir) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        fs::path file = fs::path(outDir) / (type + ".geojson");
        std::string content = rapidaid::io::FileStore::load(file.lexically_normal().string());
        if (content.empty()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "File not found: " + type;
            return resp.dump();
        }
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["type"] = type;
        resp["geojson"] = content;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Failed reading GeoJSON: " + type;
        return resp.dump();
    }
}

std::string handleGetIncidentsRequest() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    try {
        auto& store = getIncidentStore();
        nlohmann::ordered_json items = nlohmann::json::array();
        for (const auto& inc : store) {
            nlohmann::ordered_json item;
            item["id"] = inc.id;
            item["title"] = inc.title;
            item["location"] = inc.location;
            item["type"] = inc.type;
            item["priority"] = inc.priority;
            item["timestamp"] = inc.timestamp;
            item["status"] = inc.status;
            item["notes"] = inc.notes;
            items.push_back(item);
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["notice"] = "Incident report handling backend is currently under development.";
        resp["count"] = store.size();
        resp["incidents"] = items;
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"status\":\"under_development\",\"incidents\":[]}";
    }
}

std::string handleAddIncidentRequest(const std::string& requestJson) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("title") || !j.contains("location")) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Missing required fields";
            return resp.dump();
        }

        auto& store = getIncidentStore();
        std::ostringstream idSs;
        idSs.imbue(std::locale::classic());
        idSs << "INC-" << (store.size() + 101);

        StoredIncident inc;
        inc.id = idSs.str();
        inc.title = j.value("title", "Untitled Incident");
        inc.location = j.value("location", "Unknown Location");
        inc.type = j.value("type", "Medical");
        inc.priority = j.value("priority", "Medium");
        inc.timestamp = currentTimestampIso();
        inc.status = "Pending Dispatch";
        inc.notes = j.value("notes", "");

        store.push_back(inc);

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["message"] = "Incident report recorded (mock backend)";
        resp["incident"] = {
            {"id", inc.id},
            {"title", inc.title},
            {"location", inc.location},
            {"type", inc.type},
            {"priority", inc.priority},
            {"timestamp", inc.timestamp},
            {"status", inc.status},
            {"notes", inc.notes}
        };
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}

std::string handleGetStatsRequest() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    try {
        auto& store = getIncidentStore();
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["notice"] = "Past dispatch statistics module is currently under development.";
        resp["totalIncidents"] = store.size();
        resp["activeDispatches"] = 0;
        resp["averageResponseTimeMinutes"] = 8.5;
        resp["dispatchedUnitsCount"] = 0;
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"status\":\"under_development\"}";
    }
}

std::string handleBridgeMessage(Auth& auth, rapidaid::data::DataCache& cache, const std::string& messageJson, const std::string& outDir) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(messageJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("action") || !j["action"].is_string()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string action = j["action"].get<std::string>();

        // Extract payload
        std::string payloadStr = "{}";
        nlohmann::json pj = nlohmann::json::object();
        if (j.contains("payload")) {
            if (j["payload"].is_string()) {
                payloadStr = j["payload"].get<std::string>();
                try { pj = nlohmann::json::parse(payloadStr); } catch (...) {}
            } else if (j["payload"].is_object()) {
                pj = j["payload"];
                payloadStr = pj.dump();
            }
        }

        // Extract token (check top-level or payload)
        std::string token;
        if (j.contains("token") && j["token"].is_string()) {
            token = j["token"].get<std::string>();
        } else if (pj.is_object() && pj.contains("token") && pj["token"].is_string()) {
            token = pj["token"].get<std::string>();
        }

        // ==========================================
        // Action: login (no token required)
        // ==========================================
        if (action == "login") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            std::string res = handleLoginRequest(auth, payloadStr);
            try {
                nlohmann::json lr = nlohmann::json::parse(res);
                if (lr.value("ok", false)) {
                    std::string u = lr.value("username", "");
                    std::string r = lr.value("role", "");
                    std::string sessToken = generateSessionToken();
                    g_sessions[sessToken] = {u, r};

                    nlohmann::ordered_json successResp;
                    successResp["ok"] = true;
                    successResp["username"] = u;
                    successResp["role"] = r;
                    successResp["token"] = sessToken;
                    return successResp.dump();
                }
            } catch (...) {}
            return res;
        }

        // ==========================================
        // Action: signup (no token required, except admin role)
        // ==========================================
        if (action == "signup") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (!pj.is_object()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Bad request";
                return resp.dump();
            }

            std::string username = pj.value("username", "");
            std::string role = pj.value("role", "dispatcher");

            if (username.empty() || std::any_of(username.begin(), username.end(), [](unsigned char c) { return std::isspace(c); })) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Username cannot be empty or contain whitespace";
                return resp.dump();
            }

            if (role != "dispatcher" && role != "unit" && role != "admin") {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }

            if (role == "admin") {
                if (token.empty() || g_sessions.find(token) == g_sessions.end() || g_sessions[token].role != "admin") {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Not allowed";
                    return resp.dump();
                }
            }

            UnitProfile unitProf;
            if (role == "unit") {
                if (!pj.contains("unit") || !pj["unit"].is_object()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Missing unit profile for role unit";
                    return resp.dump();
                }
                const auto& uj = pj["unit"];
                unitProf.username = username;
                unitProf.kind = uj.value("kind", "");
                unitProf.name = uj.value("name", "");
                unitProf.lat = uj.value("lat", 999.0);
                unitProf.lon = uj.value("lon", 999.0);
                unitProf.count = uj.value("count", 0);

                if (unitProf.name.empty()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit name cannot be empty";
                    return resp.dump();
                }
                if (unitProf.lat < -90.0 || unitProf.lat > 90.0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit latitude must be in [-90, 90]";
                    return resp.dump();
                }
                if (unitProf.lon < -180.0 || unitProf.lon > 180.0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit longitude must be in [-180, 180]";
                    return resp.dump();
                }
                if (unitProf.count < 0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit count cannot be negative";
                    return resp.dump();
                }
                const std::vector<std::string> validKinds = {
                    "ambulance", "fire_truck", "police_unit", "hospital", "fire_station", "police_station", "shelter"
                };
                if (std::find(validKinds.begin(), validKinds.end(), unitProf.kind) == validKinds.end()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unknown unit kind: " + unitProf.kind;
                    return resp.dump();
                }
            }

            // Create user account via Auth
            std::string res = handleSignUpRequest(auth, payloadStr);
            try {
                nlohmann::json sr = nlohmann::json::parse(res);
                if (sr.value("ok", false) && role == "unit") {
                    std::string err;
                    if (!g_registry.registerUnit(unitProf, err)) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = err;
                        return resp.dump();
                    }
                }
            } catch (...) {}
            return res;
        }

        // ==========================================
        // Action: health (no token required)
        // ==========================================
        if (action == "health") {
            return handleHealthRequest();
        }

        // ==========================================
        // All subsequent actions REQUIRE a valid session token (B2)
        // ==========================================
        SessionData session;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (token.empty()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not logged in";
                return resp.dump();
            }
            auto it = g_sessions.find(token);
            if (it == g_sessions.end()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not logged in";
                return resp.dump();
            }
            session = it->second;
        }

        // ==========================================
        // Action: session (returns current profile / role)
        // ==========================================
        if (action == "session") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = session.username;
            resp["role"] = session.role;
            if (session.role == "unit") {
                auto prof = g_registry.getProfile(session.username);
                if (prof.has_value()) {
                    nlohmann::ordered_json uj;
                    uj["username"] = prof->username;
                    uj["kind"] = prof->kind;
                    uj["name"] = prof->name;
                    uj["lat"] = prof->lat;
                    uj["lon"] = prof->lon;
                    uj["count"] = prof->count;
                    resp["unit"] = uj;
                } else {
                    resp["unit"] = nullptr;
                }
            } else {
                resp["unit"] = nullptr;
            }
            return resp.dump();
        }

        // ==========================================
        // Action: logout
        // ==========================================
        if (action == "logout") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_sessions.erase(token);
            return handleLogoutRequest(auth, session.username);
        }

        // ==========================================
        // Authorization check: Dispatcher/Admin actions
        // ==========================================
        bool isDispatcherOrAdmin = (session.role == "dispatcher" || session.role == "admin");
        if (action == "area_status" || action == "geocode" || action == "create_area" ||
            action == "get_geojson" || action == "get_incidents" || action == "add_incident" ||
            action == "get_stats" || action == "list_units" || action == "send_test_alert" ||
            action == "list_alerts") {
            if (!isDispatcherOrAdmin) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }
        }

        // Pipeline actions
        if (action == "area_status") {
            return handleAreaStatusRequest(outDir);
        }
        if (action == "geocode") {
            return handleGeocodeRequest(cache, payloadStr);
        }
        if (action == "create_area") {
            return handleCreateAreaRequest(cache, payloadStr, outDir);
        }
        if (action == "get_geojson") {
            std::string type = pj.value("type", "roads");
            return handleGetGeoJsonRequest(type, outDir);
        }
        if (action == "get_incidents") {
            return handleGetIncidentsRequest();
        }
        if (action == "add_incident") {
            return handleAddIncidentRequest(payloadStr);
        }
        if (action == "get_stats") {
            return handleGetStatsRequest();
        }

        // Dispatcher Unit & Alert queries
        if (action == "list_units") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            return g_registry.listUnitsJson().dump();
        }
        if (action == "send_test_alert") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (!pj.is_object()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Bad request";
                return resp.dump();
            }
            Alert a;
            a.toUser = pj.value("toUser", "");
            a.type = pj.value("type", "Medical");
            a.severity = pj.value("severity", 3);
            a.peopleAffected = pj.value("peopleAffected", 0);
            a.locationText = pj.value("locationText", "Unknown Location");
            a.note = pj.value("note", "");
            if (pj.contains("lat") && pj["lat"].is_number()) a.lat = pj["lat"].get<double>();
            if (pj.contains("lon") && pj["lon"].is_number()) a.lon = pj["lon"].get<double>();

            std::string err;
            int alertId = g_registry.sendAlert(a, err);
            if (alertId < 0) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = err;
                return resp.dump();
            }
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["alertId"] = alertId;
            return resp.dump();
        }
        if (action == "list_alerts") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            return g_registry.listAlertsJson().dump();
        }

        // ==========================================
        // Authorization check: Unit actions (B2)
        // ==========================================
        if (action.rfind("unit_", 0) == 0) {
            if (session.role != "unit") {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }

            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (action == "unit_get_state") {
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_set_availability") {
                bool avail = pj.value("available", true);
                std::string err;
                if (!g_registry.setAvailability(session.username, avail, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_update_location") {
                if (!pj.contains("lat") || !pj.contains("lon") || !pj["lat"].is_number() || !pj["lon"].is_number()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Invalid coordinates in payload";
                    return resp.dump();
                }
                double lat = pj["lat"].get<double>();
                double lon = pj["lon"].get<double>();
                std::string err;
                if (!g_registry.updateLocation(session.username, lat, lon, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                nlohmann::ordered_json resp;
                resp["ok"] = true;
                resp["lat"] = lat;
                resp["lon"] = lon;
                return resp.dump();
            }
            if (action == "unit_update_resources") {
                std::string resAction = pj.value("action", "");
                int val = 0;
                if (pj.contains("count") && pj["count"].is_number()) {
                    val = pj["count"].get<int>();
                } else if (pj.contains("value") && pj["value"].is_number()) {
                    val = pj["value"].get<int>();
                }
                std::string err;
                if (!g_registry.updateResources(session.username, resAction, val, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_add_subunit") {
                std::string sname = pj.value("name", "");
                int scount = pj.value("count", 0);
                std::string err;
                int subId = g_registry.addSubunit(session.username, sname, scount, err);
                if (subId < 0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                nlohmann::ordered_json resp;
                resp["ok"] = true;
                resp["subunitId"] = subId;
                return resp.dump();
            }
            if (action == "unit_set_subunit_availability") {
                int subId = pj.value("subunitId", 0);
                bool avail = pj.value("available", true);
                std::string err;
                if (!g_registry.setSubunitAvailability(session.username, subId, avail, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_alert_respond") {
                int alertId = pj.value("alertId", 0);
                std::string respAction = pj.value("action", "");
                std::string reason = pj.value("reason", "");
                std::string err;
                if (!g_registry.respondToAlert(alertId, session.username, respAction, reason, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }

            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Unknown unit action: " + action;
            return resp.dump();
        }

        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Unknown action: " + action;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bridge message handling failure";
        return resp.dump();
    }
}

} // namespace rapidaid::ui

```

## src/UnitRegistry.cpp

```cpp
#include "UnitRegistry.hpp"
#include "FileStore.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace rapidaid::ui {

std::string RegisteredUnitRecord::getStatusString() const {
    if (profile.kind == "ambulance" && ambulance) {
        return rapidaid::service::Ambulance::statusToString(ambulance->getStatus());
    }
    if (profile.kind == "fire_truck" && fireTruck) {
        return rapidaid::service::FireTruck::statusToString(fireTruck->getStatus());
    }
    if (profile.kind == "police_unit" && policeUnit) {
        return rapidaid::service::PoliceUnit::statusToString(policeUnit->getStatus());
    }
    if (openAlertId != -1) {
        return "Dispatched";
    }
    if (profile.kind == "shelter" && shelter && shelter->isFull()) {
        return "Unavailable";
    }
    return "Available";
}

bool RegisteredUnitRecord::isAvailable() const {
    if (openAlertId != -1) {
        return false;
    }
    if (profile.kind == "ambulance" && ambulance) {
        return ambulance->isAvailable();
    }
    if (profile.kind == "fire_truck" && fireTruck) {
        return fireTruck->isAvailable();
    }
    if (profile.kind == "police_unit" && policeUnit) {
        return policeUnit->isAvailable();
    }
    if (profile.kind == "shelter" && shelter) {
        return !shelter->isFull();
    }
    return true;
}

UnitRegistry::UnitRegistry(std::string dataPath)
    : dataPath_(std::move(dataPath)) {}

void UnitRegistry::setDataPath(const std::string& path) {
    dataPath_ = path;
    loaded_ = false;
}

void UnitRegistry::ensureLoaded() {
    if (!loaded_) {
        load();
    }
}

void UnitRegistry::clear() {
    units_.clear();
    alerts_.clear();
    nextAlertId_ = 1;
    loaded_ = true;
}

bool UnitRegistry::load(const std::string& path) {
    std::string target = path.empty() ? dataPath_ : path;
    loaded_ = true;
    units_.clear();

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        return true;
    }

    std::string content = rapidaid::io::FileStore::load(target);
    if (content.empty()) {
        return true;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(content);
        if (!j.is_array()) {
            return false;
        }

        for (const auto& item : j) {
            if (!item.is_object()) continue;
            UnitProfile prof;
            prof.username = item.value("username", "");
            prof.kind = item.value("kind", "");
            prof.name = item.value("name", "");
            prof.lat = item.value("lat", 0.0);
            prof.lon = item.value("lon", 0.0);
            prof.count = item.value("count", 0);

            if (prof.username.empty() || prof.kind.empty()) continue;

            RegisteredUnitRecord rec;
            rec.profile = prof;

            try {
                if (prof.kind == "ambulance") {
                    rec.ambulance = std::make_unique<rapidaid::service::Ambulance>(
                        1, prof.name, prof.lat, prof.lon, 0, prof.count);
                    std::string st = item.value("status", "Available");
                    if (st == "Unavailable") {
                        rec.ambulance->markUnavailable();
                    }
                } else if (prof.kind == "fire_truck") {
                    rec.fireTruck = std::make_unique<rapidaid::service::FireTruck>(
                        1, prof.name, prof.lat, prof.lon, 0, prof.count);
                    std::string st = item.value("status", "Available");
                    if (st == "Unavailable") {
                        rec.fireTruck->markUnavailable();
                    }
                } else if (prof.kind == "police_unit") {
                    rec.policeUnit = std::make_unique<rapidaid::service::PoliceUnit>(
                        1, prof.name, prof.lat, prof.lon, 0, prof.count);
                    std::string st = item.value("status", "Available");
                    if (st == "Unavailable") {
                        rec.policeUnit->markUnavailable();
                    }
                } else if (prof.kind == "hospital") {
                    int occ = item.value("occupiedBeds", 0);
                    rec.hospital = std::make_unique<rapidaid::service::Hospital>(
                        1, prof.name, prof.lat, prof.lon, prof.count, 0);
                    if (occ > 0) {
                        rec.hospital->admitPatients(occ);
                    }
                    if (item.contains("subunits") && item["subunits"].is_array()) {
                        for (const auto& sub : item["subunits"]) {
                            int sid = sub.value("id", rec.nextSubunitId++);
                            std::string sname = sub.value("name", "Ambulance");
                            int scount = sub.value("count", 0);
                            bool savail = sub.value("available", true);
                            rapidaid::service::Ambulance amb(sid, sname, prof.lat, prof.lon, 1, scount);
                            if (!savail) {
                                amb.markUnavailable();
                            }
                            rec.hospital->addAmbulance(std::move(amb));
                            rec.subunits.push_back({sid, sname, scount, savail});
                            if (sid >= rec.nextSubunitId) rec.nextSubunitId = sid + 1;
                        }
                    }
                } else if (prof.kind == "fire_station") {
                    rec.fireStation = std::make_unique<rapidaid::service::FireStation>(
                        1, prof.name, prof.lat, prof.lon);
                    if (item.contains("subunits") && item["subunits"].is_array()) {
                        for (const auto& sub : item["subunits"]) {
                            int sid = sub.value("id", rec.nextSubunitId++);
                            std::string sname = sub.value("name", "Truck");
                            int scount = sub.value("count", 0);
                            bool savail = sub.value("available", true);
                            rapidaid::service::FireTruck truck(sid, sname, prof.lat, prof.lon, 1, scount);
                            if (!savail) {
                                truck.markUnavailable();
                            }
                            rec.fireStation->addTruck(std::move(truck));
                            rec.subunits.push_back({sid, sname, scount, savail});
                            if (sid >= rec.nextSubunitId) rec.nextSubunitId = sid + 1;
                        }
                    }
                } else if (prof.kind == "police_station") {
                    rec.policeStation = std::make_unique<rapidaid::service::PoliceStation>(
                        1, prof.name, prof.lat, prof.lon);
                    if (item.contains("subunits") && item["subunits"].is_array()) {
                        for (const auto& sub : item["subunits"]) {
                            int sid = sub.value("id", rec.nextSubunitId++);
                            std::string sname = sub.value("name", "Unit");
                            int scount = sub.value("count", 0);
                            bool savail = sub.value("available", true);
                            rapidaid::service::PoliceUnit unit(sid, sname, prof.lat, prof.lon, 1, scount);
                            if (!savail) {
                                unit.markUnavailable();
                            }
                            rec.policeStation->addUnit(std::move(unit));
                            rec.subunits.push_back({sid, sname, scount, savail});
                            if (sid >= rec.nextSubunitId) rec.nextSubunitId = sid + 1;
                        }
                    }
                } else if (prof.kind == "shelter") {
                    int occ = item.value("occupied", 0);
                    rec.shelter = std::make_unique<rapidaid::service::Shelter>(
                        1, prof.name, prof.lat, prof.lon, prof.count, 0);
                    if (occ > 0) {
                        rec.shelter->admit(occ);
                    }
                }
                units_[prof.username] = std::move(rec);
            } catch (...) {
                // Ignore single malformed entry
            }
        }
        return true;
    } catch (...) {
        units_.clear();
        return false;
    }
}

bool UnitRegistry::save(const std::string& path) const {
    std::string target = path.empty() ? dataPath_ : path;
    try {
        nlohmann::ordered_json root = nlohmann::json::array();
        for (const auto& [u, rec] : units_) {
            (void)u;
            nlohmann::ordered_json j;
            j["username"] = rec.profile.username;
            j["kind"] = rec.profile.kind;
            j["name"] = rec.profile.name;
            j["lat"] = rec.profile.lat;
            j["lon"] = rec.profile.lon;
            j["count"] = rec.profile.count;
            j["status"] = rec.getStatusString();

            if (rec.profile.kind == "hospital" && rec.hospital) {
                j["occupiedBeds"] = rec.hospital->getOccupiedBeds();
            } else if (rec.profile.kind == "shelter" && rec.shelter) {
                j["occupied"] = rec.shelter->getOccupied();
            }

            if (!rec.subunits.empty()) {
                nlohmann::ordered_json subs = nlohmann::json::array();
                for (const auto& sub : rec.subunits) {
                    subs.push_back({
                        {"id", sub.id},
                        {"name", sub.name},
                        {"count", sub.count},
                        {"available", sub.available}
                    });
                }
                j["subunits"] = subs;
            }
            root.push_back(j);
        }

        fs::path p(target);
        std::error_code ec;
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path(), ec);
        }

        std::string tmpFile = target + ".tmp";
        std::ofstream out(tmpFile, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << root.dump(2);
        out.close();

        fs::rename(tmpFile, target, ec);
        return !ec;
    } catch (...) {
        return false;
    }
}

bool UnitRegistry::hasUnit(const std::string& username) {
    ensureLoaded();
    return units_.find(username) != units_.end();
}

std::optional<UnitProfile> UnitRegistry::getProfile(const std::string& username) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        return std::nullopt;
    }
    return it->second.profile;
}

bool UnitRegistry::registerUnit(const UnitProfile& profile, std::string& errorMsg) {
    ensureLoaded();

    if (profile.username.empty()) {
        errorMsg = "Username cannot be empty";
        return false;
    }
    if (std::any_of(profile.username.begin(), profile.username.end(), [](unsigned char c) { return std::isspace(c); })) {
        errorMsg = "Username cannot contain whitespace";
        return false;
    }
    if (profile.name.empty()) {
        errorMsg = "Display name cannot be empty";
        return false;
    }
    if (profile.lat < -90.0 || profile.lat > 90.0) {
        errorMsg = "Latitude must be between -90 and 90";
        return false;
    }
    if (profile.lon < -180.0 || profile.lon > 180.0) {
        errorMsg = "Longitude must be between -180 and 180";
        return false;
    }
    if (profile.count < 0) {
        errorMsg = "Resource count cannot be negative";
        return false;
    }

    const std::vector<std::string> validKinds = {
        "ambulance", "fire_truck", "police_unit", "hospital", "fire_station", "police_station", "shelter"
    };
    if (std::find(validKinds.begin(), validKinds.end(), profile.kind) == validKinds.end()) {
        errorMsg = "Unknown unit kind: " + profile.kind;
        return false;
    }

    if (units_.find(profile.username) != units_.end()) {
        errorMsg = "Unit username already exists";
        return false;
    }

    RegisteredUnitRecord rec;
    rec.profile = profile;

    try {
        if (profile.kind == "ambulance") {
            rec.ambulance = std::make_unique<rapidaid::service::Ambulance>(
                1, profile.name, profile.lat, profile.lon, 0, profile.count);
        } else if (profile.kind == "fire_truck") {
            rec.fireTruck = std::make_unique<rapidaid::service::FireTruck>(
                1, profile.name, profile.lat, profile.lon, 0, profile.count);
        } else if (profile.kind == "police_unit") {
            rec.policeUnit = std::make_unique<rapidaid::service::PoliceUnit>(
                1, profile.name, profile.lat, profile.lon, 0, profile.count);
        } else if (profile.kind == "hospital") {
            rec.hospital = std::make_unique<rapidaid::service::Hospital>(
                1, profile.name, profile.lat, profile.lon, profile.count, 0);
        } else if (profile.kind == "fire_station") {
            rec.fireStation = std::make_unique<rapidaid::service::FireStation>(
                1, profile.name, profile.lat, profile.lon);
        } else if (profile.kind == "police_station") {
            rec.policeStation = std::make_unique<rapidaid::service::PoliceStation>(
                1, profile.name, profile.lat, profile.lon);
        } else if (profile.kind == "shelter") {
            rec.shelter = std::make_unique<rapidaid::service::Shelter>(
                1, profile.name, profile.lat, profile.lon, profile.count, 0);
        }
    } catch (const std::invalid_argument& e) {
        errorMsg = e.what();
        return false;
    } catch (...) {
        errorMsg = "Failed to construct unit model";
        return false;
    }

    units_[profile.username] = std::move(rec);
    save();
    return true;
}

bool UnitRegistry::setAvailability(const std::string& username, bool available, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = it->second;
    if (rec.profile.kind == "ambulance" && rec.ambulance) {
        if (rec.ambulance->getStatus() == rapidaid::service::Ambulance::Status::Dispatched) {
            errorMsg = "Cannot change availability while dispatched";
            return false;
        }
        if (available) {
            rec.ambulance->release();
        } else {
            rec.ambulance->markUnavailable();
        }
    } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
        if (rec.fireTruck->getStatus() == rapidaid::service::FireTruck::Status::Dispatched) {
            errorMsg = "Cannot change availability while dispatched";
            return false;
        }
        if (available) {
            rec.fireTruck->release();
        } else {
            rec.fireTruck->markUnavailable();
        }
    } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
        if (rec.policeUnit->getStatus() == rapidaid::service::PoliceUnit::Status::Dispatched) {
            errorMsg = "Cannot change availability while dispatched";
            return false;
        }
        if (available) {
            rec.policeUnit->release();
        } else {
            rec.policeUnit->markUnavailable();
        }
    } else {
        errorMsg = "Only mobile service units support availability toggling";
        return false;
    }

    save();
    return true;
}

bool UnitRegistry::updateLocation(const std::string& username, double lat, double lon, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        errorMsg = "Coordinates out of valid range";
        return false;
    }

    it->second.profile.lat = lat;
    it->second.profile.lon = lon;
    save();
    return true;
}

void UnitRegistry::reinstantiateMobile(RegisteredUnitRecord& rec, int newCount) {
    rec.profile.count = newCount;
    if (rec.profile.kind == "ambulance" && rec.ambulance) {
        auto oldStatus = rec.ambulance->getStatus();
        int incId = rec.ambulance->getCurrentIncidentId();
        rec.ambulance = std::make_unique<rapidaid::service::Ambulance>(
            1, rec.profile.name, rec.profile.lat, rec.profile.lon, 0, newCount);
        if (oldStatus == rapidaid::service::Ambulance::Status::Dispatched && incId >= 0) {
            rec.ambulance->assign(incId);
        } else if (oldStatus == rapidaid::service::Ambulance::Status::Unavailable) {
            rec.ambulance->markUnavailable();
        }
    } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
        auto oldStatus = rec.fireTruck->getStatus();
        int incId = rec.fireTruck->getCurrentIncidentId();
        rec.fireTruck = std::make_unique<rapidaid::service::FireTruck>(
            1, rec.profile.name, rec.profile.lat, rec.profile.lon, 0, newCount);
        if (oldStatus == rapidaid::service::FireTruck::Status::Dispatched && incId >= 0) {
            rec.fireTruck->assign(incId);
        } else if (oldStatus == rapidaid::service::FireTruck::Status::Unavailable) {
            rec.fireTruck->markUnavailable();
        }
    } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
        auto oldStatus = rec.policeUnit->getStatus();
        int incId = rec.policeUnit->getCurrentIncidentId();
        rec.policeUnit = std::make_unique<rapidaid::service::PoliceUnit>(
            1, rec.profile.name, rec.profile.lat, rec.profile.lon, 0, newCount);
        if (oldStatus == rapidaid::service::PoliceUnit::Status::Dispatched && incId >= 0) {
            rec.policeUnit->assign(incId);
        } else if (oldStatus == rapidaid::service::PoliceUnit::Status::Unavailable) {
            rec.policeUnit->markUnavailable();
        }
    }
}

bool UnitRegistry::updateResources(const std::string& username, const std::string& action, int value, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = it->second;
    try {
        if (rec.profile.kind == "hospital" && rec.hospital) {
            if (value < 0) {
                errorMsg = "Count cannot be negative";
                return false;
            }
            if (action == "admit") {
                if (!rec.hospital->admitPatients(value)) {
                    errorMsg = "Not enough available beds";
                    return false;
                }
            } else if (action == "discharge") {
                if (value > rec.hospital->getOccupiedBeds()) {
                    errorMsg = "Cannot discharge more patients than currently occupied";
                    return false;
                }
                rec.hospital->dischargePatients(value);
            } else {
                errorMsg = "Unknown action for hospital: must be admit or discharge";
                return false;
            }
        } else if (rec.profile.kind == "shelter" && rec.shelter) {
            if (value < 0) {
                errorMsg = "Count cannot be negative";
                return false;
            }
            if (action == "admit") {
                if (!rec.shelter->admit(value)) {
                    errorMsg = "Admission exceeds shelter capacity";
                    return false;
                }
            } else if (action == "leave") {
                if (value > rec.shelter->getOccupied()) {
                    errorMsg = "Cannot leave more occupants than currently in shelter";
                    return false;
                }
                rec.shelter->leave(value);
            } else {
                errorMsg = "Unknown action for shelter: must be admit or leave";
                return false;
            }
        } else if (rec.profile.kind == "ambulance" || rec.profile.kind == "fire_truck" || rec.profile.kind == "police_unit") {
            if (value < 0) {
                errorMsg = "Resource count cannot be negative";
                return false;
            }
            reinstantiateMobile(rec, value);
        } else {
            errorMsg = "Resource updates not supported for this unit";
            return false;
        }
    } catch (const std::invalid_argument& e) {
        errorMsg = e.what();
        return false;
    }

    save();
    return true;
}

int UnitRegistry::addSubunit(const std::string& username, const std::string& name, int count, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return -1;
    }

    RegisteredUnitRecord& rec = it->second;
    if (name.empty()) {
        errorMsg = "Subunit name cannot be empty";
        return -1;
    }
    if (count < 0) {
        errorMsg = "Count cannot be negative";
        return -1;
    }

    int subId = rec.nextSubunitId++;
    try {
        if (rec.profile.kind == "hospital" && rec.hospital) {
            rapidaid::service::Ambulance amb(subId, name, rec.profile.lat, rec.profile.lon, 1, count);
            rec.hospital->addAmbulance(std::move(amb));
        } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
            rapidaid::service::FireTruck truck(subId, name, rec.profile.lat, rec.profile.lon, 1, count);
            rec.fireStation->addTruck(std::move(truck));
        } else if (rec.profile.kind == "police_station" && rec.policeStation) {
            rapidaid::service::PoliceUnit unit(subId, name, rec.profile.lat, rec.profile.lon, 1, count);
            rec.policeStation->addUnit(std::move(unit));
        } else {
            errorMsg = "This unit kind does not support child subunits";
            return -1;
        }
    } catch (const std::invalid_argument& e) {
        errorMsg = e.what();
        return -1;
    }

    rec.subunits.push_back({subId, name, count, true});
    save();
    return subId;
}

bool UnitRegistry::setSubunitAvailability(const std::string& username, int subunitId, bool available, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = it->second;
    auto subIt = std::find_if(rec.subunits.begin(), rec.subunits.end(), [subunitId](const SubUnitInfo& s) {
        return s.id == subunitId;
    });
    if (subIt == rec.subunits.end()) {
        errorMsg = "Subunit not found";
        return false;
    }

    if (rec.profile.kind == "hospital" && rec.hospital) {
        rapidaid::service::Ambulance* amb = rec.hospital->findAmbulanceById(subunitId);
        if (!amb) {
            errorMsg = "Ambulance subunit not found";
            return false;
        }
        if (available) amb->release();
        else amb->markUnavailable();
    } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
        rapidaid::service::FireTruck* truck = rec.fireStation->findTruckById(subunitId);
        if (!truck) {
            errorMsg = "Truck subunit not found";
            return false;
        }
        if (available) truck->release();
        else truck->markUnavailable();
    } else if (rec.profile.kind == "police_station" && rec.policeStation) {
        rapidaid::service::PoliceUnit* u = rec.policeStation->findUnitById(subunitId);
        if (!u) {
            errorMsg = "Police subunit not found";
            return false;
        }
        if (available) u->release();
        else u->markUnavailable();
    } else {
        errorMsg = "Unit does not manage subunits";
        return false;
    }

    subIt->available = available;
    save();
    return true;
}

nlohmann::ordered_json UnitRegistry::getUnitStateJson(const std::string& username) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        return {{"ok", false}, {"error", "Unit not found"}};
    }

    RegisteredUnitRecord& rec = it->second;
    nlohmann::ordered_json resp;
    resp["ok"] = true;

    nlohmann::ordered_json prof;
    prof["username"] = rec.profile.username;
    prof["kind"] = rec.profile.kind;
    prof["name"] = rec.profile.name;
    prof["lat"] = rec.profile.lat;
    prof["lon"] = rec.profile.lon;
    prof["count"] = rec.profile.count;
    resp["profile"] = prof;

    resp["status"] = rec.getStatusString();

    nlohmann::ordered_json resources;
    if (rec.profile.kind == "hospital" && rec.hospital) {
        resources["totalBeds"] = rec.hospital->getTotalBeds();
        resources["occupiedBeds"] = rec.hospital->getOccupiedBeds();
        resources["availableBeds"] = rec.hospital->availableBeds();
    } else if (rec.profile.kind == "shelter" && rec.shelter) {
        resources["capacity"] = rec.shelter->getCapacity();
        resources["occupied"] = rec.shelter->getOccupied();
        resources["availableSpace"] = rec.shelter->availableSpace();
        resources["isFull"] = rec.shelter->isFull();
    } else if (rec.profile.kind == "ambulance" && rec.ambulance) {
        resources["paramedics"] = rec.ambulance->getParamedicCount();
    } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
        resources["waterLitres"] = rec.fireTruck->getWaterCapacityLitres();
    } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
        resources["officers"] = rec.policeUnit->getOfficerCount();
    }
    resp["resources"] = resources;

    nlohmann::ordered_json subs = nlohmann::json::array();
    for (const auto& s : rec.subunits) {
        std::string subStatus = s.available ? "Available" : "Unavailable";
        if (rec.profile.kind == "hospital" && rec.hospital) {
            auto* amb = rec.hospital->findAmbulanceById(s.id);
            if (amb) subStatus = rapidaid::service::Ambulance::statusToString(amb->getStatus());
        } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
            auto* tr = rec.fireStation->findTruckById(s.id);
            if (tr) subStatus = rapidaid::service::FireTruck::statusToString(tr->getStatus());
        } else if (rec.profile.kind == "police_station" && rec.policeStation) {
            auto* pu = rec.policeStation->findUnitById(s.id);
            if (pu) subStatus = rapidaid::service::PoliceUnit::statusToString(pu->getStatus());
        }
        subs.push_back({
            {"id", s.id},
            {"name", s.name},
            {"count", s.count},
            {"available", s.available},
            {"status", subStatus}
        });
    }
    resp["subunits"] = subs;

    // Open alert
    int64_t now = std::time(nullptr);
    Alert* openA = nullptr;
    for (auto& a : alerts_) {
        if (a.toUser == username && a.state != "Declined" && a.state != "Cleared") {
            openA = &a;
            break;
        }
    }

    if (openA) {
        int elapsed = static_cast<int>(now - openA->createdAt);
        int secondsLeft = std::max(0, openA->ackSeconds - elapsed);
        nlohmann::ordered_json aj;
        aj["id"] = openA->id;
        aj["toUser"] = openA->toUser;
        aj["type"] = openA->type;
        aj["severity"] = openA->severity;
        aj["peopleAffected"] = openA->peopleAffected;
        aj["locationText"] = openA->locationText;
        if (openA->lat.has_value()) aj["lat"] = *openA->lat;
        if (openA->lon.has_value()) aj["lon"] = *openA->lon;
        aj["note"] = openA->note;
        aj["createdAt"] = openA->createdAt;
        aj["ackSeconds"] = openA->ackSeconds;
        aj["secondsLeft"] = secondsLeft;
        aj["state"] = openA->state;
        if (!openA->reason.empty()) aj["reason"] = openA->reason;
        resp["openAlert"] = aj;
    } else {
        resp["openAlert"] = nullptr;
    }

    // Recent 5 closed alerts (Declined or Cleared)
    nlohmann::ordered_json recents = nlohmann::json::array();
    int countClosed = 0;
    for (auto itA = alerts_.rbegin(); itA != alerts_.rend(); ++itA) {
        if (itA->toUser == username && (itA->state == "Declined" || itA->state == "Cleared")) {
            nlohmann::ordered_json cj;
            cj["id"] = itA->id;
            cj["toUser"] = itA->toUser;
            cj["type"] = itA->type;
            cj["severity"] = itA->severity;
            cj["peopleAffected"] = itA->peopleAffected;
            cj["locationText"] = itA->locationText;
            if (itA->lat.has_value()) cj["lat"] = *itA->lat;
            if (itA->lon.has_value()) cj["lon"] = *itA->lon;
            cj["note"] = itA->note;
            cj["createdAt"] = itA->createdAt;
            cj["state"] = itA->state;
            if (!itA->reason.empty()) cj["reason"] = itA->reason;
            recents.push_back(cj);
            countClosed++;
            if (countClosed >= 5) break;
        }
    }
    resp["recentAlerts"] = recents;

    return resp;
}

nlohmann::ordered_json UnitRegistry::listUnitsJson() {
    ensureLoaded();
    nlohmann::ordered_json list = nlohmann::json::array();

    for (const auto& [username, rec] : units_) {
        nlohmann::ordered_json u;
        u["username"] = username;
        u["kind"] = rec.profile.kind;
        u["name"] = rec.profile.name;
        u["status"] = rec.getStatusString();
        u["lat"] = rec.profile.lat;
        u["lon"] = rec.profile.lon;

        int availCount = 0;
        if (rec.profile.kind == "ambulance" && rec.ambulance) {
            availCount = rec.ambulance->getParamedicCount();
        } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
            availCount = rec.fireTruck->getWaterCapacityLitres();
        } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
            availCount = rec.policeUnit->getOfficerCount();
        } else if (rec.profile.kind == "hospital" && rec.hospital) {
            availCount = rec.hospital->availableBeds();
        } else if (rec.profile.kind == "shelter" && rec.shelter) {
            availCount = rec.shelter->availableSpace();
        } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
            availCount = static_cast<int>(rec.fireStation->getAvailableTrucks().size());
        } else if (rec.profile.kind == "police_station" && rec.policeStation) {
            availCount = static_cast<int>(rec.policeStation->getAvailableUnits().size());
        }
        u["availableCount"] = availCount;
        list.push_back(u);
    }

    nlohmann::ordered_json resp;
    resp["ok"] = true;
    resp["units"] = list;
    return resp;
}

int UnitRegistry::sendAlert(const Alert& alert, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(alert.toUser);
    if (it == units_.end()) {
        errorMsg = "Target unit '" + alert.toUser + "' not found";
        return -1;
    }

    RegisteredUnitRecord& rec = it->second;

    // Check open alert
    for (const auto& a : alerts_) {
        if (a.toUser == alert.toUser && a.state != "Declined" && a.state != "Cleared") {
            errorMsg = "Unit already has an active alert in progress";
            return -1;
        }
    }

    // Check available and not dispatched
    if (!rec.isAvailable()) {
        errorMsg = "Target unit is unavailable or already dispatched";
        return -1;
    }

    Alert newA = alert;
    newA.id = nextAlertId_++;
    newA.state = "Pending";
    newA.ackSeconds = (alert.ackSeconds > 0) ? alert.ackSeconds : 60;
    newA.createdAt = (alert.createdAt > 0) ? alert.createdAt : std::time(nullptr);

    rec.openAlertId = newA.id;
    alerts_.push_back(newA);
    return newA.id;
}

bool UnitRegistry::respondToAlert(int alertId, const std::string& username, const std::string& action, const std::string& reason, std::string& errorMsg) {
    ensureLoaded();

    auto alertIt = std::find_if(alerts_.begin(), alerts_.end(), [alertId](const Alert& a) {
        return a.id == alertId;
    });
    if (alertIt == alerts_.end()) {
        errorMsg = "Alert not found";
        return false;
    }

    Alert& alert = *alertIt;
    if (alert.toUser != username) {
        errorMsg = "Not allowed: cannot respond to alert assigned to another unit";
        return false;
    }

    auto unitIt = units_.find(username);
    if (unitIt == units_.end()) {
        errorMsg = "Receiving unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = unitIt->second;
    bool isMobile = (rec.profile.kind == "ambulance" || rec.profile.kind == "fire_truck" || rec.profile.kind == "police_unit");

    if (alert.state == "Pending") {
        if (action == "accept") {
            try {
                if (rec.profile.kind == "ambulance" && rec.ambulance) {
                    rec.ambulance->assign(alertId);
                } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
                    rec.fireTruck->assign(alertId);
                } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
                    rec.policeUnit->assign(alertId);
                }
            } catch (const std::invalid_argument& e) {
                errorMsg = e.what();
                return false;
            }
            alert.state = "Accepted";
            rec.openAlertId = alert.id;
        } else if (action == "decline") {
            alert.state = "Declined";
            alert.reason = reason;
            rec.openAlertId = -1;
        } else {
            errorMsg = "Invalid transition from Pending: must be accept or decline";
            return false;
        }
    } else if (alert.state == "Accepted") {
        if (action == "clear") {
            if (rec.profile.kind == "ambulance" && rec.ambulance) {
                rec.ambulance->release();
            } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
                rec.fireTruck->release();
            } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
                rec.policeUnit->release();
            }
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else if (isMobile && action == "en_route") {
            alert.state = "EnRoute";
        } else {
            errorMsg = isMobile ? "Invalid transition from Accepted: must be en_route or clear"
                                : "Invalid transition from Accepted: facility units can only clear";
            return false;
        }
    } else if (alert.state == "EnRoute") {
        if (!isMobile) {
            errorMsg = "Facility units cannot be in EnRoute state";
            return false;
        }
        if (action == "on_scene") {
            alert.state = "OnScene";
        } else if (action == "clear") {
            if (rec.profile.kind == "ambulance" && rec.ambulance) rec.ambulance->release();
            else if (rec.profile.kind == "fire_truck" && rec.fireTruck) rec.fireTruck->release();
            else if (rec.profile.kind == "police_unit" && rec.policeUnit) rec.policeUnit->release();
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else {
            errorMsg = "Invalid transition from EnRoute: must be on_scene or clear";
            return false;
        }
    } else if (alert.state == "OnScene") {
        if (!isMobile) {
            errorMsg = "Facility units cannot be in OnScene state";
            return false;
        }
        if (action == "transporting") {
            if (rec.profile.kind != "ambulance") {
                errorMsg = "Only ambulances can enter transporting state";
                return false;
            }
            alert.state = "Transporting";
        } else if (action == "clear") {
            if (rec.profile.kind == "ambulance" && rec.ambulance) rec.ambulance->release();
            else if (rec.profile.kind == "fire_truck" && rec.fireTruck) rec.fireTruck->release();
            else if (rec.profile.kind == "police_unit" && rec.policeUnit) rec.policeUnit->release();
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else {
            errorMsg = (rec.profile.kind == "ambulance") ? "Invalid transition from OnScene: must be transporting or clear"
                                                         : "Invalid transition from OnScene: must be clear";
            return false;
        }
    } else if (alert.state == "Transporting") {
        if (action == "clear") {
            if (rec.ambulance) rec.ambulance->release();
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else {
            errorMsg = "Invalid transition from Transporting: must be clear";
            return false;
        }
    } else {
        errorMsg = "Cannot respond to alert in final state (" + alert.state + ")";
        return false;
    }

    save();
    return true;
}

nlohmann::ordered_json UnitRegistry::listAlertsJson() const {
    nlohmann::ordered_json list = nlohmann::json::array();
    for (const auto& a : alerts_) {
        nlohmann::ordered_json aj;
        aj["id"] = a.id;
        aj["toUser"] = a.toUser;
        aj["type"] = a.type;
        aj["severity"] = a.severity;
        aj["peopleAffected"] = a.peopleAffected;
        aj["locationText"] = a.locationText;
        if (a.lat.has_value()) aj["lat"] = *a.lat;
        if (a.lon.has_value()) aj["lon"] = *a.lon;
        aj["note"] = a.note;
        aj["createdAt"] = a.createdAt;
        aj["ackSeconds"] = a.ackSeconds;
        aj["state"] = a.state;
        if (!a.reason.empty()) aj["reason"] = a.reason;
        list.push_back(aj);
    }

    nlohmann::ordered_json resp;
    resp["ok"] = true;
    resp["alerts"] = list;
    return resp;
}

} // namespace rapidaid::ui

```

## tests/test_datacache_offline.cpp

```cpp
#include "DataCache.hpp"
#include "FileStore.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

static bool nearlyEqual(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

int main() {
    int failed = 0;
    const std::string tempDir = "build/test_tmp_cache";

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    // Case 1: Path functions with default directory ("data")
    {
        rapidaid::data::DataCache cache;

        std::string query = "Eiffel Tower, Paris";
        std::string gPath = cache.geocodePath(query);
        std::string expectedG = "data/geocode_eiffel_tower_paris.json";

        std::string pPath = cache.placesPath(rapidaid::osm::Category::Hospital, 40.7128, -74.0060, 2500);
        std::string expectedP = "data/places_hospital_40.7128_-74.0060_2500.json";

        std::string rPath = cache.roadsPath(40.7128, -74.0060, 2500);
        std::string expectedR = "data/roads_40.7128_-74.0060_2500.json";

        if (gPath == expectedG && pPath == expectedP && rPath == expectedR) {
            std::cout << "[PASS] Case 1: Path functions produce exact expected strings (default dir, negative lon)\n";
        } else {
            std::cout << "[FAIL] Case 1: Path mismatch:\n"
                      << "  geocode: got " << gPath << ", expected " << expectedG << "\n"
                      << "  places:  got " << pPath << ", expected " << expectedP << "\n"
                      << "  roads:   got " << rPath << ", expected " << expectedR << "\n";
            failed++;
        }
    }

    // Case 2: Path functions with custom directory and query with commas and spaces
    {
        rapidaid::data::DataCache cache(tempDir, false);

        std::string query = "Statue of Liberty, New York, NY";
        std::string gPath = cache.geocodePath(query);
        std::string expectedG = tempDir + "/geocode_statue_of_liberty_new_york_ny.json";

        std::string pPath = cache.placesPath(rapidaid::osm::Category::Clinic, -33.8688, 151.2093, 1500);
        std::string expectedP = tempDir + "/places_clinic_-33.8688_151.2093_1500.json";

        std::string rPath = cache.roadsPath(-33.8688, -70.6693, 1000);
        std::string expectedR = tempDir + "/roads_-33.8688_-70.6693_1000.json";

        if (gPath == expectedG && pPath == expectedP && rPath == expectedR) {
            std::cout << "[PASS] Case 2: Path functions produce exact expected strings (custom dir, spaces and commas)\n";
        } else {
            std::cout << "[FAIL] Case 2: Path mismatch (custom dir):\n"
                      << "  geocode: got " << gPath << ", expected " << expectedG << "\n"
                      << "  places:  got " << pPath << ", expected " << expectedP << "\n"
                      << "  roads:   got " << rPath << ", expected " << expectedR << "\n";
            failed++;
        }
    }

    // Case 3: Cache hit for geocode from canned file
    {
        rapidaid::data::DataCache cache(tempDir, false);
        std::string query = "Eiffel Tower, Paris";
        std::string path = cache.geocodePath(query);

        std::string cannedJson = R"([{"lat":"48.8584","lon":"2.2945","display_name":"Eiffel Tower, Paris"}])";
        if (rapidaid::io::FileStore::save(path, cannedJson)) {
            auto loc = cache.geocode(query);
            if (loc.has_value() &&
                nearlyEqual(loc->lat, 48.8584) &&
                nearlyEqual(loc->lon, 2.2945) &&
                loc->displayName == "Eiffel Tower, Paris") {
                std::cout << "[PASS] Case 3: geocode returns parsed data from canned file (cache hit)\n";
            } else {
                std::cout << "[FAIL] Case 3: geocode did not return expected canned data\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 3: failed to save canned geocode file\n";
            failed++;
        }
    }

    // Case 4: Cache hit for places from canned file
    {
        rapidaid::data::DataCache cache(tempDir, false);
        double lat = 48.8584;
        double lon = 2.2945;
        int radius = 1000;
        std::string path = cache.placesPath(rapidaid::osm::Category::Hospital, lat, lon, radius);

        std::string cannedJson = R"({
            "elements": [
                {
                    "type": "node",
                    "id": 1001,
                    "lat": 48.8585,
                    "lon": 2.2946,
                    "tags": {
                        "amenity": "hospital",
                        "name": "Eiffel General Hospital"
                    }
                }
            ]
        })";

        if (rapidaid::io::FileStore::save(path, cannedJson)) {
            auto places = cache.places(rapidaid::osm::Category::Hospital, lat, lon, radius);
            if (places.size() == 1 &&
                places[0].id == 1001 &&
                places[0].name == "Eiffel General Hospital" &&
                nearlyEqual(places[0].lat, 48.8585) &&
                nearlyEqual(places[0].lon, 2.2946)) {
                std::cout << "[PASS] Case 4: places returns parsed data from canned file (cache hit)\n";
            } else {
                std::cout << "[FAIL] Case 4: places did not return expected canned data\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 4: failed to save canned places file\n";
            failed++;
        }
    }

    // Case 5: Cache hit for roads from canned file
    {
        rapidaid::data::DataCache cache(tempDir, false);
        double lat = 48.8584;
        double lon = 2.2945;
        int radius = 1000;
        std::string path = cache.roadsPath(lat, lon, radius);

        std::string cannedJson = R"({
            "elements": [
                { "type": "node", "id": 10, "lat": 48.8580, "lon": 2.2940 },
                { "type": "node", "id": 20, "lat": 48.8590, "lon": 2.2950 },
                {
                    "type": "way",
                    "id": 201,
                    "nodes": [10, 20],
                    "tags": { "highway": "residential", "name": "Rue de Champ de Mars" }
                }
            ]
        })";

        if (rapidaid::io::FileStore::save(path, cannedJson)) {
            auto roads = cache.roads(lat, lon, radius);
            if (!roads.empty() &&
                roads.nodes.size() == 2 &&
                roads.ways.size() == 1 &&
                roads.ways[0].id == 201 &&
                roads.ways[0].name == "Rue de Champ de Mars") {
                std::cout << "[PASS] Case 5: roads returns parsed data from canned file (cache hit)\n";
            } else {
                std::cout << "[FAIL] Case 5: roads did not return expected canned data\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 5: failed to save canned roads file\n";
            failed++;
        }
    }

    // Case 6: Canned file with invalid JSON is not returned as result and is not deleted
    {
        rapidaid::data::DataCache cache(tempDir, false);

        // Geocode invalid JSON with empty query so fetchRaw returns "" without network
        std::string badGeoPath = cache.geocodePath("");
        rapidaid::io::FileStore::save(badGeoPath, "{not valid json at all");
        auto badGeo = cache.geocode("");
        bool geoPass = (!badGeo.has_value() && rapidaid::io::FileStore::exists(badGeoPath));

        // Places invalid JSON with out-of-range lat so fetchRaw returns "" without network
        std::string badPlacesPath = cache.placesPath(rapidaid::osm::Category::Hospital, 999.0, 999.0, 1000);
        rapidaid::io::FileStore::save(badPlacesPath, "{bad json elements [");
        auto badPlaces = cache.places(rapidaid::osm::Category::Hospital, 999.0, 999.0, 1000);
        bool placesPass = (badPlaces.empty() && rapidaid::io::FileStore::exists(badPlacesPath));

        // Roads invalid JSON with out-of-range lat so fetchRaw returns "" without network
        std::string badRoadsPath = cache.roadsPath(999.0, 999.0, 1000);
        rapidaid::io::FileStore::save(badRoadsPath, "invalid json string");
        auto badRoads = cache.roads(999.0, 999.0, 1000);
        bool roadsPass = (badRoads.empty() && rapidaid::io::FileStore::exists(badRoadsPath));

        if (geoPass && placesPass && roadsPass) {
            std::cout << "[PASS] Case 6: Invalid JSON files are treated as misses, not returned, and not deleted\n";
        } else {
            std::cout << "[FAIL] Case 6: Invalid JSON handling failed (geo=" << geoPass
                      << ", places=" << placesPass << ", roads=" << roadsPass << ")\n";
            failed++;
        }
    }

    // Case 7: Clean up temporary test directory
    std::filesystem::remove_all(tempDir, ec);
    if (!std::filesystem::exists(tempDir)) {
        std::cout << "[PASS] Case 7: Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Case 7: Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nDataCache Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}

```

## tests/test_filestore_offline.cpp

```cpp
#include "FileStore.hpp"

#include <filesystem>
#include <iostream>
#include <string>

using rapidaid::io::FileStore;

int main() {
    int failed = 0;
    const std::string testDir = "build/test_tmp_filestore";

    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    // Case 1: save then load round-trips text with newlines and non-ASCII bytes
    {
        std::string path = testDir + "/roundtrip.bin";
        std::string original = "Line 1\r\nLine 2\nLine 3\nCaf\xC3\xA9 \xE2\x82\xAC \x80\xFF\xAA";
        original.push_back('\0');
        original.append("null_byte_following\r\n");

        if (FileStore::save(path, original)) {
            std::string loaded = FileStore::load(path);
            if (loaded == original) {
                std::cout << "[PASS] Case 1: save then load round-trips text with newlines and non-ASCII bytes\n";
            } else {
                std::cout << "[FAIL] Case 1: loaded text did not match original\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 1: save failed\n";
            failed++;
        }
    }

    // Case 2: save creates nested directories
    {
        std::string nestedPath = testDir + "/a/b/c.json";
        std::string content = "{\"nested\": true}";
        if (FileStore::save(nestedPath, content) &&
            FileStore::exists(nestedPath) &&
            FileStore::load(nestedPath) == content) {
            std::cout << "[PASS] Case 2: save creates nested directories\n";
        } else {
            std::cout << "[FAIL] Case 2: save failed to create nested directories\n";
            failed++;
        }
    }

    // Case 3: load of a missing file returns ""
    {
        std::string missingPath = testDir + "/missing_file_xyz.txt";
        std::string result = FileStore::load(missingPath);
        if (result.empty()) {
            std::cout << "[PASS] Case 3: load of a missing file returns \"\"\n";
        } else {
            std::cout << "[FAIL] Case 3: load of missing file did not return empty string\n";
            failed++;
        }
    }

    // Case 4: exists returns true for existing file, false for non-existing or directory
    {
        std::string filePath = testDir + "/exists_test.txt";
        FileStore::save(filePath, "exists");

        bool fileExists = FileStore::exists(filePath);
        bool missingExists = FileStore::exists(testDir + "/nonexistent.txt");
        bool dirExists = FileStore::exists(testDir);

        if (fileExists && !missingExists && !dirExists) {
            std::cout << "[PASS] Case 4: exists returns true for existing file, false for non-existing or directory\n";
        } else {
            std::cout << "[FAIL] Case 4: exists check failed (fileExists=" << fileExists
                      << ", missingExists=" << missingExists
                      << ", dirExists=" << dirExists << ")\n";
            failed++;
        }
    }

    // Case 5: save overwrites an existing file
    {
        std::string overwritePath = testDir + "/overwrite.txt";
        FileStore::save(overwritePath, "initial");
        bool savedNew = FileStore::save(overwritePath, "overwritten");
        std::string loaded = FileStore::load(overwritePath);
        if (savedNew && loaded == "overwritten") {
            std::cout << "[PASS] Case 5: save overwrites an existing file\n";
        } else {
            std::cout << "[FAIL] Case 5: save did not overwrite existing file (loaded=\"" << loaded << "\")\n";
            failed++;
        }
    }

    // Case 6: no ".tmp" file left behind after save
    {
        std::string targetPath = testDir + "/tmp_check.txt";
        std::string tmpPath = targetPath + ".tmp";
        FileStore::save(targetPath, "check tmp");
        if (!std::filesystem::exists(tmpPath)) {
            std::cout << "[PASS] Case 6: no .tmp file left behind after save\n";
        } else {
            std::cout << "[FAIL] Case 6: .tmp file was left behind\n";
            failed++;
        }
    }

    // Case 7: sanitizeFileName cases
    {
        bool s1 = (FileStore::sanitizeFileName("Eiffel Tower, Paris") == "eiffel_tower_paris");
        bool s2 = (FileStore::sanitizeFileName("  !!A  b  ") == "a_b");
        bool s3 = (FileStore::sanitizeFileName("") == "unnamed");

        std::string longInput(200, 'x');
        std::string s4Res = FileStore::sanitizeFileName(longInput);
        bool s4 = (s4Res.size() == 80 && s4Res == std::string(80, 'x'));

        if (s1 && s2 && s3 && s4) {
            std::cout << "[PASS] Case 7: sanitizeFileName cases\n";
        } else {
            std::cout << "[FAIL] Case 7: sanitizeFileName failed (s1=" << s1
                      << ", s2=" << s2 << ", s3=" << s3 << ", s4=" << s4
                      << ", s4Len=" << s4Res.size() << ")\n";
            failed++;
        }
    }

    // Cleanup temp directory
    std::filesystem::remove_all(testDir, ec);
    if (!std::filesystem::exists(testDir)) {
        std::cout << "[PASS] Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nFileStore Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}

```

## tests/test_geocoder.cpp

```cpp
#include "Geocoder.hpp"
#include <iostream>

int main() {
    std::cout << "Testing live geocoding for 'Eiffel Tower, Paris'...\n";
    auto loc = rapidaid::geo::Geocoder::geocode("Eiffel Tower, Paris");
    if (loc) {
        std::cout << "Geocode successful!\n"
                  << "DisplayName: " << loc->displayName << "\n"
                  << "Latitude: " << loc->lat << "\n"
                  << "Longitude: " << loc->lon << "\n";
    } else {
        std::cout << "Geocode failed (no result or network/parse error).\n";
    }
    return 0;
}

```

## tests/test_geocoder_offline.cpp

```cpp
#include "Geocoder.hpp"
#include <iostream>
#include <cmath>

static bool nearlyEqual(double a, double b, double epsilon = 0.0001) {
    return std::abs(a - b) < epsilon;
}

int main() {
    int failed = 0;

    // Case 1: Valid result
    {
        std::string json = R"([{"lat":"48.8584","lon":"2.2945","display_name":"Eiffel Tower"}])";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (res.has_value() &&
            nearlyEqual(res->lat, 48.8584) &&
            nearlyEqual(res->lon, 2.2945) &&
            res->displayName == "Eiffel Tower") {
            std::cout << "[PASS] Case 1: Valid result\n";
        } else {
            std::cout << "[FAIL] Case 1: Valid result\n";
            failed++;
        }
    }

    // Case 2: Empty array
    {
        std::string json = "[]";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 2: Empty array\n";
        } else {
            std::cout << "[FAIL] Case 2: Empty array\n";
            failed++;
        }
    }

    // Case 3: Invalid JSON
    {
        std::string json = "[{lat: 48.8584, invalid";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 3: Invalid JSON\n";
        } else {
            std::cout << "[FAIL] Case 3: Invalid JSON\n";
            failed++;
        }
    }

    // Case 4: Missing lat
    {
        std::string json = R"([{"lon":"2.2945","display_name":"Eiffel Tower"}])";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 4: Missing lat\n";
        } else {
            std::cout << "[FAIL] Case 4: Missing lat\n";
            failed++;
        }
    }

    // Case 5: Non-numeric lat
    {
        std::string json = R"([{"lat":"abc","lon":"2.2945","display_name":"Eiffel Tower"}])";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 5: Non-numeric lat\n";
        } else {
            std::cout << "[FAIL] Case 5: Non-numeric lat\n";
            failed++;
        }
    }

    // Case 6: Empty string
    {
        std::string json = "";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 6: Empty string\n";
        } else {
            std::cout << "[FAIL] Case 6: Empty string\n";
            failed++;
        }
    }

    // Case 7: buildUrl encoding of a query with spaces and a comma
    {
        std::string query = "Eiffel Tower, Paris";
        std::string url = rapidaid::geo::Geocoder::buildUrl(query);
        std::string expected = "https://nominatim.openstreetmap.org/search?q=Eiffel%20Tower%2C%20Paris&format=json&limit=1";
        if (url == expected) {
            std::cout << "[PASS] Case 7: buildUrl encoding (" << url << ")\n";
        } else {
            std::cout << "[FAIL] Case 7: buildUrl encoding (expected: " << expected << ", got: " << url << ")\n";
            failed++;
        }
    }

    if (failed == 0) {
        std::cout << "\nAll offline tests PASSED.\n";
        return 0;
    } else {
        std::cout << "\n" << failed << " test(s) FAILED.\n";
        return 1;
    }
}

```

## tests/test_geojson_offline.cpp

```cpp
#include "GeoJsonWriter.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "OverpassClient.hpp"
#include "json.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using rapidaid::geojson::GeoJsonWriter;
using rapidaid::graph::Graph;
using rapidaid::graph::PathResult;
using rapidaid::graph::SnappedPlace;
using rapidaid::osm::Category;
using rapidaid::osm::Place;
using json = nlohmann::json;

namespace {

bool nearlyEqual(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) < eps;
}

} // anonymous namespace

int main() {
    int failed = 0;
    const std::string testDir = "build/test_tmp_geojson";

    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    // Case 1: Top-level type is FeatureCollection and feature count matches input
    {
        std::vector<Place> places;
        Place p1{1, "node", 48.8584, 2.2945, "Eiffel Tower", {{"tourism", "attraction"}}, Category::Hospital};
        Place p2{2, "node", 48.8606, 2.3376, "Louvre", {{"tourism", "museum"}}, Category::Hospital};
        places.push_back(p1);
        places.push_back(p2);

        std::string jsonStr = GeoJsonWriter::placesCollection(places);
        json j = json::parse(jsonStr);

        bool okType = (j.value("type", "") == "FeatureCollection");
        bool okCount = (j.contains("features") && j["features"].is_array() && j["features"].size() == 2);

        std::string emptyStr = GeoJsonWriter::placesCollection({});
        json jEmpty = json::parse(emptyStr);
        bool okEmptyType = (jEmpty.value("type", "") == "FeatureCollection");
        bool okEmptyCount = (jEmpty.contains("features") && jEmpty["features"].is_array() && jEmpty["features"].empty());

        if (okType && okCount && okEmptyType && okEmptyCount) {
            std::cout << "[PASS] Case 1: Top-level type is FeatureCollection and feature count matches input\n";
        } else {
            std::cout << "[FAIL] Case 1: FeatureCollection structure or count mismatch\n";
            failed++;
        }
    }

    // Case 2: Coordinate order: lon then lat rounded to 7 decimals
    {
        Place p{10, "node", 48.8584, 2.2945, "Eiffel Tower", {}, Category::Hospital};
        std::string jsonStr = GeoJsonWriter::placesCollection({p});
        json j = json::parse(jsonStr);

        bool ok = false;
        if (j.contains("features") && j["features"].size() == 1) {
            const auto& feat = j["features"][0];
            if (feat.value("type", "") == "Feature" &&
                feat.contains("geometry") &&
                feat["geometry"].value("type", "") == "Point" &&
                feat["geometry"].contains("coordinates") &&
                feat["geometry"]["coordinates"].size() == 2) {
                double lon = feat["geometry"]["coordinates"][0].get<double>();
                double lat = feat["geometry"]["coordinates"][1].get<double>();
                if (nearlyEqual(lon, 2.2945) && nearlyEqual(lat, 48.8584)) {
                    ok = true;
                }
            }
        }

        if (ok) {
            std::cout << "[PASS] Case 2: Coordinate order: coordinates[0] is lon (2.2945), coordinates[1] is lat (48.8584)\n";
        } else {
            std::cout << "[FAIL] Case 2: Coordinate order or values incorrect\n";
            failed++;
        }
    }

    // Case 3: Place name with quotes, backslashes, and accented characters round-trips unchanged
    {
        std::string complexName = "H\xC3\xB4tel \"L'Arc\\de Triomphe\" \xC3\xA9t\xC3\xA9";
        Place p{20, "way", 10.0, 20.0, complexName, {{"amenity", "hotel"}}, Category::Hospital};

        std::string jsonStr = GeoJsonWriter::placesCollection({p});
        json j = json::parse(jsonStr);

        std::string parsedName = j["features"][0]["properties"]["name"].get<std::string>();
        std::string parsedCategory = j["features"][0]["properties"]["category"].get<std::string>();
        std::string parsedAmenity = j["features"][0]["properties"]["tags"]["amenity"].get<std::string>();

        if (parsedName == complexName && parsedCategory == "hospital" && parsedAmenity == "hotel") {
            std::cout << "[PASS] Case 3: Complex name with quotes, backslash, and accents round-trips unchanged\n";
        } else {
            std::cout << "[FAIL] Case 3: Complex name round-trip failed\n";
            failed++;
        }
    }

    // Case 4: snappedPlacesCollection carries snapped flag and distance for snapped and unsnapped places
    {
        Place p1{31, "node", 12.0, 77.0, "Near Clinic", {}, Category::Clinic};
        Place p2{32, "node", 13.0, 78.0, "Far Clinic", {}, Category::Clinic};

        SnappedPlace sp1{p1, 0, 15.25, true};
        SnappedPlace sp2{p2, -1, 550.0, false};

        std::string jsonStr = GeoJsonWriter::snappedPlacesCollection({sp1, sp2});
        json j = json::parse(jsonStr);

        bool ok = false;
        if (j["features"].size() == 2) {
            const auto& f1 = j["features"][0]["properties"];
            const auto& f2 = j["features"][1]["properties"];

            bool snap1 = f1["snapped"].get<bool>();
            double dist1 = f1["snapDistanceMeters"].get<double>();
            bool snap2 = f2["snapped"].get<bool>();
            double dist2 = f2["snapDistanceMeters"].get<double>();

            if (snap1 == true && nearlyEqual(dist1, 15.25) &&
                snap2 == false && nearlyEqual(dist2, 550.0) &&
                f1["id"].get<long long>() == 31 &&
                f2["id"].get<long long>() == 32 &&
                f1["category"].get<std::string>() == "clinic") {
                ok = true;
            }
        }

        if (ok) {
            std::cout << "[PASS] Case 4: snappedPlacesCollection carries snapped flag and distance for snapped/unsnapped\n";
        } else {
            std::cout << "[FAIL] Case 4: snappedPlacesCollection properties mismatch\n";
            failed++;
        }
    }

    // Case 5: pathCollection on 3-node path, found=false, 1-node path, out-of-range index
    {
        Graph g;
        g.addNode(101, 51.5000, -0.1000);
        g.addNode(102, 51.5100, -0.1100);
        g.addNode(103, 51.5200, -0.1200);
        g.addEdge(0, 1, 100.0);
        g.addEdge(1, 2, 150.0);

        // 3-node valid path
        PathResult pValid;
        pValid.found = true;
        pValid.distanceMeters = 250.0;
        pValid.nodes = {0, 1, 2};

        std::string validJsonStr = GeoJsonWriter::pathCollection(g, pValid);
        json jValid = json::parse(validJsonStr);

        bool okValid = false;
        if (jValid["features"].size() == 1) {
            const auto& f = jValid["features"][0];
            if (f["geometry"]["type"] == "LineString" &&
                f["geometry"]["coordinates"].size() == 3) {
                double lon0 = f["geometry"]["coordinates"][0][0].get<double>();
                double lat0 = f["geometry"]["coordinates"][0][1].get<double>();
                double lon1 = f["geometry"]["coordinates"][1][0].get<double>();
                double lat1 = f["geometry"]["coordinates"][1][1].get<double>();
                double lon2 = f["geometry"]["coordinates"][2][0].get<double>();
                double lat2 = f["geometry"]["coordinates"][2][1].get<double>();

                double dist = f["properties"]["distanceMeters"].get<double>();
                int count = f["properties"]["nodeCount"].get<int>();

                if (nearlyEqual(lon0, -0.1000) && nearlyEqual(lat0, 51.5000) &&
                    nearlyEqual(lon1, -0.1100) && nearlyEqual(lat1, 51.5100) &&
                    nearlyEqual(lon2, -0.1200) && nearlyEqual(lat2, 51.5200) &&
                    nearlyEqual(dist, 250.0) && count == 3) {
                    okValid = true;
                }
            }
        }

        // found = false
        PathResult pNotFound;
        pNotFound.found = false;
        pNotFound.distanceMeters = 0.0;
        pNotFound.nodes = {0, 1, 2};
        json jNotFound = json::parse(GeoJsonWriter::pathCollection(g, pNotFound));
        bool okNotFound = (jNotFound["features"].empty());

        // 1-node path
        PathResult pOneNode;
        pOneNode.found = true;
        pOneNode.distanceMeters = 0.0;
        pOneNode.nodes = {0};
        json jOneNode = json::parse(GeoJsonWriter::pathCollection(g, pOneNode));
        bool okOneNode = (jOneNode["features"].empty());

        // Out-of-range index (high)
        PathResult pOutOfRangeHigh;
        pOutOfRangeHigh.found = true;
        pOutOfRangeHigh.distanceMeters = 100.0;
        pOutOfRangeHigh.nodes = {0, 99};
        json jOutOfRangeHigh = json::parse(GeoJsonWriter::pathCollection(g, pOutOfRangeHigh));
        bool okOutOfRangeHigh = (jOutOfRangeHigh["features"].empty());

        // Out-of-range index (negative)
        PathResult pOutOfRangeNeg;
        pOutOfRangeNeg.found = true;
        pOutOfRangeNeg.distanceMeters = 100.0;
        pOutOfRangeNeg.nodes = {-1, 1};
        json jOutOfRangeNeg = json::parse(GeoJsonWriter::pathCollection(g, pOutOfRangeNeg));
        bool okOutOfRangeNeg = (jOutOfRangeNeg["features"].empty());

        if (okValid && okNotFound && okOneNode && okOutOfRangeHigh && okOutOfRangeNeg) {
            std::cout << "[PASS] Case 5: pathCollection handles 3-node path, found=false, 1-node path, and out-of-range indices\n";
        } else {
            std::cout << "[FAIL] Case 5: pathCollection behavior incorrect (valid=" << okValid
                      << ", notFound=" << okNotFound
                      << ", oneNode=" << okOneNode
                      << ", outHigh=" << okOutOfRangeHigh
                      << ", outNeg=" << okOutOfRangeNeg << ")\n";
            failed++;
        }
    }

    // Case 6: roadsCollection feature count equals graph.edgeCount() on small graph, empty gives zero
    {
        Graph emptyGraph;
        json jEmpty = json::parse(GeoJsonWriter::roadsCollection(emptyGraph));
        bool okEmpty = (jEmpty["type"] == "FeatureCollection" && jEmpty["features"].empty());

        Graph g;
        g.addNode(1, 48.0, 2.0);
        g.addNode(2, 48.1, 2.1);
        g.addNode(3, 48.2, 2.2);
        g.addNode(4, 48.3, 2.3);

        g.addEdge(0, 1, 100.0);
        g.addEdge(1, 2, 200.0);
        g.addEdge(2, 3, 300.0);
        g.addEdge(0, 2, 400.0);

        bool countMatches = (g.edgeCount() == 4);
        json jRoads = json::parse(GeoJsonWriter::roadsCollection(g));
        bool okCount = (jRoads["type"] == "FeatureCollection" && jRoads["features"].size() == 4);

        bool okGeom = true;
        for (const auto& feat : jRoads["features"]) {
            if (feat["geometry"]["type"] != "LineString" ||
                feat["geometry"]["coordinates"].size() != 2 ||
                !feat["properties"].contains("weightMeters")) {
                okGeom = false;
                break;
            }
        }

        if (okEmpty && countMatches && okCount && okGeom) {
            std::cout << "[PASS] Case 6: roadsCollection feature count matches edgeCount and empty gives 0\n";
        } else {
            std::cout << "[FAIL] Case 6: roadsCollection check failed\n";
            failed++;
        }
    }

    // Case 7: writeFile round-trips via FileStore::load, creates nested dirs, returns false for impossible path
    {
        std::string filePath = testDir + "/sub/nested/map.geojson";
        std::string content = "{\"type\":\"FeatureCollection\",\"features\":[]}";

        bool saveOk = GeoJsonWriter::writeFile(filePath, content);
        std::string loaded = rapidaid::io::FileStore::load(filePath);
        bool match = (saveOk && loaded == content);

        bool impossibleOk = !GeoJsonWriter::writeFile("", content);

        if (match && impossibleOk) {
            std::cout << "[PASS] Case 7: writeFile creates nested dirs, round-trips identically, and rejects invalid path\n";
        } else {
            std::cout << "[FAIL] Case 7: writeFile behavior incorrect (match=" << match
                      << ", impossibleOk=" << impossibleOk << ")\n";
            failed++;
        }
    }

    // Clean up temporary directory
    std::filesystem::remove_all(testDir, ec);
    if (!std::filesystem::exists(testDir)) {
        std::cout << "[PASS] Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nGeoJsonWriter Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}

```

## tests/test_geomath_offline.cpp

```cpp
#include "GeoMath.hpp"

#include <cmath>
#include <iostream>

using rapidaid::geo::haversineMeters;

int main() {
    int failed = 0;

    // Case 1: identical points -> 0.0
    {
        double d1 = haversineMeters(0.0, 0.0, 0.0, 0.0);
        double d2 = haversineMeters(48.8584, 2.2945, 48.8584, 2.2945);
        double d3 = haversineMeters(-33.8688, 151.2093, -33.8688, 151.2093);

        if (std::abs(d1) < 1e-6 && std::abs(d2) < 1e-6 && std::abs(d3) < 1e-6) {
            std::cout << "[PASS] Case 1: identical points -> 0.0\n";
        } else {
            std::cout << "[FAIL] Case 1: identical points did not yield 0.0 (d1="
                      << d1 << ", d2=" << d2 << ", d3=" << d3 << ")\n";
            failed++;
        }
    }

    // Case 2: (0,0) to (0,1) is within 1 m of 111195 m
    {
        double dist = haversineMeters(0.0, 0.0, 0.0, 1.0);
        if (std::abs(dist - 111195.0) <= 1.0) {
            std::cout << "[PASS] Case 2: (0,0) to (0,1) is within 1 m of 111195 m (dist=" << dist << ")\n";
        } else {
            std::cout << "[FAIL] Case 2: (0,0) to (0,1) expected within 1m of 111195 m, got " << dist << "\n";
            failed++;
        }
    }

    // Case 3: symmetry: haversineMeters(a, b, c, d) == haversineMeters(c, d, a, b)
    {
        bool sym1 = (haversineMeters(12.34, 56.78, -23.45, 123.45) ==
                     haversineMeters(-23.45, 123.45, 12.34, 56.78));
        bool sym2 = (haversineMeters(0.0, 179.9, 0.0, -179.9) ==
                     haversineMeters(0.0, -179.9, 0.0, 179.9));
        bool sym3 = (haversineMeters(51.5074, -0.1278, 40.7128, -74.0060) ==
                     haversineMeters(40.7128, -74.0060, 51.5074, -0.1278));

        if (sym1 && sym2 && sym3) {
            std::cout << "[PASS] Case 3: symmetry haversineMeters(a, b, c, d) == haversineMeters(c, d, a, b)\n";
        } else {
            std::cout << "[FAIL] Case 3: symmetry check failed (sym1=" << sym1
                      << ", sym2=" << sym2 << ", sym3=" << sym3 << ")\n";
            failed++;
        }
    }

    // Case 4: antimeridian crossing: (lat 0, lon 179.9) to (lat 0, lon -179.9) is ~22 km (~22239 m), not ~40000 km
    {
        double dist = haversineMeters(0.0, 179.9, 0.0, -179.9);
        if (std::abs(dist - 22239.0) <= 50.0 && dist < 40000.0) {
            std::cout << "[PASS] Case 4: antimeridian crossing is ~22 km (dist=" << dist << ")\n";
        } else {
            std::cout << "[FAIL] Case 4: antimeridian crossing expected ~22239 m, got " << dist << "\n";
            failed++;
        }
    }

    std::cout << "\nGeoMath Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}

```

## tests/test_graph_offline.cpp

```cpp
#include "Graph.hpp"
#include "GeoMath.hpp"

#include <cmath>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using rapidaid::graph::Graph;
using rapidaid::graph::PathResult;
using rapidaid::geo::haversineMeters;

static int failures = 0;

static void assertTest(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failures++;
    }
}

int main() {
    std::cout << "Running test_graph_offline...\n";

    // 1. addNode dedupe
    {
        Graph g;
        int idx1 = g.addNode(1001, 12.0, 77.0);
        int idx2 = g.addNode(1001, 15.0, 80.0); // same osmId, different coords ignored
        assertTest(idx1 == 0 && idx2 == 0, "addNode dedupe returns same index");
        assertTest(g.nodeCount() == 1, "addNode dedupe keeps node count at 1");
        assertTest(g.indexOf(1001) == 0, "indexOf finds deduplicated node");
        assertTest(std::abs(g.lat(0) - 12.0) < 1e-6 && std::abs(g.lon(0) - 77.0) < 1e-6, "addNode dedupe keeps original coordinates");

        int idx3 = g.addNode(1002, 13.0, 78.0);
        assertTest(idx3 == 1 && g.nodeCount() == 2, "addNode with new osmId increments count and returns index 1");
    }

    // 2. addEdge undirected (neighbors of both ends)
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        g.addEdge(a, b, 150.0);

        const auto& nA = g.neighbors(a);
        const auto& nB = g.neighbors(b);

        bool aToB = (nA.size() == 1 && nA[0].to == b && std::abs(nA[0].weightMeters - 150.0) < 1e-6);
        bool bToA = (nB.size() == 1 && nB[0].to == a && std::abs(nB[0].weightMeters - 150.0) < 1e-6);
        assertTest(aToB && bToA, "addEdge creates undirected edges in both neighbor lists");
    }

    // 3. self-loop ignored
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        g.addEdge(a, a, 50.0);
        assertTest(g.edgeCount() == 0, "self-loop edge is ignored (edgeCount is 0)");
        assertTest(g.neighbors(a).empty(), "self-loop does not appear in neighbors list");
    }

    // 4. parallel edge keeps the smaller weight
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        g.addEdge(a, b, 100.0);
        g.addEdge(a, b, 150.0); // larger weight, should be ignored
        assertTest(g.edgeCount() == 1, "parallel edge does not increase edge count");
        assertTest(std::abs(g.neighbors(a)[0].weightMeters - 100.0) < 1e-6, "parallel edge with larger weight ignored");
        assertTest(std::abs(g.neighbors(b)[0].weightMeters - 100.0) < 1e-6, "parallel edge larger weight ignored in reverse list");

        g.addEdge(a, b, 60.0); // smaller weight, should replace
        assertTest(g.edgeCount() == 1, "parallel edge smaller weight does not increase edge count");
        assertTest(std::abs(g.neighbors(a)[0].weightMeters - 60.0) < 1e-6, "parallel edge updates to smaller weight in a");
        assertTest(std::abs(g.neighbors(b)[0].weightMeters - 60.0) < 1e-6, "parallel edge updates to smaller weight in b");
    }

    // 5. edgeCount counts once
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        int c = g.addNode(3, 10.2, 20.2);
        g.addEdge(a, b, 10.0);
        assertTest(g.edgeCount() == 1, "edgeCount is 1 after first undirected edge");
        g.addEdge(b, c, 20.0);
        assertTest(g.edgeCount() == 2, "edgeCount is 2 after second undirected edge");
        g.addEdge(a, c, 30.0);
        assertTest(g.edgeCount() == 3, "edgeCount is 3 after third undirected edge");
    }

    // 6. shortestPath picks a cheaper two-hop route over a longer direct edge
    {
        Graph g;
        int n0 = g.addNode(10, 0.0, 0.0);
        int n1 = g.addNode(20, 0.0, 1.0);
        int n2 = g.addNode(30, 0.0, 2.0);

        g.addEdge(n0, n2, 100.0); // Direct edge with weight 100
        g.addEdge(n0, n1, 20.0);  // Two-hop route: 20 + 30 = 50
        g.addEdge(n1, n2, 30.0);

        PathResult res = g.shortestPath(n0, n2);
        assertTest(res.found, "shortestPath found two-hop route");
        assertTest(std::abs(res.distanceMeters - 50.0) < 1e-6, "shortestPath distance is 50.0");
        std::vector<int> expectedNodes = {n0, n1, n2};
        assertTest(res.nodes == expectedNodes, "shortestPath nodes sequence is [0, 1, 2]");
    }

    // 7. from == to
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        PathResult res = g.shortestPath(a, a);
        assertTest(res.found, "shortestPath from==to found is true");
        assertTest(std::abs(res.distanceMeters) < 1e-6, "shortestPath from==to distance is 0");
        std::vector<int> expected = {a};
        assertTest(res.nodes == expected, "shortestPath from==to nodes is {from}");
    }

    // 8. no path between two disconnected nodes
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        int c = g.addNode(3, 10.2, 20.2);
        g.addEdge(a, b, 10.0);
        // c is disconnected
        PathResult res = g.shortestPath(a, c);
        assertTest(!res.found, "shortestPath between disconnected nodes has found=false");
        assertTest(std::abs(res.distanceMeters) < 1e-6, "shortestPath disconnected distance is 0.0");
        assertTest(res.nodes.empty(), "shortestPath disconnected nodes list is empty");
    }

    // 9. invalid indices
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        g.addEdge(a, b, 15.0);

        PathResult badFrom = g.shortestPath(-1, b);
        assertTest(!badFrom.found && badFrom.nodes.empty(), "shortestPath with negative from returns not found");

        PathResult badTo = g.shortestPath(a, 99);
        assertTest(!badTo.found && badTo.nodes.empty(), "shortestPath with out-of-range to returns not found");

        assertTest(g.osmId(-1) == 0 && g.osmId(99) == 0, "osmId out-of-range returns 0");
        assertTest(std::abs(g.lat(-1)) < 1e-6 && std::abs(g.lat(99)) < 1e-6, "lat out-of-range returns 0.0");
        assertTest(std::abs(g.lon(-1)) < 1e-6 && std::abs(g.lon(99)) < 1e-6, "lon out-of-range returns 0.0");
        assertTest(g.indexOf(999999) == -1, "indexOf nonexistent osmId returns -1");

        // addEdge invalid indices and negative weight
        size_t initialEdges = g.edgeCount();
        g.addEdge(-1, b, 10.0);
        g.addEdge(a, 99, 10.0);
        g.addEdge(a, b, -10.0);
        assertTest(g.edgeCount() == initialEdges, "addEdge ignores bad indices and negative weights");
    }

    // 10. nearestNode on a small grid and on an empty graph
    {
        Graph emptyGraph;
        double dummyDist = 999.0;
        int emptyNearest = emptyGraph.nearestNode(10.0, 20.0, &dummyDist);
        assertTest(emptyNearest == -1, "nearestNode on empty graph returns -1");
        assertTest(std::abs(dummyDist - 999.0) < 1e-6, "nearestNode on empty graph does not modify distance out");

        Graph grid;
        int n00 = grid.addNode(1, 0.0, 0.0);
        grid.addNode(2, 0.0, 1.0);
        grid.addNode(3, 1.0, 0.0);
        grid.addNode(4, 1.0, 1.0);

        double outDist = 0.0;
        int nearest = grid.nearestNode(0.001, 0.001, &outDist);
        assertTest(nearest == n00, "nearestNode on small grid finds (0,0)");
        double expectedDist = haversineMeters(0.001, 0.001, 0.0, 0.0);
        assertTest(std::abs(outDist - expectedDist) < 1e-3, "nearestNode returns correct haversine distance");
    }

    // 11. largestComponent keeps the bigger of two components and preserves ids
    {
        Graph g;
        // Component A: 2 nodes
        int a1 = g.addNode(101, 1.0, 1.0);
        int a2 = g.addNode(102, 1.1, 1.1);
        g.addEdge(a1, a2, 10.0);

        // Component B: 3 nodes
        int b1 = g.addNode(201, 2.0, 2.0);
        int b2 = g.addNode(202, 2.1, 2.1);
        int b3 = g.addNode(203, 2.2, 2.2);
        g.addEdge(b1, b2, 20.0);
        g.addEdge(b2, b3, 30.0);

        Graph lc = g.largestComponent();
        assertTest(lc.nodeCount() == 3, "largestComponent nodeCount is 3");
        assertTest(lc.edgeCount() == 2, "largestComponent edgeCount is 2");

        assertTest(lc.indexOf(201) != -1 && lc.indexOf(202) != -1 && lc.indexOf(203) != -1,
                   "largestComponent preserves Component B OSM IDs");
        assertTest(lc.indexOf(101) == -1 && lc.indexOf(102) == -1,
                   "largestComponent drops Component A OSM IDs");

        int newB1 = lc.indexOf(201);
        assertTest(std::abs(lc.lat(newB1) - 2.0) < 1e-6 && std::abs(lc.lon(newB1) - 2.0) < 1e-6,
                   "largestComponent preserves coordinates");

        // Tie-breaker test: two components of equal size
        Graph tieGraph;
        int t0 = tieGraph.addNode(301, 3.0, 3.0);
        int t1 = tieGraph.addNode(302, 3.1, 3.1);
        tieGraph.addEdge(t0, t1, 5.0);

        int t2 = tieGraph.addNode(401, 4.0, 4.0);
        int t3 = tieGraph.addNode(402, 4.1, 4.1);
        tieGraph.addEdge(t2, t3, 5.0);

        Graph lcTie = tieGraph.largestComponent();
        assertTest(lcTie.nodeCount() == 2, "largestComponent tie-breaker keeps size 2");
        assertTest(lcTie.indexOf(301) != -1 && lcTie.indexOf(302) != -1,
                   "largestComponent tie-breaker selects component containing lowest node index");
        assertTest(lcTie.indexOf(401) == -1 && lcTie.indexOf(402) == -1,
                   "largestComponent tie-breaker excludes component with higher indices");
    }

    // 12. neighbors of a bad index
    {
        Graph g;
        g.addNode(1, 10.0, 20.0);
        assertTest(g.neighbors(-1).empty(), "neighbors of -1 is empty");
        assertTest(g.neighbors(99).empty(), "neighbors of 99 is empty");
    }

    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "Graph Test Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
        << " (" << failures << " failures)";
    std::cout << oss.str() << "\n";

    return (failures == 0) ? 0 : 1;
}

```

## tests/test_graphbuilder_offline.cpp

```cpp
#include "GraphBuilder.hpp"
#include "GeoMath.hpp"
#include "OverpassClient.hpp"
#include "RoadClient.hpp"

#include <cmath>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using rapidaid::graph::Graph;
using rapidaid::graph::GraphBuilder;
using rapidaid::graph::SnappedPlace;
using rapidaid::geo::haversineMeters;
using rapidaid::osm::Place;
using rapidaid::osm::RoadData;
using rapidaid::osm::RoadNode;
using rapidaid::osm::RoadWay;

static int failures = 0;

static void assertTest(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failures++;
    }
}

int main() {
    std::cout << "Running test_graphbuilder_offline...\n";

    // 1. build from a hand-made RoadData (4 nodes, 2 ways)
    // check node/edge counts and that edge weights equal haversineMeters of the endpoints
    {
        RoadData rd;
        rd.nodes[10] = RoadNode{10, 12.9716, 77.5946};
        rd.nodes[20] = RoadNode{20, 12.9720, 77.5950};
        rd.nodes[30] = RoadNode{30, 12.9725, 77.5955};
        rd.nodes[40] = RoadNode{40, 12.9730, 77.5960};

        RoadWay w1;
        w1.id = 100;
        w1.nodeIds = {10, 20, 30};
        w1.highway = "residential";

        RoadWay w2;
        w2.id = 200;
        w2.nodeIds = {30, 40};
        w2.highway = "residential";

        rd.ways = {w1, w2};

        Graph g = GraphBuilder::build(rd, false);
        assertTest(g.nodeCount() == 4, "Case 1: hand-made RoadData nodeCount is 4");
        assertTest(g.edgeCount() == 3, "Case 1: hand-made RoadData edgeCount is 3");

        int idx10 = g.indexOf(10);
        int idx20 = g.indexOf(20);
        int idx30 = g.indexOf(30);
        int idx40 = g.indexOf(40);

        assertTest(idx10 != -1 && idx20 != -1 && idx30 != -1 && idx40 != -1,
                   "Case 1: all 4 nodes present in graph");

        // Check edge weight 10 <-> 20
        double expectedW1 = haversineMeters(12.9716, 77.5946, 12.9720, 77.5950);
        bool foundW1 = false;
        for (const auto& edge : g.neighbors(idx10)) {
            if (edge.to == idx20 && std::abs(edge.weightMeters - expectedW1) < 1e-6) {
                foundW1 = true;
                break;
            }
        }
        assertTest(foundW1, "Case 1: edge weight (10, 20) equals haversineMeters");

        // Check edge weight 20 <-> 30
        double expectedW2 = haversineMeters(12.9720, 77.5950, 12.9725, 77.5955);
        bool foundW2 = false;
        for (const auto& edge : g.neighbors(idx20)) {
            if (edge.to == idx30 && std::abs(edge.weightMeters - expectedW2) < 1e-6) {
                foundW2 = true;
                break;
            }
        }
        assertTest(foundW2, "Case 1: edge weight (20, 30) equals haversineMeters");

        // Check edge weight 30 <-> 40
        double expectedW3 = haversineMeters(12.9725, 77.5955, 12.9730, 77.5960);
        bool foundW3 = false;
        for (const auto& edge : g.neighbors(idx30)) {
            if (edge.to == idx40 && std::abs(edge.weightMeters - expectedW3) < 1e-6) {
                foundW3 = true;
                break;
            }
        }
        assertTest(foundW3, "Case 1: edge weight (30, 40) equals haversineMeters");
    }

    // 2. a way referencing a missing node skips only that segment
    {
        RoadData rd;
        rd.nodes[1] = RoadNode{1, 10.0, 10.0};
        rd.nodes[2] = RoadNode{2, 10.1, 10.0};
        // Node 999 is intentionally missing
        rd.nodes[4] = RoadNode{4, 10.3, 10.0};
        rd.nodes[5] = RoadNode{5, 10.4, 10.0};

        RoadWay w;
        w.id = 500;
        // Segments: (1, 2) valid, (2, 999) invalid, (999, 4) invalid, (4, 5) valid
        w.nodeIds = {1, 2, 999, 4, 5};
        rd.ways = {w};

        Graph g = GraphBuilder::build(rd, false);
        assertTest(g.nodeCount() == 4, "Case 2: 4 valid nodes added");
        assertTest(g.edgeCount() == 2, "Case 2: 2 valid edges added, missing node segments skipped");
        assertTest(g.indexOf(999) == -1, "Case 2: missing node 999 not in graph");

        int idx1 = g.indexOf(1);
        int idx2 = g.indexOf(2);
        int idx4 = g.indexOf(4);
        int idx5 = g.indexOf(5);
        assertTest(g.neighbors(idx1).size() == 1 && g.neighbors(idx1)[0].to == idx2,
                   "Case 2: segment (1, 2) connected");
        assertTest(g.neighbors(idx4).size() == 1 && g.neighbors(idx4)[0].to == idx5,
                   "Case 2: segment (4, 5) connected");
    }

    // 3. a RoadData with two disconnected roads keeps only the larger one when keepLargestComponent is true and both when false
    {
        RoadData rd;
        // Road A: 2 nodes
        rd.nodes[11] = RoadNode{11, 20.0, 30.0};
        rd.nodes[12] = RoadNode{12, 20.1, 30.1};
        RoadWay wa;
        wa.id = 1;
        wa.nodeIds = {11, 12};

        // Road B: 3 nodes
        rd.nodes[21] = RoadNode{21, 40.0, 50.0};
        rd.nodes[22] = RoadNode{22, 40.1, 50.1};
        rd.nodes[23] = RoadNode{23, 40.2, 50.2};
        RoadWay wb;
        wb.id = 2;
        wb.nodeIds = {21, 22, 23};

        rd.ways = {wa, wb};

        // keepLargestComponent == true
        Graph gLargest = GraphBuilder::build(rd, true);
        assertTest(gLargest.nodeCount() == 3, "Case 3: keepLargestComponent true has 3 nodes");
        assertTest(gLargest.edgeCount() == 2, "Case 3: keepLargestComponent true has 2 edges");
        assertTest(gLargest.indexOf(21) != -1 && gLargest.indexOf(22) != -1 && gLargest.indexOf(23) != -1,
                   "Case 3: larger road B nodes present");
        assertTest(gLargest.indexOf(11) == -1 && gLargest.indexOf(12) == -1,
                   "Case 3: smaller road A nodes dropped");

        // keepLargestComponent == false
        Graph gAll = GraphBuilder::build(rd, false);
        assertTest(gAll.nodeCount() == 5, "Case 3: keepLargestComponent false has 5 nodes");
        assertTest(gAll.edgeCount() == 3, "Case 3: keepLargestComponent false has 3 edges");
        assertTest(gAll.indexOf(11) != -1 && gAll.indexOf(12) != -1 &&
                   gAll.indexOf(21) != -1 && gAll.indexOf(22) != -1 && gAll.indexOf(23) != -1,
                   "Case 3: all nodes from both roads present");
    }

    // 4. empty RoadData gives an empty graph
    {
        RoadData emptyRd;
        Graph g = GraphBuilder::build(emptyRd, true);
        assertTest(g.nodeCount() == 0, "Case 4: empty RoadData nodeCount is 0");
        assertTest(g.edgeCount() == 0, "Case 4: empty RoadData edgeCount is 0");

        Graph gNoFilter = GraphBuilder::build(emptyRd, false);
        assertTest(gNoFilter.nodeCount() == 0, "Case 4: empty RoadData (keepLargestComponent=false) nodeCount is 0");
        assertTest(gNoFilter.edgeCount() == 0, "Case 4: empty RoadData (keepLargestComponent=false) edgeCount is 0");
    }

    // 5. snapPlaces: a place near a node snaps, a place 5 km away is unsnapped, input order is preserved
    {
        Graph g;
        int nIdx = g.addNode(100, 12.9716, 77.5946);

        // Place near node (approx 15 meters away)
        Place pNear;
        pNear.id = 1;
        pNear.lat = 12.9717;
        pNear.lon = 77.5947;
        pNear.name = "Near Clinic";

        // Place far away (~5.5 km away)
        // 0.05 degrees latitude is roughly 5.5 km
        Place pFar;
        pFar.id = 2;
        pFar.lat = 13.0216;
        pFar.lon = 77.5946;
        pFar.name = "Far Hospital";

        std::vector<Place> places = {pNear, pFar};
        std::vector<SnappedPlace> snapped = GraphBuilder::snapPlaces(g, places, 500.0);

        assertTest(snapped.size() == 2, "Case 5: snapPlaces output size matches input size");
        assertTest(snapped[0].place.id == pNear.id, "Case 5: input order preserved for first place");
        assertTest(snapped[1].place.id == pFar.id, "Case 5: input order preserved for second place");

        // Verify near place
        assertTest(snapped[0].snapped, "Case 5: near place is snapped");
        assertTest(snapped[0].nodeIndex == nIdx, "Case 5: near place snapped to correct nodeIndex");
        assertTest(snapped[0].snapDistanceMeters < 500.0 && snapped[0].snapDistanceMeters > 0.0,
                   "Case 5: near place snap distance within 500m");

        // Verify far place
        assertTest(!snapped[1].snapped, "Case 5: far place (5km) is unsnapped");
        assertTest(snapped[1].nodeIndex == -1, "Case 5: unsnapped place nodeIndex is -1");
        assertTest(snapped[1].snapDistanceMeters > 5000.0,
                   "Case 5: far place distance is > 5000m");
    }

    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "GraphBuilder Test Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
        << " (" << failures << " failures)";
    std::cout << oss.str() << "\n";

    return (failures == 0) ? 0 : 1;
}

```

## tests/test_http.cpp

```cpp
#include "HttpClient.hpp"
#include <iostream>
#include <string>

int main() {
    using rapidaid::net::HttpClient;

    // Test GET request to Nominatim API
    const std::string getUrl = "https://nominatim.openstreetmap.org/search?q=Eiffel+Tower&format=json&limit=1";
    std::cout << "Sending GET request to: " << getUrl << "\n";
    std::string getResult = HttpClient::get(getUrl);

    if (getResult.empty()) {
        std::cerr << "GET request failed (empty result)\n";
        return 1;
    }

    std::cout << "Response size: " << getResult.size() << " bytes\n";
    std::cout << "First 300 characters:\n" << getResult.substr(0, 300) << "\n\n";

    // Test POST request to Overpass API with "data=" and URL-encoded query: [out:json];node(1);out;
    const std::string postUrl = "https://overpass-api.de/api/interpreter";
    const std::string postBody = "data=%5Bout%3Ajson%5D%3Bnode%281%29%3Bout%3B";
    std::cout << "Sending POST request to: " << postUrl << "\n";
    std::string postResult = HttpClient::post(postUrl, postBody);

    if (postResult.empty()) {
        std::cerr << "POST request failed (empty result)\n";
        return 1;
    }

    std::cout << "Response size: " << postResult.size() << " bytes\n";
    std::cout << "First 300 characters:\n" << postResult.substr(0, 300) << "\n";

    return 0;
}

```

## tests/test_login_offline.cpp

```cpp
#include "Login.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void check(bool condition, const std::string& testName, int& failed) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failed++;
    }
}

int main() {
    int failed = 0;
    fs::path origDir = fs::current_path();
    fs::path tempDir = origDir / "build" / "test_tmp_login";

    // Setup isolated temporary environment
    fs::create_directories(tempDir);
    fs::current_path(tempDir);

    std::vector<std::string> allResponses;
    const std::string pass1 = "DispatcherSecr3t!P@ss";
    const std::string passAdmin = "admin123";
    const std::string passWrong = "wrong_password_attempt";

    {
        // Pass temporary path to Auth constructor
        std::string tempUsersFile = (tempDir / "users.txt").string();
        Auth auth(tempUsersFile);

        // Case 1: Existing Auth sign-up and login via handleLoginRequest
        bool signedUp = auth.signUp("dispatcher_alpha", pass1, "dispatcher");
        check(signedUp, "Auth::signUp creates a new dispatcher user", failed);

        std::string reqSuccess = "{\"username\":\"dispatcher_alpha\",\"password\":\"" + pass1 + "\"}";
        std::string resSuccess = handleLoginRequest(auth, reqSuccess);
        allResponses.push_back(resSuccess);

        bool successOk = false;
        try {
            auto j = json::parse(resSuccess);
            successOk = (j["ok"] == true && j["username"] == "dispatcher_alpha" && j["role"] == "dispatcher");
        } catch (...) {
            successOk = false;
        }
        check(successOk, "handleLoginRequest succeeds with correct username and role", failed);

        // Verify default admin account login as well
        std::string reqAdmin = "{\"username\":\"admin\",\"password\":\"" + passAdmin + "\"}";
        std::string resAdmin = handleLoginRequest(auth, reqAdmin);
        allResponses.push_back(resAdmin);

        bool adminOk = false;
        try {
            auto j = json::parse(resAdmin);
            adminOk = (j["ok"] == true && j["username"] == "admin" && j["role"] == "admin");
        } catch (...) {
            adminOk = false;
        }
        check(adminOk, "handleLoginRequest succeeds for default admin account", failed);

        // Case 2: Wrong password and unknown username give ok=false with identical error text
        std::string reqWrongPass = "{\"username\":\"dispatcher_alpha\",\"password\":\"" + passWrong + "\"}";
        std::string resWrongPass = handleLoginRequest(auth, reqWrongPass);
        allResponses.push_back(resWrongPass);

        std::string reqUnknownUser = "{\"username\":\"ghost_user\",\"password\":\"" + pass1 + "\"}";
        std::string resUnknownUser = handleLoginRequest(auth, reqUnknownUser);
        allResponses.push_back(resUnknownUser);

        bool wrongPassOk = false;
        std::string errWrong;
        try {
            auto j = json::parse(resWrongPass);
            wrongPassOk = (j["ok"] == false && j.contains("error"));
            if (wrongPassOk) errWrong = j["error"].get<std::string>();
        } catch (...) {
            wrongPassOk = false;
        }

        bool unknownUserOk = false;
        std::string errUnknown;
        try {
            auto j = json::parse(resUnknownUser);
            unknownUserOk = (j["ok"] == false && j.contains("error"));
            if (unknownUserOk) errUnknown = j["error"].get<std::string>();
        } catch (...) {
            unknownUserOk = false;
        }

        check(wrongPassOk && errWrong == "Invalid username or password",
              "Wrong password gives ok=false and error='Invalid username or password'", failed);
        check(unknownUserOk && errUnknown == "Invalid username or password",
              "Unknown username gives ok=false and error='Invalid username or password'", failed);
        check(errWrong == errUnknown,
              "Error text is exactly identical for wrong password and unknown username", failed);

        // Case 3: Invalid JSON, missing 'password' field, and numeric 'username' give {"ok":false,"error":"Bad request"}
        std::string resBadJson = handleLoginRequest(auth, "not a valid {json");
        allResponses.push_back(resBadJson);
        check(resBadJson == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Invalid JSON gives exact Bad request response", failed);

        std::string resMissingPass = handleLoginRequest(auth, "{\"username\":\"dispatcher_alpha\"}");
        allResponses.push_back(resMissingPass);
        check(resMissingPass == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Missing password field gives exact Bad request response", failed);

        std::string resMissingUser = handleLoginRequest(auth, "{\"password\":\"" + pass1 + "\"}");
        allResponses.push_back(resMissingUser);
        check(resMissingUser == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Missing username field gives exact Bad request response", failed);

        std::string resNumericUser = handleLoginRequest(auth, "{\"username\":12345,\"password\":\"" + pass1 + "\"}");
        allResponses.push_back(resNumericUser);
        check(resNumericUser == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Numeric username field gives exact Bad request response", failed);

        std::string resNumericPass = handleLoginRequest(auth, "{\"username\":\"dispatcher_alpha\",\"password\":99999}");
        allResponses.push_back(resNumericPass);
        check(resNumericPass == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Numeric password field gives exact Bad request response", failed);

        std::string resArrayJson = handleLoginRequest(auth, "[1, 2, 3]");
        allResponses.push_back(resArrayJson);
        check(resArrayJson == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Non-object JSON array gives exact Bad request response", failed);

        // Case 4: The response never contains the password text
        bool responseLeakedPassword = false;
        for (const auto& resp : allResponses) {
            if (resp.find(pass1) != std::string::npos ||
                resp.find(passAdmin) != std::string::npos ||
                resp.find(passWrong) != std::string::npos) {
                responseLeakedPassword = true;
                break;
            }
        }
        check(!responseLeakedPassword, "Login responses never contain plaintext password strings", failed);

        // Case 5: handleLogoutRequest returns ok=true
        std::string resLogout = handleLogoutRequest(auth, "dispatcher_alpha");
        check(resLogout == "{\"ok\":true}", "handleLogoutRequest returns exact {\"ok\":true}", failed);

        // Case 6: handleHealthRequest returns ok=true
        std::string resHealth = handleHealthRequest();
        check(resHealth == "{\"ok\":true,\"service\":\"login\"}",
              "handleHealthRequest returns exact {\"ok\":true,\"service\":\"login\"}", failed);

        // Case 7: The log file (if one is written) never contains the password text
        fs::path logFilePath = tempDir / "log.txt";
        bool logFileFound = fs::exists(logFilePath);
        if (logFileFound) {
            std::ifstream logIn(logFilePath);
            std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                   std::istreambuf_iterator<char>());
            bool logLeakedPassword = (logContent.find(pass1) != std::string::npos ||
                                      logContent.find(passAdmin) != std::string::npos ||
                                      logContent.find(passWrong) != std::string::npos);
            check(!logLeakedPassword, "Log file exists and never contains plaintext passwords", failed);

            bool loggedEvents = (logContent.find("LOGIN_SUCCESS") != std::string::npos &&
                                 logContent.find("LOGIN_FAILED") != std::string::npos &&
                                 logContent.find("LOGOUT") != std::string::npos);
            check(loggedEvents, "Log file contains expected LOGIN_SUCCESS, LOGIN_FAILED, and LOGOUT entries", failed);
        } else {
            check(true, "Log file verification skipped as no log file was written", failed);
        }
    }

    // Restore working directory
    fs::current_path(origDir);

    // Verify workspace was never touched
    bool workspaceUsersExists = fs::exists(origDir / "users.txt");
    bool workspaceLogExists = fs::exists(origDir / "log.txt");
    check(!workspaceUsersExists, "Workspace root users.txt was never created or accessed", failed);
    check(!workspaceLogExists, "Workspace root log.txt was never created or accessed", failed);

    // Cleanup temp directory
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory build/test_tmp_login/ cleanly removed", failed);

    std::cout << "\nTest Summary: " << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}

```

## tests/test_overpass.cpp

```cpp
#include "OverpassClient.hpp"
#include <iostream>
#include <vector>

using rapidaid::osm::Category;
using rapidaid::osm::OverpassClient;
using rapidaid::osm::Place;

int main() {
    std::cout << "Searching for hospitals near Eiffel Tower (48.8584, 2.2945, radius 3000m)...\n";

    std::vector<Place> places = OverpassClient::findNearby(Category::Hospital, 48.8584, 2.2945, 3000);

    std::cout << "Found " << places.size() << " places:\n";
    for (const auto& place : places) {
        std::cout << "- [" << place.osmType << "/" << place.id << "] "
                  << (place.name.empty() ? "(unnamed)" : place.name)
                  << " (" << place.lat << ", " << place.lon << ")\n";
    }

    return 0;
}

```

## tests/test_overpass_offline.cpp

```cpp
#include "OverpassClient.hpp"
#include <iostream>
#include <string>
#include <vector>

using rapidaid::osm::Category;
using rapidaid::osm::OverpassClient;
using rapidaid::osm::Place;

static int failures = 0;

static void assertTest(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failures++;
    }
}

int main() {
    std::cout << "Running test_overpass_offline...\n";

    // 1. categoryName check
    assertTest(OverpassClient::categoryName(Category::Hospital) == "hospital", "categoryName(Hospital)");
    assertTest(OverpassClient::categoryName(Category::Clinic) == "clinic", "categoryName(Clinic)");
    assertTest(OverpassClient::categoryName(Category::Pharmacy) == "pharmacy", "categoryName(Pharmacy)");
    assertTest(OverpassClient::categoryName(Category::FireStation) == "fire_station", "categoryName(FireStation)");
    assertTest(OverpassClient::categoryName(Category::Police) == "police", "categoryName(Police)");
    assertTest(OverpassClient::categoryName(Category::Shelter) == "shelter", "categoryName(Shelter)");
    assertTest(OverpassClient::categoryName(Category::Water) == "water", "categoryName(Water)");

    // 2. buildQuery output clauses check
    // Check Hospital
    std::string hospitalQuery = OverpassClient::buildQuery(Category::Hospital, 48.8584, 2.2945, 1000);
    assertTest(hospitalQuery.find("[out:json][timeout:25];") != std::string::npos, "Hospital query header");
    assertTest(hospitalQuery.find("node[\"amenity\"=\"hospital\"](around:1000,48.8584000,2.2945000);") != std::string::npos, "Hospital query node clause");
    assertTest(hospitalQuery.find("way[\"amenity\"=\"hospital\"](around:1000,48.8584000,2.2945000);") != std::string::npos, "Hospital query way clause");
    assertTest(hospitalQuery.find("relation[\"amenity\"=\"hospital\"](around:1000,48.8584000,2.2945000);") != std::string::npos, "Hospital query relation clause");
    assertTest(hospitalQuery.find("out center tags;") != std::string::npos, "Hospital query footer");

    // Check Shelter (amenity=shelter and emergency=assembly_point)
    std::string shelterQuery = OverpassClient::buildQuery(Category::Shelter, 12.3456, 78.9012, 500);
    assertTest(shelterQuery.find("node[\"amenity\"=\"shelter\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query node amenity clause");
    assertTest(shelterQuery.find("node[\"emergency\"=\"assembly_point\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query node emergency clause");
    assertTest(shelterQuery.find("way[\"amenity\"=\"shelter\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query way amenity clause");
    assertTest(shelterQuery.find("way[\"emergency\"=\"assembly_point\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query way emergency clause");
    assertTest(shelterQuery.find("relation[\"amenity\"=\"shelter\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query relation amenity clause");
    assertTest(shelterQuery.find("relation[\"emergency\"=\"assembly_point\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query relation emergency clause");

    // 3. buildRequestBody starts with "data=" and contains no raw spaces
    std::string reqBody = OverpassClient::buildRequestBody(hospitalQuery);
    assertTest(reqBody.rfind("data=", 0) == 0, "buildRequestBody starts with data=");
    assertTest(reqBody.find(' ') == std::string::npos, "buildRequestBody contains no raw spaces");
    assertTest(reqBody.length() > 5, "buildRequestBody contains encoded content");

    // 4. parseResponse on canned JSON
    const std::string cannedJson = R"({
      "version": 0.6,
      "generator": "Overpass API",
      "elements": [
        {
          "type": "node",
          "id": 1001,
          "lat": 12.9716,
          "lon": 77.5946,
          "tags": {
            "amenity": "hospital",
            "name": "City General Hospital",
            "emergency": "yes"
          }
        },
        {
          "type": "way",
          "id": 2002,
          "center": {
            "lat": 12.9750,
            "lon": 77.5990
          },
          "tags": {
            "amenity": "hospital",
            "name": "Apollo Clinic",
            "healthcare": "hospital"
          }
        },
        {
          "type": "relation",
          "id": 3003,
          "center": {
            "lat": 12.9800,
            "lon": 77.6000
          },
          "tags": {
            "amenity": "hospital"
          }
        },
        {
          "type": "way",
          "id": 4004,
          "tags": {
            "amenity": "hospital",
            "name": "Way Without Center"
          }
        },
        {
          "type": "node",
          "id": 5005,
          "tags": {
            "amenity": "hospital",
            "name": "Node Without Coords"
          }
        }
      ]
    })";

    std::vector<Place> places = OverpassClient::parseResponse(cannedJson, Category::Hospital);
    assertTest(places.size() == 3, "parseResponse filters elements without coordinates (expected 3)");

    if (places.size() >= 3) {
        // Check node
        assertTest(places[0].id == 1001, "Place 0 id == 1001");
        assertTest(places[0].osmType == "node", "Place 0 osmType == node");
        assertTest(places[0].lat == 12.9716, "Place 0 lat == 12.9716");
        assertTest(places[0].lon == 77.5946, "Place 0 lon == 77.5946");
        assertTest(places[0].name == "City General Hospital", "Place 0 name == City General Hospital");
        assertTest(places[0].tags["amenity"] == "hospital", "Place 0 tags[amenity]");
        assertTest(places[0].tags["emergency"] == "yes", "Place 0 tags[emergency]");
        assertTest(places[0].category == Category::Hospital, "Place 0 category == Hospital");

        // Check way
        assertTest(places[1].id == 2002, "Place 1 id == 2002");
        assertTest(places[1].osmType == "way", "Place 1 osmType == way");
        assertTest(places[1].lat == 12.9750, "Place 1 lat == 12.9750");
        assertTest(places[1].lon == 77.5990, "Place 1 lon == 77.5990");
        assertTest(places[1].name == "Apollo Clinic", "Place 1 name == Apollo Clinic");
        assertTest(places[1].tags["healthcare"] == "hospital", "Place 1 tags[healthcare]");
        assertTest(places[1].category == Category::Hospital, "Place 1 category == Hospital");

        // Check relation without name tag
        assertTest(places[2].id == 3003, "Place 2 id == 3003");
        assertTest(places[2].osmType == "relation", "Place 2 osmType == relation");
        assertTest(places[2].lat == 12.9800, "Place 2 lat == 12.9800");
        assertTest(places[2].lon == 77.6000, "Place 2 lon == 77.6000");
        assertTest(places[2].name == "", "Place 2 name empty");
        assertTest(places[2].tags["amenity"] == "hospital", "Place 2 tags[amenity]");
        assertTest(places[2].category == Category::Hospital, "Place 2 category == Hospital");
    }

    // 5. Test invalid JSON and empty strings
    std::vector<Place> emptyPlaces = OverpassClient::parseResponse("", Category::Hospital);
    assertTest(emptyPlaces.empty(), "parseResponse on empty string returns empty vector");

    std::vector<Place> invalidPlaces = OverpassClient::parseResponse("not valid json", Category::Hospital);
    assertTest(invalidPlaces.empty(), "parseResponse on invalid JSON returns empty vector");

    std::vector<Place> noElementsPlaces = OverpassClient::parseResponse("{\"version\": 0.6}", Category::Hospital);
    assertTest(noElementsPlaces.empty(), "parseResponse on missing elements returns empty vector");

    // 6. Test input validation in findNearby
    // Negative radius
    auto invalidRadius1 = OverpassClient::findNearby(Category::Hospital, 10.0, 10.0, 0);
    assertTest(invalidRadius1.empty(), "findNearby radius 0 returns empty vector");
    auto invalidRadius2 = OverpassClient::findNearby(Category::Hospital, 10.0, 10.0, 50001);
    assertTest(invalidRadius2.empty(), "findNearby radius 50001 returns empty vector");

    // Invalid lat / lon
    auto invalidLat1 = OverpassClient::findNearby(Category::Hospital, -90.1, 0.0, 100);
    assertTest(invalidLat1.empty(), "findNearby lat < -90 returns empty vector");
    auto invalidLat2 = OverpassClient::findNearby(Category::Hospital, 90.1, 0.0, 100);
    assertTest(invalidLat2.empty(), "findNearby lat > 90 returns empty vector");

    auto invalidLon1 = OverpassClient::findNearby(Category::Hospital, 0.0, -180.1, 100);
    assertTest(invalidLon1.empty(), "findNearby lon < -180 returns empty vector");
    auto invalidLon2 = OverpassClient::findNearby(Category::Hospital, 0.0, 180.1, 100);
    assertTest(invalidLon2.empty(), "findNearby lon > 180 returns empty vector");

    std::cout << "\nTest Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failures << " failures)\n";

    return (failures == 0) ? 0 : 1;
}

```

## tests/test_pipeline.cpp

```cpp
#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "GeoJsonWriter.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
    rapidaid::data::DataCache cache("data", false);

    // 1. Geocode
    std::string query = "Eiffel Tower, Paris";
    std::string geoPath = cache.geocodePath(query);
    bool geoFromCache = rapidaid::io::FileStore::exists(geoPath);
    std::cout << "Geocode input source: " << (geoFromCache ? "cache" : "network") << "\n";

    auto loc = cache.geocode(query);
    if (!loc.has_value()) {
        std::cerr << "Geocode failed for: " << query << "\n";
        return 1;
    }
    std::cout << "Geocoded location: (" << loc->lat << ", " << loc->lon << ") "
              << loc->displayName << "\n";

    // 2. Places (Hospital, radius 3000 around result)
    int radius = 3000;
    std::string placesPath = cache.placesPath(rapidaid::osm::Category::Hospital, loc->lat, loc->lon, radius);
    bool placesFromCache = rapidaid::io::FileStore::exists(placesPath);
    std::cout << "Places input source: " << (placesFromCache ? "cache" : "network") << "\n";

    auto places = cache.places(rapidaid::osm::Category::Hospital, loc->lat, loc->lon, radius);
    std::cout << "Found " << places.size() << " hospitals.\n";

    // 3. Roads (radius 3000 around result)
    std::string roadsPath = cache.roadsPath(loc->lat, loc->lon, radius);
    bool roadsFromCache = rapidaid::io::FileStore::exists(roadsPath);
    std::cout << "Roads input source: " << (roadsFromCache ? "cache" : "network") << "\n";

    auto roads = cache.roads(loc->lat, loc->lon, radius);
    std::cout << "Road data contains " << roads.nodes.size() << " nodes, "
              << roads.ways.size() << " ways.\n";

    // 4. Build graph
    rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads);
    std::cout << "Node count: " << graph.nodeCount() << "\n";
    std::cout << "Edge count: " << graph.edgeCount() << "\n";

    // 5. Snap places
    auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, places);
    std::vector<rapidaid::graph::SnappedPlace> successfullySnapped;
    for (const auto& sp : snapped) {
        if (sp.snapped) {
            successfullySnapped.push_back(sp);
        }
    }
    std::cout << "Number of snapped places: " << successfullySnapped.size() << "\n";

    // 6. Shortest path distance between the first two snapped places
    if (successfullySnapped.size() >= 2) {
        auto path = graph.shortestPath(successfullySnapped[0].nodeIndex, successfullySnapped[1].nodeIndex);
        if (path.found) {
            std::cout << "Shortest path distance between first two snapped places: "
                      << path.distanceMeters << " meters\n";
        } else {
            std::cout << "No path found between first two snapped places.\n";
        }
    } else {
        std::cout << "Fewer than two snapped places found (" << successfullySnapped.size()
                  << "); cannot compute shortest path between two places.\n";
    }

    // 7. Export GeoJSON
    std::string placesOut = "data/out/places.geojson";
    std::string roadsOut = "data/out/roads.geojson";
    std::string pathOut = "data/out/path.geojson";

    rapidaid::geojson::GeoJsonWriter::writeFile(placesOut,
        rapidaid::geojson::GeoJsonWriter::snappedPlacesCollection(snapped));
    rapidaid::geojson::GeoJsonWriter::writeFile(roadsOut,
        rapidaid::geojson::GeoJsonWriter::roadsCollection(graph));

    rapidaid::graph::PathResult pathExport;
    if (successfullySnapped.size() >= 2) {
        pathExport = graph.shortestPath(successfullySnapped[0].nodeIndex, successfullySnapped[1].nodeIndex);
    }
    rapidaid::geojson::GeoJsonWriter::writeFile(pathOut,
        rapidaid::geojson::GeoJsonWriter::pathCollection(graph, pathExport));

    std::cout << placesOut << "\n";
    std::cout << roadsOut << "\n";
    std::cout << pathOut << "\n";

    return 0;
}

```

## tests/test_pipeline_offline.cpp

```cpp
#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "GeoMath.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

static bool nearlyEqual(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

int main() {
    int failed = 0;
    const std::string tempDir = "build/test_tmp_pipeline";

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    rapidaid::data::DataCache cache(tempDir, false);

    // 1. Prepare canned Geocode JSON
    std::string query = "Paris Center";
    std::string geocodeJson = R"([
        {
            "lat": "48.8500",
            "lon": "2.3000",
            "display_name": "Paris Center"
        }
    ])";
    if (!rapidaid::io::FileStore::save(cache.geocodePath(query), geocodeJson)) {
        std::cerr << "Failed to save canned geocode file.\n";
        return 1;
    }

    // 2. Prepare canned Places JSON (2 hospitals)
    double lat = 48.8500;
    double lon = 2.3000;
    int radius = 3000;
    std::string placesJson = R"({
        "elements": [
            {
                "type": "node",
                "id": 101,
                "lat": 48.8500,
                "lon": 2.3000,
                "tags": {
                    "amenity": "hospital",
                    "name": "Hospital Saint-Michel"
                }
            },
            {
                "type": "node",
                "id": 102,
                "lat": 48.8510,
                "lon": 2.3020,
                "tags": {
                    "amenity": "hospital",
                    "name": "Hospital Saint-Louis"
                }
            }
        ]
    })";
    if (!rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Hospital, lat, lon, radius), placesJson)) {
        std::cerr << "Failed to save canned places file.\n";
        return 1;
    }

    // 3. Prepare canned Roads JSON (6 nodes street grid, 5 ways, 7 edges)
    std::string roadsJson = R"({
        "elements": [
            { "type": "node", "id": 1, "lat": 48.8500, "lon": 2.3000 },
            { "type": "node", "id": 2, "lat": 48.8500, "lon": 2.3010 },
            { "type": "node", "id": 3, "lat": 48.8500, "lon": 2.3020 },
            { "type": "node", "id": 4, "lat": 48.8510, "lon": 2.3000 },
            { "type": "node", "id": 5, "lat": 48.8510, "lon": 2.3010 },
            { "type": "node", "id": 6, "lat": 48.8510, "lon": 2.3020 },
            {
                "type": "way",
                "id": 1001,
                "nodes": [1, 2, 3],
                "tags": { "highway": "residential", "name": "South Street" }
            },
            {
                "type": "way",
                "id": 1002,
                "nodes": [4, 5, 6],
                "tags": { "highway": "residential", "name": "North Street" }
            },
            {
                "type": "way",
                "id": 1003,
                "nodes": [1, 4],
                "tags": { "highway": "residential", "name": "West Street" }
            },
            {
                "type": "way",
                "id": 1004,
                "nodes": [2, 5],
                "tags": { "highway": "residential", "name": "Central Street" }
            },
            {
                "type": "way",
                "id": 1005,
                "nodes": [3, 6],
                "tags": { "highway": "residential", "name": "East Street" }
            }
        ]
    })";
    if (!rapidaid::io::FileStore::save(cache.roadsPath(lat, lon, radius), roadsJson)) {
        std::cerr << "Failed to save canned roads file.\n";
        return 1;
    }

    // Pipeline Step 1: DataCache -> geocode
    auto loc = cache.geocode(query);
    if (loc.has_value() && nearlyEqual(loc->lat, 48.8500) && nearlyEqual(loc->lon, 2.3000)) {
        std::cout << "[PASS] Pipeline Step 1: geocode from cache\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 1: geocode failed\n";
        failed++;
    }

    // Pipeline Step 2: DataCache -> places
    auto places = cache.places(rapidaid::osm::Category::Hospital, loc ? loc->lat : 48.8500, loc ? loc->lon : 2.3000, radius);
    if (places.size() == 2) {
        std::cout << "[PASS] Pipeline Step 2: places from cache (2 hospitals)\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 2: places returned " << places.size() << " items (expected 2)\n";
        failed++;
    }

    // Pipeline Step 3: DataCache -> roads
    auto roads = cache.roads(loc ? loc->lat : 48.8500, loc ? loc->lon : 2.3000, radius);
    if (!roads.empty() && roads.nodes.size() == 6 && roads.ways.size() == 5) {
        std::cout << "[PASS] Pipeline Step 3: roads from cache\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 3: roads returned unexpected structure\n";
        failed++;
    }

    // Pipeline Step 4: GraphBuilder::build
    rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads, true);
    if (graph.nodeCount() == 6 && graph.edgeCount() == 7) {
        std::cout << "[PASS] Pipeline Step 4: GraphBuilder::build (6 nodes, 7 edges)\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 4: unexpected graph size (nodes=" << graph.nodeCount()
                  << ", edges=" << graph.edgeCount() << ")\n";
        failed++;
    }

    // Pipeline Step 5: snapPlaces
    auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, places, 500.0);
    if (snapped.size() == 2 && snapped[0].snapped && snapped[1].snapped) {
        std::cout << "[PASS] Pipeline Step 5: snapPlaces successfully snapped both hospitals\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 5: snapPlaces failed to snap both hospitals\n";
        failed++;
    }

    // Pipeline Step 6: shortestPath between the two snapped places
    if (snapped.size() >= 2 && snapped[0].snapped && snapped[1].snapped) {
        auto path = graph.shortestPath(snapped[0].nodeIndex, snapped[1].nodeIndex);
        if (path.found && path.nodes.size() >= 2) {
            double sumWeights = 0.0;
            for (size_t i = 0; i + 1 < path.nodes.size(); ++i) {
                int u = path.nodes[i];
                int v = path.nodes[i + 1];
                sumWeights += rapidaid::geo::haversineMeters(graph.lat(u), graph.lon(u),
                                                             graph.lat(v), graph.lon(v));
            }

            if (nearlyEqual(path.distanceMeters, sumWeights, 1e-3)) {
                std::cout << "[PASS] Pipeline Step 6: shortestPath found and distance ("
                          << path.distanceMeters << " m) equals sum of haversine edge weights\n";
            } else {
                std::cout << "[FAIL] Pipeline Step 6: path distance mismatch (path="
                          << path.distanceMeters << ", sum=" << sumWeights << ")\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Pipeline Step 6: shortestPath not found\n";
            failed++;
        }
    } else {
        std::cout << "[FAIL] Pipeline Step 6: places not snapped, skipping path test\n";
        failed++;
    }

    // Step 7: Clean up temp directory
    std::filesystem::remove_all(tempDir, ec);
    if (!std::filesystem::exists(tempDir)) {
        std::cout << "[PASS] Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nPipeline Offline Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}

```

## tests/test_roadclient.cpp

```cpp
#include "RoadClient.hpp"
#include <iostream>

using rapidaid::osm::RoadClient;
using rapidaid::osm::RoadData;

int main() {
    std::cout << "Fetching live road data around Eiffel Tower (48.8584, 2.2945, 1000m)...\n";
    std::string raw = RoadClient::fetchRaw(48.8584, 2.2945, 1000);
    if (raw.empty()) {
        std::cerr << "Failed to fetch road data from Overpass API.\n";
        return 1;
    }

    RoadData data = RoadClient::parseRoads(raw);
    std::cout << "Fetched " << data.nodes.size() << " nodes, " << data.ways.size() << " ways.\n";

    std::cout << "First 5 way names:\n";
    size_t count = 0;
    for (const auto& way : data.ways) {
        if (!way.name.empty()) {
            std::cout << "  - " << way.name << " (" << way.highway << ")\n";
            count++;
            if (count >= 5) {
                break;
            }
        }
    }

    if (count == 0) {
        std::cout << "  (No named ways found in first ways)\n";
    }

    return 0;
}

```

## tests/test_roadclient_offline.cpp

```cpp
#include "RoadClient.hpp"
#include <iostream>
#include <string>

using rapidaid::osm::RoadClient;
using rapidaid::osm::RoadData;

static int failures = 0;

static void assertTest(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failures++;
    }
}

int main() {
    std::cout << "Running test_roadclient_offline...\n";

    // Case 1: buildRoadQuery exact string for a sample input, and "" for radius 0, radius 5001, lat 91, lon -181.
    std::string sampleQuery = RoadClient::buildRoadQuery(48.8584, 2.2945, 1000);
    std::string expectedSample = "[out:json][timeout:60];way[\"highway\"](around:1000,48.8584000,2.2945000);(._;>;);out body;";
    assertTest(sampleQuery == expectedSample, "Case 1: buildRoadQuery exact sample query");
    assertTest(RoadClient::buildRoadQuery(48.8584, 2.2945, 0) == "", "Case 1: buildRoadQuery radius 0 returns empty");
    assertTest(RoadClient::buildRoadQuery(48.8584, 2.2945, 5001) == "", "Case 1: buildRoadQuery radius 5001 returns empty");
    assertTest(RoadClient::buildRoadQuery(91.0, 2.2945, 1000) == "", "Case 1: buildRoadQuery lat 91 returns empty");
    assertTest(RoadClient::buildRoadQuery(-91.0, 2.2945, 1000) == "", "Case 1: buildRoadQuery lat -91 returns empty");
    assertTest(RoadClient::buildRoadQuery(48.8584, -181.0, 1000) == "", "Case 1: buildRoadQuery lon -181 returns empty");
    assertTest(RoadClient::buildRoadQuery(48.8584, 181.0, 1000) == "", "Case 1: buildRoadQuery lon 181 returns empty");

    // Case 2: buildRequestBody starts with "data=" and has no raw space.
    std::string reqBody = RoadClient::buildRequestBody(expectedSample);
    assertTest(reqBody.rfind("data=", 0) == 0, "Case 2: buildRequestBody starts with data=");
    assertTest(reqBody.find(' ') == std::string::npos, "Case 2: buildRequestBody has no raw space");
    assertTest(reqBody.length() > 5, "Case 2: buildRequestBody has content");

    // Case 3: isDrivable checks
    // True cases: residential, service, primary_link
    assertTest(RoadClient::isDrivable({{"highway", "residential"}}), "Case 3: isDrivable residential is true");
    assertTest(RoadClient::isDrivable({{"highway", "service"}}), "Case 3: isDrivable service is true");
    assertTest(RoadClient::isDrivable({{"highway", "primary_link"}}), "Case 3: isDrivable primary_link is true");

    // False cases: footway, cycleway, steps, track, no highway tag, service with access=private, residential with motor_vehicle=no, residential with area=yes
    assertTest(!RoadClient::isDrivable({{"highway", "footway"}}), "Case 3: isDrivable footway is false");
    assertTest(!RoadClient::isDrivable({{"highway", "cycleway"}}), "Case 3: isDrivable cycleway is false");
    assertTest(!RoadClient::isDrivable({{"highway", "steps"}}), "Case 3: isDrivable steps is false");
    assertTest(!RoadClient::isDrivable({{"highway", "track"}}), "Case 3: isDrivable track is false");
    assertTest(!RoadClient::isDrivable({}), "Case 3: isDrivable no highway tag is false");
    assertTest(!RoadClient::isDrivable({{"name", "Some Way"}}), "Case 3: isDrivable tags without highway is false");
    assertTest(!RoadClient::isDrivable({{"highway", "service"}, {"access", "private"}}), "Case 3: isDrivable service with access=private is false");
    assertTest(!RoadClient::isDrivable({{"highway", "residential"}, {"motor_vehicle", "no"}}), "Case 3: isDrivable residential with motor_vehicle=no is false");
    assertTest(!RoadClient::isDrivable({{"highway", "residential"}, {"area", "yes"}}), "Case 3: isDrivable residential with area=yes is false");
    assertTest(!RoadClient::isDrivable({{"highway", "primary"}, {"vehicle", "no"}}), "Case 3: isDrivable primary with vehicle=no is false");
    assertTest(!RoadClient::isDrivable({{"highway", "trunk"}, {"access", "no"}}), "Case 3: isDrivable trunk with access=no is false");

    // Case 4: parseRoads on a canned response with 4 nodes and 3 ways (one residential, one footway, one with a single node) keeps only the residential way and all 4 nodes.
    const std::string cannedJson = R"({
      "version": 0.6,
      "generator": "Overpass API",
      "elements": [
        { "type": "node", "id": 1, "lat": 10.0, "lon": 20.0 },
        { "type": "node", "id": 2, "lat": 10.1, "lon": 20.1 },
        { "type": "node", "id": 3, "lat": 10.2, "lon": 20.2 },
        { "type": "node", "id": 4, "lat": 10.3, "lon": 20.3 },
        {
          "type": "way",
          "id": 101,
          "nodes": [1, 2],
          "tags": {
            "highway": "residential",
            "name": "Main Street"
          }
        },
        {
          "type": "way",
          "id": 102,
          "nodes": [2, 3],
          "tags": {
            "highway": "footway",
            "name": "Walking Path"
          }
        },
        {
          "type": "way",
          "id": 103,
          "nodes": [3],
          "tags": {
            "highway": "residential",
            "name": "Stub Way"
          }
        }
      ]
    })";

    RoadData data = RoadClient::parseRoads(cannedJson);
    assertTest(data.nodes.size() == 4, "Case 4: parseRoads keeps all 4 nodes");
    assertTest(data.nodes.count(1) == 1 && data.nodes[1].lat == 10.0 && data.nodes[1].lon == 20.0, "Case 4: node 1 verified");
    assertTest(data.nodes.count(2) == 1 && data.nodes[2].lat == 10.1 && data.nodes[2].lon == 20.1, "Case 4: node 2 verified");
    assertTest(data.nodes.count(3) == 1 && data.nodes[3].lat == 10.2 && data.nodes[3].lon == 20.2, "Case 4: node 3 verified");
    assertTest(data.nodes.count(4) == 1 && data.nodes[4].lat == 10.3 && data.nodes[4].lon == 20.3, "Case 4: node 4 verified");

    assertTest(data.ways.size() == 1, "Case 4: parseRoads keeps only 1 drivable way with >= 2 nodes");
    if (!data.ways.empty()) {
        assertTest(data.ways[0].id == 101, "Case 4: kept way id is 101");
        assertTest(data.ways[0].highway == "residential", "Case 4: kept way highway is residential");
        assertTest(data.ways[0].name == "Main Street", "Case 4: kept way name is Main Street");
        assertTest(data.ways[0].nodeIds.size() == 2 && data.ways[0].nodeIds[0] == 1 && data.ways[0].nodeIds[1] == 2, "Case 4: kept way nodeIds are [1, 2]");
        assertTest(data.ways[0].tags["highway"] == "residential", "Case 4: tags map contains highway");
    }
    assertTest(!data.empty(), "Case 4: data.empty() is false when ways present");

    // Case 5: A way whose tags contain a numeric value does not crash (e.g. "maxspeed": 50).
    const std::string numericTagJson = R"({
      "elements": [
        { "type": "node", "id": 10, "lat": 1.0, "lon": 2.0 },
        { "type": "node", "id": 11, "lat": 1.1, "lon": 2.1 },
        {
          "type": "way",
          "id": 201,
          "nodes": [10, 11],
          "tags": {
            "highway": "primary",
            "name": "Highway 50",
            "maxspeed": 50
          }
        }
      ]
    })";

    RoadData numData = RoadClient::parseRoads(numericTagJson);
    assertTest(numData.ways.size() == 1, "Case 5: numeric tag way parsed successfully");
    if (!numData.ways.empty()) {
        assertTest(numData.ways[0].tags.count("maxspeed") == 1, "Case 5: maxspeed tag present");
        assertTest(numData.ways[0].tags["maxspeed"] == "50", "Case 5: maxspeed tag converted to string 50");
    }

    // Case 6: Empty string, invalid JSON, and JSON without "elements" all return empty data.
    RoadData emptyStrData = RoadClient::parseRoads("");
    assertTest(emptyStrData.empty(), "Case 6: empty string returns empty RoadData");
    assertTest(emptyStrData.nodes.empty() && emptyStrData.ways.empty(), "Case 6: empty string has no nodes or ways");

    RoadData invalidJsonData = RoadClient::parseRoads("this is not json { [");
    assertTest(invalidJsonData.empty(), "Case 6: invalid JSON returns empty RoadData");
    assertTest(invalidJsonData.nodes.empty() && invalidJsonData.ways.empty(), "Case 6: invalid JSON has no nodes or ways");

    RoadData noElementsData = RoadClient::parseRoads(R"({"version": 0.6, "generator": "Overpass"})");
    assertTest(noElementsData.empty(), "Case 6: missing elements returns empty RoadData");
    assertTest(noElementsData.nodes.empty() && noElementsData.ways.empty(), "Case 6: missing elements has no nodes or ways");

    std::cout << "\nTest Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failures << " failures)\n";

    return (failures == 0) ? 0 : 1;
}

```

## tests/test_service_offline.cpp

```cpp
#include "Ambulance.hpp"
#include "Fire_station.hpp"
#include "Fire_truck.hpp"
#include "Hospital.hpp"
#include "Police_station.hpp"
#include "Police_unit.hpp"
#include "Shelter.hpp"

#include <cassert>
#include <stdexcept>

using namespace rapidaid::service;

int main() {
    Ambulance ambulance(1, "Ambulance 1", 0.0, 0.0, 10, 2);
    assert(ambulance.getGraphNodeId() == -1);
    ambulance.setGraphNodeId(42);
    assert(ambulance.getGraphNodeId() == 42);
    assert(ambulance.isAvailable());
    assert(ambulance.assign(100));
    assert(!ambulance.assign(101));
    assert(ambulance.getCurrentIncidentId() == 100);

    Hospital hospital(10, "General Hospital", 1.0, 2.0, 5, 2);
    hospital.addAmbulance(ambulance);
    assert(hospital.getAvailableAmbulances().empty());
    assert(hospital.findAmbulanceById(1) != nullptr);
    assert(hospital.admitPatients(3));
    assert(hospital.availableBeds() == 0);
    assert(!hospital.admitPatients(1));
    assert(hospital.getOccupiedBeds() == 5);
    hospital.dischargePatients(2);
    assert(hospital.availableBeds() == 2);
    hospital.dischargePatients(10);
    assert(hospital.getOccupiedBeds() == 0);

    FireStation fireStation(20, "Central Fire Station", 3.0, 4.0);
    fireStation.addTruck(FireTruck(2, "Truck 1", 3.0, 4.0, 20, 5000));
    assert(fireStation.getAvailableTrucks().size() == 1);
    assert(fireStation.findTruckById(2)->assign(200));
    assert(fireStation.getAvailableTrucks().empty());

    PoliceStation policeStation(30, "Central Police Station", 5.0, 6.0);
    policeStation.addUnit(PoliceUnit(3, "Unit 1", 5.0, 6.0, 30, 4));
    assert(policeStation.getAvailableUnits().size() == 1);
    assert(policeStation.findUnitById(3)->assign(300));
    assert(policeStation.getAvailableUnits().empty());

    ambulance.release();
    assert(ambulance.isAvailable());
    assert(ambulance.getCurrentIncidentId() == -1);

    Shelter shelter(40, "North Shelter", 7.0, 8.0, 10, 7);
    assert(shelter.availableSpace() == 3);
    assert(!shelter.isFull());
    assert(shelter.admit(3));
    assert(shelter.isFull());
    assert(!shelter.admit(1));
    assert(shelter.getOccupied() == 10);
    shelter.leave(4);
    assert(shelter.availableSpace() == 4);
    shelter.leave(20);
    assert(shelter.getOccupied() == 0);

    bool rejectedNegativeCount = false;
    try {
        Shelter invalidShelter(41, "Invalid Shelter", 0.0, 0.0, -1);
    } catch (const std::invalid_argument&) {
        rejectedNegativeCount = true;
    }
    assert(rejectedNegativeCount);
}

```

## tests/test_ui_bridge_offline.cpp

```cpp
#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "json.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void check(bool condition, const std::string& testName, int& failed) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failed++;
    }
}

int main() {
    int failed = 0;
    const std::string tempDir = "build/test_tmp_ui";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    std::string usersPath = tempDir + "/users.txt";
    std::string cacheDir = tempDir + "/cache";
    std::string outDir = tempDir + "/out";
    fs::create_directories(cacheDir, ec);
    fs::create_directories(outDir, ec);

    // 1. Test Auth & SignUp Bridge
    Auth auth(usersPath);
    
    // Login with seeded admin
    std::string loginAdminReq = R"({"username":"admin","password":"admin123"})";
    std::string loginAdminResp = handleLoginRequest(auth, loginAdminReq);
    json jAdmin = json::parse(loginAdminResp);
    check(jAdmin["ok"] == true && jAdmin["role"] == "admin", "Admin login succeeds via bridge", failed);

    // Signup new dispatcher
    std::string signupReq = R"({"username":"sarah","password":"secret456","role":"dispatcher"})";
    std::string signupResp = handleSignUpRequest(auth, signupReq);
    json jSignup = json::parse(signupResp);
    check(jSignup["ok"] == true && jSignup["username"] == "sarah", "SignUp new dispatcher succeeds", failed);

    // Duplicate signup fails
    std::string signupDupResp = handleSignUpRequest(auth, signupReq);
    json jSignupDup = json::parse(signupDupResp);
    check(jSignupDup["ok"] == false && jSignupDup["error"] == "Username already exists", "Duplicate signup fails gracefully", failed);

    // Login with new dispatcher
    std::string loginSarahReq = R"({"username":"sarah","password":"secret456"})";
    std::string loginSarahResp = handleLoginRequest(auth, loginSarahReq);
    json jSarah = json::parse(loginSarahResp);
    check(jSarah["ok"] == true && jSarah["role"] == "dispatcher", "New dispatcher login succeeds", failed);

    // 2. Test Area Status Bridge (Before file creation)
    std::string statusResp1 = rapidaid::ui::handleAreaStatusRequest(outDir);
    json jStat1 = json::parse(statusResp1);
    check(jStat1["ok"] == true && jStat1["exists"] == false, "Area status reports false when no GeoJSON exists", failed);

    // 3. Prepare Canned Cache Data for Offline Pipeline Testing
    rapidaid::data::DataCache cache(cacheDir, false);
    std::string query = "Paris Center";
    std::string geocodeJson = R"([
        {
            "lat": "48.8500",
            "lon": "2.3000",
            "display_name": "Paris Center"
        }
    ])";
    rapidaid::io::FileStore::save(cache.geocodePath(query), geocodeJson);

    double lat = 48.8500;
    double lon = 2.3000;
    int radius = 3000;
    std::string placesJson = R"({
        "elements": [
            {
                "type": "node",
                "id": 201,
                "lat": 48.8500,
                "lon": 2.3000,
                "tags": { "amenity": "hospital", "name": "Center Hospital" }
            }
        ]
    })";
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Hospital, lat, lon, radius), placesJson);

    std::string roadsJson = R"({
        "elements": [
            { "type": "node", "id": 1, "lat": 48.8500, "lon": 2.3000 },
            { "type": "node", "id": 2, "lat": 48.8510, "lon": 2.3010 },
            {
                "type": "way",
                "id": 101,
                "nodes": [1, 2],
                "tags": { "highway": "primary", "name": "Main Blvd" }
            }
        ]
    })";
    rapidaid::io::FileStore::save(cache.roadsPath(lat, lon, radius), roadsJson);

    // 4. Test Geocode Bridge
    std::string geoReq = R"({"query":"Paris Center"})";
    std::string geoResp = rapidaid::ui::handleGeocodeRequest(cache, geoReq);
    json jGeo = json::parse(geoResp);
    check(jGeo["ok"] == true && jGeo["lat"] == 48.8500 && jGeo["lon"] == 2.3000, "handleGeocodeRequest resolves coordinates from cache", failed);

    // 5. Test Create Area Pipeline Bridge
    std::string createReq = R"({
        "name": "Paris Center",
        "radius": 3000,
        "category": "hospital"
    })";
    std::string createResp = rapidaid::ui::handleCreateAreaRequest(cache, createReq, outDir);
    json jCreate = json::parse(createResp);
    check(jCreate["ok"] == true, "handleCreateAreaRequest completes pipeline", failed);
    check(jCreate["nodesCount"] == 2, "Nodes count matches road network", failed);
    check(jCreate["edgesCount"] == 1, "Edges count matches road network", failed);
    check(jCreate["placesCount"] == 1, "Places count matches hospital data", failed);
    check(fs::exists(outDir + "/roads.geojson"), "Permanent roads.geojson created on disk", failed);
    check(fs::exists(outDir + "/places.geojson"), "Permanent places.geojson created on disk", failed);

    // 6. Test Area Status Bridge (After file creation)
    std::string statusResp2 = rapidaid::ui::handleAreaStatusRequest(outDir);
    json jStat2 = json::parse(statusResp2);
    check(jStat2["ok"] == true && jStat2["exists"] == true, "Area status reports true when GeoJSON files exist", failed);

    // 7. Test Get GeoJSON Bridge
    std::string roadsGeoResp = rapidaid::ui::handleGetGeoJsonRequest("roads", outDir);
    json jRoads = json::parse(roadsGeoResp);
    check(jRoads["ok"] == true && jRoads.contains("geojson"), "handleGetGeoJsonRequest returns roads geojson", failed);

    // 8. Test Incident Handling
    std::string incReq = R"({
        "title": "Medical emergency at boulevard",
        "location": "Main Blvd & 1st Ave",
        "type": "Medical",
        "priority": "Critical",
        "notes": "Patient requires immediate transport"
    })";
    std::string addIncResp = rapidaid::ui::handleAddIncidentRequest(incReq);
    json jAddInc = json::parse(addIncResp);
    check(jAddInc["ok"] == true && jAddInc["status"] == "under_development", "handleAddIncidentRequest records incident", failed);

    std::string getIncResp = rapidaid::ui::handleGetIncidentsRequest();
    json jGetInc = json::parse(getIncResp);
    check(jGetInc["ok"] == true && jGetInc["count"] >= 1, "handleGetIncidentsRequest returns active queue", failed);

    // 9. Test Stats Bridge
    std::string statsResp = rapidaid::ui::handleGetStatsRequest();
    json jStats = json::parse(statsResp);
    check(jStats["ok"] == true && jStats["status"] == "under_development", "handleGetStatsRequest returns stats payload", failed);

    // 10. Test Unified Bridge Router
    std::string bridgeMsg = R"({"action":"health","payload":{}})";
    std::string bridgeResp = rapidaid::ui::handleBridgeMessage(auth, cache, bridgeMsg, outDir);
    json jBridge = json::parse(bridgeResp);
    check(jBridge["ok"] == true && jBridge["service"] == "login", "Unified bridge dispatches health action", failed);

    // Clean up temporary test directory
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory cleaned up", failed);

    std::cout << "\nTest Summary: " << (failed == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failed << " failures)\n";
    return (failed == 0) ? 0 : 1;
}

```

## tests/test_units_offline.cpp

```cpp
#include "DataCache.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "UnitRegistry.hpp"
#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

void check(bool cond, const std::string& desc, int& failures) {
    if (cond) {
        std::cout << "[PASS] " << desc << "\n";
    } else {
        std::cout << "[FAIL] " << desc << "\n";
        failures++;
    }
}

} // namespace

int main() {
    int failed = 0;
    fs::path origDir = fs::current_path();
    fs::path tempDir = origDir / "build" / "test_tmp_units";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);
    fs::current_path(tempDir, ec);

    // Initialize Auth and Cache inside temporary directory
    Auth auth("users.txt");
    rapidaid::data::DataCache cache("cache_data", false);
    std::string outDir = "data/out";
    fs::create_directories(outDir, ec);

    rapidaid::ui::resetBridgeState();

    std::cout << "Starting Unit & Dispatch Offline Tests...\n";

    // =========================================================================
    // Case 1: Signup rules & validation
    // =========================================================================
    {
        std::cout << "\n--- Case 1: Signup validation & rules ---\n";

        // Dispatcher signup works
        std::string req1 = R"({"action":"signup","payload":{"username":"disp1","password":"pw1","role":"dispatcher"}})";
        std::string res1 = rapidaid::ui::handleBridgeMessage(auth, cache, req1, outDir);
        auto j1 = nlohmann::json::parse(res1);
        check(j1.value("ok", false), "Case 1: signup of a dispatcher works", failed);

        // Role admin without admin token is rejected
        std::string req2 = R"({"action":"signup","payload":{"username":"admin2","password":"pw2","role":"admin"}})";
        std::string res2 = rapidaid::ui::handleBridgeMessage(auth, cache, req2, outDir);
        auto j2 = nlohmann::json::parse(res2);
        check(!j2.value("ok", true), "Case 1: signup role admin without admin token is rejected", failed);

        // Username with space is rejected
        std::string req3 = R"({"action":"signup","payload":{"username":"bad user","password":"pw3","role":"dispatcher"}})";
        std::string res3 = rapidaid::ui::handleBridgeMessage(auth, cache, req3, outDir);
        auto j3 = nlohmann::json::parse(res3);
        check(!j3.value("ok", true), "Case 1: username with a space is rejected", failed);

        // Invalid unit profiles rejected and account not created
        // a) Negative count
        std::string req4 = R"({"action":"signup","payload":{"username":"bad_amb1","password":"pw","role":"unit","unit":{"kind":"ambulance","name":"A1","lat":48.8,"lon":2.2,"count":-5}}})";
        std::string res4 = rapidaid::ui::handleBridgeMessage(auth, cache, req4, outDir);
        auto j4 = nlohmann::json::parse(res4);
        check(!j4.value("ok", true), "Case 1: signup unit with negative count rejected", failed);

        std::string loginCheck1 = R"({"action":"login","payload":{"username":"bad_amb1","password":"pw"}})";
        std::string lres1 = rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck1, outDir);
        auto lj1 = nlohmann::json::parse(lres1);
        check(!lj1.value("ok", true), "Case 1: failed signup with negative count did not create account", failed);

        // b) Bad latitude (> 90)
        std::string req5 = R"({"action":"signup","payload":{"username":"bad_amb2","password":"pw","role":"unit","unit":{"kind":"ambulance","name":"A2","lat":120.0,"lon":2.2,"count":2}}})";
        std::string res5 = rapidaid::ui::handleBridgeMessage(auth, cache, req5, outDir);
        auto j5 = nlohmann::json::parse(res5);
        check(!j5.value("ok", true), "Case 1: signup unit with bad lat rejected", failed);

        std::string loginCheck2 = R"({"action":"login","payload":{"username":"bad_amb2","password":"pw"}})";
        auto lj2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck2, outDir));
        check(!lj2.value("ok", true), "Case 1: failed signup with bad lat did not create account", failed);

        // c) Empty name
        std::string req6 = R"({"action":"signup","payload":{"username":"bad_amb3","password":"pw","role":"unit","unit":{"kind":"ambulance","name":"","lat":48.8,"lon":2.2,"count":2}}})";
        std::string res6 = rapidaid::ui::handleBridgeMessage(auth, cache, req6, outDir);
        auto j6 = nlohmann::json::parse(res6);
        check(!j6.value("ok", true), "Case 1: signup unit with empty name rejected", failed);

        std::string loginCheck3 = R"({"action":"login","payload":{"username":"bad_amb3","password":"pw"}})";
        auto lj3 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck3, outDir));
        check(!lj3.value("ok", true), "Case 1: failed signup with empty name did not create account", failed);

        // d) Unknown kind
        std::string req7 = R"({"action":"signup","payload":{"username":"bad_amb4","password":"pw","role":"unit","unit":{"kind":"helicopter","name":"H1","lat":48.8,"lon":2.2,"count":2}}})";
        std::string res7 = rapidaid::ui::handleBridgeMessage(auth, cache, req7, outDir);
        auto j7 = nlohmann::json::parse(res7);
        check(!j7.value("ok", true), "Case 1: signup unit with unknown kind rejected", failed);

        std::string loginCheck4 = R"({"action":"login","payload":{"username":"bad_amb4","password":"pw"}})";
        auto lj4 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck4, outDir));
        check(!lj4.value("ok", true), "Case 1: failed signup with unknown kind did not create account", failed);
    }

    // =========================================================================
    // Case 2: One account for each of the 7 kinds & login verification
    // =========================================================================
    std::string dispToken;
    std::string ambToken;
    std::string truckToken;
    std::string copToken;
    std::string hospToken;
    std::string fireStToken;
    std::string policeStToken;
    std::string shelterToken;

    {
        std::cout << "\n--- Case 2: Signup & login for 7 service kinds ---\n";

        // Login dispatcher to obtain token
        std::string dLogReq = R"({"action":"login","payload":{"username":"disp1","password":"pw1"}})";
        std::string dLogRes = rapidaid::ui::handleBridgeMessage(auth, cache, dLogReq, outDir);
        auto dLogJ = nlohmann::json::parse(dLogRes);
        dispToken = dLogJ.value("token", "");
        check(!dispToken.empty(), "Case 2: dispatcher login returned session token", failed);

        struct KindSpec {
            std::string user;
            std::string kind;
            int count;
            std::string* tokenOut;
        };

        KindSpec kinds[] = {
            {"amb_user", "ambulance", 2, &ambToken},
            {"truck_user", "fire_truck", 4000, &truckToken},
            {"cop_user", "police_unit", 3, &copToken},
            {"hosp_user", "hospital", 30, &hospToken},
            {"fire_st_user", "fire_station", 0, &fireStToken},
            {"police_st_user", "police_station", 0, &policeStToken},
            {"shelter_user", "shelter", 40, &shelterToken}
        };

        for (const auto& k : kinds) {
            std::string pw = "pass_" + k.user;
            nlohmann::ordered_json suReq;
            suReq["action"] = "signup";
            suReq["payload"] = {
                {"username", k.user},
                {"password", pw},
                {"role", "unit"},
                {"unit", {
                    {"kind", k.kind},
                    {"name", "Display " + k.user},
                    {"lat", 48.85},
                    {"lon", 2.29},
                    {"count", k.count}
                }}
            };

            std::string suRes = rapidaid::ui::handleBridgeMessage(auth, cache, suReq.dump(), outDir);
            auto suJ = nlohmann::json::parse(suRes);
            check(suJ.value("ok", false), "Case 2: signup succeeded for kind " + k.kind, failed);

            // Login
            nlohmann::ordered_json logReq;
            logReq["action"] = "login";
            logReq["payload"] = {{"username", k.user}, {"password", pw}};
            std::string logRes = rapidaid::ui::handleBridgeMessage(auth, cache, logReq.dump(), outDir);
            auto logJ = nlohmann::json::parse(logRes);

            check(logJ.value("ok", false), "Case 2: login succeeded for kind " + k.kind, failed);
            check(logJ.value("role", "") == "unit", "Case 2: role is 'unit' for " + k.kind, failed);

            std::string tok = logJ.value("token", "");
            check(tok.length() == 32, "Case 2: 32-char token returned for " + k.kind, failed);
            *(k.tokenOut) = tok;

            // Responses never leak password or hash
            check(logRes.find(pw) == std::string::npos, "Case 2: response does not contain password for " + k.kind, failed);
        }
    }

    // =========================================================================
    // Case 3: Authorization enforcement
    // =========================================================================
    {
        std::cout << "\n--- Case 3: Authorization checks ---\n";

        // No token
        std::string rNoTok = R"({"action":"session"})";
        auto jNoTok = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, rNoTok, outDir));
        check(!jNoTok.value("ok", true) && jNoTok.value("error", "") == "Not logged in", "Case 3: no token rejected on session", failed);

        std::string rNoTok2 = R"({"action":"list_units"})";
        auto jNoTok2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, rNoTok2, outDir));
        check(!jNoTok2.value("ok", true) && jNoTok2.value("error", "") == "Not logged in", "Case 3: no token rejected on list_units", failed);

        // Fake token
        std::string rFake = R"({"action":"session","token":"badtoken000000000000000000000000"})";
        auto jFake = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, rFake, outDir));
        check(!jFake.value("ok", true) && jFake.value("error", "") == "Not logged in", "Case 3: fake token rejected on session", failed);

        // Unit token rejected on dispatcher actions
        nlohmann::ordered_json u1;
        u1["action"] = "create_area";
        u1["token"] = ambToken;
        u1["payload"] = {{"name", "Paris"}};
        auto ju1 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u1.dump(), outDir));
        check(!ju1.value("ok", true) && ju1.value("error", "") == "Not allowed", "Case 3: unit token rejected on create_area", failed);

        nlohmann::ordered_json u2;
        u2["action"] = "get_incidents";
        u2["token"] = ambToken;
        auto ju2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u2.dump(), outDir));
        check(!ju2.value("ok", true) && ju2.value("error", "") == "Not allowed", "Case 3: unit token rejected on get_incidents", failed);

        nlohmann::ordered_json u3;
        u3["action"] = "list_units";
        u3["token"] = ambToken;
        auto ju3 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u3.dump(), outDir));
        check(!ju3.value("ok", true) && ju3.value("error", "") == "Not allowed", "Case 3: unit token rejected on list_units", failed);

        nlohmann::ordered_json u4;
        u4["action"] = "send_test_alert";
        u4["token"] = ambToken;
        u4["payload"] = {{"toUser", "cop_user"}};
        auto ju4 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u4.dump(), outDir));
        check(!ju4.value("ok", true) && ju4.value("error", "") == "Not allowed", "Case 3: unit token rejected on send_test_alert", failed);

        // Dispatcher token rejected on unit actions
        nlohmann::ordered_json d1;
        d1["action"] = "unit_get_state";
        d1["token"] = dispToken;
        auto jd1 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, d1.dump(), outDir));
        check(!jd1.value("ok", true) && jd1.value("error", "") == "Not allowed", "Case 3: dispatcher token rejected on unit_get_state", failed);

        nlohmann::ordered_json d2;
        d2["action"] = "unit_set_availability";
        d2["token"] = dispToken;
        d2["payload"] = {{"available", false}};
        auto jd2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, d2.dump(), outDir));
        check(!jd2.value("ok", true) && jd2.value("error", "") == "Not allowed", "Case 3: dispatcher token rejected on unit_set_availability", failed);
    }

    // =========================================================================
    // Case 4: Ambulance alert lifecycle & transitions
    // =========================================================================
    {
        std::cout << "\n--- Case 4: Ambulance alert lifecycle ---\n";

        // Ambulance: set unavailable -> test alert rejected
        nlohmann::ordered_json setUnavail;
        setUnavail["action"] = "unit_set_availability";
        setUnavail["token"] = ambToken;
        setUnavail["payload"] = {{"available", false}};
        auto jUnavail = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, setUnavail.dump(), outDir));
        check(jUnavail.value("ok", false), "Case 4: ambulance set unavailable succeeds", failed);

        nlohmann::ordered_json sendFail;
        sendFail["action"] = "send_test_alert";
        sendFail["token"] = dispToken;
        sendFail["payload"] = {{"toUser", "amb_user"}, {"locationText", "10 Rue A"}};
        auto jSendFail = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendFail.dump(), outDir));
        check(!jSendFail.value("ok", true), "Case 4: test alert to unavailable ambulance rejected", failed);

        // Set available -> test alert accepted
        nlohmann::ordered_json setAvail;
        setAvail["action"] = "unit_set_availability";
        setAvail["token"] = ambToken;
        setAvail["payload"] = {{"available", true}};
        auto jAvail = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, setAvail.dump(), outDir));
        check(jAvail.value("ok", false), "Case 4: ambulance set available succeeds", failed);

        nlohmann::ordered_json sendOk;
        sendOk["action"] = "send_test_alert";
        sendOk["token"] = dispToken;
        sendOk["payload"] = {{"toUser", "amb_user"}, {"locationText", "10 Rue A"}, {"type", "Medical"}, {"severity", 4}};
        auto jSendOk = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendOk.dump(), outDir));
        check(jSendOk.value("ok", false), "Case 4: test alert to available ambulance accepted", failed);
        int alertId = jSendOk.value("alertId", 0);
        check(alertId > 0, "Case 4: valid alert ID returned", failed);

        // Second alert to same unit is rejected
        nlohmann::ordered_json sendSecond;
        sendSecond["action"] = "send_test_alert";
        sendSecond["token"] = dispToken;
        sendSecond["payload"] = {{"toUser", "amb_user"}, {"locationText", "20 Rue B"}};
        auto jSendSecond = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendSecond.dump(), outDir));
        check(!jSendSecond.value("ok", true), "Case 4: second alert to unit rejected", failed);

        // Invalid transition: Pending -> OnScene rejected
        nlohmann::ordered_json badTrans;
        badTrans["action"] = "unit_alert_respond";
        badTrans["token"] = ambToken;
        badTrans["payload"] = {{"alertId", alertId}, {"action", "on_scene"}};
        auto jBadTrans = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, badTrans.dump(), outDir));
        check(!jBadTrans.value("ok", true), "Case 4: invalid transition Pending -> OnScene rejected", failed);

        // Another unit cannot respond to this alert
        nlohmann::ordered_json otherResp;
        otherResp["action"] = "unit_alert_respond";
        otherResp["token"] = copToken;
        otherResp["payload"] = {{"alertId", alertId}, {"action", "accept"}};
        auto jOtherResp = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, otherResp.dump(), outDir));
        check(!jOtherResp.value("ok", true), "Case 4: other unit cannot respond to alert", failed);

        // Accept alert: Pending -> Accepted (status becomes Dispatched)
        nlohmann::ordered_json acceptReq;
        acceptReq["action"] = "unit_alert_respond";
        acceptReq["token"] = ambToken;
        acceptReq["payload"] = {{"alertId", alertId}, {"action", "accept"}};
        auto jAccept = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, acceptReq.dump(), outDir));
        check(jAccept.value("ok", false), "Case 4: Pending -> Accepted succeeded", failed);
        check(jAccept.value("status", "") == "Dispatched", "Case 4: ambulance status became Dispatched", failed);

        // Accepted -> EnRoute
        nlohmann::ordered_json enRouteReq;
        enRouteReq["action"] = "unit_alert_respond";
        enRouteReq["token"] = ambToken;
        enRouteReq["payload"] = {{"alertId", alertId}, {"action", "en_route"}};
        auto jEnRoute = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, enRouteReq.dump(), outDir));
        check(jEnRoute.value("ok", false), "Case 4: Accepted -> EnRoute succeeded", failed);
        check(jEnRoute["openAlert"].value("state", "") == "EnRoute", "Case 4: alert state is EnRoute", failed);

        // EnRoute -> OnScene
        nlohmann::ordered_json onSceneReq;
        onSceneReq["action"] = "unit_alert_respond";
        onSceneReq["token"] = ambToken;
        onSceneReq["payload"] = {{"alertId", alertId}, {"action", "on_scene"}};
        auto jOnScene = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, onSceneReq.dump(), outDir));
        check(jOnScene.value("ok", false), "Case 4: EnRoute -> OnScene succeeded", failed);

        // OnScene -> Transporting (Ambulance only)
        nlohmann::ordered_json transReq;
        transReq["action"] = "unit_alert_respond";
        transReq["token"] = ambToken;
        transReq["payload"] = {{"alertId", alertId}, {"action", "transporting"}};
        auto jTrans = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, transReq.dump(), outDir));
        check(jTrans.value("ok", false), "Case 4: OnScene -> Transporting succeeded for ambulance", failed);

        // Transporting -> Cleared (status becomes Available)
        nlohmann::ordered_json clearReq;
        clearReq["action"] = "unit_alert_respond";
        clearReq["token"] = ambToken;
        clearReq["payload"] = {{"alertId", alertId}, {"action", "clear"}};
        auto jClear = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, clearReq.dump(), outDir));
        check(jClear.value("ok", false), "Case 4: Transporting -> Cleared succeeded", failed);
        check(jClear.value("status", "") == "Available", "Case 4: ambulance status became Available again", failed);

        // Fire truck cannot use "transporting"
        nlohmann::ordered_json ftAlert;
        ftAlert["action"] = "send_test_alert";
        ftAlert["token"] = dispToken;
        ftAlert["payload"] = {{"toUser", "truck_user"}, {"locationText", "Fire at 5 Blvd"}};
        auto jFtAlert = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, ftAlert.dump(), outDir));
        int ftAlertId = jFtAlert.value("alertId", 0);

        nlohmann::ordered_json ftAcc;
        ftAcc["action"] = "unit_alert_respond";
        ftAcc["token"] = truckToken;
        ftAcc["payload"] = {{"alertId", ftAlertId}, {"action", "accept"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftAcc.dump(), outDir);

        nlohmann::ordered_json ftEnR;
        ftEnR["action"] = "unit_alert_respond";
        ftEnR["token"] = truckToken;
        ftEnR["payload"] = {{"alertId", ftAlertId}, {"action", "en_route"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftEnR.dump(), outDir);

        nlohmann::ordered_json ftOnS;
        ftOnS["action"] = "unit_alert_respond";
        ftOnS["token"] = truckToken;
        ftOnS["payload"] = {{"alertId", ftAlertId}, {"action", "on_scene"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftOnS.dump(), outDir);

        nlohmann::ordered_json ftTrans;
        ftTrans["action"] = "unit_alert_respond";
        ftTrans["token"] = truckToken;
        ftTrans["payload"] = {{"alertId", ftAlertId}, {"action", "transporting"}};
        auto jFtTrans = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, ftTrans.dump(), outDir));
        check(!jFtTrans.value("ok", true), "Case 4: fire truck cannot use transporting", failed);

        nlohmann::ordered_json ftClr;
        ftClr["action"] = "unit_alert_respond";
        ftClr["token"] = truckToken;
        ftClr["payload"] = {{"alertId", ftAlertId}, {"action", "clear"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftClr.dump(), outDir);
    }

    // =========================================================================
    // Case 5: Decline alert leaves unit Available
    // =========================================================================
    {
        std::cout << "\n--- Case 5: Decline alert ---\n";

        nlohmann::ordered_json sendOk2;
        sendOk2["action"] = "send_test_alert";
        sendOk2["token"] = dispToken;
        sendOk2["payload"] = {{"toUser", "amb_user"}, {"locationText", "30 Ave C"}};
        auto jSendOk2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendOk2.dump(), outDir));
        int alertId2 = jSendOk2.value("alertId", 0);

        nlohmann::ordered_json declReq;
        declReq["action"] = "unit_alert_respond";
        declReq["token"] = ambToken;
        declReq["payload"] = {{"alertId", alertId2}, {"action", "decline"}, {"reason", "Fuel refill"}};
        auto jDecl = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, declReq.dump(), outDir));
        check(jDecl.value("ok", false), "Case 5: alert decline succeeded", failed);
        check(jDecl.value("status", "") == "Available", "Case 5: decline leaves the unit Available", failed);
        check(jDecl["openAlert"].is_null(), "Case 5: no open alert remains after decline", failed);
    }

    // =========================================================================
    // Case 6: Facility resource management & subunits
    // =========================================================================
    {
        std::cout << "\n--- Case 6: Facility resources & subunits ---\n";

        // Hospital: 30 total beds
        // Admit > free beds is rejected
        nlohmann::ordered_json hAdmitOver;
        hAdmitOver["action"] = "unit_update_resources";
        hAdmitOver["token"] = hospToken;
        hAdmitOver["payload"] = {{"action", "admit"}, {"count", 35}};
        auto jHOver = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hAdmitOver.dump(), outDir));
        check(!jHOver.value("ok", true), "Case 6: hospital admit more patients than free beds rejected", failed);

        // Admit 10
        nlohmann::ordered_json hAdmit10;
        hAdmit10["action"] = "unit_update_resources";
        hAdmit10["token"] = hospToken;
        hAdmit10["payload"] = {{"action", "admit"}, {"count", 10}};
        auto jH10 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hAdmit10.dump(), outDir));
        check(jH10.value("ok", false), "Case 6: hospital admit 10 patients succeeded", failed);
        check(jH10["resources"].value("occupiedBeds", 0) == 10, "Case 6: hospital occupied beds is 10", failed);

        // Discharge > occupied is rejected
        nlohmann::ordered_json hDiscOver;
        hDiscOver["action"] = "unit_update_resources";
        hDiscOver["token"] = hospToken;
        hDiscOver["payload"] = {{"action", "discharge"}, {"count", 15}};
        auto jHDiscOver = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hDiscOver.dump(), outDir));
        check(!jHDiscOver.value("ok", true), "Case 6: hospital discharge more than occupied is rejected", failed);

        // Discharge 5
        nlohmann::ordered_json hDisc5;
        hDisc5["action"] = "unit_update_resources";
        hDisc5["token"] = hospToken;
        hDisc5["payload"] = {{"action", "discharge"}, {"count", 5}};
        auto jHDisc5 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hDisc5.dump(), outDir));
        check(jHDisc5.value("ok", false), "Case 6: hospital discharge 5 patients succeeded", failed);
        check(jHDisc5["resources"].value("occupiedBeds", 0) == 5, "Case 6: hospital occupied beds is now 5", failed);

        // Add ambulance to hospital and toggle it
        nlohmann::ordered_json hAddAmb;
        hAddAmb["action"] = "unit_add_subunit";
        hAddAmb["token"] = hospToken;
        hAddAmb["payload"] = {{"name", "Hosp Amb 1"}, {"count", 2}};
        auto jHAddAmb = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hAddAmb.dump(), outDir));
        check(jHAddAmb.value("ok", false), "Case 6: hospital add ambulance succeeded", failed);
        int subAmbId = jHAddAmb.value("subunitId", 0);
        check(subAmbId > 0, "Case 6: valid subunit ID returned", failed);

        nlohmann::ordered_json hTog;
        hTog["action"] = "unit_set_subunit_availability";
        hTog["token"] = hospToken;
        hTog["payload"] = {{"subunitId", subAmbId}, {"available", false}};
        auto jHTog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hTog.dump(), outDir));
        check(jHTog.value("ok", false), "Case 6: toggle hospital ambulance to unavailable succeeded", failed);
        check(jHTog["subunits"][0].value("available", true) == false, "Case 6: hospital subunit available is false", failed);

        // Shelter: capacity 40
        // Admit beyond capacity rejected
        nlohmann::ordered_json sAdmitOver;
        sAdmitOver["action"] = "unit_update_resources";
        sAdmitOver["token"] = shelterToken;
        sAdmitOver["payload"] = {{"action", "admit"}, {"count", 45}};
        auto jSOver = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sAdmitOver.dump(), outDir));
        check(!jSOver.value("ok", true), "Case 6: shelter admit beyond capacity rejected", failed);

        // Admit 40 -> isFull becomes true
        nlohmann::ordered_json sAdmitFull;
        sAdmitFull["action"] = "unit_update_resources";
        sAdmitFull["token"] = shelterToken;
        sAdmitFull["payload"] = {{"action", "admit"}, {"count", 40}};
        auto jSFull = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sAdmitFull.dump(), outDir));
        check(jSFull.value("ok", false), "Case 6: shelter admit 40 succeeded", failed);
        check(jSFull["resources"].value("isFull", false) == true, "Case 6: shelter isFull becomes true when full", failed);

        // Fire station & Police station: add and toggle subunits
        nlohmann::ordered_json fsAdd;
        fsAdd["action"] = "unit_add_subunit";
        fsAdd["token"] = fireStToken;
        fsAdd["payload"] = {{"name", "Engine 1"}, {"count", 3000}};
        auto jFsAdd = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, fsAdd.dump(), outDir));
        check(jFsAdd.value("ok", false), "Case 6: fire station add truck succeeded", failed);
        int truckSubId = jFsAdd.value("subunitId", 0);

        nlohmann::ordered_json fsTog;
        fsTog["action"] = "unit_set_subunit_availability";
        fsTog["token"] = fireStToken;
        fsTog["payload"] = {{"subunitId", truckSubId}, {"available", false}};
        auto jFsTog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, fsTog.dump(), outDir));
        check(jFsTog.value("ok", false), "Case 6: fire truck subunit toggle succeeded", failed);

        nlohmann::ordered_json psAdd;
        psAdd["action"] = "unit_add_subunit";
        psAdd["token"] = policeStToken;
        psAdd["payload"] = {{"name", "Patrol Car 1"}, {"count", 2}};
        auto jPsAdd = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, psAdd.dump(), outDir));
        check(jPsAdd.value("ok", false), "Case 6: police station add unit succeeded", failed);
        int policeSubId = jPsAdd.value("subunitId", 0);

        nlohmann::ordered_json psTog;
        psTog["action"] = "unit_set_subunit_availability";
        psTog["token"] = policeStToken;
        psTog["payload"] = {{"subunitId", policeSubId}, {"available", false}};
        auto jPsTog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, psTog.dump(), outDir));
        check(jPsTog.value("ok", false), "Case 6: police unit subunit toggle succeeded", failed);
    }

    // =========================================================================
    // Case 7: Persistence round-trip & tolerance of corrupt file
    // =========================================================================
    {
        std::cout << "\n--- Case 7: Persistence & corrupt file tolerance ---\n";

        // Recreate registry from disk file
        rapidaid::ui::UnitRegistry reg2("data/units.json");
        reg2.load("data/units.json");

        check(reg2.hasUnit("amb_user"), "Case 7: persisted ambulance found after reload", failed);
        check(reg2.hasUnit("hosp_user"), "Case 7: persisted hospital found after reload", failed);
        check(reg2.hasUnit("shelter_user"), "Case 7: persisted shelter found after reload", failed);

        auto hospSt = reg2.getUnitStateJson("hosp_user");
        check(hospSt["resources"].value("occupiedBeds", 0) == 5, "Case 7: hospital occupied beds restored", failed);
        check(hospSt["subunits"].size() == 1, "Case 7: hospital subunit restored", failed);

        auto shelterSt = reg2.getUnitStateJson("shelter_user");
        check(shelterSt["resources"].value("isFull", false) == true, "Case 7: shelter isFull restored", failed);

        // Corrupt file tolerance
        std::ofstream corruptOut("data/corrupt_units.json");
        corruptOut << "{INVALID JSON SYNTAX %%%";
        corruptOut.close();

        rapidaid::ui::UnitRegistry corruptReg("data/corrupt_units.json");
        bool cRes = corruptReg.load("data/corrupt_units.json");
        check(!cRes, "Case 7: load returns false on corrupt file", failed);
        check(!corruptReg.hasUnit("amb_user"), "Case 7: corrupt units.json yields empty registry without crashing", failed);
    }

    // =========================================================================
    // Case 8: Session action returns profile for valid token, error after logout
    // =========================================================================
    {
        std::cout << "\n--- Case 8: Session action and logout ---\n";

        nlohmann::ordered_json sessReq;
        sessReq["action"] = "session";
        sessReq["token"] = ambToken;
        auto jSess = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(jSess.value("ok", false), "Case 8: session action succeeded for valid unit token", failed);
        check(jSess["unit"].value("kind", "") == "ambulance", "Case 8: session returned unit profile", failed);
        check(jSess.value("role", "") == "unit", "Case 8: session returned correct role", failed);

        // Logout
        nlohmann::ordered_json logOutReq;
        logOutReq["action"] = "logout";
        logOutReq["token"] = ambToken;
        auto jLogout = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, logOutReq.dump(), outDir));
        check(jLogout.value("ok", false), "Case 8: logout succeeded", failed);

        // Session after logout returns error
        auto jSessAfter = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(!jSessAfter.value("ok", true) && jSessAfter.value("error", "") == "Not logged in",
              "Case 8: session action returns 'Not logged in' error after logout", failed);
    }

    // Restore working directory and cleanup temp test directory
    fs::current_path(origDir, ec);
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Cleaned up temporary test directory build/test_tmp_units/", failed);

    std::cout << "\nUnit Offline Test Summary: " << (failed == 0 ? "ALL PASSED (0 failures)" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}

```

## unit.js

```javascript
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

```

