<?php
declare(strict_types=1);

/**
 * console/cms.php — TezzNative Content & Announcement Management
 * Robust CMS for Managing Articles (Blog & News) and Announcement Ribbon via MariaDB.
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'cms';
$console_title = 'Announcement & Content CMS';
$pdo = tn_pdo();

// ── Ensure schema is in place ──────────────────────────────────────────────────
try {
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

    $pdo->exec("
    CREATE TABLE IF NOT EXISTS `tn_settings` (
        `setting_key` VARCHAR(100) PRIMARY KEY,
        `setting_val` LONGTEXT NOT NULL,
        `updated_at` DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
    ");
} catch (Throwable $e) {
    // Continue
}

// ── Helper Slug Generator ──────────────────────────────────────────────────────
function tn_slugify(string $text): string {
    $text = preg_replace('~[^\pL\d]+~u', '-', $text);
    $text = iconv('utf-8', 'us-ascii//TRANSLIT', $text) ?: $text;
    $text = preg_replace('~[^-\w]+~', '', $text);
    $text = trim($text, '-');
    $text = preg_replace('~-+~', '-', $text);
    $text = strtolower($text);
    return empty($text) ? 'post-' . time() : $text;
}

// ── Load Announcement Ribbon ───────────────────────────────────────────────────
$ribbon = tn_get_setting('announcement_ribbon', [
    'enabled'   => true,
    'badge'     => 'OFFICIAL RELEASE',
    'text'      => 'High-Performance Systems & AI Language with Native Tensors, Coroutines & Zero-GC Execution!',
    'link_text' => 'Get TezzNative v1.0.0',
    'link_url'  => '/download/',
]);

// ── Handle POST Requests ───────────────────────────────────────────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $csrf   = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/cms.php');
        exit;
    }

    // 1. Save Announcement Ribbon
    if ($action === 'save_announcement') {
        $ribbon_data = [
            'enabled'   => !empty($_POST['banner_enabled']),
            'badge'     => trim((string)($_POST['banner_badge'] ?? 'OFFICIAL RELEASE')),
            'text'      => trim((string)($_POST['banner_text'] ?? '')),
            'link_text' => trim((string)($_POST['banner_link_text'] ?? 'Get TezzNative v1.0.0')),
            'link_url'  => trim((string)($_POST['banner_link_url'] ?? '/download/')),
        ];

        if (tn_set_setting('announcement_ribbon', $ribbon_data)) {
            tn_set_flash('success', 'Announcement ribbon settings updated successfully in database.');
        } else {
            tn_set_flash('error', 'Failed to update announcement settings.');
        }
        header('Location: /console/cms.php');
        exit;
    }

    // Helper for article image upload
    $handleArticleImageUpload = function(): ?string {
        if (!isset($_FILES['article_image']) || $_FILES['article_image']['error'] !== UPLOAD_ERR_OK) {
            return null;
        }
        $file = $_FILES['article_image'];
        $maxBytes = 6 * 1024 * 1024; // 6MB
        if ($file['size'] > $maxBytes) {
            return null;
        }
        $finfo = new finfo(FILEINFO_MIME_TYPE);
        $mime = $finfo->file($file['tmp_name']);
        $allowed = ['image/jpeg' => '.jpg', 'image/png' => '.png', 'image/webp' => '.webp', 'image/svg+xml' => '.svg'];
        if (!isset($allowed[$mime])) {
            return null;
        }
        $uploadDir = dirname(__DIR__) . '/uploads/articles';
        if (!is_dir($uploadDir)) {
            @mkdir($uploadDir, 0755, true);
        }
        $ext = $allowed[$mime];
        $filename = 'art_' . time() . '_' . bin2hex(random_bytes(6)) . $ext;
        $dest = $uploadDir . '/' . $filename;
        if (move_uploaded_file($file['tmp_name'], $dest)) {
            return '/uploads/articles/' . $filename;
        }
        return null;
    };

    // 2. Create Article (Blog or News)
    if ($action === 'create_article') {
        $type       = in_array($_POST['type'] ?? '', ['blog', 'news'], true) ? $_POST['type'] : 'blog';
        $title      = trim((string)($_POST['title'] ?? ''));
        $slug_input = trim((string)($_POST['slug'] ?? ''));
        $slug       = tn_slugify($slug_input !== '' ? $slug_input : $title);
        $category   = trim((string)($_POST['category'] ?? 'General'));
        $author     = trim((string)($_POST['author'] ?? 'Rohit Pathak'));
        $read_time  = trim((string)($_POST['read_time'] ?? '5 min'));
        $tags       = trim((string)($_POST['tags'] ?? ''));
        $status     = in_array($_POST['status'] ?? '', ['published', 'draft', 'archived'], true) ? $_POST['status'] : 'published';
        $featured   = !empty($_POST['featured']) ? 1 : 0;
        $excerpt    = trim((string)($_POST['excerpt'] ?? ''));
        $content    = trim((string)($_POST['content'] ?? ''));

        $uploadedImg = $handleArticleImageUpload();
        $imageUrl    = $uploadedImg ?: trim((string)($_POST['image_url'] ?? ''));

        if ($title === '') {
            tn_set_flash('error', 'Article title cannot be empty.');
            header('Location: /console/cms.php');
            exit;
        }

        try {
            // Check slug collision
            $stmt = $pdo->prepare("SELECT COUNT(*) FROM tn_articles WHERE slug = ?");
            $stmt->execute([$slug]);
            if ($stmt->fetchColumn() > 0) {
                $slug .= '-' . time();
            }

            $stmt = $pdo->prepare("
                INSERT INTO tn_articles 
                (`type`, `title`, `slug`, `category`, `image_url`, `excerpt`, `content`, `author`, `read_time`, `tags`, `status`, `featured`, `published_at`, `created_at`, `updated_at`)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW(), NOW())
            ");
            $stmt->execute([$type, $title, $slug, $category, $imageUrl ?: null, $excerpt, $content, $author, $read_time, $tags, $status, $featured]);
            tn_set_flash('success', ucfirst($type) . " article '{$title}' created successfully.");
        } catch (Throwable $e) {
            tn_set_flash('error', 'Database error: ' . $e->getMessage());
        }
        header('Location: /console/cms.php');
        exit;
    }

    // 3. Edit Article
    if ($action === 'edit_article') {
        $id         = (int)($_POST['article_id'] ?? 0);
        $type       = in_array($_POST['type'] ?? '', ['blog', 'news'], true) ? $_POST['type'] : 'blog';
        $title      = trim((string)($_POST['title'] ?? ''));
        $slug_input = trim((string)($_POST['slug'] ?? ''));
        $slug       = tn_slugify($slug_input !== '' ? $slug_input : $title);
        $category   = trim((string)($_POST['category'] ?? 'General'));
        $author     = trim((string)($_POST['author'] ?? 'Rohit Pathak'));
        $read_time  = trim((string)($_POST['read_time'] ?? '5 min'));
        $tags       = trim((string)($_POST['tags'] ?? ''));
        $status     = in_array($_POST['status'] ?? '', ['published', 'draft', 'archived'], true) ? $_POST['status'] : 'published';
        $featured   = !empty($_POST['featured']) ? 1 : 0;
        $excerpt    = trim((string)($_POST['excerpt'] ?? ''));
        $content    = trim((string)($_POST['content'] ?? ''));

        $uploadedImg = $handleArticleImageUpload();
        $manualUrl   = trim((string)($_POST['image_url'] ?? ''));

        if ($id <= 0 || $title === '') {
            tn_set_flash('error', 'Invalid article ID or title.');
            header('Location: /console/cms.php');
            exit;
        }

        try {
            // Check slug uniqueness excluding this article
            $stmt = $pdo->prepare("SELECT COUNT(*) FROM tn_articles WHERE slug = ? AND id != ?");
            $stmt->execute([$slug, $id]);
            if ($stmt->fetchColumn() > 0) {
                $slug .= '-' . $id;
            }

            if ($uploadedImg) {
                $stmt = $pdo->prepare("
                    UPDATE tn_articles 
                    SET `type` = ?, `title` = ?, `slug` = ?, `category` = ?, `image_url` = ?, `excerpt` = ?, `content` = ?, `author` = ?, `read_time` = ?, `tags` = ?, `status` = ?, `featured` = ?, `updated_at` = NOW()
                    WHERE `id` = ?
                ");
                $stmt->execute([$type, $title, $slug, $category, $uploadedImg, $excerpt, $content, $author, $read_time, $tags, $status, $featured, $id]);
            } else {
                $stmt = $pdo->prepare("
                    UPDATE tn_articles 
                    SET `type` = ?, `title` = ?, `slug` = ?, `category` = ?, `image_url` = COALESCE(NULLIF(?, ''), `image_url`), `excerpt` = ?, `content` = ?, `author` = ?, `read_time` = ?, `tags` = ?, `status` = ?, `featured` = ?, `updated_at` = NOW()
                    WHERE `id` = ?
                ");
                $stmt->execute([$type, $title, $slug, $category, $manualUrl, $excerpt, $content, $author, $read_time, $tags, $status, $featured, $id]);
            }
            tn_set_flash('success', "Article #{$id} '{$title}' updated successfully.");
        } catch (Throwable $e) {
            tn_set_flash('error', 'Database error: ' . $e->getMessage());
        }
        header('Location: /console/cms.php');
        exit;
    }
        exit;
    }

    // 4. Toggle Status (published <-> draft)
    if ($action === 'toggle_status') {
        $id = (int)($_POST['article_id'] ?? 0);
        if ($id > 0) {
            try {
                $stmt = $pdo->prepare("SELECT status FROM tn_articles WHERE id = ?");
                $stmt->execute([$id]);
                $curr = $stmt->fetchColumn();
                $next = ($curr === 'published') ? 'draft' : 'published';
                $stmt = $pdo->prepare("UPDATE tn_articles SET status = ?, updated_at = NOW() WHERE id = ?");
                $stmt->execute([$next, $id]);
                tn_set_flash('success', "Article #{$id} status changed to {$next}.");
            } catch (Throwable $e) {
                tn_set_flash('error', 'Database error: ' . $e->getMessage());
            }
        }
        header('Location: /console/cms.php');
        exit;
    }

    // 5. Delete Article
    if ($action === 'delete_article') {
        $id = (int)($_POST['article_id'] ?? 0);
        if ($id > 0) {
            try {
                $stmt = $pdo->prepare("DELETE FROM tn_articles WHERE id = ?");
                $stmt->execute([$id]);
                tn_set_flash('success', "Article #{$id} deleted permanently.");
            } catch (Throwable $e) {
                tn_set_flash('error', 'Database error: ' . $e->getMessage());
            }
        }
        header('Location: /console/cms.php');
        exit;
    }
}

// ── Fetch Articles & KPI Stats ────────────────────────────────────────────────
$stats = [
    'total'     => 0,
    'blogs'     => 0,
    'news'      => 0,
    'published' => 0,
    'drafts'    => 0,
];
$articles = [];

try {
    $stats['total']     = (int)$pdo->query("SELECT COUNT(*) FROM tn_articles")->fetchColumn();
    $stats['blogs']     = (int)$pdo->query("SELECT COUNT(*) FROM tn_articles WHERE `type` = 'blog' AND `status` = 'published'")->fetchColumn();
    $stats['news']      = (int)$pdo->query("SELECT COUNT(*) FROM tn_articles WHERE `type` = 'news' AND `status` = 'published'")->fetchColumn();
    $stats['drafts']    = (int)$pdo->query("SELECT COUNT(*) FROM tn_articles WHERE `status` = 'draft'")->fetchColumn();

    $stmt = $pdo->query("SELECT * FROM tn_articles ORDER BY published_at DESC, id DESC");
    $articles = $stmt->fetchAll();
} catch (Throwable $e) {
    // If DB is temporarily unavailable
}

require_once __DIR__ . '/includes/header.php';
?>

<!-- KPI Overview Grid -->
<div class="kpi-grid" style="margin-bottom: 24px;">
  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Total Articles</span>
      <div class="kpi-icon-badge" style="color: #38bdf8;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path><polyline points="14 2 14 8 20 8"></polyline><line x1="16" y1="13" x2="8" y2="13"></line><line x1="16" y1="17" x2="8" y2="17"></line><polyline points="10 9 9 9 8 9"></polyline></svg>
      </div>
    </div>
    <div class="kpi-value"><?= number_format($stats['total']) ?></div>
    <div class="kpi-footer">Across Blog &amp; News feeds</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Published Blog Posts</span>
      <div class="kpi-icon-badge" style="color: #10b981;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"></path><path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"></path></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #10b981;"><?= number_format($stats['blogs']) ?></div>
    <div class="kpi-footer">Live on <code>tezznative.org/blog/</code></div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Published News Items</span>
      <div class="kpi-icon-badge" style="color: #f59e0b;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 22h16a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2H8a2 2 0 0 0-2 2v16a2 2 0 0 1-2 2Zm0 0a2 2 0 0 1-2-2v-9c0-1.1.9-2 2-2h2"></path><path d="M18 14h-8"></path><path d="M15 18h-5"></path><path d="M10 6h8v4h-8V6Z"></path></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #f59e0b;"><?= number_format($stats['news']) ?></div>
    <div class="kpi-footer">Live on <code>tezznative.org/news</code></div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Drafts &amp; In-Review</span>
      <div class="kpi-icon-badge" style="color: #94a3b8;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #94a3b8;"><?= number_format($stats['drafts']) ?></div>
    <div class="kpi-footer">Private staging content</div>
  </div>
</div>

<!-- Announcement Ribbon Live Manager -->
<div class="panel" style="margin-bottom: 24px;">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Homepage Announcement Ribbon</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Top banner displayed across public headers on <code>tezznative.org</code>. Stored directly in MariaDB <code>tn_settings</code>.</p>
    </div>
  </div>

  <div style="background: rgba(15, 23, 42, 0.85); border: 1px solid rgba(56, 189, 248, 0.25); border-radius: 8px; padding: 14px 18px; margin-bottom: 20px;">
    <div style="font-size: 0.72rem; text-transform: uppercase; letter-spacing: 0.8px; color: #64748b; margin-bottom: 8px; font-weight: 700;">Live Ribbon Preview (Day / Night Adaptive)</div>
    <div style="background: linear-gradient(90deg, rgba(0, 210, 255, 0.12) 0%, rgba(14, 165, 233, 0.16) 50%, rgba(37, 99, 235, 0.12) 100%); border: 1px solid rgba(56, 189, 248, 0.35); border-radius: 6px; padding: 10px 16px; display: flex; align-items: center; justify-content: center; gap: 10px; flex-wrap: wrap; text-align: center;">
      <span style="background: linear-gradient(135deg, #00d2ff, #0284c7); color: #020617; font-size: 0.68rem; font-weight: 800; padding: 2px 8px; border-radius: 9999px; letter-spacing: 0.5px;"><?= htmlspecialchars($ribbon['badge'] ?? 'OFFICIAL RELEASE') ?></span>
      <span style="font-size: 0.85rem; color: #f1f5f9; font-weight: 500;"><?= htmlspecialchars($ribbon['text'] ?? '') ?></span>
      <span style="font-size: 0.85rem; color: #38bdf8; font-weight: 600; text-decoration: underline;"><?= htmlspecialchars($ribbon['link_text'] ?? 'Get TezzNative v1.0.0') ?> &rarr;</span>
    </div>
    <?php if (empty($ribbon['enabled'])): ?>
      <div style="margin-top: 8px; font-size: 0.75rem; color: #f59e0b; font-weight: 600;">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="vertical-align: -2px; margin-right: 4px;"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>
        Notice: The ribbon is currently toggled OFF and will not display publicly.
      </div>
    <?php endif; ?>
  </div>

  <form method="POST">
    <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
    <input type="hidden" name="action" value="save_announcement">

    <div class="form-group" style="margin-bottom: 16px;">
      <label style="display: flex; align-items: center; gap: 8px; font-size: 0.9rem; color: #f8fafc; font-weight: 600; cursor: pointer;">
        <input type="checkbox" name="banner_enabled" value="1" <?= !empty($ribbon['enabled']) ? 'checked' : '' ?>>
        Enable Announcement Banner on Public Header
      </label>
    </div>

    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 16px; margin-bottom: 16px;">
      <div class="form-group">
        <label class="form-label" for="banner_badge">Ribbon Pill / Badge *</label>
        <input type="text" id="banner_badge" name="banner_badge" class="form-control" value="<?= htmlspecialchars($ribbon['badge'] ?? '') ?>" placeholder="e.g. OFFICIAL RELEASE" required>
      </div>

      <div class="form-group">
        <label class="form-label" for="banner_link_text">Call-to-Action Link Text *</label>
        <input type="text" id="banner_link_text" name="banner_link_text" class="form-control" value="<?= htmlspecialchars($ribbon['link_text'] ?? '') ?>" placeholder="e.g. Get TezzNative v1.0.0" required>
      </div>

      <div class="form-group">
        <label class="form-label" for="banner_link_url">Target Destination URL *</label>
        <input type="text" id="banner_link_url" name="banner_link_url" class="form-control" value="<?= htmlspecialchars($ribbon['link_url'] ?? '') ?>" placeholder="e.g. /download/" required>
      </div>
    </div>

    <div class="form-group" style="margin-bottom: 20px;">
      <label class="form-label" for="banner_text">Banner Announcement Message *</label>
      <input type="text" id="banner_text" name="banner_text" class="form-control" value="<?= htmlspecialchars($ribbon['text'] ?? '') ?>" placeholder="e.g. High-Performance Systems & AI Language with Native Tensors, Coroutines & Zero-GC Execution!" required>
    </div>

    <button type="submit" class="btn btn-primary" style="display: inline-flex; align-items: center; gap: 8px;">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"></path><polyline points="17 21 17 13 7 13 7 21"></polyline><polyline points="7 3 7 8 15 8"></polyline></svg>
      Save Ribbon Settings
    </button>
  </form>
</div>

<!-- Content & Articles Manager (Blog & News) -->
<div class="panel">
  <div class="panel-header" style="flex-wrap: wrap; gap: 16px;">
    <div>
      <h3 class="panel-title">Articles, Blog Posts &amp; Releases</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Create, publish, edit and draft articles across <code>/blog/</code> and <code>/news.php</code>.</p>
    </div>

    <div class="panel-actions" style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">
      <div style="position: relative; min-width: 200px;">
        <input type="text" id="articleFilterInput" class="form-control" placeholder="Filter articles..." style="padding-left: 32px; font-size: 0.84rem;">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#64748b" stroke-width="2.5" style="position: absolute; left: 10px; top: 50%; transform: translateY(-50%); pointer-events: none;"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
      </div>
      <button type="button" class="btn btn-primary" onclick="openNewArticleModal()" style="display: inline-flex; align-items: center; gap: 6px;">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
        New Article
      </button>
    </div>
  </div>

  <!-- Type & Status Filter Buttons -->
  <div style="display: flex; gap: 8px; margin-bottom: 16px; flex-wrap: wrap;">
    <button type="button" class="btn btn-sm btn-outline active-filter-btn" data-filter="all" onclick="filterArticles('all', this)" style="background: rgba(14, 165, 233, 0.15); border-color: #38bdf8; color: #38bdf8;">All Articles (<?= count($articles) ?>)</button>
    <button type="button" class="btn btn-sm btn-outline" data-filter="blog" onclick="filterArticles('blog', this)">Blog Posts (<?= $stats['blogs'] ?>)</button>
    <button type="button" class="btn btn-sm btn-outline" data-filter="news" onclick="filterArticles('news', this)">News Releases (<?= $stats['news'] ?>)</button>
    <button type="button" class="btn btn-sm btn-outline" data-filter="draft" onclick="filterArticles('draft', this)">Drafts (<?= $stats['drafts'] ?>)</button>
  </div>

  <div class="table-responsive">
    <table class="data-table" id="articlesTable">
      <thead>
        <tr>
          <th style="width: 50px;">ID</th>
          <th style="width: 80px;">Type</th>
          <th>Title &amp; Slug</th>
          <th>Category</th>
          <th>Author</th>
          <th>Status</th>
          <th>Date</th>
          <th style="text-align: right; width: 180px;">Actions</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($articles)): ?>
          <tr><td colspan="8" style="text-align: center; color: #64748b; padding: 32px;">No articles found in <code>tn_articles</code>.</td></tr>
        <?php else: ?>
          <?php foreach ($articles as $art): ?>
          <tr class="article-row" data-type="<?= htmlspecialchars($art['type']) ?>" data-status="<?= htmlspecialchars($art['status']) ?>">
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.8rem; color: #64748b;">#<?= $art['id'] ?></td>
            <td>
              <?php if ($art['type'] === 'blog'): ?>
                <span class="badge" style="background: rgba(14, 165, 233, 0.15); color: #38bdf8; font-size: 0.72rem; text-transform: uppercase; font-weight: 700;">Blog</span>
              <?php else: ?>
                <span class="badge" style="background: rgba(245, 158, 11, 0.15); color: #f59e0b; font-size: 0.72rem; text-transform: uppercase; font-weight: 700;">News</span>
              <?php endif; ?>
            </td>
            <td>
              <div style="font-weight: 700; color: #f8fafc; font-size: 0.9rem; margin-bottom: 2px;">
                <?= htmlspecialchars($art['title']) ?>
                <?php if (!empty($art['featured'])): ?>
                  <span class="badge" style="background: rgba(168, 85, 247, 0.2); color: #c084fc; font-size: 0.65rem; margin-left: 4px;">Pinned</span>
                <?php endif; ?>
              </div>
              <div style="font-size: 0.75rem; color: #64748b; font-family: 'JetBrains Mono', monospace;">
                /<?= $art['type'] === 'blog' ? 'blog/' : 'news.php#' ?><?= htmlspecialchars($art['slug']) ?>
              </div>
            </td>
            <td>
              <span class="badge badge-info" style="font-size: 0.75rem;"><?= htmlspecialchars($art['category']) ?></span>
            </td>
            <td style="font-size: 0.82rem; color: #94a3b8; white-space: nowrap;">
              <?= htmlspecialchars($art['author']) ?>
            </td>
            <td>
              <form method="POST" style="margin: 0; display: inline;">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="toggle_status">
                <input type="hidden" name="article_id" value="<?= $art['id'] ?>">
                <?php if ($art['status'] === 'published'): ?>
                  <button type="submit" class="badge" style="background: rgba(16, 185, 129, 0.2); color: #10b981; border: none; cursor: pointer; font-size: 0.72rem; font-weight: 700;" title="Click to unpublish">Published &bull;</button>
                <?php else: ?>
                  <button type="submit" class="badge" style="background: rgba(148, 163, 184, 0.2); color: #94a3b8; border: none; cursor: pointer; font-size: 0.72rem; font-weight: 700;" title="Click to publish">Draft &bull;</button>
                <?php endif; ?>
              </form>
            </td>
            <td style="font-size: 0.8rem; color: #94a3b8; white-space: nowrap;">
              <?= date('M d, Y', strtotime($art['published_at'] ?? $art['created_at'])) ?>
            </td>
            <td style="text-align: right; white-space: nowrap;">
              <div style="display: inline-flex; align-items: center; gap: 6px;">
                <button type="button" class="btn btn-secondary" style="padding: 4px 8px; font-size: 0.74rem;" 
                  onclick='openEditArticleModal(<?= json_encode($art, JSON_HEX_TAG | JSON_HEX_APOS | JSON_HEX_QUOT | JSON_HEX_AMP) ?>)'>
                  Edit
                </button>
                <form method="POST" style="margin: 0; display: inline;" onsubmit="return confirm('Delete article #<?= $art['id'] ?> (<?= htmlspecialchars(addslashes($art['title'])) ?>)?');">
                  <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                  <input type="hidden" name="action" value="delete_article">
                  <input type="hidden" name="article_id" value="<?= $art['id'] ?>">
                  <button type="submit" class="btn btn-danger" style="padding: 4px 8px; font-size: 0.74rem;">Delete</button>
                </form>
              </div>
            </td>
          </tr>
          <?php endforeach; ?>
        <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<!-- Create / Edit Article Modal -->
<div class="modal-backdrop" id="articleModal">
  <div class="modal-card" style="max-width: 720px;">
    <div class="modal-header">
      <h3 class="modal-title" id="articleModalTitle">Create New Article</h3>
      <button type="button" class="modal-close" onclick="closeModal('articleModal')">&times;</button>
    </div>
    <form method="POST" id="articleForm" enctype="multipart/form-data">
      <div class="modal-body">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" id="articleFormAction" value="create_article">
        <input type="hidden" name="article_id" id="articleFormId" value="0">

        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-bottom: 14px;">
          <div class="form-group">
            <label class="form-label" for="art_type">Content Type *</label>
            <select id="art_type" name="type" class="form-control" required>
              <option value="blog">Engineering Blog (/blog/)</option>
              <option value="news">News &amp; Releases (/news.php)</option>
            </select>
          </div>

          <div class="form-group">
            <label class="form-label" for="art_category">Category *</label>
            <input type="text" id="art_category" name="category" class="form-control" placeholder="e.g. Language, AI / ML, Release" required>
          </div>
        </div>

        <div class="form-group" style="margin-bottom: 14px;">
          <label class="form-label" for="art_title">Headline / Title *</label>
          <input type="text" id="art_title" name="title" class="form-control" placeholder="e.g. Introducing TezzNative v1.0 — A New Era of Systems Programming" required oninput="autoSlug()">
        </div>

        <div class="form-group" style="margin-bottom: 14px;">
          <label class="form-label" for="art_slug">URL Slug *</label>
          <input type="text" id="art_slug" name="slug" class="form-control" placeholder="e.g. tezznative-v1-launch" required style="font-family: 'JetBrains Mono', monospace; font-size: 0.85rem;">
        </div>

        <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 14px; margin-bottom: 14px;">
          <div class="form-group">
            <label class="form-label" for="art_author">Author</label>
            <input type="text" id="art_author" name="author" class="form-control" value="Rohit Pathak" required>
          </div>
          <div class="form-group">
            <label class="form-label" for="art_read_time">Read Time</label>
            <input type="text" id="art_read_time" name="read_time" class="form-control" value="5 min">
          </div>
          <div class="form-group">
            <label class="form-label" for="art_status">Publishing Status</label>
            <select id="art_status" name="status" class="form-control">
              <option value="published">Published</option>
              <option value="draft">Draft / Hidden</option>
              <option value="archived">Archived</option>
            </select>
          </div>
        </div>

        <!-- Banner Image Upload / Remote URL -->
        <div style="background: rgba(14, 165, 233, 0.04); border: 1px solid var(--c-border-subtle); border-radius: 12px; padding: 16px; margin-bottom: 14px;">
          <label class="form-label" style="margin-bottom: 8px;">Featured Banner Image (Blog &amp; News)</label>
          <div style="display: flex; align-items: center; gap: 16px; margin-bottom: 10px;">
            <div id="art_image_preview" style="width: 88px; height: 56px; border-radius: 8px; overflow: hidden; background: #0f172a; border: 1.5px solid var(--c-primary); display: flex; align-items: center; justify-content: center; font-size: 0.72rem; color: #64748b; flex-shrink: 0;">
              No image
            </div>
            <div style="flex: 1;">
              <input type="file" name="article_image" id="art_image_file" accept="image/*" class="form-control" style="padding: 6px;" onchange="previewArticleImage(this)">
              <div style="font-size: 0.74rem; color: var(--c-text-tertiary); margin-top: 4px;">Upload JPG, PNG, or WebP file (Max 6MB)</div>
            </div>
          </div>
          <div>
            <label class="form-label" style="font-size: 0.78rem;">Or Remote Image URL</label>
            <input type="url" id="art_image_url" name="image_url" class="form-control" placeholder="https://..." oninput="previewArticleUrl(this.value)">
          </div>
        </div>

        <div class="form-group" style="margin-bottom: 14px;">
          <label class="form-label" for="art_tags">Tags (comma-separated)</label>
          <input type="text" id="art_tags" name="tags" class="form-control" placeholder="e.g. systems, compiler, v1.0.0, ai">
        </div>

        <div class="form-group" style="margin-bottom: 14px;">
          <label class="form-label" for="art_excerpt">Summary / Lead Excerpt *</label>
          <textarea id="art_excerpt" name="excerpt" class="form-control" rows="2" placeholder="Brief 1-2 sentence preview shown in feeds and cards..." required></textarea>
        </div>

        <div class="form-group" style="margin-bottom: 14px;">
          <label class="form-label" for="art_content">Full Article Content (Markdown / HTML) *</label>
          <textarea id="art_content" name="content" class="form-control" rows="8" placeholder="# Article Title&#10;&#10;Write markdown or HTML content here..." required style="font-family: 'JetBrains Mono', monospace; font-size: 0.85rem; line-height: 1.5;"></textarea>
        </div>

        <div class="form-group">
          <label style="display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: #f8fafc; cursor: pointer;">
            <input type="checkbox" id="art_featured" name="featured" value="1">
            Pin as Featured / Highlighted Article
          </label>
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-outline" onclick="closeModal('articleModal')">Cancel</button>
        <button type="submit" class="btn btn-primary" id="articleSubmitBtn">Publish Article</button>
      </div>
    </form>
  </div>
</div>

<script>
let isManualSlug = false;

function autoSlug() {
  if (isManualSlug) return;
  const title = document.getElementById('art_title').value;
  const slug = title.toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, '')
    .replace(/[\s_-]+/g, '-')
    .replace(/^-+|-+$/g, '');
  document.getElementById('art_slug').value = slug;
}

document.getElementById('art_slug').addEventListener('input', function() {
  isManualSlug = this.value.length > 0;
});

function openNewArticleModal() {
  document.getElementById('articleForm').reset();
  document.getElementById('articleModalTitle').innerText = 'Create New Article';
  document.getElementById('articleFormAction').value = 'create_article';
  document.getElementById('articleFormId').value = '0';
  document.getElementById('articleSubmitBtn').innerText = 'Publish Article';
  document.getElementById('art_author').value = 'Rohit Pathak';
  document.getElementById('art_read_time').value = '5 min';
  document.getElementById('art_status').value = 'published';
  document.getElementById('art_image_url').value = '';
  document.getElementById('art_image_file').value = '';
  document.getElementById('art_image_preview').innerHTML = 'No image';
  isManualSlug = false;
  openModal('articleModal');
}

function openEditArticleModal(data) {
  document.getElementById('articleModalTitle').innerText = 'Edit Article #' + data.id;
  document.getElementById('articleFormAction').value = 'edit_article';
  document.getElementById('articleFormId').value = data.id;
  document.getElementById('articleSubmitBtn').innerText = 'Save Changes';
  
  document.getElementById('art_type').value = data.type || 'blog';
  document.getElementById('art_category').value = data.category || '';
  document.getElementById('art_title').value = data.title || '';
  document.getElementById('art_slug').value = data.slug || '';
  document.getElementById('art_author').value = data.author || 'Rohit Pathak';
  document.getElementById('art_read_time').value = data.read_time || '5 min';
  document.getElementById('art_status').value = data.status || 'published';
  document.getElementById('art_tags').value = data.tags || '';
  document.getElementById('art_excerpt').value = data.excerpt || '';
  document.getElementById('art_content').value = data.content || '';
  document.getElementById('art_featured').checked = (parseInt(data.featured) === 1);
  
  document.getElementById('art_image_url').value = data.image_url || '';
  document.getElementById('art_image_file').value = '';
  var prev = document.getElementById('art_image_preview');
  if (data.image_url) {
    prev.innerHTML = '<img src="' + data.image_url + '" style="width: 100%; height: 100%; object-fit: cover;">';
  } else {
    prev.innerHTML = 'No image';
  }

  isManualSlug = true;
  openModal('articleModal');
}

function previewArticleImage(input) {
  if (input.files && input.files[0]) {
    var reader = new FileReader();
    reader.onload = function(e) {
      document.getElementById('art_image_preview').innerHTML = '<img src="' + e.target.result + '" style="width: 100%; height: 100%; object-fit: cover;">';
    };
    reader.readAsDataURL(input.files[0]);
  }
}

function previewArticleUrl(url) {
  var prev = document.getElementById('art_image_preview');
  if (url && url.length > 5) {
    prev.innerHTML = '<img src="' + url + '" style="width: 100%; height: 100%; object-fit: cover;" onerror="this.parentNode.innerHTML=\'Invalid\'">';
  } else {
    prev.innerHTML = 'No image';
  }
}

// Client-side filtering by tab
function filterArticles(filter, btn) {
  document.querySelectorAll('[data-filter]').forEach(b => {
    b.style.background = '';
    b.style.borderColor = '';
    b.style.color = '';
  });
  btn.style.background = 'rgba(14, 165, 233, 0.15)';
  btn.style.borderColor = '#38bdf8';
  btn.style.color = '#38bdf8';

  const rows = document.querySelectorAll('.article-row');
  rows.forEach(row => {
    const type = row.dataset.type;
    const status = row.dataset.status;
    let show = true;
    if (filter === 'blog') show = (type === 'blog');
    else if (filter === 'news') show = (type === 'news');
    else if (filter === 'draft') show = (status === 'draft');
    row.style.display = show ? '' : 'none';
  });
}

// Live search in table
const searchIn = document.getElementById('articleFilterInput');
if (searchIn) {
  searchIn.addEventListener('input', function() {
    const q = this.value.toLowerCase().trim();
    const rows = document.querySelectorAll('.article-row');
    rows.forEach(row => {
      const text = row.innerText.toLowerCase();
      row.style.display = text.includes(q) ? '' : 'none';
    });
  });
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
