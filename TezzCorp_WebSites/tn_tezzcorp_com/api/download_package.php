<?php
declare(strict_types=1);

/**
 * api/download_package.php — Official TezzNative Library Download Endpoint
 * Increments download metrics in MySQL tn_package_registry and serves the .tn file.
 */

require_once __DIR__ . '/../config/db.php';

$name = isset($_GET['name']) ? trim((string)$_GET['name']) : '';
$cleanName = preg_replace('/[^a-zA-Z0-9_]/', '', $name);

if ($cleanName === '') {
    http_response_code(400);
    header('Content-Type: text/plain; charset=utf-8');
    echo "Error: Invalid package name specified.";
    exit;
}

$libFile = __DIR__ . '/../download/sdk/lib/' . $cleanName . '.tn';

if (!file_exists($libFile)) {
    http_response_code(404);
    header('Content-Type: text/plain; charset=utf-8');
    echo "Error: Package '{$cleanName}.tn' not found on official registry.";
    exit;
}

// Increment downloads in MySQL (non-blocking)
try {
    $pdo = tn_pdo();
    $stmt = $pdo->prepare("UPDATE tn_package_registry SET downloads = downloads + 1 WHERE name = ?");
    $stmt->execute([$cleanName]);
} catch (Throwable $e) {
    // Non-blocking telemetry
}

// Serve file
header('Content-Description: File Transfer');
header('Content-Type: text/plain; charset=utf-8');
header('Content-Disposition: attachment; filename="' . $cleanName . '.tn"');
header('Expires: 0');
header('Cache-Control: must-revalidate, post-check=0, pre-check=0');
header('Pragma: public');
header('Content-Length: ' . filesize($libFile));

readfile($libFile);
exit;
