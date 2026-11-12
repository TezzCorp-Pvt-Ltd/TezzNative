<?php
declare(strict_types=1);

require_once __DIR__ . '/../../config/db.php';

header('Content-Type: application/json; charset=utf-8');

function jsonResponse(array $data, int $status = 200): void {
    http_response_code($status);
    echo json_encode($data, JSON_UNESCAPED_SLASHES);
    exit;
}

$input = json_decode(file_get_contents('php://input'), true) ?: [];

$email = strtolower(trim((string)($input['email'] ?? '')));
$password = (string)($input['password'] ?? '');

if ($email === '' || $password === '') {
    jsonResponse(['success' => false, 'message' => 'Email and password are required'], 422);
}

try {
    $pdo = tn_pdo();

    // 1. Find user in tn_community_users
    $stmt = $pdo->prepare("
        SELECT id, display_name AS name, email, password_hash, role, is_verified, avatar_url
        FROM tn_community_users
        WHERE email = ?
        LIMIT 1
    ");
    $stmt->execute([$email]);
    $user = $stmt->fetch();

    // 2. Fallback to tn_users
    if (!$user) {
        $stmt = $pdo->prepare("
            SELECT id, display_name AS name, email, password_hash, 'developer' AS role, 1 AS is_verified, NULL AS avatar_url
            FROM tn_users
            WHERE email = ?
            LIMIT 1
        ");
        $stmt->execute([$email]);
        $user = $stmt->fetch();
    }

    if (!$user || !password_verify($password, $user['password_hash'])) {
        jsonResponse(['success' => false, 'message' => 'Invalid email or password'], 401);
    }

    if (session_status() === PHP_SESSION_NONE) {
        session_start();
    }
    session_regenerate_id(true);
    $_SESSION['user_id'] = $user['id'];
    $_SESSION['user_email'] = $user['email'];
    $_SESSION['user_name'] = $user['name'];
    $_SESSION['user_role'] = $user['role'];
    $_SESSION['user_avatar'] = $user['avatar_url'];
    $_SESSION['logged_in'] = true;

    jsonResponse([
        'success' => true,
        'message' => 'Login successful',
        'user' => [
            'id' => $user['id'],
            'email' => $user['email'],
            'name' => $user['name'],
            'role' => $user['role'],
            'avatar_url' => $user['avatar_url']
        ],
        'redirect' => '/community.php'
    ]);
} catch (Throwable $e) {
    jsonResponse(['success' => false, 'message' => 'Authentication failed: ' . $e->getMessage()], 500);
}
