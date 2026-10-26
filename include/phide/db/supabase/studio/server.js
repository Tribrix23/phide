/**
 * @file server.js
 * @brief Standalone lightweight Supabase Studio & REST API Server for port 54323
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

let customPort = null;
const portArgIdx = process.argv.indexOf('--port');
if (portArgIdx !== -1 && process.argv[portArgIdx + 1]) {
    customPort = parseInt(process.argv[portArgIdx + 1], 10);
}
const PORT = customPort || parseInt(process.env.PORT || '54323', 10);
const HOST = '0.0.0.0';

const mimeTypes = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'text/javascript',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon'
};

const server = http.createServer((req, res) => {
    // CORS headers for local development and Supabase JS client
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', '*');

    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    let pathname = url.pathname;

    // Supabase REST endpoint simulation
    if (pathname.startsWith('/rest/v1/')) {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            status: 'online',
            service: 'supabase-postgrest',
            endpoint: pathname,
            data: [
                { id: 1, student_number: '2026-00104', full_name: 'Alexander Vance', course: 'BS Computer Science' },
                { id: 2, student_number: '2026-00105', full_name: 'Elena Rostova', course: 'BS Information Tech' }
            ]
        }));
        return;
    }

    // Health check endpoint
    if (pathname === '/health' || pathname === '/status') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ok', studio: true, port: PORT }));
        return;
    }

    // Static files
    if (pathname === '/' || pathname === '') {
        pathname = '/index.html';
    }

    const filePath = path.join(__dirname, pathname);
    fs.readFile(filePath, (err, data) => {
        if (err) {
            // Fallback to index.html for SPA routes
            const indexPath = path.join(__dirname, 'index.html');
            fs.readFile(indexPath, (idxErr, idxData) => {
                if (idxErr) {
                    res.writeHead(404, { 'Content-Type': 'text/plain' });
                    res.end('404 Not Found');
                } else {
                    res.writeHead(200, { 'Content-Type': 'text/html' });
                    res.end(idxData);
                }
            });
            return;
        }

        const ext = path.extname(filePath).toLowerCase();
        const contentType = mimeTypes[ext] || 'application/octet-stream';
        res.writeHead(200, { 'Content-Type': contentType });
        res.end(data);
    });
});

server.listen(PORT, HOST, () => {
    console.log(`[Supabase Studio] Listening on http://localhost:${PORT}`);
});
