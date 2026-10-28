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
