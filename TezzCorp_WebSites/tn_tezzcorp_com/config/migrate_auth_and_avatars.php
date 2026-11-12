<?php
declare(strict_types=1);

/**
 * config/migrate_auth_and_avatars.php
 * Creates tn_admins table, adds avatar and OTP columns to tn_community_users.
 */

require_once __DIR__ . '/db.php';

try {
    $pdo = tn_pdo();
    echo "=== TEZZNATIVE AUTH & AVATAR MIGRATION ===\n";

    // 1. Create tn_admins table
    echo "1. Creating tn_admins table...\n";
    $pdo->exec("
    CREATE TABLE IF NOT EXISTS `tn_admins` (
        `id` INT AUTO_INCREMENT PRIMARY KEY,
        `username` VARCHAR(100) NOT NULL UNIQUE,
        `email` VARCHAR(190) NOT NULL UNIQUE,
        `password_hash` VARCHAR(255) NOT NULL,
        `full_name` VARCHAR(100) NOT NULL DEFAULT 'Administrator',
        `role` VARCHAR(50) NOT NULL DEFAULT 'superadmin',
        `two_factor_enabled` TINYINT(1) NOT NULL DEFAULT 1,
        `two_factor_otp` VARCHAR(10) DEFAULT NULL,
        `two_factor_expires_at` DATETIME DEFAULT NULL,
        `reset_otp` VARCHAR(10) DEFAULT NULL,
        `reset_expires_at` DATETIME DEFAULT NULL,
        `last_login` DATETIME DEFAULT NULL,
        `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
        `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        INDEX idx_username (`username`),
        INDEX idx_email (`email`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
    ");
    echo "   [OK] Table tn_admins created/verified.\n";

    // Seed default admin if table is empty
    $count = (int)$pdo->query("SELECT COUNT(*) FROM tn_admins")->fetchColumn();
    if ($count === 0) {
        $hash = password_hash('Rohit@8084', PASSWORD_BCRYPT);
        $stmt = $pdo->prepare("
            INSERT INTO tn_admins 
            (`username`, `email`, `password_hash`, `full_name`, `role`, `two_factor_enabled`) 
            VALUES (?, ?, ?, ?, ?, 1)
        ");
        $stmt->execute(['admin', 'no-reply@tezznative.org', $hash, 'Rohit Pathak', 'superadmin']);
        echo "   [OK] Seeded default superadmin: 'admin' (email: no-reply@tezznative.org)\n";
    }

    // 2. Add avatar_url, reset_otp to tn_community_users
    echo "2. Updating tn_community_users table...\n";
    $cols = $pdo->query("SHOW COLUMNS FROM tn_community_users")->fetchAll(PDO::FETCH_COLUMN);
    
    if (!in_array('avatar_url', $cols, true)) {
        $pdo->exec("ALTER TABLE tn_community_users ADD COLUMN avatar_url VARCHAR(255) DEFAULT NULL AFTER github_url");
        echo "   [OK] Added avatar_url column to tn_community_users.\n";
    }
    if (!in_array('reset_otp', $cols, true)) {
        $pdo->exec("ALTER TABLE tn_community_users ADD COLUMN reset_otp VARCHAR(10) DEFAULT NULL, ADD COLUMN reset_expires_at DATETIME DEFAULT NULL");
        echo "   [OK] Added reset_otp columns to tn_community_users.\n";
    }

    // 3. Ensure uploads/avatars directory
    $uploadDir = __DIR__ . '/../uploads/avatars';
    if (!is_dir($uploadDir)) {
        @mkdir($uploadDir, 0755, true);
        echo "   [OK] Created upload directory: uploads/avatars\n";
    }

    echo "=== MIGRATION COMPLETED SUCCESSFULLY ===\n";

} catch (Throwable $e) {
    echo "ERROR: " . $e->getMessage() . "\n";
    exit(1);
}
