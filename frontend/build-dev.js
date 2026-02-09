const fs = require('fs');
const path = require('path');

const SRC_HTML = path.join(__dirname, 'src', 'index.html');
const DEV_HTML = path.join(__dirname, 'dev.html');

console.log('Generating dev.html from src/index.html...');

// Read source HTML
let html = fs.readFileSync(SRC_HTML, 'utf8');

// Add dev banner and mock script
html = html.replace(
    '</head>',
    `    <style>
        .dev-banner {
            background-color: #ff9800;
            color: white;
            padding: 10px;
            text-align: center;
            font-weight: bold;
        }
    </style>
</head>`
);

html = html.replace(
    '<body>',
    `<body>
    <div class="dev-banner">⚠️ DEVELOPMENT MODE - Backend is mocked</div>`
);

html = html.replace(
    '<script src="app.js"></script>',
    `<script>
        // Development mode - mock backend by overriding form submission
        window.addEventListener('DOMContentLoaded', () => {
            const originalScript = document.createElement('script');
            originalScript.src = 'src/app.js';
            
            // Load original app.js
            document.body.appendChild(originalScript);
            
            // Wait for it to load, then override
            originalScript.onload = () => {
                const form = document.getElementById('uploadForm');
                
                // Remove original listener and add mock
                const newForm = form.cloneNode(true);
                form.parentNode.replaceChild(newForm, form);
                
                newForm.addEventListener('submit', async (e) => {
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
                    
                    // Simulate upload progress
                    let progress = 0;
                    const interval = setInterval(() => {
                        progress += 10;
                        progressBar.style.width = progress + '%';
                        status.textContent = 'Uploading... ' + progress + '%';
                        
                        if (progress >= 100) {
                            clearInterval(interval);
                            status.textContent = '✓ Firmware flashed successfully (MOCKED)';
                            status.className = 'success';
                            uploadBtn.disabled = false;
                        }
                    }, 200);
                });
            };
        });
    </script>`
);

html = html.replace('href="styles.css"', 'href="src/styles.css"');
html = html.replace(
    '<title>BlackMagic - Flash Firmware</title>',
    '<title>BlackMagic - Flash Firmware (DEV)</title>'
);

// Write dev.html
fs.writeFileSync(DEV_HTML, html);

console.log('✓ dev.html generated successfully!');
