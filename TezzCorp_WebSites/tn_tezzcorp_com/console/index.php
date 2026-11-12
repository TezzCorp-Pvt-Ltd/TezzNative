<?php
declare(strict_types=1);

/**
 * console/index.php — TezzNative Main Admin Dashboard Overview
 */

$active_nav = 'dashboard';
$console_title = 'System Overview';

require_once __DIR__ . '/includes/header.php';

// Fetch stats
$total_packages = 67;
$total_downloads = 0;
$top_packages = [];
$total_users = 0;
$total_posts = 0;

try {
    $pdo = tn_pdo();
    $total_packages = (int)$pdo->query("SELECT count(*) FROM tn_package_registry")->fetchColumn();
    $total_downloads = (int)$pdo->query("SELECT COALESCE(SUM(downloads), 0) FROM tn_package_registry")->fetchColumn();
    
    $stmt = $pdo->query("SELECT id, name, version, category, downloads, updated_at FROM tn_package_registry ORDER BY downloads DESC LIMIT 5");
    $top_packages = $stmt->fetchAll(PDO::FETCH_ASSOC);

    $tables = $pdo->query("SHOW TABLES")->fetchAll(PDO::FETCH_COLUMN);
    if (in_array('tn_community_users', $tables)) {
        $total_users = (int)$pdo->query("SELECT count(*) FROM tn_community_users")->fetchColumn();
    }
    if (in_array('tn_community_posts', $tables)) {
        $total_posts = (int)$pdo->query("SELECT count(*) FROM tn_community_posts")->fetchColumn();
    }
} catch (Throwable $e) {
    // Fallback if DB offline
    $seed_file = __DIR__ . '/../config/packages_seed.json';
    if (file_exists($seed_file)) {
        $data = json_decode(file_get_contents($seed_file), true);
        if (is_array($data)) {
            $total_packages = count($data);
            $total_downloads = array_sum(array_column($data, 'downloads'));
            usort($data, fn($a, $b) => ($b['downloads'] ?? 0) <=> ($a['downloads'] ?? 0));
            $top_packages = array_slice($data, 0, 5);
        }
    }
}
?>

<!-- KPI Overview Grid -->
<div class="kpi-grid">
  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-label">Verified Packages</span>
      <div class="kpi-icon">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path></svg>
      </div>
    </div>
    <div class="kpi-value"><?= number_format($total_packages) ?></div>
    <div class="kpi-subtext" style="color: #38bdf8;">Standard &amp; Core Libraries</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-label">Total Downloads</span>
      <div class="kpi-icon" style="background: rgba(16, 185, 129, 0.12); color: #10b981;">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #10b981;"><?= number_format($total_downloads) ?></div>
    <div class="kpi-subtext">Active telemetry counter</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-label">Toolchain Release</span>
      <div class="kpi-icon" style="background: rgba(245, 158, 11, 0.12); color: #f59e0b;">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><polyline points="12 6 12 12 16 14"></polyline></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #f59e0b;">v1.0.0</div>
    <div class="kpi-subtext">Production Stable API</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-label">Community Developers</span>
      <div class="kpi-icon" style="background: rgba(168, 85, 247, 0.12); color: #c084fc;">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"></path><circle cx="9" cy="7" r="4"></circle></svg>
      </div>
    </div>
    <div class="kpi-value"><?= number_format($total_users) ?></div>
    <div class="kpi-subtext"><?= number_format($total_posts) ?> forum discussions</div>
  </div>
</div>

<!-- Quick Actions Panel -->
<div class="panel" style="background: linear-gradient(135deg, rgba(14, 165, 233, 0.08) 0%, rgba(10, 15, 29, 0.6) 100%); border-color: rgba(56, 189, 248, 0.3);">
  <div class="panel-header" style="margin-bottom: 14px;">
    <div class="panel-title">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2.2"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon></svg>
      <span>Instant Command Actions</span>
    </div>
  </div>
  <div style="display: flex; flex-wrap: wrap; gap: 12px;">
    <a href="/console/packages.php#add" class="c-btn c-btn-primary">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
      <span>Add New Package</span>
    </a>

    <a href="/console/releases.php" class="c-btn c-btn-secondary">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
      <span>Re-sync Release Manifest</span>
    </a>

    <a href="/console/cms.php" class="c-btn c-btn-secondary">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg>
      <span>Edit Announcement Banner</span>
    </a>

    <a href="/console/database.php?action=export_packages_json&csrf_token=<?= tn_csrf_token() ?>" class="c-btn c-btn-secondary">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="17 8 12 3 7 8"></polyline><line x1="12" y1="3" x2="12" y2="15"></line></svg>
      <span>Export Registry JSON</span>
    </a>
  </div>
</div>

<!-- Two-Column Section: Top Packages & System Health -->
<div style="display: grid; grid-template-columns: 2fr 1fr; gap: 24px;" class="dashboard-grid">
  <!-- Top Libraries Table -->
  <div class="panel">
    <div class="panel-header">
      <div class="panel-title">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 20V10"></path><path d="M18 20V4"></path><path d="M6 20v-4"></path></svg>
        <span>Top Downloaded Libraries</span>
      </div>
      <a href="/console/packages.php" class="c-btn c-btn-secondary c-btn-sm">View All 67 &rarr;</a>
    </div>

    <div class="table-wrapper">
      <table class="console-table">
        <thead>
          <tr>
            <th>Library</th>
            <th>Version</th>
            <th>Category</th>
            <th>Downloads</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <?php foreach ($top_packages as $pkg): ?>
            <tr>
              <td>
                <div style="display: flex; align-items: center; gap: 8px;">
                  <code style="font-weight: 700; color: #38bdf8; font-size: 0.95rem;"><?= htmlspecialchars($pkg['name']) ?></code>
                </div>
              </td>
              <td>
                <span style="font-size: 0.72rem; padding: 2px 7px; border-radius: 9999px; background: rgba(14, 165, 233, 0.12); color: #0284c7; font-weight: 700; border: 1px solid rgba(14, 165, 233, 0.3);">
                  v<?= htmlspecialchars($pkg['version'] ?? '1.0.0') ?>
                </span>
              </td>
              <td><span style="color: var(--c-text-secondary);"><?= htmlspecialchars($pkg['category'] ?? 'Core') ?></span></td>
              <td><strong style="color: #10b981;"><?= number_format((int)($pkg['downloads'] ?? 0)) ?></strong></td>
              <td>
                <a href="/console/packages.php?edit=<?= urlencode($pkg['name']) ?>" class="c-btn c-btn-secondary c-btn-sm">Edit</a>
              </td>
            </tr>
          <?php endforeach; ?>
        </tbody>
      </table>
    </div>
  </div>

  <!-- Server & Environment Diagnostics -->
  <div class="panel">
    <div class="panel-header">
      <div class="panel-title">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect><rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect><line x1="6" y1="6" x2="6.01" y2="6"></line><line x1="6" y1="18" x2="6.01" y2="18"></line></svg>
        <span>System Diagnostics</span>
      </div>
    </div>

    <ul style="list-style: none; display: flex; flex-direction: column; gap: 14px; font-size: 0.86rem;">
      <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--c-border-subtle); padding-bottom: 8px;">
        <span style="color: var(--c-text-muted);">Language Version:</span>
        <strong style="color: #38bdf8;">TezzNative 1.0.0</strong>
      </li>
      <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--c-border-subtle); padding-bottom: 8px;">
        <span style="color: var(--c-text-muted);">Backend Server:</span>
        <span>PHP <?= PHP_VERSION ?></span>
      </li>
      <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--c-border-subtle); padding-bottom: 8px;">
        <span style="color: var(--c-text-muted);">Database:</span>
        <span style="color: #10b981; font-weight: 600;">MySQL Connected</span>
      </li>
      <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--c-border-subtle); padding-bottom: 8px;">
        <span style="color: var(--c-text-muted);">DB Table:</span>
        <code>tn_package_registry</code>
      </li>
      <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--c-border-subtle); padding-bottom: 8px;">
        <span style="color: var(--c-text-muted);">Company:</span>
        <strong style="color: var(--c-text-primary);">TEZZCORP PRIVATE LIMITED</strong>
      </li>
      <li style="display: flex; justify-content: space-between;">
        <span style="color: var(--c-text-muted);">Portal Domain:</span>
        <a href="https://tezznative.org" target="_blank" style="color: #0284c7;">tezznative.org</a>
      </li>
    </ul>
  </div>
</div>

<style>
@media (max-width: 900px) {
  .dashboard-grid {
    grid-template-columns: 1fr !important;
  }
}
</style>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
