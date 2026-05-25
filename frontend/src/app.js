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
    
    fileInfo.textContent = `✓ Selected: ${file.name} (${(file.size/1024).toFixed(1)} KB)`;
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
    
    try {
        const paramsResponse = await fetch('/flash-params', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: 'baseAddr=' + encodeURIComponent(baseAddr) + '&iface=' + encodeURIComponent(iface)
        });
        
        if (!paramsResponse.ok) {
            const errorText = await paramsResponse.text();
            status.textContent = '✗ Failed to set parameters: ' + errorText;
            status.className = 'error';
            uploadBtn.disabled = false;
            return;
        }
        
        const result = await paramsResponse.json();
        if (!result.success) {
            status.textContent = '✗ Failed to set parameters: ' + (result.error || 'Unknown error');
            status.className = 'error';
            uploadBtn.disabled = false;
            return;
        }
        
        console.log('Flash parameters set:', result);
        progressBar.style.width = '5%';
    } catch (error) {
        status.textContent = '✗ Failed to set parameters: ' + error.message;
        status.className = 'error';
        uploadBtn.disabled = false;
        return;
    }
    
    // Step 2: Upload firmware file
    status.textContent = 'Uploading ' + file.name + ' (' + (file.size/1024).toFixed(1) + ' KB)...';
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
        
        xhr.open('POST', '/upload');
        xhr.send(formData);
    } catch (error) {
        status.textContent = '✗ Upload failed: ' + error.message;
        status.className = 'error';
        uploadBtn.disabled = false;
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

// Load current settings function
async function loadSettings() {
    settingsStatus.textContent = '';
    settingsStatus.className = '';
    
    try {
        const response = await fetch('/nvs-settings');
        
        if (!response.ok) {
            throw new Error('Failed to load settings');
        }
        
        const data = await response.json();
        
        document.getElementById('hostname').value = data.hostname || ''
        
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
    
    const hostname = document.getElementById('hostname').value;
    
    settingsStatus.textContent = 'Saving settings...';
    settingsStatus.className = 'info';
    saveSettingsBtn.disabled = true;
    
    try {
        const params = new URLSearchParams();
        if (hostname) params.append('hostname', hostname);
        
        const response = await fetch('/nvs-settings', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: params.toString()
        });
        
        if (!response.ok) {
            throw new Error('Failed to save settings');
        }
        
        const result = await response.json();
        
        if (result.success) {
            settingsStatus.textContent = '✓ Settings saved successfully! Restart device to apply changes.';
            settingsStatus.className = 'success';
            
            // Reload settings to verify
            setTimeout(() => {
                loadSettings();
            }, 2000);
        } else {
            settingsStatus.textContent = '✗ ' + (result.error || 'Failed to save settings');
            settingsStatus.className = 'error';
        }
        
    } catch (error) {
        settingsStatus.textContent = '✗ Error saving settings: ' + error.message;
        settingsStatus.className = 'error';
    } finally {
        saveSettingsBtn.disabled = false;
    }
});

// Auto-load settings when settings tab is opened
tabButtons.forEach(button => {
    button.addEventListener('click', () => {
        if (button.getAttribute('data-tab') === 'settings') {
            loadSettings();
            loadPins();
        }
        if (button.getAttribute('data-tab') === 'networks') {
            loadNetworks();
        }
    });
});

// Pins form functionality
const pinsForm = document.getElementById('pinsForm');
const pinsStatus = document.getElementById('pinsStatus');

async function loadPins() {
    try {
        const response = await fetch('/pins');
        if (!response.ok) throw new Error('Failed to load pins');
        const data = await response.json();

        document.getElementById('pinSwdio').value = data.swdio;
        document.getElementById('pinSwclk').value = data.swclk;
        document.getElementById('pinTdi').value   = data.tdi;
        document.getElementById('pinTdo').value   = data.tdo;
        document.getElementById('pinTrst').value  = data.trst;
    } catch (error) {
        console.error('Error loading pins:', error);
    }
}

pinsForm.addEventListener('submit', async (e) => {
    e.preventDefault();

    const savePinsBtn = document.getElementById('savePinsBtn');
    pinsStatus.textContent = 'Saving pins...';
    pinsStatus.className = 'info';
    savePinsBtn.disabled = true;

    try {
        const params = new URLSearchParams({
            swdio: document.getElementById('pinSwdio').value,
            swclk: document.getElementById('pinSwclk').value,
            tdi:   document.getElementById('pinTdi').value,
            tdo:   document.getElementById('pinTdo').value,
            trst:  document.getElementById('pinTrst').value,
        });

        const response = await fetch('/pins', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });

        if (!response.ok) throw new Error('Failed to save pins');
        const result = await response.json();

        if (result.success) {
            pinsStatus.textContent = '✓ Pins saved successfully!';
            pinsStatus.className = 'success';
        } else {
            pinsStatus.textContent = '✗ ' + (result.error || 'Failed to save pins');
            pinsStatus.className = 'error';
        }
    } catch (error) {
        pinsStatus.textContent = '✗ Error: ' + error.message;
        pinsStatus.className = 'error';
    } finally {
        document.getElementById('savePinsBtn').disabled = false;
        setTimeout(() => { pinsStatus.textContent = ''; pinsStatus.className = ''; }, 4000);
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
        const resp = await fetch('/networks');
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
        const resp = await fetch('/networks/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        const result = await resp.json();
        if (result.success) {
            showNetworksStatus('✓ Network updated', 'success');
            loadNetworks();
        } else {
            showNetworksStatus('✗ ' + (result.error || 'Failed to update'), 'error');
        }
    } catch (e) {
        showNetworksStatus('✗ Error: ' + e.message, 'error');
    }
}

async function deleteNetwork(idx) {
    if (!confirm('Delete this network?')) return;
    try {
        const resp = await fetch('/networks/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'idx=' + idx
        });
        const result = await resp.json();
        if (result.success) {
            showNetworksStatus('✓ Network deleted', 'success');
            loadNetworks();
        } else {
            showNetworksStatus('✗ ' + (result.error || 'Failed to delete'), 'error');
        }
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
        const resp = await fetch('/networks/add', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        });
        const result = await resp.json();
        if (result.success) {
            showNetworksStatus('✓ Network added', 'success');
            document.getElementById('addNetworkForm').style.display = 'none';
            document.getElementById('addNetworkBtn').style.display = 'inline-block';
            document.getElementById('addNetFormEl').reset();
            loadNetworks();
        } else {
            showNetworksStatus('✗ ' + (result.error || 'Failed to add'), 'error');
        }
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
