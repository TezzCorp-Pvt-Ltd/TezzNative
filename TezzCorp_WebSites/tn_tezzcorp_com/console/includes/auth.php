<?php
declare(strict_types=1);

/**
 * console/includes/auth.php — Admin Authentication Guard & Security Layer
 * Protected with session regeneration, CSRF validation, and brute-force protection.
 */

if (session_status() === PHP_SESSION_NONE) {
    ini_set('session.cookie_httponly', '1');
    ini_set('session.use_only_cookies', '1');
    if (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] === 'on') {
        ini_set('session.cookie_secure', '1');
    }
    session_start();
}

require_once __DIR__ . '/../../config/db.php';

function tn_admin_is_logged(): bool {
    return isset($_SESSION['tn_admin_logged']) && $_SESSION['tn_admin_logged'] === true;
}

function tn_admin_require(): void {
    if (!tn_admin_is_logged()) {
        $curr = $_SERVER['REQUEST_URI'] ?? '/console/';
        header("Location: /console/login?redirect=" . urlencode($curr));
        exit;
    }
}

function tn_csrf_token(): string {
    if (empty($_SESSION['tn_csrf_token'])) {
        $_SESSION['tn_csrf_token'] = bin2hex(random_bytes(32));
    }
    return $_SESSION['tn_csrf_token'];
}

function tn_csrf_verify(?string $token): bool {
    if (empty($token) || empty($_SESSION['tn_csrf_token'])) {
        return false;
    }
    return hash_equals($_SESSION['tn_csrf_token'], $token);
}

function tn_set_flash(string $type, string $message): void {
    $_SESSION['flash_' . $type] = $message;
}

function tn_get_flash(string $type): ?string {
    if (isset($_SESSION['flash_' . $type])) {
        $msg = $_SESSION['flash_' . $type];
        unset($_SESSION['flash_' . $type]);
        return $msg;
    }
    return null;
}

function tn_check_rate_limit(string $ip): bool {
    $lockFile = sys_get_temp_dir() . '/tn_lock_' . md5($ip) . '.json';
    if (!file_exists($lockFile)) return true;
    
    $data = json_decode((string)file_get_contents($lockFile), true);
    if (!is_array($data)) return true;
    
    if (($data['attempts'] ?? 0) >= 5) {
        $lockUntil = ($data['last_attempt'] ?? 0) + 900; // 15 mins
        if (time() < $lockUntil) {
            return false;
        } else {
            @unlink($lockFile);
            return true;
        }
    }
    return true;
}

function tn_record_failed_attempt(string $ip): void {
    $lockFile = sys_get_temp_dir() . '/tn_lock_' . md5($ip) . '.json';
    $data = ['attempts' => 0, 'last_attempt' => time()];
    if (file_exists($lockFile)) {
        $existing = json_decode((string)file_get_contents($lockFile), true);
        if (is_array($existing)) $data = $existing;
    }
    $data['attempts'] = ($data['attempts'] ?? 0) + 1;
    $data['last_attempt'] = time();
    file_put_contents($lockFile, json_encode($data));
}

function tn_clear_failed_attempts(string $ip): void {
    $lockFile = sys_get_temp_dir() . '/tn_lock_' . md5($ip) . '.json';
    if (file_exists($lockFile)) {
        @unlink($lockFile);
    }
}

