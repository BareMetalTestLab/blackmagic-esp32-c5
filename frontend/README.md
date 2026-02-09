# BlackMagic ESP32 Frontend

Web interface for the BlackMagic ESP32 firmware flasher.

## Setup

```bash
npm install
```

## Build

```bash
npm run build
```

## Development Server

```bash
npm run dev
```

This will:
1. Auto-generate `dev.html` from `src/index.html` with mocked backend
2. Start a local server 
3. Open the page in your browser

**Note:** `dev.html` is auto-generated - don't edit it manually! Edit `src/index.html` instead, and `dev.html` will be regenerated automatically when you run `npm run dev`.

Press `Ctrl+C` to stop the server.

This will:
1. Inline CSS and JavaScript into HTML
2. Minify the resulting HTML
3. Generate `../build/network-http-page.h` with the HTML as a C string

**Note**: The frontend is automatically built during `idf.py build` via CMake custom command, so manual building is optional.

## Development

After making changes to files in `src/`, you can either:
- Run `npm run build` to test the output
- Or simply run `idf.py build` which will automatically rebuild the frontend

### Frontend Debugging

To test the frontend independently without ESP32:

**Recommended:** Just run `npm run dev` - automatically syncs `src/index.html` changes to dev version!

Other options:
1. **Manual generation** - Run `npm run build:dev` to regenerate `dev.html` from sources
2. **Test production build** - Run `npm run build` then open `dist/index.html`
3. **Custom server** - Use Python (`python3 -m http.server 8000`) or any other HTTP server

## File Structure

- `src/index.html` - Main HTML page (edit this!)
- `src/styles.css` - Styles
- `src/app.js` - JavaScript functionality
- `build.js` - Build script that generates C header
- `build-dev.js` - Script that generates dev.html from src/
- `dev.html` - **Auto-generated** development testing page (don't edit!)
- `dist/` - Build output (minified HTML and C header)
- `dist/network-http-page.h` - Generated C header (auto-created)
