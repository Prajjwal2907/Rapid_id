/**
 * RapidAid User Account Administration Module (admin.js)
 * Implements window.RapidAidAdmin = { mount(rootEl), refresh(), stop() } per CONTRACT E.
 *
 * Responsibilities:
 * - Calls bridge action list_users
 * - Renders user management table:
 *     - Username
 *     - Role
 *     - Status (status chip)
 *     - Must-change password flag
 *     - Created time
 * - Action buttons:
 *     - Approve (for pending users -> bridge action approve_user)
 *     - Disable (for active users -> bridge action disable_user)
 *     - Enable (for disabled users -> bridge action enable_user)
 *     - Reset password (confirm dialog -> bridge action admin_reset_password;
 *       displays temporaryPassword once in marked box with Copy button and text
 *       "Shown once. The user must change it at next login.")
 * - Top bar: Pending-approval count badge and Refresh button
 * - Graceful handling of not_allowed for non-admin accounts
 * - Dispatches rapidaid:session-expired CustomEvent and stops timers on session_expired / not_logged_in
 * - All dynamic text rendered strictly via textContent / DOM nodes (NO innerHTML)
 * - Styled exclusively via modules.css (prefixed admin-) and CSS variables
 */

(function () {
  'use strict';

  let mountElement = null;
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
  // DOM Construction
  // ==========================================

  function buildAdminUI(rootEl) {
    rootEl.textContent = '';
    dom = {};

    const root = document.createElement('div');
    root.id = 'admin-root-container';
    root.className = 'admin-root';

    // 1. Header
    const header = document.createElement('div');
    header.className = 'admin-header';

    const title = document.createElement('h2');
    title.className = 'admin-title';
    title.textContent = 'User Account Administration';
    header.appendChild(title);

    const actions = document.createElement('div');
    actions.className = 'admin-header-actions';

    const pendingBadge = document.createElement('span');
    pendingBadge.className = 'admin-badge-pending admin-badge-pending-zero';
    pendingBadge.textContent = 'Pending Approvals: 0';
    actions.appendChild(pendingBadge);
    dom.pendingBadge = pendingBadge;

    const btnRefresh = document.createElement('button');
    btnRefresh.type = 'button';
    btnRefresh.className = 'admin-btn-refresh';
    btnRefresh.textContent = 'Refresh';
    btnRefresh.addEventListener('click', () => refresh());
    actions.appendChild(btnRefresh);
    dom.btnRefresh = btnRefresh;

    header.appendChild(actions);
    root.appendChild(header);

    // 2. Alert / Feedback Box (hidden by default)
    const alertBox = document.createElement('div');
    alertBox.className = 'admin-alert';
    root.appendChild(alertBox);
    dom.alertBox = alertBox;

    // 3. Temporary Password Marked Box Container (rendered on password reset)
    const tempPwContainer = document.createElement('div');
    tempPwContainer.id = 'admin-temp-pw-slot';
    root.appendChild(tempPwContainer);
    dom.tempPwContainer = tempPwContainer;

    // 4. Users Table Section
    const tableWrapper = document.createElement('div');
    tableWrapper.className = 'admin-table-responsive';

    const table = document.createElement('table');
    table.className = 'admin-table';

    const thead = document.createElement('thead');
    const headerRow = document.createElement('tr');
    ['Username', 'Role', 'Status', 'Must Change Password', 'Created Time', 'Actions'].forEach(h => {
      const th = document.createElement('th');
      th.className = 'admin-th';
      th.textContent = h;
      headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    tbody.className = 'admin-tbody';
    table.appendChild(tbody);
    dom.usersTbody = tbody;

    tableWrapper.appendChild(table);
    root.appendChild(tableWrapper);

    rootEl.appendChild(root);
  }

  function showAlert(message, isError = false) {
    if (!dom.alertBox) return;
    dom.alertBox.textContent = message;
    dom.alertBox.className = 'admin-alert admin-alert-visible ' + (isError ? 'admin-alert-error' : 'admin-alert-success');

    if (!isError) {
      setTimeout(() => {
        if (dom.alertBox && dom.alertBox.textContent === message) {
          dom.alertBox.classList.remove('admin-alert-visible');
          dom.alertBox.textContent = '';
        }
      }, 5000);
    }
  }

  function hideAlert() {
    if (dom.alertBox) {
      dom.alertBox.classList.remove('admin-alert-visible');
      dom.alertBox.textContent = '';
    }
  }

  function showTempPasswordBox(username, tempPassword) {
    if (!dom.tempPwContainer) return;
    dom.tempPwContainer.textContent = '';

    const box = document.createElement('div');
    box.className = 'admin-temp-pw-box';

    // Header
    const header = document.createElement('div');
    header.className = 'admin-temp-pw-header';

    const label = document.createElement('span');
    label.className = 'admin-temp-pw-label';
    label.textContent = 'Temporary Password for user "' + username + '":';
    header.appendChild(label);

    const btnClose = document.createElement('button');
    btnClose.type = 'button';
    btnClose.className = 'admin-btn admin-btn-close-box';
    btnClose.textContent = '✕ Close';
    btnClose.addEventListener('click', () => {
      dom.tempPwContainer.textContent = '';
    });
    header.appendChild(btnClose);
    box.appendChild(header);

    // Code & Copy Row
    const codeRow = document.createElement('div');
    codeRow.className = 'admin-temp-pw-code-row';

    const codeSpan = document.createElement('span');
    codeSpan.className = 'admin-temp-pw-code';
    codeSpan.textContent = tempPassword;
    codeRow.appendChild(codeSpan);

    const btnCopy = document.createElement('button');
    btnCopy.type = 'button';
    btnCopy.className = 'admin-btn-copy';
    btnCopy.textContent = 'Copy Password';
    btnCopy.addEventListener('click', () => {
      if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
        navigator.clipboard.writeText(tempPassword).then(() => {
          btnCopy.textContent = 'Copied!';
          setTimeout(() => {
            if (btnCopy) btnCopy.textContent = 'Copy Password';
          }, 2000);
        }).catch(() => {
          fallbackCopyText(tempPassword, btnCopy);
        });
      } else {
        fallbackCopyText(tempPassword, btnCopy);
      }
    });
    codeRow.appendChild(btnCopy);
    box.appendChild(codeRow);

    // Notice
    const note = document.createElement('p');
    note.className = 'admin-temp-pw-note';
    note.textContent = 'Shown once. The user must change it at next login.';
    box.appendChild(note);

    dom.tempPwContainer.appendChild(box);
    try {
      box.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    } catch (e) {}
  }

  function fallbackCopyText(text, btn) {
    try {
      const textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.className = 'admin-clipboard-helper';
      document.body.appendChild(textarea);
      textarea.select();
      document.execCommand('copy');
      document.body.removeChild(textarea);
      if (btn) {
        btn.textContent = 'Copied!';
        setTimeout(() => {
          if (btn) btn.textContent = 'Copy Password';
        }, 2000);
      }
    } catch (e) {}
  }

  // ==========================================
  // Render Users Table
  // ==========================================

  function renderUsersTable(users) {
    if (!dom.usersTbody) return;
    dom.usersTbody.textContent = '';

    if (!Array.isArray(users) || users.length === 0) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.setAttribute('colspan', '6');
      td.className = 'admin-empty';
      td.textContent = 'No user accounts found.';
      tr.appendChild(td);
      dom.usersTbody.appendChild(tr);
      return;
    }

    users.forEach(u => {
      const tr = document.createElement('tr');
      const uname = u.username || u.name || '';
      const statusStr = String(u.status || 'active').trim();
      const statusLower = statusStr.toLowerCase();

      // 1. Username
      const tdUser = document.createElement('td');
      tdUser.className = 'admin-td';
      const strongUser = document.createElement('strong');
      strongUser.textContent = uname;
      tdUser.appendChild(strongUser);
      tr.appendChild(tdUser);

      // 2. Role
      const tdRole = document.createElement('td');
      tdRole.className = 'admin-td';
      tdRole.textContent = u.role || 'user';
      tr.appendChild(tdRole);

      // 3. Status Chip
      const tdStatus = document.createElement('td');
      tdStatus.className = 'admin-td';
      const chip = document.createElement('span');
      chip.textContent = statusStr;
      const cleanStat = statusLower.replace(/[\s_]+/g, '');
      chip.className = 'status-chip status-chip-' + cleanStat;
      tdStatus.appendChild(chip);
      tr.appendChild(tdStatus);

      // 4. Must-Change Flag
      const tdMustChange = document.createElement('td');
      tdMustChange.className = 'admin-td';
      const mustChange = Boolean(u.mustChangePassword || u.must_change || u.mustChange);
      tdMustChange.textContent = mustChange ? 'Yes' : 'No';
      tr.appendChild(tdMustChange);

      // 5. Created Time
      const tdCreated = document.createElement('td');
      tdCreated.className = 'admin-td';
      const cTime = u.createdAt || u.created_time || u.created || u.timestamp;
      if (cTime) {
        const d = new Date(typeof cTime === 'number' && cTime < 10000000000 ? cTime * 1000 : cTime);
        tdCreated.textContent = isNaN(d.getTime()) ? String(cTime) : d.toLocaleString();
      } else {
        tdCreated.textContent = '-';
      }
      tr.appendChild(tdCreated);

      // 6. Actions
      const tdActions = document.createElement('td');
      tdActions.className = 'admin-td';
      const actionGroup = document.createElement('div');
      actionGroup.className = 'admin-actions-cell';

      // Approve button (for pending users)
      if (statusLower === 'pending') {
        const btnApprove = document.createElement('button');
        btnApprove.type = 'button';
        btnApprove.className = 'admin-btn admin-btn-approve';
        btnApprove.textContent = 'Approve';
        btnApprove.addEventListener('click', async () => {
          btnApprove.disabled = true;
          hideAlert();
          const res = await callBridge('approve_user', { username: uname, user: uname });
          if (res && res.ok) {
            showAlert('User "' + uname + '" approved successfully.');
            await refresh();
          } else {
            showAlert((res && res.error) ? res.error : 'Failed to approve user.', true);
            btnApprove.disabled = false;
          }
        });
        actionGroup.appendChild(btnApprove);
      }

      // Disable button (for active users)
      if (statusLower === 'active') {
        const btnDisable = document.createElement('button');
        btnDisable.type = 'button';
        btnDisable.className = 'admin-btn admin-btn-disable';
        btnDisable.textContent = 'Disable';
        btnDisable.addEventListener('click', async () => {
          btnDisable.disabled = true;
          hideAlert();
          const res = await callBridge('disable_user', { username: uname, user: uname });
          if (res && res.ok) {
            showAlert('User "' + uname + '" disabled.');
            await refresh();
          } else {
            showAlert((res && res.error) ? res.error : 'Failed to disable user.', true);
            btnDisable.disabled = false;
          }
        });
        actionGroup.appendChild(btnDisable);
      }

      // Enable button (for disabled users)
      if (statusLower === 'disabled') {
        const btnEnable = document.createElement('button');
        btnEnable.type = 'button';
        btnEnable.className = 'admin-btn admin-btn-enable';
        btnEnable.textContent = 'Enable';
        btnEnable.addEventListener('click', async () => {
          btnEnable.disabled = true;
          hideAlert();
          const res = await callBridge('enable_user', { username: uname, user: uname });
          if (res && res.ok) {
            showAlert('User "' + uname + '" enabled.');
            await refresh();
          } else {
            showAlert((res && res.error) ? res.error : 'Failed to enable user.', true);
            btnEnable.disabled = false;
          }
        });
        actionGroup.appendChild(btnEnable);
      }

      // Reset Password button (for all users)
      const btnReset = document.createElement('button');
      btnReset.type = 'button';
      btnReset.className = 'admin-btn admin-btn-reset';
      btnReset.textContent = 'Reset Password';
      btnReset.addEventListener('click', async () => {
        const confirmed = (typeof window !== 'undefined' && typeof window.confirm === 'function')
          ? window.confirm('Are you sure you want to reset password for user "' + uname + '"?')
          : true;
        if (!confirmed) return;

        btnReset.disabled = true;
        hideAlert();
        try {
          const res = await callBridge('admin_reset_password', { username: uname, user: uname });
          if (res && res.ok) {
            const tempPw = res.temporaryPassword || res.tempPassword || res.password || res.temp_password || '';
            showAlert('Password reset successfully for user "' + uname + '".');
            showTempPasswordBox(uname, tempPw);
            await refresh();
          } else {
            showAlert((res && res.error) ? res.error : 'Failed to reset password.', true);
          }
        } catch (err) {
          showAlert('Error resetting password: ' + (err.message || 'Server error'), true);
        } finally {
          btnReset.disabled = false;
        }
      });
      actionGroup.appendChild(btnReset);

      tdActions.appendChild(actionGroup);
      tr.appendChild(tdActions);

      dom.usersTbody.appendChild(tr);
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
      rootEl = document.getElementById('admin-root');
    }
    if (!rootEl) return;

    stop();
    mountElement = rootEl;
    buildAdminUI(rootEl);
    refresh();
  }

  async function refresh() {
    if (isFetching) return;
    isFetching = true;

    if (dom.btnRefresh) {
      dom.btnRefresh.disabled = true;
    }

    try {
      const res = await callBridge('list_users');

      // Gracefully handle not_allowed
      if (res && !res.ok && (res.code === 'not_allowed' || res.error === 'not_allowed' || (typeof res.error === 'string' && res.error.toLowerCase().includes('not allowed')))) {
        showAlert('Access Restricted: You must be logged in as an administrator to access User Account Administration.', true);
        if (dom.usersTbody) {
          dom.usersTbody.textContent = '';
          const tr = document.createElement('tr');
          const td = document.createElement('td');
          td.setAttribute('colspan', '6');
          td.className = 'admin-empty';
          td.textContent = 'Administrator permissions required.';
          tr.appendChild(td);
          dom.usersTbody.appendChild(tr);
        }
        if (dom.pendingBadge) {
          dom.pendingBadge.textContent = 'Pending Approvals: -';
        }
        return;
      }

      if (!res || !res.ok) {
        showAlert((res && res.error) ? res.error : 'Failed to retrieve users list.', true);
        return;
      }

      const users = Array.isArray(res.users) ? res.users : (Array.isArray(res.data) ? res.data : (Array.isArray(res) ? res : []));

      // Calculate pending approvals count
      const pendingCount = users.filter(u => String(u.status || '').trim().toLowerCase() === 'pending').length;
      if (dom.pendingBadge) {
        dom.pendingBadge.textContent = 'Pending Approvals: ' + pendingCount;
        if (pendingCount > 0) {
          dom.pendingBadge.className = 'admin-badge-pending';
        } else {
          dom.pendingBadge.className = 'admin-badge-pending admin-badge-pending-zero';
        }
      }

      renderUsersTable(users);
    } catch (err) {
      showAlert('Error loading users: ' + (err.message || 'Server error'), true);
    } finally {
      isFetching = false;
      if (dom.btnRefresh) {
        dom.btnRefresh.disabled = false;
      }
    }
  }

  function stop() {
    isFetching = false;
  }

  // Export to window
  if (typeof window !== 'undefined') {
    window.RapidAidAdmin = {
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
