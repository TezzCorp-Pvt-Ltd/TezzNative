<?php
declare(strict_types=1);

/**
 * api/packages.php — TezzNative Official Package Registry API
 * Provides package listings, category filtering, search, and package metadata.
 * Powered by MySQL database (tn_package_registry) with JSON fallback.
 */

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

require_once __DIR__ . '/../config/db.php';

$q = isset($_GET['q']) ? trim((string)$_GET['q']) : '';
$category = isset($_GET['category']) ? trim((string)$_GET['category']) : '';
$name = isset($_GET['name']) ? trim((string)$_GET['name']) : '';

function load_fallback_packages(): array {
    $seed_file = __DIR__ . '/../config/packages_seed.json';
    if (file_exists($seed_file)) {
        $data = json_decode(file_get_contents($seed_file), true);
        if (is_array($data)) return $data;
    }
    return [];
}

$packages = [];
$from_db = false;

try {
    $pdo = tn_pdo();
    $table = 'tn_package_registry';

    if ($name !== '') {
        $stmt = $pdo->prepare("SELECT * FROM $table WHERE name = ? LIMIT 1");
        $stmt->execute([$name]);
        $pkg = $stmt->fetch(PDO::FETCH_ASSOC);
        if ($pkg) {
            echo json_encode([
                'status'  => 'success',
                'source'  => 'mysql',
                'package' => $pkg
            ], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
            exit;
        } else {
            http_response_code(404);
            echo json_encode(['status' => 'error', 'message' => "Package '{$name}' not found"], JSON_PRETTY_PRINT);
            exit;
        }
    }

    $sql = "SELECT id, name, version, description, category, author_name, repository_url, downloads, checksum, tags, license, updated_at FROM $table";
    $params = [];
    $where = [];

    if ($category !== '' && strtolower($category) !== 'all') {
        $where[] = "category = ?";
        $params[] = $category;
    }

    if ($q !== '') {
        $where[] = "(name LIKE ? OR description LIKE ? OR tags LIKE ?)";
        $searchTerm = '%' . $q . '%';
        $params[] = $searchTerm;
        $params[] = $searchTerm;
        $params[] = $searchTerm;
    }

    if (!empty($where)) {
        $sql .= " WHERE " . implode(" AND ", $where);
    }

    $sql .= " ORDER BY downloads DESC, name ASC";

    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $packages = $stmt->fetchAll(PDO::FETCH_ASSOC);
    $from_db = true;
} catch (Throwable $e) {
    // Graceful fallback to static JSON
    $all = load_fallback_packages();
    if ($name !== '') {
        foreach ($all as $item) {
            if (strcasecmp($item['name'], $name) === 0) {
                echo json_encode([
                    'status'  => 'success',
                    'source'  => 'seed_fallback',
                    'package' => $item
                ], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
                exit;
            }
        }
        http_response_code(404);
        echo json_encode(['status' => 'error', 'message' => "Package '{$name}' not found"], JSON_PRETTY_PRINT);
        exit;
    }

    $packages = array_filter($all, function($p) use ($category, $q) {
        if ($category !== '' && strtolower($category) !== 'all' && strcasecmp($p['category'], $category) !== 0) {
            return false;
        }
        if ($q !== '') {
            $hay = strtolower($p['name'] . ' ' . $p['description'] . ' ' . ($p['tags'] ?? ''));
            if (strpos($hay, strtolower($q)) === false) return false;
        }
        return true;
    });
    $packages = array_values($packages);
}

echo json_encode([
    'status'   => 'success',
    'source'   => $from_db ? 'mysql' : 'seed_fallback',
    'version'  => '1.0.0',
    'total'    => count($packages),
    'packages' => $packages,
], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
