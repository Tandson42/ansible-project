<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store, no-cache, must-revalidate');

$host = getenv('DB_HOST') ?: '10.0.2.2';
$port = getenv('DB_PORT') ?: '3304';
$dbName = getenv('DB_NAME') ?: 'banco_do_tand';
$user = getenv('DB_USER') ?: 'tand';
$password = getenv('DB_PASSWORD') ?: '';
$nodeName = getenv('WEB_NODE_NAME') ?: (gethostname() ?: 'unknown');

$dsn = sprintf('mysql:host=%s;port=%s;dbname=%s;charset=utf8mb4', $host, $port, $dbName);

try {
    $pdo = new PDO($dsn, $user, $password, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_TIMEOUT => 3,
    ]);

    $stmt = $pdo->query(
        'SELECT VERSION() AS mysql_version, DATABASE() AS current_db, USER() AS db_user, NOW() AS server_time'
    );
    $info = $stmt->fetch();

    http_response_code(200);
    echo json_encode([
        'status' => 'ok',
        'node' => $nodeName,
        'db' => [
            'host' => $host,
            'port' => $port,
            'database' => $info['current_db'] ?? $dbName,
            'user' => $info['db_user'] ?? $user,
            'version' => $info['mysql_version'] ?? null,
            'server_time' => $info['server_time'] ?? null,
        ],
    ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
} catch (Throwable $e) {
    http_response_code(503);
    echo json_encode([
        'status' => 'error',
        'node' => $nodeName,
        'db' => [
            'host' => $host,
            'port' => $port,
            'database' => $dbName,
        ],
        'error' => $e->getMessage(),
    ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
}
