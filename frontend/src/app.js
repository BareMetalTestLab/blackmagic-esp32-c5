// Drag and drop functionality
const dropZone = document.getElementById('dropZone');
const fileInput = document.getElementById('fileInput');
const fileInfo = document.getElementById('fileInfo');
const uploadBtn = document.getElementById('uploadBtn');
const uploadFormElement = document.getElementById('uploadFormElement');
const uploadFormDiv = document.getElementById('uploadForm');

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
