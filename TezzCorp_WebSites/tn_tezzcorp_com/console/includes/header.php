<?php
declare(strict_types=1);

/**
 * console/includes/header.php — Shared Console Header & Sidebar Layout
 */

require_once __DIR__ . '/auth.php';
tn_admin_require();

$active_nav = $active_nav ?? 'dashboard';
$console_title = $console_title ?? 'Management Console';
$admin_user = htmlspecialchars($_SESSION['tn_admin_user'] ?? 'Administrator');

$flash_success = tn_get_flash('success');
$flash_error = tn_get_flash('error');

// Package count for badge
$pkg_count = 67;
try {
    $pdo = tn_pdo();
    $pkg_count = (int)$pdo->query("SELECT count(*) FROM tn_package_registry")->fetchColumn();
} catch (Throwable $e) {}
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="robots" content="noindex, nofollow, noarchive">
  <title><?= htmlspecialchars($console_title) ?> — TezzNative Console</title>
  <link rel="icon" type="image/x-icon" href="/favicon.ico?v=1.0.0">
  <link rel="stylesheet" href="/console/assets/console.css?v=1.0.0">
</head>
<body>

<div class="console-wrapper">
  <!-- Mobile Sidebar Backdrop Overlay -->
  <div class="sidebar-backdrop" id="sidebarBackdrop"></div>

  <!-- Sidebar -->
  <aside class="console-sidebar" id="consoleSidebar">
    <div class="sidebar-header-row">
      <a href="/console/" class="sidebar-brand">
        <div class="sidebar-emblem">
          <img src="/assets/logo.png" alt="TezzNative">
        </div>
        <div>
          <div class="sidebar-brand-title">Tezz<span style="color: #00d2ff;">Native</span></div>
          <div class="sidebar-brand-sub">Command Console</div>
        </div>
      </a>
      <button type="button" class="sidebar-close-btn" id="sidebarCloseBtn" aria-label="Close Sidebar">&times;</button>
    </div>

    <div class="sidebar-menu">
      <div class="sidebar-section-title">Core Management</div>
      <a href="/console/" class="sidebar-item <?= ($active_nav === 'dashboard') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7"></rect><rect x="14" y="3" width="7" height="7"></rect><rect x="14" y="14" width="7" height="7"></rect><rect x="3" y="14" width="7" height="7"></rect></svg>
        <span>Dashboard</span>
      </a>

      <a href="/console/packages.php" class="sidebar-item <?= ($active_nav === 'packages') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path><polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline><line x1="12" y1="22.08" x2="12" y2="12"></line></svg>
        <span>Package Registry</span>
        <span class="sidebar-badge"><?= $pkg_count ?></span>
      </a>

      <a href="/console/releases.php" class="sidebar-item <?= ($active_nav === 'releases') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
        <span>Releases &amp; SDK</span>
      </a>

      <div class="sidebar-section-title">Ecosystem &amp; Content</div>
      <a href="/console/community.php" class="sidebar-item <?= ($active_nav === 'community') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"></path><circle cx="9" cy="7" r="4"></circle><path d="M23 21v-2a4 4 0 0 0-3-3.87"></path><path d="M16 3.13a4 4 0 0 1 0 7.75"></path></svg>
        <span>Community &amp; Forum</span>
      </a>

      <a href="/console/cms.php" class="sidebar-item <?= ($active_nav === 'cms') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg>
        <span>Announcement &amp; CMS</span>
      </a>

      <div class="sidebar-section-title">Infrastructure</div>
      <a href="/console/database.php" class="sidebar-item <?= ($active_nav === 'database') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><ellipse cx="12" cy="5" rx="9" ry="3"></ellipse><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"></path><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"></path></svg>
        <span>Database &amp; Backup</span>
      </a>
    </div>

    <!-- Admin User Footer -->
    <div class="sidebar-footer">
      <div class="admin-profile">
        <div class="admin-avatar"><?= strtoupper(substr($admin_user, 0, 2)) ?></div>
        <div>
          <div class="admin-name"><?= $admin_user ?></div>
          <div class="admin-role">TEZZCORP ADMIN</div>
        </div>
      </div>
      <a href="/console/logout.php" class="logout-btn" title="Sign Out of Console">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"></path><polyline points="16 17 21 12 16 7"></polyline><line x1="21" y1="12" x2="9" y2="12"></line></svg>
      </a>
    </div>
  </aside>

  <!-- Main Content Wrapper -->
  <div class="console-main">
    <!-- Topbar -->
    <header class="console-topbar">
      <div class="topbar-left">
        <button class="mobile-nav-toggle" id="mobileNavToggle" aria-label="Toggle Navigation">
          <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="3" y1="12" x2="21" y2="12"></line><line x1="3" y1="6" x2="21" y2="6"></line><line x1="3" y1="18" x2="21" y2="18"></line></svg>
        </button>
        <h2 class="topbar-title"><?= htmlspecialchars($console_title) ?></h2>
      </div>

      <div class="topbar-right">
        <div class="system-status-pill">
          <span class="pulse-dot"></span>
          <span>v1.0.0 Production &bull; MySQL Active</span>
        </div>
        <a href="/" target="_blank" class="c-btn c-btn-secondary c-btn-sm" style="display: inline-flex; align-items: center; gap: 4px;">
          <span>Live Site</span>
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path><polyline points="15 3 21 3 21 9"></polyline><line x1="10" y1="14" x2="21" y2="3"></line></svg>
        </a>
      </div>
    </header>

    <!-- Body Container -->
    <div class="console-body">
      <?php if (!empty($flash_success)): ?>
        <div class="flash-msg flash-success">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline></svg>
          <span><?= htmlspecialchars($flash_success) ?></span>
        </div>
      <?php endif; ?>

      <?php if (!empty($flash_error)): ?>
        <div class="flash-msg flash-error">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line></svg>
          <span><?= htmlspecialchars($flash_error) ?></span>
        </div>
      <?php endif; ?>

