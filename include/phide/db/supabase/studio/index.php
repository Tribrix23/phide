<?php
// Supabase Studio fallback entry for PHP built-in server
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: *');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

$uri = $_SERVER['REQUEST_URI'];
if (strpos($uri, '/rest/v1/') === 0) {
    header('Content-Type: application/json');
    echo json_encode([
        'status' => 'online',
        'service' => 'supabase-postgrest',
        'endpoint' => $uri,
        'data' => [
            ['id' => 1, 'student_number' => '2026-00104', 'full_name' => 'Alexander Vance', 'course' => 'BS Computer Science'],
            ['id' => 2, 'student_number' => '2026-00105', 'full_name' => 'Elena Rostova', 'course' => 'BS Information Tech']
        ]
    ]);
    exit;
}

if ($uri === '/health' || $uri === '/status') {
    header('Content-Type: application/json');
    echo json_encode(['status' => 'ok', 'studio' => true]);
    exit;
}

$filePath = __DIR__ . parse_url($uri, PHP_URL_PATH);
if (is_file($filePath) && file_exists($filePath)) {
    return false; // serve directly via built-in server
}

header('Content-Type: text/html; charset=utf-8');
readfile(__DIR__ . '/index.html');
