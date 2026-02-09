document.getElementById('uploadForm').addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const fileInput = document.getElementById('fileInput');
    const file = fileInput.files[0];
    const uploadBtn = document.getElementById('uploadBtn');
    
    if (!file) return;
    
    const status = document.getElementById('status');
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');
    
    uploadBtn.disabled = true;
    status.textContent = 'Uploading ' + file.name + ' (' + (file.size/1024).toFixed(1) + ' KB)...';
    status.className = 'info';
    progressContainer.style.display = 'block';
    progressBar.style.width = '0%';
    
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
