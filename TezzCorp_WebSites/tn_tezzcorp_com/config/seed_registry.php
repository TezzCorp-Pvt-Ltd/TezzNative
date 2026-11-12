<?php
require_once '/home/u190073748/domains/tezznative.org/public_html/config/db.php';

try {
    $pdo = tn_pdo();
    echo "Connected to MySQL successfully.
";

    // 1. Ensure columns exist on tn_package_registry and tezz_package_registry
    foreach (['tn_package_registry', 'tezz_package_registry'] as $table) {
        $cols = $pdo->query("SHOW COLUMNS FROM $table")->fetchAll(PDO::FETCH_COLUMN);
        
        if (!in_array('category', $cols)) {
            $pdo->exec("ALTER TABLE $table ADD COLUMN category VARCHAR(60) NOT NULL DEFAULT 'Systems & OS'");
            echo "Added column category to $table
";
        }
        if (!in_array('tags', $cols)) {
            $pdo->exec("ALTER TABLE $table ADD COLUMN tags VARCHAR(255) NULL DEFAULT ''");
            echo "Added column tags to $table
";
        }
        if (!in_array('license', $cols)) {
            $pdo->exec("ALTER TABLE $table ADD COLUMN license VARCHAR(50) NOT NULL DEFAULT 'MIT'");
            echo "Added column license to $table
";
        }
        if (!in_array('author_name', $cols)) {
            $pdo->exec("ALTER TABLE $table ADD COLUMN author_name VARCHAR(120) NOT NULL DEFAULT 'Rohit Pathak (TEZZCORP PRIVATE LIMITED)'");
            echo "Added column author_name to $table
";
        }
        if (!in_array('example_code', $cols)) {
            $pdo->exec("ALTER TABLE $table ADD COLUMN example_code TEXT NULL");
            echo "Added column example_code to $table
";
        }
    }

    $json = file_get_contents('/home/u190073748/domains/tezznative.org/public_html/config/packages_seed.json');
    $packages = json_decode($json, true);
    if (!$packages) {
        throw new Exception("Could not load packages_seed.json");
    }

    echo "Loaded " . count($packages) . " packages from JSON.
";

    foreach (['tn_package_registry', 'tezz_package_registry'] as $table) {
        $stmtCheck = $pdo->prepare("SELECT id FROM $table WHERE name = ?");
        $stmtInsert = $pdo->prepare("
            INSERT INTO $table 
            (name, version, description, category, author_id, author_name, repository_url, downloads, checksum, tags, license, example_code, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW())
        ");
        $stmtUpdate = $pdo->prepare("
            UPDATE $table SET 
                version = ?,
                description = ?,
                category = ?,
                author_name = ?,
                repository_url = ?,
                checksum = ?,
                tags = ?,
                license = ?,
                example_code = ?,
                updated_at = NOW()
            WHERE id = ?
        ");

        $inserted = 0;
        $updated = 0;
        foreach ($packages as $pkg) {
            $stmtCheck->execute([$pkg['name']]);
            $existing = $stmtCheck->fetch(PDO::FETCH_ASSOC);
            if ($existing) {
                $stmtUpdate->execute([
                    $pkg['version'],
                    $pkg['description'],
                    $pkg['category'],
                    $pkg['author_name'],
                    $pkg['repository_url'],
                    $pkg['checksum'],
                    $pkg['tags'],
                    $pkg['license'],
                    $pkg['example_code'],
                    $existing['id']
                ]);
                $updated++;
            } else {
                $stmtInsert->execute([
                    $pkg['name'],
                    $pkg['version'],
                    $pkg['description'],
                    $pkg['category'],
                    $pkg['author_id'],
                    $pkg['author_name'],
                    $pkg['repository_url'],
                    $pkg['downloads'],
                    $pkg['checksum'],
                    $pkg['tags'],
                    $pkg['license'],
                    $pkg['example_code']
                ]);
                $inserted++;
            }
        }
        $total = $pdo->query("SELECT count(*) FROM $table")->fetchColumn();
        echo "Table $table: inserted $inserted, updated $updated, total rows = $total
";
    }

    echo "SEEDING COMPLETED SUCCESSFULLY!
";
} catch (Throwable $e) {
    echo "ERROR: " . $e->getMessage() . "
" . $e->getTraceAsString() . "
";
    exit(1);
}
