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

This will start a local server and automatically open `dev.html` in your browser with mocked backend for testing.

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

**Recommended:** Just run `npm run dev` - opens dev.html with mocked backend automatically!

Other options:
1. **Manual test** - Open `dev.html` in your browser (works offline)
2. **Test production build** - Open `dist/index.html` after running `npm run build`
3. **Custom server** - Use Python (`python3 -m http.server 8000`) or any other HTTP server

## File Structure

- `src/index.html` - Main HTML page
- `src/styles.css` - Styles
- `src/app.js` - JavaScript functionality
- `build.js` - Build script that generates C header
- `dev.html` - Development testing page with mocked backend
- `dist/` - Build output (minified HTML and C header)
- `dist/network-http-page.h` - Generated C header (auto-created)
