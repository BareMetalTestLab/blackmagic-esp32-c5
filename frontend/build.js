const fs = require('fs');
const path = require('path');
const { minify } = require('html-minifier');

const SRC_DIR = path.join(__dirname, 'src');
const DIST_DIR = path.join(__dirname, 'dist');
const OUTPUT_FILE = path.join(DIST_DIR, 'network-http-page.h');

// Create dist directory if it doesn't exist
if (!fs.existsSync(DIST_DIR)) {
    fs.mkdirSync(DIST_DIR, { recursive: true });
}

console.log('Building frontend...');

// Read source files
const html = fs.readFileSync(path.join(SRC_DIR, 'index.html'), 'utf8');
const css = fs.readFileSync(path.join(SRC_DIR, 'styles.css'), 'utf8');
const js = fs.readFileSync(path.join(SRC_DIR, 'app.js'), 'utf8');

// Inline CSS and JS into HTML
let fullHtml = html;
fullHtml = fullHtml.replace('<link rel="stylesheet" href="styles.css">', `<style>${css}</style>`);
fullHtml = fullHtml.replace('<script src="app.js"></script>', `<script>${js}</script>`);

// Minify HTML
const minifiedHtml = minify(fullHtml, {
    collapseWhitespace: true,
    removeComments: true,
    minifyJS: true,
    minifyCSS: true
});

// Save minified version to dist
fs.writeFileSync(path.join(DIST_DIR, 'index.html'), minifiedHtml);

// Convert to C string
const cString = minifiedHtml
    .replace(/\\/g, '\\\\')
    .replace(/"/g, '\\"')
    .replace(/\n/g, '\\n');

// Split into multiple lines for readability in C code (max 100 chars per line)
const chunks = [];
let currentChunk = '';
for (let i = 0; i < cString.length; i++) {
    currentChunk += cString[i];
    if (currentChunk.length >= 100 && (cString[i] === ' ' || cString[i] === '>')) {
        chunks.push(currentChunk);
        currentChunk = '';
    }
}
if (currentChunk) {
    chunks.push(currentChunk);
}

const cCode = `// Auto-generated file - do not edit manually
// Generated from frontend sources

#ifndef NETWORK_HTTP_PAGE_H
#define NETWORK_HTTP_PAGE_H

static const char *html_page = 
${chunks.map(chunk => `    "${chunk}"`).join('\n')};

#endif // NETWORK_HTTP_PAGE_H
`;

// Write C header file
fs.writeFileSync(OUTPUT_FILE, cCode);

console.log(`✓ Build complete!`);
console.log(`  - Minified HTML: ${minifiedHtml.length} bytes`);
console.log(`  - Output: ${OUTPUT_FILE}`);
