<?php
declare(strict_types=1);

/**
 * config/migrate_db.php — TezzNative Database Cleanup & CMS Articles Schema Migration
 */

require_once __DIR__ . '/db.php';

try {
    $pdo = tn_pdo();
    echo "=== TEZZNATIVE DATABASE MIGRATION & CLEANUP ===\n";

    // 1. Clean legacy / duplicate tables
    echo "1. Cleaning legacy tables...\n";
    $legacy = [
        'tezz_community_likes',
        'tezz_community_posts',
        'tezz_community_replies',
        'tezz_community_users',
        'tezz_package_registry'
    ];
    foreach ($legacy as $t) {
        $pdo->exec("DROP TABLE IF EXISTS `{$t}`");
        echo "   [OK] Dropped legacy table: {$t}\n";
    }

    // 2. Create tn_articles table
    echo "2. Ensuring tn_articles table...\n";
    $pdo->exec("
    CREATE TABLE IF NOT EXISTS `tn_articles` (
        `id` INT AUTO_INCREMENT PRIMARY KEY,
        `type` ENUM('blog', 'news') NOT NULL DEFAULT 'blog',
        `title` VARCHAR(255) NOT NULL,
        `slug` VARCHAR(255) NOT NULL UNIQUE,
        `category` VARCHAR(100) NOT NULL DEFAULT 'General',
        `excerpt` TEXT,
        `content` LONGTEXT NOT NULL,
        `author` VARCHAR(100) NOT NULL DEFAULT 'Rohit Pathak',
        `read_time` VARCHAR(30) DEFAULT '5 min',
        `tags` VARCHAR(255) DEFAULT '',
        `status` ENUM('draft', 'published', 'archived') NOT NULL DEFAULT 'published',
        `featured` TINYINT(1) DEFAULT 0,
        `views` INT DEFAULT 0,
        `published_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
        `created_at` DATETIME DEFAULT CURRENT_TIMESTAMP,
        `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        INDEX idx_type_status (`type`, `status`, `published_at`),
        INDEX idx_slug (`slug`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
    ");
    echo "   [OK] Table tn_articles created/verified.\n";

    // 3. Create tn_settings table
    echo "3. Ensuring tn_settings table...\n";
    $pdo->exec("
    CREATE TABLE IF NOT EXISTS `tn_settings` (
        `setting_key` VARCHAR(100) PRIMARY KEY,
        `setting_val` LONGTEXT NOT NULL,
        `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
    ");

    $ribbon = json_encode([
        'enabled'   => true,
        'badge'     => 'OFFICIAL RELEASE',
        'text'      => 'High-Performance Systems & AI Language with Native Tensors, Coroutines & Zero-GC Execution!',
        'link_text' => 'Get TezzNative v1.0.0',
        'link_url'  => '/download/'
    ], JSON_UNESCAPED_SLASHES);
    $stmt = $pdo->prepare("INSERT INTO tn_settings (setting_key, setting_val) VALUES ('announcement_ribbon', ?) ON DUPLICATE KEY UPDATE setting_val = VALUES(setting_val)");
    $stmt->execute([$ribbon]);
    echo "   [OK] Table tn_settings verified and ribbon seeded.\n";

    // 4. Seed initial articles
    echo "4. Seeding initial Blog posts & News items...\n";
    $initial_articles = [
        [
            'type' => 'blog',
            'title' => 'Introducing TezzNative v1.0 — A New Era of Systems Programming from India',
            'slug' => 'tezznative-v1-launch',
            'category' => 'Language',
            'excerpt' => 'TezzNative reaches its first stable release: a high-performance compiled language with a single-pass compiler, <50ms build times, native Win32 GUI, and a full ecosystem — all zero-dependency. Here is the story behind it.',
            'content' => "# Introducing TezzNative v1.0\n\nTezzNative combines the clean, human readability of Python with the raw bare-metal performance of C and assembly. Built from the ground up by Rohit Pathak at TEZZCORP PRIVATE LIMITED, it produces standalone PE and ELF executables with zero runtime dependencies and zero garbage collection pauses.\n\n### Key Milestones:\n- **Single-Pass Optimizing Compiler**: Sub-50ms native builds.\n- **First-Class 4D Tensor Arithmetic**: Infix `@` operator, SIMD AVX2 acceleration.\n- **Zero-GC Scoped Resource Management**: Deterministic `defer` and RAII lifetime tracking.\n- **Integrated Native GUI**: 60 FPS hardware-accelerated Win32 and DirectX rendering.",
            'author' => 'Rohit Pathak',
            'read_time' => '8 min',
            'tags' => 'v1.0.0,systems,release,compiler',
            'status' => 'published',
            'featured' => 1,
            'published_at' => '2026-05-15 10:00:00'
        ],
        [
            'type' => 'blog',
            'title' => 'How TezzLLM Achieves Zero-Dependency On-Device AI with Federated Training',
            'slug' => 'tezzllm-federated-ai',
            'category' => 'AI / ML',
            'excerpt' => 'A deep technical dive into the architecture of TezzLLM v2.0: byte-level BPE tokenization, RoPE positional encoding, SwiGLU feedforward networks, federated averaging across 8 parallel trainers, and running 1M+ parameters in 64MB RAM.',
            'content' => "# TezzLLM Architecture Deep Dive\n\nTezzLLM is engineered to execute transformer models natively on edge and consumer devices without requiring PyTorch, CUDA runtime bloat, or gigabytes of memory overhead.\n\n### Core Innovations:\n- **Byte-Level BPE Tokenizer**: Fully native C-speed tokenizer module.\n- **Rotary Positional Embeddings (RoPE)**: High context precision.\n- **SwiGLU Activations**: Peak inference throughput.\n- **8-Trainer Parallel Federated Averaging**: High privacy distributed fine-tuning.",
            'author' => 'Rohit Pathak',
            'read_time' => '12 min',
            'tags' => 'ai,llm,gguf,federated',
            'status' => 'published',
            'featured' => 1,
            'published_at' => '2026-04-28 14:30:00'
        ],
        [
            'type' => 'blog',
            'title' => 'Why Zero Dependencies Should Be Your Default — The Case for TezzNative Philosophy',
            'slug' => 'why-zero-dependencies',
            'category' => 'Engineering',
            'excerpt' => 'Modern software engineering has a dependency problem. TezzNative was built around the radical idea that an entire production stack — compiler, GUI, network, database, AI — should ship as a single binary with zero external requirements.',
            'content' => "# Why Zero Dependencies Matter\n\nSoftware stability suffers when applications rely on fragile dependency graphs with thousands of transient packages. TezzNative takes a stand for engineering resilience:\n\n1. Standalone PE/ELF binaries that run out-of-the-box.\n2. In-tree standard library covering networking, GUI, database, and tensors.\n3. Completely reproducible builds with cryptographic sha256 manifests.",
            'author' => 'Rohit Pathak',
            'read_time' => '10 min',
            'tags' => 'engineering,architecture,philosophy',
            'status' => 'published',
            'featured' => 0,
            'published_at' => '2026-02-20 09:15:00'
        ],
        [
            'type' => 'news',
            'title' => 'TezzNative v1.0.0 Released — A Systems Programming Language Made in India',
            'slug' => 'news-tezznative-v1-released',
            'category' => 'Product Launch',
            'excerpt' => 'TEZZCORP PRIVATE LIMITED officially releases TezzNative v1.0.0 for Windows x64. The production-grade systems programming language features a single-pass compiler, <50ms build times, built-in native GUI, and a complete ecosystem of zero-dependency frameworks.',
            'content' => "TEZZCORP PRIVATE LIMITED has officially released the first production-ready release of TezzNative (v1.0.0). Developers across the globe can now download the complete SDK archive or install via one-line PowerShell and Bash scripts.",
            'author' => 'TEZZCORP PRIVATE LIMITED',
            'read_time' => '3 min',
            'tags' => 'launch,v1.0.0,press-release',
            'status' => 'published',
            'featured' => 1,
            'published_at' => '2026-05-01 12:00:00'
        ],
        [
            'type' => 'news',
            'title' => 'TezzDB Embedded SQL Database Engine Ships with TezzNative Ecosystem',
            'slug' => 'news-tezzdb-embedded-released',
            'category' => 'Framework',
            'excerpt' => 'TezzDB, a fully embedded SQL-compatible database engine with ACID transaction support and B-tree storage, is released as part of the TezzNative standard ecosystem — no external SQLite or database runtimes needed.',
            'content' => "TezzDB is now available as a standard module within TezzNative v1.0.0. It provides zero-configuration local ACID storage with zero external dynamic dependencies.",
            'author' => 'TEZZCORP PRIVATE LIMITED',
            'read_time' => '3 min',
            'tags' => 'tezzdb,sql,framework',
            'status' => 'published',
            'featured' => 0,
            'published_at' => '2026-02-15 11:00:00'
        ],
        [
            'type' => 'news',
            'title' => 'TezzUI Native GUI Toolkit — Win32 Desktop Apps in Pure TezzNative',
            'slug' => 'news-tezzui-native-gui',
            'category' => 'Framework',
            'excerpt' => 'TezzUI releases as a stable native GUI framework for building full-featured Windows desktop applications using only TezzNative. Supports windows, controls, dialogs, custom rendering, and message loop management.',
            'content' => "TezzUI delivers high-performance 60 FPS Win32 and DirectX hardware-accelerated user interfaces with declarative layout models and zero Electron / Chromium runtime overhead.",
            'author' => 'TEZZCORP PRIVATE LIMITED',
            'read_time' => '4 min',
            'tags' => 'gui,tezzui,win32,directx',
            'status' => 'published',
            'featured' => 0,
            'published_at' => '2026-01-20 15:00:00'
        ]
    ];

    $stmt = $pdo->prepare("
        INSERT INTO `tn_articles` 
        (`type`, `title`, `slug`, `category`, `excerpt`, `content`, `author`, `read_time`, `tags`, `status`, `featured`, `published_at`)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON DUPLICATE KEY UPDATE 
        `title` = VALUES(`title`), `excerpt` = VALUES(`excerpt`), `category` = VALUES(`category`), `content` = VALUES(`content`)
    ");

    foreach ($initial_articles as $a) {
        $stmt->execute([
            $a['type'], $a['title'], $a['slug'], $a['category'],
            $a['excerpt'], $a['content'], $a['author'], $a['read_time'],
            $a['tags'], $a['status'], $a['featured'], $a['published_at']
        ]);
        echo "   [OK] Seeded: [{$a['type']}] {$a['title']}\n";
    }

    echo "=== DATABASE CLEANUP & MIGRATION COMPLETE ===\n";

} catch (Throwable $e) {
    echo "ERROR: " . $e->getMessage() . "\n";
    exit(1);
}

