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
    
    try {
        const paramsResponse = await fetch('/flash-params', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: 'baseAddr=' + encodeURIComponent(baseAddr)
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
    settingsStatus.textContent = 'Loading settings...';
    settingsStatus.className = 'info';
    
    try {
        const response = await fetch('/nvs-settings');
        
        if (!response.ok) {
            throw new Error('Failed to load settings');
        }
        
        const data = await response.json();
        
        document.getElementById('ssid').value = data.ssid || '';
        document.getElementById('pass').value = data.pass || '';
        document.getElementById('hostname').value = data.hostname || '';
        
        settingsStatus.textContent = '✓ Settings loaded successfully';
        settingsStatus.className = 'success';
        
        setTimeout(() => {
            settingsStatus.textContent = '';
            settingsStatus.className = '';
        }, 3000);
        
    } catch (error) {
        settingsStatus.textContent = '✗ Error loading settings: ' + error.message;
        settingsStatus.className = 'error';
    }
}

// Save settings
settingsForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const ssid = document.getElementById('ssid').value;
    const pass = document.getElementById('pass').value;
    const hostname = document.getElementById('hostname').value;
    
    settingsStatus.textContent = 'Saving settings...';
    settingsStatus.className = 'info';
    saveSettingsBtn.disabled = true;
    
    try {
        const params = new URLSearchParams();
        if (ssid) params.append('ssid', ssid);
        if (pass) params.append('pass', pass);
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
        }
    });
});
