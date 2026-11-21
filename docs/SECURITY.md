# Rapid Aid Security Architecture and Policy

This document outlines the security architecture, implemented cryptographic controls, and known limitations of Rapid Aid.

---

## 1. Security Architecture Overview

Rapid Aid is an emergency response dispatch application designed for low-latency dispatch operations. Because emergency dispatch environments handle critical incident data, unit locations, and operator credentials, the system incorporates defensive engineering at the protocol, storage, and presentation layers.

---

## 2. Implemented Security Controls

### 2.1 Password Hashing & Key Derivation (PBKDF2 + Pepper)
- **Algorithm**: Passwords are never stored in plaintext. Passwords are derived using **PBKDF2** (Password-Based Key Derivation Function 2) with HMAC-SHA256 across at least 100,000 iterations.
- **Salt & Pepper**: Each user record contains an independently generated, cryptographically secure 16-byte random salt. In addition, password verification incorporates a system-wide secret **pepper** maintained in `data/auth.key`.
- **Database Storage**: User records in `users.txt` store the derived key representation, salt, role, and approval status. Without `data/auth.key`, an exfiltrated `users.txt` cannot be subjected to offline dictionary or rainbow-table attacks.

### 2.2 Brute-Force Protection & Account Lockout
- **Lockout Tracking**: The authentication subsystem tracks consecutive failed login attempts per username.
- **Progressive Delay**: After repeated consecutive failures (e.g., 5 attempts), the account enters a timed lockout state to thwart automated credential-stuffing and dictionary attacks.
- **Uniform Error Messages**: Login failures return generic `Invalid username or password` responses regardless of whether the username exists or the password was incorrect, preventing user enumeration.

### 2.3 Session Management
- **Token Generation**: Successful logins generate high-entropy, random session tokens stored in volatile server memory.
- **Transport**: The client web application stores session tokens in browser `sessionStorage` (isolated per browser tab/session, preventing cross-tab leakage) and sends them in the `token` field of JSON-RPC requests.
- **Validation**: All sensitive actions on `/api/bridge`, `/api/create_area`, and `/api/pipeline` validate session tokens and enforce role-based permissions (`admin`, `dispatcher`, `unit`).

### 2.4 User Approval Workflow
- **Default State**: Self-service user registrations default to an unapproved/pending status.
- **Admin Review**: New users cannot log in or interact with the dispatch system until an administrator explicitly reviews and approves the account in the Users screen.
- **Role Enforcement**: Normal users cannot self-assign the `admin` role during sign-up; admin privileges can only be granted by an authenticated administrator.

### 2.5 HTTP Security Headers & Content Security Policy (CSP)
Every HTTP response served by the Rapid Aid server includes defensive headers to protect browser sessions:
- **`Content-Security-Policy`**: Restricts resource execution to trusted origins (`'self'`), trusted map tile providers (`openstreetmap.org`, `cartocdn.com`), and approved geocoding endpoints. Blocks unauthorized frame embedding (`frame-ancestors 'none'`).
- **`X-Content-Type-Options: nosniff`**: Prevents MIME-type sniffing by browsers.
- **`X-Frame-Options: DENY`**: Mitigates clickjacking attacks.
- **`Referrer-Policy: no-referrer`**: Prevents leaking sensitive URL query parameters to third-party endpoints.

### 2.6 Same-Origin & Host Header Enforcement
- **Same-Origin Only**: API endpoints inspect incoming HTTP headers (`Origin`, `Referer`, and `Sec-Fetch-Site`). Requests originating from external domains are rejected with `403 Forbidden` to prevent Cross-Site Request Forgery (CSRF).
- **Host Header Validation**: The server validates the HTTP `Host` header against allowed loopback and local network identities, preventing DNS rebinding attacks.

### 2.7 Cross-Site Scripting (XSS) Prevention
- **DOM Insertion**: Frontend code uses DOM properties (`textContent`) instead of `innerHTML` when rendering dynamic content, incident descriptions, notes, and external OpenStreetMap place tags.
- **Protocol Scrubbing**: Hyperlinks and URLs loaded from data stores or external services are validated to prevent `javascript:` and data URL execution.

### 2.8 Atomic File Writes & Storage Security
- **Atomic File Store**: When persisting configuration, cache files, incident stores, or credentials, `FileStore` writes to a temporary file (`.tmp`) and atomically renames it over the target file. This prevents corrupted state from power failures or crashes.
- **Restrictive Permissions**: On POSIX environments, sensitive files (such as `data/auth.key`) are created with restricted file masks (POSIX `0600`), preventing access by unprivileged local accounts.

---

## 3. Honest Limitations & Operational Risks

Operators must be aware of the following security trade-offs and operational constraints:

### 3.1 Plain HTTP Transport
The built-in C++ HTTP server communicates over plain HTTP/1.1 without native TLS encryption.
- **Loopback Safety**: On local machines (`localhost` / `127.0.0.1`), unencrypted HTTP is secure against external network eavesdropping.
- **Network Risk**: When launched with the `--lan` flag, unencrypted traffic travels across the local network. Passwords, session tokens, and incident logs can be captured by anyone on the same network segment.
- **Mitigation**: For multi-user or network deployments, Rapid Aid **must** be deployed behind a TLS-terminating reverse proxy (such as Nginx, Caddy, or HAProxy) with valid HTTPS certificates.

### 3.2 Windows File Permissions
On Windows operating systems, POSIX `chmod(0600)` calls are ineffective.
- **Impact**: File security on Windows relies entirely on Windows NTFS Access Control Lists (ACLs) and the security of the Windows user profile folder.
- **Mitigation**: Run Rapid Aid from a dedicated Windows user account with restrictive NTFS folder permissions.

### 3.3 Coupled Backups (`users.txt` and `data/auth.key`)
- **Pepper Dependency**: Password hashes in `users.txt` cannot be verified without the system pepper in `data/auth.key`.
- **Impact**: If `data/auth.key` is lost or deleted, existing user passwords cannot be verified or recovered; all user accounts must be reset.
- **Mitigation**: Operators **must** back up `users.txt` and `data/auth.key` together in encrypted offline storage.

### 3.4 No Email / Out-of-Band Password Reset
- Rapid Aid does not implement an SMTP or SMS recovery mechanism.
- **Impact**: If an operator forgets their password, there is no automated reset link.
- **Mitigation**: An administrator must manually update or reset credentials in the user administration screen or directly modify the user database.

### 3.5 In-Memory Sessions Lost on Restart
- Active session tokens are stored in volatile server memory.
- **Impact**: Restarting the Rapid Aid executable terminates all active sessions. Logged-in dispatchers and units will be prompted to log in again upon page reload.

### 3.6 Legacy `users.txt` Migration
- Existing user files from older prototype versions of Rapid Aid used `std::hash`.
- **Impact**: Older credential lines remain at the weaker hashing tier until that specific user logs in successfully for the first time, at which point the system automatically upgrades the hash to PBKDF2.
- **Mitigation**: Operators migrating from older versions should prompt all users to log in promptly or re-create accounts.

### 3.7 OpenSSL Patching
- Rapid Aid links against the operating system's OpenSSL runtime library.
- **Impact**: Cryptographic vulnerability fixes and security patches are not bundled into the repository.
- **Mitigation**: System operators are responsible for keeping the underlying OS, MSYS2 environment, and OpenSSL packages updated via the host package manager.
