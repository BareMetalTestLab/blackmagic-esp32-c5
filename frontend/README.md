# BlackMagic ESP32 Frontend

Web interface for the BlackMagic ESP32 firmware flasher with drag & drop support.

## Features

- 📁 **Drag & Drop** - Simply drag firmware files (.bin, .elf) onto the upload area
- 🖱️ **Click to Browse** - Traditional file selection also available
- ✅ **File Validation** - Automatic validation of file types
- 📊 **Upload Progress** - Real-time progress bar during upload
- 🎯 **Flash Address** - Configurable flash base address

## Setup

Dependencies are automatically installed during `idf.py build`. To manually install:

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

This will start a local server on the `src/` directory and open it in your browser. 

**Note:** The backend endpoints won't work locally - you'll need to test with the actual ESP32 device or mock the requests in browser dev tools.

Press `Ctrl+C` to stop the server.

This will:
1. Inline CSS and JavaScript into HTML
2. Minify the resulting HTML
3. Generate `../build/network-http-page.h` with the HTML as a C string

**Note**: During `idf.py build`, the build system automatically:
- Installs npm dependencies if needed (`npm install`)
- Rebuilds the frontend when source files change

Manual building is optional and only needed for independent testing.

## Development

After making changes to files in `src/`, you can either:
- Run `npm run build` to test the output
- Or simply run `idf.py build` which will automatically rebuild the frontend

### Frontend Debugging

To test the frontend independently:

**Simple:** Run `npm run dev` - opens `src/index.html` with live server.

For testing uploads without ESP32:
- Use browser dev tools Network tab to inspect requests
- Or connect to actual ESP32 device on your network
- Or use tools like Postman to mock the `/upload` endpoint

## File Structure

- `src/index.html` - Main HTML page
- `src/styles.css` - Styles
- `src/app.js` - JavaScript functionality
- `build.js` - Build script that generates C header
- `dist/` - Build output (minified HTML and C header)
- `dist/network-http-page.h` - Generated C header (auto-created)
