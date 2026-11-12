<?php
declare(strict_types=1);

/**
 * console/cms.php — TezzNative Content & Announcement Management
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'cms';
$console_title = 'Announcement & CMS Management';

$settings_file = __DIR__ . '/../config/site_settings.json';

// Helper to load settings
function get_site_settings(string $path): array {
    if (is_file($path)) {
        $raw = @file_get_contents($path);
        if ($raw) {
            $data = json_decode($raw, true);
            if (is_array($data)) return $data;
        }
    }
    return [
        'announcement' => [
            'enabled'   => true,
            'badge'     => 'OFFICIAL RELEASE',
            'text'      => 'High-Performance Systems & AI Language with Native Tensors, Coroutines & Zero-GC Execution!',
            'link_text' => 'Get TezzNative v1.0.0',
            'link_url'  => '/download/',
        ],
        'site_version' => '1.0.0',
        'company_name' => 'TEZZCORP PRIVATE LIMITED',
        'news'         => [],
    ];
}

$settings = get_site_settings($settings_file);

// ── Handle Actions ────────────────────────────────────────────────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $csrf = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/cms.php');
        exit;
    }

    if ($action === 'save_announcement') {
        $settings['announcement'] = [
            'enabled'   => !empty($_POST['banner_enabled']),
            'badge'     => trim((string)($_POST['banner_badge'] ?? 'OFFICIAL RELEASE')),
            'text'      => trim((string)($_POST['banner_text'] ?? '')),
            'link_text' => trim((string)($_POST['banner_link_text'] ?? 'Get TezzNative v1.0.0')),
            'link_url'  => trim((string)($_POST['banner_link_url'] ?? '/download/')),
        ];

        file_put_contents($settings_file, json_encode($settings, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
        tn_set_flash('success', 'Announcement ribbon settings saved successfully.');
        header('Location: /console/cms.php');
        exit;
    }

    if ($action === 'add_news') {
        $title = trim((string)($_POST['title'] ?? ''));
        $category = trim((string)($_POST['category'] ?? 'Release'));
        $summary = trim((string)($_POST['summary'] ?? ''));
        $date = trim((string)($_POST['date'] ?? date('Y-m-d')));

        if ($title !== '') {
            $new_id = time();
            $settings['news'][] = [
                'id'       => $new_id,
                'title'    => $title,
                'category' => $category,
                'date'     => $date,
                'summary'  => $summary,
            ];
            file_put_contents($settings_file, json_encode($settings, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
            tn_set_flash('success', "News article '{$title}' published.");
        }
        header('Location: /console/cms.php');
        exit;
    }

    if ($action === 'delete_news') {
        $del_id = (int)($_POST['news_id'] ?? 0);
        $settings['news'] = array_values(array_filter($settings['news'] ?? [], fn($n) => (int)($n['id'] ?? 0) !== $del_id));
        file_put_contents($settings_file, json_encode($settings, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
        tn_set_flash('success', "News item #{$del_id} deleted.");
        header('Location: /console/cms.php');
        exit;
    }
}

require_once __DIR__ . '/includes/header.php';
$banner = $settings['announcement'] ?? [];
?>

<!-- Announcement Ribbon Live Preview -->
<div class="panel" style="margin-bottom: 24px;">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Homepage Announcement Ribbon</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Top banner displayed across all public pages on <code>tezznative.org</code>.</p>
    </div>
  </div>

  <div style="background: rgba(15, 23, 42, 0.85); border: 1px solid rgba(56, 189, 248, 0.25); border-radius: 8px; padding: 14px 18px; margin-bottom: 20px;">
    <div style="font-size: 0.72rem; text-transform: uppercase; letter-spacing: 0.8px; color: #64748b; margin-bottom: 8px; font-weight: 700;">Live Ribbon Preview (Day / Night Adaptive)</div>
    <div style="background: linear-gradient(90deg, rgba(0, 210, 255, 0.12) 0%, rgba(14, 165, 233, 0.16) 50%, rgba(37, 99, 235, 0.12) 100%); border: 1px solid rgba(56, 189, 248, 0.35); border-radius: 6px; padding: 10px 16px; display: flex; align-items: center; justify-content: center; gap: 10px; flex-wrap: wrap; text-align: center;">
      <span style="background: linear-gradient(135deg, #00d2ff, #0284c7); color: #020617; font-size: 0.68rem; font-weight: 800; padding: 2px 8px; border-radius: 9999px; letter-spacing: 0.5px;"><?= htmlspecialchars($banner['badge'] ?? 'OFFICIAL RELEASE') ?></span>
      <span style="font-size: 0.85rem; color: #f1f5f9; font-weight: 500;"><?= htmlspecialchars($banner['text'] ?? '') ?></span>
      <span style="font-size: 0.85rem; color: #38bdf8; font-weight: 600; text-decoration: underline;"><?= htmlspecialchars($banner['link_text'] ?? 'Get TezzNative v1.0.0') ?> &rarr;</span>
    </div>
    <?php if (empty($banner['enabled'])): ?>
      <div style="margin-top: 8px; font-size: 0.75rem; color: #f59e0b; font-weight: 600;">⚠️ Notice: The ribbon is currently toggled OFF and will not display publicly.</div>
    <?php endif; ?>
  </div>

  <form method="POST">
    <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
    <input type="hidden" name="action" value="save_announcement">

    <div class="form-group">
      <label style="display: flex; align-items: center; gap: 8px; font-size: 0.9rem; color: #f8fafc; font-weight: 600; cursor: pointer;">
        <input type="checkbox" name="banner_enabled" value="1" <?= !empty($banner['enabled']) ? 'checked' : '' ?>>
        Enable Announcement Banner on Public Header
      </label>
    </div>

    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 16px;">
      <div class="form-group">
        <label class="form-label" for="banner_badge">Ribbon Pill / Badge *</label>
        <input type="text" id="banner_badge" name="banner_badge" class="form-control" value="<?= htmlspecialchars($banner['badge'] ?? '') ?>" placeholder="e.g. OFFICIAL RELEASE" required>
      </div>

      <div class="form-group">
        <label class="form-label" for="banner_link_text">Call-to-Action Link Text *</label>
        <input type="text" id="banner_link_text" name="banner_link_text" class="form-control" value="<?= htmlspecialchars($banner['link_text'] ?? '') ?>" placeholder="e.g. Get TezzNative v1.0.0" required>
      </div>

      <div class="form-group">
        <label class="form-label" for="banner_link_url">Target Destination URL *</label>
        <input type="text" id="banner_link_url" name="banner_link_url" class="form-control" value="<?= htmlspecialchars($banner['link_url'] ?? '') ?>" placeholder="e.g. /download/" required>
      </div>
    </div>

    <div class="form-group">
      <label class="form-label" for="banner_text">Banner Announcement Message *</label>
      <input type="text" id="banner_text" name="banner_text" class="form-control" value="<?= htmlspecialchars($banner['text'] ?? '') ?>" placeholder="e.g. High-Performance Systems & AI Language with Native Tensors, Coroutines & Zero-GC Execution!" required>
    </div>

    <button type="submit" class="btn btn-primary">Save Announcement Settings</button>
  </form>
</div>

<!-- News & Announcements Panel -->
<div class="panel">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Published News &amp; Updates</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Articles appearing on community news feeds and release notes.</p>
    </div>
    <button type="button" class="btn btn-primary" onclick="openModal('addNewsModal')">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
      Publish News
    </button>
  </div>

  <div class="table-responsive">
    <table class="data-table">
      <thead>
        <tr>
          <th>ID</th>
          <th>Article Title</th>
          <th>Category</th>
          <th>Date</th>
          <th>Summary</th>
          <th>Action</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($settings['news'])): ?>
          <tr><td colspan="6" style="text-align: center; color: #64748b; padding: 24px;">No news items published yet.</td></tr>
        <?php else: ?>
          <?php foreach ($settings['news'] as $n): ?>
          <tr>
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.8rem; color: #64748b;">#<?= $n['id'] ?></td>
            <td style="font-weight: 700; color: #f8fafc; font-size: 0.9rem;"><?= htmlspecialchars($n['title']) ?></td>
            <td><span class="badge badge-info"><?= htmlspecialchars($n['category']) ?></span></td>
            <td style="font-size: 0.8rem; color: #94a3b8; white-space: nowrap;"><?= htmlspecialchars($n['date']) ?></td>
            <td style="font-size: 0.82rem; color: #cbd5e1; max-width: 340px;"><?= htmlspecialchars($n['summary']) ?></td>
            <td>
              <form method="POST" style="margin: 0; display: inline;" onsubmit="return confirm('Delete article #<?= $n['id'] ?>?');">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="delete_news">
                <input type="hidden" name="news_id" value="<?= $n['id'] ?>">
                <button type="submit" class="btn btn-danger" style="padding: 3px 8px; font-size: 0.72rem;">Delete</button>
              </form>
            </td>
          </tr>
          <?php endforeach; ?>
        <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<!-- Add News Modal -->
<div class="modal-backdrop" id="addNewsModal">
  <div class="modal-card">
    <div class="modal-header">
      <h3 class="modal-title">Publish News Article</h3>
      <button type="button" class="modal-close" onclick="closeModal('addNewsModal')">&times;</button>
    </div>
    <form method="POST">
      <div class="modal-body">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" value="add_news">

        <div class="form-group">
          <label class="form-label" for="news_title">Headline / Title *</label>
          <input type="text" id="news_title" name="title" class="form-control" placeholder="e.g. TezzNative v1.0.0 Production Release" required>
        </div>

        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
          <div class="form-group">
            <label class="form-label" for="news_category">Category</label>
            <select id="news_category" name="category" class="form-control">
              <option value="Release">Release</option>
              <option value="Ecosystem">Ecosystem</option>
              <option value="Security">Security</option>
              <option value="Community">Community</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label" for="news_date">Publish Date</label>
            <input type="date" id="news_date" name="date" class="form-control" value="<?= date('Y-m-d') ?>">
          </div>
        </div>

        <div class="form-group">
          <label class="form-label" for="news_summary">Summary / Lead Text *</label>
          <textarea id="news_summary" name="summary" class="form-control" rows="3" placeholder="Brief 1-2 sentence description..." required></textarea>
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-outline" onclick="closeModal('addNewsModal')">Cancel</button>
        <button type="submit" class="btn btn-primary">Publish Article</button>
      </div>
    </form>
  </div>
</div>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
