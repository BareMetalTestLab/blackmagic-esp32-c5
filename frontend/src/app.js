// Drag and drop functionality
const dropZone = document.getElementById('dropZone');
const fileInput = document.getElementById('fileInput');
const fileInfo = document.getElementById('fileInfo');
const uploadBtn = document.getElementById('uploadBtn');
const uploadFormElement = document.getElementById('uploadFormElement');
const uploadFormDiv = document.getElementById('uploadForm');
const rebootBtn = document.getElementById('rebootBtn');

// Reboot button functionality
rebootBtn.addEventListener('click', async () => {
    if (confirm('Are you sure you want to reboot the device?')) {
        rebootBtn.disabled = true;
        rebootBtn.textContent = '⏳';

        try {
            const response = await fetch('/reboot', {
                method: 'POST'
            });

            if (response.ok) {
                alert('Device is rebooting...');
                // Optionally reload page after some delay
                setTimeout(() => {
                    window.location.reload();
                }, 5000);
            } else {
                alert('Failed to reboot device');
                rebootBtn.disabled = false;
                rebootBtn.textContent = '🔄';
            }
        } catch (error) {
            console.error('Reboot error:', error);
            alert('Error communicating with device');
            rebootBtn.disabled = false;
            rebootBtn.textContent = '🔄';
        }
    }
});

// Click to browse
dropZone.addEventListener('click', () => {
    fileInput.click();
});

// File input change
fileInput.addEventListener('change', (e) => {
    if (e.target.files.length > 0) {
        handleFile(e.target.files[0]);
    }
});

// Prevent default drag behaviors
['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
    dropZone.addEventListener(eventName, preventDefaults, false);
    document.body.addEventListener(eventName, preventDefaults, false);
});

function preventDefaults(e) {
    e.preventDefault();
    e.stopPropagation();
}

// Highlight drop zone when item is dragged over it
['dragenter', 'dragover'].forEach(eventName => {
    dropZone.addEventListener(eventName, () => {
        dropZone.classList.add('drag-over');
        uploadFormDiv.classList.add('drag-over');
    }, false);
});

['dragleave', 'drop'].forEach(eventName => {
    dropZone.addEventListener(eventName, () => {
        dropZone.classList.remove('drag-over');
        uploadFormDiv.classList.remove('drag-over');
    }, false);
});

// Handle dropped files
dropZone.addEventListener('drop', (e) => {
    const dt = e.dataTransfer;
    const files = dt.files;

    if (files.length > 0) {
        fileInput.files = files;
        handleFile(files[0]);
    }
}, false);

function handleFile(file) {
    const validTypes = ['.bin'];
    const fileExt = '.' + file.name.split('.').pop().toLowerCase();

    if (!validTypes.includes(fileExt)) {
        fileInfo.textContent = '❌ Invalid file type. Please select a .bin or .elf file.';
        fileInfo.style.display = 'block';
        fileInfo.style.backgroundColor = '#ffe6e6';
        fileInfo.style.color = '#cc0000';
        uploadBtn.disabled = true;
        return;
    }

    fileInfo.textContent = `✓ Selected: ${file.name} (${(file.size / 1024).toFixed(1)} KB)`;
    fileInfo.style.display = 'block';
    fileInfo.style.backgroundColor = '#e6f7e6';
    fileInfo.style.color = '#2d5016';
    uploadBtn.disabled = false;

    // Auto-upload if advanced settings are closed
    const advancedSettings = document.getElementById('advancedSettings');
    if (!advancedSettings.open) {
        setTimeout(() => {
            document.getElementById('uploadFormElement').dispatchEvent(new Event('submit'));
        }, 500);
    }
}

document.getElementById('uploadFormElement').addEventListener('submit', async (e) => {
    e.preventDefault();

    const file = fileInput.files[0];

    if (!file) return;

    const status = document.getElementById('status');
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');

    uploadBtn.disabled = true;
    progressContainer.style.display = 'block';
    progressBar.style.width = '0%';

    // Step 1: Always send flash parameters before upload
    status.textContent = 'Setting flash parameters...';
    status.className = 'info';

    const baseAddr = document.getElementById('baseAddr').value;
    const iface = document.querySelector('input[name="iface"]:checked').value;

    // Step 2: Upload firmware file
    status.textContent = 'Uploading ' + file.name + ' (' + (file.size / 1024).toFixed(1) + ' KB)...';
    status.className = 'info';

    const formData = new FormData();
    formData.append('file', file);

    try {
        const xhr = new XMLHttpRequest();

        xhr.upload.addEventListener('progress', (e) => {
            if (e.lengthComputable) {
                const percent = (e.loaded / e.total) * 100;
                progressBar.style.width = percent + '%';
                status.textContent = 'Uploading... ' + percent.toFixed(0) + '%';
            }
        });

        xhr.addEventListener('load', () => {
            if (xhr.status === 200) {
                status.textContent = '✓ ' + xhr.responseText;
                status.className = 'success';
                progressBar.style.width = '100%';
            } else {
                status.textContent = '✗ Error: ' + xhr.responseText;
                status.className = 'error';
            }
            uploadBtn.disabled = false;
        });

        xhr.addEventListener('error', () => {
            status.textContent = '✗ Upload failed: Network error';
            status.className = 'error';
            uploadBtn.disabled = false;
        });

        xhr.open('POST', '/target/flash/upload?baseAddr=' + encodeURIComponent(baseAddr) +
            '&iface=' + encodeURIComponent(iface) + '&length=' + file.size);
        xhr.send(formData);
    } catch (error) {
        status.textContent = '✗ Upload failed: ' + error.message;
        status.className = 'error';
        uploadBtn.disabled = false;
    }
});

// Firmware length / end address: two-way auto calculation anchored at baseAddr
const baseAddrInput = document.getElementById('baseAddr');
const fwLengthInput = document.getElementById('fwLength');
const fwEndAddrInput = document.getElementById('fwEndAddr');
const fwLengthDecInput = document.getElementById('fwLengthDec');

function parseHexInput(input) {
    const v = parseInt(input.value, 16);
    return isNaN(v) ? null : v;
}

function formatHexAddr(v) {
    return v.toString(16).toUpperCase().padStart(8, '0');
}

function updateEndAddrFromLength() {
    const base = parseHexInput(baseAddrInput);
    const length = parseHexInput(fwLengthInput);
    if (base === null || length === null) return;
    fwEndAddrInput.value = formatHexAddr(base + length);
    fwLengthDecInput.value = length.toString(10);
}

function updateLengthFromEndAddr() {
    const base = parseHexInput(baseAddrInput);
    const end = parseHexInput(fwEndAddrInput);
    if (base === null || end === null || end < base) return;
    fwLengthInput.value = formatHexAddr(end - base);
    fwLengthDecInput.value = (end - base).toString(10);
}

// Live filtering: only hex digits, max 8 characters (no 0x prefix)
function sanitizeHexInput(input) {
    const raw = input.value;
    const clean = raw.replace(/[^0-9A-Fa-f]/g, '').toUpperCase().slice(0, 8);
    if (raw !== clean) {
        const caret = input.selectionStart === null ? clean.length : Math.min(input.selectionStart, clean.length);
        input.value = clean;
        input.setSelectionRange(caret, caret);
    }
}

// Live filtering: only decimal digits, max 10 characters (max 4294967295)
function sanitizeDecInput(input) {
    const raw = input.value;
    const clean = raw.replace(/[^0-9]/g, '').slice(0, 10);
    if (raw !== clean) {
        const caret = input.selectionStart === null ? clean.length : Math.min(input.selectionStart, clean.length);
        input.value = clean;
        input.setSelectionRange(caret, caret);
    }
}

fwLengthInput.addEventListener('input', () => { sanitizeHexInput(fwLengthInput); updateEndAddrFromLength(); });
fwLengthDecInput.addEventListener('input', () => {
    sanitizeDecInput(fwLengthDecInput);
    const d = parseInt(fwLengthDecInput.value, 10);
    if (!isNaN(d)) {
        fwLengthInput.value = formatHexAddr(d);
        updateEndAddrFromLength();
    }
});
fwEndAddrInput.addEventListener('input', () => { sanitizeHexInput(fwEndAddrInput); updateLengthFromEndAddr(); });
baseAddrInput.addEventListener('input', () => { sanitizeHexInput(baseAddrInput); updateEndAddrFromLength(); });

// Erase flash button functionality
document.getElementById('eraseBtn').addEventListener('click', async () => {
    const eraseBtn = document.getElementById('eraseBtn');
    const status = document.getElementById('status');
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');

    eraseBtn.disabled = true;
    progressContainer.style.display = 'block';
    progressBar.style.width = '0%';

    // Step 1: Always send connection parameters before erase
    status.textContent = 'Setting flash parameters...';
    status.className = 'info';

    const baseAddr = document.getElementById('baseAddr').value;
    const iface = document.querySelector('input[name="iface"]:checked').value;
    const length = document.getElementById('fwLengthDec').value;

    const reenableBtn = () => {
        eraseBtn.disabled = false;
    };

    const resetProgress = () => {
        progressBar.style.width = '0%';
        progressContainer.style.display = 'none';
        reenableBtn();
    };

    // Step 2: Erasing flash (no data body is sent)
    status.textContent = 'Erasing flash...';
    status.className = 'info';

    try {
        const xhr = new XMLHttpRequest();

        xhr.addEventListener('load', () => {
            if (xhr.status === 200) {
                status.textContent = '✓ ' + xhr.responseText;
                status.className = 'success';
                progressBar.style.width = '100%';
                reenableBtn();
            } else {
                status.textContent = '✗ Error: ' + xhr.responseText;
                status.className = 'error';
                resetProgress();
            }
        });

        xhr.addEventListener('error', () => {
            status.textContent = '✗ Erase failed: Network error';
            status.className = 'error';
            resetProgress();
        });

        xhr.open('POST', '/target/flash/erase?baseAddr=' + encodeURIComponent(baseAddr) +
            '&iface=' + encodeURIComponent(iface) + '&length=' + encodeURIComponent(length));
        xhr.send();
    } catch (error) {
        status.textContent = '✗ Erase failed: ' + error.message;
        status.className = 'error';
        resetProgress();
    }
});

// Read flash button functionality
const readFlashBtn = document.getElementById('readFlashBtn');
readFlashBtn.addEventListener('click', async () => {
    const status = document.getElementById('status');
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');

    readFlashBtn.disabled = true;
    progressContainer.style.display = 'block';
    progressBar.style.width = '0%';

    // Step 1: Always send connection parameters before read
    status.textContent = 'Setting flash parameters...';
    status.className = 'info';

    const baseAddr = document.getElementById('baseAddr').value;
    const iface = document.querySelector('input[name="iface"]:checked').value;
    const length = document.getElementById('fwLengthDec').value;

    const reenableBtn = () => {
        readFlashBtn.disabled = false;
    };

    const resetProgress = () => {
        progressBar.style.width = '0%';
        progressContainer.style.display = 'none';
        reenableBtn();
    };

    // Step 2: Read flash and download the dump (binary response)
    try {
        const res = await fetch('/target/flash/read?baseAddr=' + encodeURIComponent(baseAddr) +
            '&iface=' + encodeURIComponent(iface) + '&length=' + encodeURIComponent(length), { method: 'POST' });

        if (!res.ok) {
            const errorText = await res.text();
            status.textContent = '✗ Read failed: ' + errorText;
            status.className = 'error';
            resetProgress();
            return;
        }

        const contentLength = res.headers.get('Content-Length');
        const total = parseInt(contentLength, 10);

        status.textContent = 'Reading flash (' + (total / 1024) + ' KB)...';
        status.className = 'info';

        const reader = res.body.getReader();
        const chunks = [];
        let received = 0;

        while (true) {
            const { done, value } = await reader.read();
            if (done) break;

            chunks.push(value);
            received += value.length;

            const percent = Math.min((received / total) * 100, 100);
            progressBar.style.width = percent.toFixed(1) + '%';
            status.textContent = 'Reading flash: ' +
                (received / 1024).toFixed(0) + ' / ' +
                (total / 1024).toFixed(0) + ' KB (' +
                percent.toFixed(1) + '%)';
        }

        const blob = new Blob(chunks);

        // Mid-stream failures return a truncated 200, so validate the size
        if (blob.size !== total) {
            status.textContent = '✗ Read failed: got ' + blob.size + ' of ' + total + ' bytes';
            status.className = 'error';
            resetProgress();
            return;
        }

        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = 'flash_dump.bin';
        a.click();
        URL.revokeObjectURL(a.href);

        status.textContent = '✓ Flash dump saved (' + blob.size + ' bytes)';
        status.className = 'success';
        progressBar.style.width = '100%';
        reenableBtn();
    } catch (error) {
        status.textContent = '✗ Read failed: ' + error.message;
        status.className = 'error';
        resetProgress();
    }
});

// Tab switching functionality
const tabButtons = document.querySelectorAll('.tab-button');
const tabContents = document.querySelectorAll('.tab-content');

tabButtons.forEach(button => {
    button.addEventListener('click', () => {
        const targetTab = button.getAttribute('data-tab');

        // Remove active class from all buttons and contents
        tabButtons.forEach(btn => btn.classList.remove('active'));
        tabContents.forEach(content => content.classList.remove('active'));

        // Add active class to clicked button and corresponding content
        button.classList.add('active');
        document.getElementById(targetTab + '-tab').classList.add('active');
    });
});

// Settings form functionality
const settingsForm = document.getElementById('settingsForm');
const saveSettingsBtn = document.getElementById('saveSettingsBtn');
const settingsStatus = document.getElementById('settingsStatus');
const saveCloudBtn = document.getElementById('saveCloudBtn');
const cloudStatus = document.getElementById('cloudStatus');

// Load current settings function
async function loadSettingsMDNs() {
    settingsStatus.textContent = '';
    settingsStatus.className = '';

    try {
        const response = await fetch('/settings/mdns');

        if (!response.ok) {
            throw new Error('Failed to load settings');
        }

        const data = await response.json();

        document.getElementById('mdnsName').value = data.mdns_name || ''

        settingsStatus.textContent = '';
        settingsStatus.className = '';

    } catch (error) {
        settingsStatus.textContent = '✗ Error loading settings: ' + error.message;
        settingsStatus.className = 'error';
    }
}

// Save settings
settingsForm.addEventListener('submit', async (e) => {
    e.preventDefault();

    const mdnsName = document.getElementById('mdnsName').value;

    settingsStatus.textContent = 'Saving settings...';
    settingsStatus.className = 'info';
    saveSettingsBtn.disabled = true;

    try {
        const params = new URLSearchParams();
        if (mdnsName) params.append('mdns_name', mdnsName);

        const response = await fetch('/settings/mdns', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: params.toString()
        });

        if (!response.ok) {
            throw new Error('Failed to save settings');
        }

        settingsStatus.textContent = '✓ Settings saved successfully! Restart device to apply changes.';
        settingsStatus.className = 'success';

        // Reload settings to verify
        setTimeout(() => {
            loadSettingsMDNs();
        }, 2000);

    } catch (error) {
        settingsStatus.textContent = '✗ Error saving settings: ' + error.message;
        settingsStatus.className = 'error';
    } finally {
        saveSettingsBtn.disabled = false;
    }
});

async function loadCloud() {
    try {
        const response = await fetch('/settings/cloud');
        if (!response.ok) throw new Error('Failed to load cloud settings');
        const data = await response.json();

        document.getElementById('cloudHost').value = data.cloud_host;
        document.getElementById('cloudToken').value = data.cloud_token;
    } catch (error) {
        console.error('Error loading cloud settings:', error);
    }
}

// Auto-load settings when settings tab is opened
tabButtons.forEach(button => {
    button.addEventListener('click', () => {
        if (button.getAttribute('data-tab') === 'settings') {
            loadSettingsMDNs();
            loadPins();
        }
        if (button.getAttribute('data-tab') === 'networks') {
            loadNetworks();
        }
        if (button.getAttribute('data-tab') === 'cloud') {
            loadCloud();
        }
    });
});

// Pins form functionality
const pinsForm = document.getElementById('pinsForm');
const pinsStatus = document.getElementById('pinsStatus');

const PIN_INPUTS = {
    swdio: 'pinSwdio',
    swclk: 'pinSwclk',
    tdi: 'pinTdi',
    tdo: 'pinTdo',
    trst: 'pinTrst',
};

function pinsFromForm() {
    return Object.fromEntries(
        Object.entries(PIN_INPUTS).map(([name, id]) => [name, document.getElementById(id).value]));
}

function pinsToForm(pins) {
    for (const [name, id] of Object.entries(PIN_INPUTS))
        document.getElementById(id).value = pins[name];
}

async function loadPins() {
    try {
        const response = await fetch('/settings/pins');
        if (!response.ok) throw new Error('Failed to load pins');
        pinsToForm(await response.json());
    } catch (error) {
        console.error('Error loading pins:', error);
    }
}

// POST pins to the probe; zeros fall back to hardware defaults.
// msg: {doing, done, failed} status texts. Returns true on success,
// so the caller can react (e.g. reload the form).
async function applyPins(pins, msg, btn) {
    pinsStatus.textContent = msg.doing + '...';
    pinsStatus.className = 'info';
    btn.disabled = true;

    try {
        const response = await fetch('/settings/pins', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams(pins).toString()
        });

        if (!response.ok) throw new Error(msg.failed);

        pinsStatus.textContent = '✓ ' + msg.done;
        pinsStatus.className = 'success';
        return true;
    } catch (error) {
        pinsStatus.textContent = '✗ Error: ' + error.message;
        pinsStatus.className = 'error';
    } finally {
        btn.disabled = false;
        setTimeout(() => { pinsStatus.textContent = ''; pinsStatus.className = ''; }, 4000);
    }
    return false;
}

pinsForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    await applyPins(pinsFromForm(),
        { doing: 'Saving pins', done: 'Pins saved successfully', failed: 'Failed to save pins' },
        document.getElementById('savePinsBtn'));
});

// Reset pins to hardware defaults: send zeros, then read back the values that were set
document.getElementById('defaultPinsBtn').addEventListener('click', async () => {
    const zeros = Object.fromEntries(Object.keys(PIN_INPUTS).map(name => [name, 0]));
    if (await applyPins(zeros,
            { doing: 'Applying default pins', done: 'Default pins applied', failed: 'Failed to apply default pins' },
            document.getElementById('defaultPinsBtn')))
        await loadPins();  // receive the values that were actually set
});

// Save cloud settings
document.getElementById('cloudForm').addEventListener('submit', async (e) => {
    e.preventDefault();

    cloudStatus.textContent = 'Saving cloud settings...';
    cloudStatus.className = 'info';
    saveCloudBtn.disabled = true;

    try {
        const params = new URLSearchParams({
            cloud_host: document.getElementById('cloudHost').value,
            cloud_token: document.getElementById('cloudToken').value,
        });

        const response = await fetch('/settings/cloud', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });

        if (!response.ok) throw new Error('Failed to save cloud settings');

        cloudStatus.textContent = '✓ Cloud settings saved successfully!';
        cloudStatus.className = 'success';
    } catch (error) {
        cloudStatus.textContent = '✗ Error saving cloud settings: ' + error.message;
        cloudStatus.className = 'error';
    } finally {
        saveCloudBtn.disabled = false;
        setTimeout(() => { cloudStatus.textContent = ''; cloudStatus.className = ''; }, 4000);
    }
});

/* ── Networks ── */

function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function escapeAttr(str) {
    return str.replace(/\\/g, '\\\\').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

async function loadNetworks() {
    try {
        const resp = await fetch('/settings/networks');
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        const networks = await resp.json();
        renderNetworks(networks);
    } catch (e) {
        showNetworksStatus('✗ Error loading networks: ' + e.message, 'error');
    }
}

function renderNetworks(networks) {
    const list = document.getElementById('networksList');
    if (networks.length === 0) {
        list.innerHTML = '<p style="color:#888;">No networks configured yet.</p>';
        return;
    }
    list.innerHTML = networks.map(n => `
        <div class="network-card" id="net-${n.idx}">
            <div class="network-info">
                <span class="network-ssid">${escapeHtml(n.ssid)}</span>
                <span class="badge ${n.auto_connect ? 'badge-auto' : 'badge-manual'}">${n.auto_connect ? 'Auto' : 'Manual'}</span>
            </div>
            <div class="network-actions">
                <button class="btn-small btn-edit" onclick="showEditForm(${n.idx})">Edit</button>
                <button class="btn-small btn-delete" onclick="deleteNetwork(${n.idx})">Delete</button>
            </div>
            <div class="network-edit-form" id="edit-form-${n.idx}" style="display:none;">
                <form onsubmit="updateNetwork(event, ${n.idx})">
                    <div class="form-group">
                        <label>SSID:</label>
                        <input type="text" id="edit-ssid-${n.idx}" value="${escapeAttr(n.ssid)}" maxlength="32" required>
                    </div>
                    <div class="form-group">
                        <label>Password (leave empty to keep existing):</label>
                        <input type="password" id="edit-pass-${n.idx}" placeholder="••••••••" maxlength="64">
                    </div>
                    <div class="form-group checkbox-group">
                        <label>
                            <input type="checkbox" id="edit-auto-${n.idx}" ${n.auto_connect ? 'checked' : ''}>
                            Auto-connect on boot
                        </label>
                    </div>
                    <div class="btn-row">
                        <button type="submit">Save</button>
                        <button type="button" class="btn-secondary" onclick="hideEditForm(${n.idx})">Cancel</button>
                    </div>
                </form>
            </div>
        </div>
    `).join('');
}

function showEditForm(idx) {
    document.querySelectorAll('.network-edit-form').forEach(f => f.style.display = 'none');
    document.getElementById('edit-form-' + idx).style.display = 'block';
}

function hideEditForm(idx) {
    document.getElementById('edit-form-' + idx).style.display = 'none';
}

async function updateNetwork(event, idx) {
    event.preventDefault();
    const ssid = document.getElementById('edit-ssid-' + idx).value;
    const pass = document.getElementById('edit-pass-' + idx).value;
    const auto = document.getElementById('edit-auto-' + idx).checked;

    const params = new URLSearchParams({ idx: idx, ssid: ssid, auto_connect: auto ? '1' : '0' });
    if (pass) params.append('pass', pass);

    try {
        const resp = await fetch('/settings/networks', {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        if (!resp.ok) throw new Error('Failed to update');
        showNetworksStatus('✓ Network updated', 'success');
        loadNetworks();
    } catch (e) {
        showNetworksStatus('✗ Error: ' + e.message, 'error');
    }
}

async function deleteNetwork(idx) {
    if (!confirm('Delete this network?')) return;
    try {
        const resp = await fetch('/settings/networks', {
            method: 'DELETE',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'idx=' + idx
        });
        if (!resp.ok) throw new Error('Failed to delete');
        showNetworksStatus('✓ Network deleted', 'success');
        loadNetworks();
    } catch (e) {
        showNetworksStatus('✗ Error: ' + e.message, 'error');
    }
}

document.getElementById('addNetworkBtn').addEventListener('click', () => {
    document.getElementById('addNetworkForm').style.display = 'block';
    document.getElementById('addNetworkBtn').style.display = 'none';
});

document.getElementById('cancelAddBtn').addEventListener('click', () => {
    document.getElementById('addNetworkForm').style.display = 'none';
    document.getElementById('addNetworkBtn').style.display = 'inline-block';
    document.getElementById('addNetFormEl').reset();
});

document.getElementById('addNetFormEl').addEventListener('submit', async (e) => {
    e.preventDefault();
    const ssid = document.getElementById('newSsid').value.trim();
    const pass = document.getElementById('newPass').value;
    const auto = document.getElementById('newAutoConnect').checked;

    if (!ssid) {
        showNetworksStatus('✗ SSID cannot be empty', 'error');
        return;
    }

    const params = new URLSearchParams({
        ssid: ssid,
        pass: pass,
        auto_connect: auto ? '1' : '0'
    });

    try {
        const resp = await fetch('/settings/networks', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        if (!resp.ok) throw new Error('Failed to add');
        showNetworksStatus('✓ Network added', 'success');
        document.getElementById('addNetworkForm').style.display = 'none';
        document.getElementById('addNetworkBtn').style.display = 'inline-block';
        document.getElementById('addNetFormEl').reset();
        loadNetworks();
    } catch (e) {
        showNetworksStatus('✗ Error: ' + e.message, 'error');
    }
});

function showNetworksStatus(msg, cls) {
    const el = document.getElementById('networksStatus');
    el.textContent = msg;
    el.className = cls;
    setTimeout(() => { el.textContent = ''; el.className = ''; }, 4000);
}

function showError(message, duration = 2000) {
    const container = document.getElementById('toast-container');

    const toast = document.createElement('div');
    toast.className = 'toast';
    toast.textContent = message;
    container.appendChild(toast);

    requestAnimationFrame(() => toast.classList.add('show'));

    setTimeout(() => {
        toast.classList.remove('show');
        toast.addEventListener('transitionend', () => toast.remove(), { once: true });
    }, duration);
}
