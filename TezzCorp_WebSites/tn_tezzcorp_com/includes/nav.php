<?php
// includes/nav.php - Navigation, helper wrappers, and UI layout functions
require_once __DIR__ . '/i18n.php';

function tn_boot(string $path = '/'): array {
  // Load config + .env (idempotent)
  static $booted = false;
  if (!$booted) {
    $cfgFile = __DIR__ . '/../config/config.php';
    if (is_file($cfgFile)) {
      require_once $cfgFile;
    }
    $booted = true;
  }

  // Load DB layer
  if (!function_exists('tn_pdo')) {
    require_once __DIR__ . '/../config/db.php';
  }

  // Load community DB functions
  if (!function_exists('tezz_register_user')) {
    require_once __DIR__ . '/../config/community_db.php';
  }

  // Get PDO (may throw — callers should catch)
  $pdo = null;
  try {
    $pdo = tn_pdo();
  } catch (Throwable $e) {
    // Silently degrade — page will render without DB features
  }

  return [
    'config' => [
      'site_name' => 'TezzNative',
      'site_url' => 'https://tezznative.org',
      'version' => '1.0.0'
    ],
    'version' => [
      'version' => '1.0.0',
      'build' => '2026.11',
      'status' => 'Production Release'
    ],
    'pdo' => $pdo,
  ];
}

function tn_head(string $title = '', string $desc = '', string $active = 'home', string $path = '/'): void {
  $page_title = $title;
  $page_desc = $desc;
  $active_nav = $active;
  $canonical_path = $path;
  require __DIR__ . '/header.php';
}

function tn_nav(string $active = 'home'): void {
  static $rendered = false;
  if ($rendered) {
    return;
  }
  $rendered = true;
  $ribbon = function_exists('tn_get_setting') ? tn_get_setting('announcement_ribbon') : null;
  $banner_enabled = is_array($ribbon) ? !empty($ribbon['enabled']) : true;
  $banner_badge = (is_array($ribbon) && !empty($ribbon['badge'])) ? $ribbon['badge'] : __('banner_badge', 'OFFICIAL RELEASE');
  $banner_text = (is_array($ribbon) && !empty($ribbon['text'])) ? $ribbon['text'] : __('banner_text', 'High-Performance Systems & AI Language with Native Tensors, Coroutines & Zero-GC Execution!');
  $banner_link_text = (is_array($ribbon) && !empty($ribbon['link_text'])) ? $ribbon['link_text'] : __('banner_link', 'Get TezzNative v1.0.0');
  $banner_link_url = (is_array($ribbon) && !empty($ribbon['link_url'])) ? $ribbon['link_url'] : '/download/';
?>
<?php if ($banner_enabled): ?>
<!-- Top Announcement Ribbon -->
<div class="announcement-banner">
  <div class="banner-container">
    <span class="banner-badge"><?= htmlspecialchars($banner_badge) ?></span>
    <span class="banner-text"><?= htmlspecialchars($banner_text) ?></span>
    <a href="<?= htmlspecialchars($banner_link_url) ?>" class="banner-link">
      <?= htmlspecialchars($banner_link_text) ?>
      <svg class="icon-inline" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
    </a>
  </div>
</div>
<?php endif; ?>

<!-- Primary Navigation Header -->
<header class="main-header" id="mainHeader">
  <div class="nav-container">
    <!-- Brand Logo -->
    <a href="/" class="brand-logo" aria-label="TezzNative Home">
      <div class="logo-symbol" style="display: flex; align-items: center; justify-content: center; width: 38px; height: 38px; border-radius: 10px; overflow: hidden; background: radial-gradient(circle at 30% 25%, #1e293b 0%, #0b1329 60%, #030712 100%); border: 1px solid rgba(56, 189, 248, 0.45); box-shadow: 0 4px 14px rgba(2, 6, 23, 0.28), inset 0 1px 1px rgba(255, 255, 255, 0.18); padding: 3px; flex-shrink: 0;">
        <img src="/assets/logo.png" alt="TezzNative Official Logo" style="width: 100%; height: 100%; object-fit: contain;" />
      </div>
      <div style="display: flex; flex-direction: column; line-height: 1.15;">
        <span class="brand-text">Tezz<span class="brand-accent">Native</span></span>
        <span style="font-size: 0.58rem; color: #94a3b8; font-weight: 600; letter-spacing: 0.5px; text-transform: uppercase;">TEZZCORP PRIVATE LIMITED</span>
      </div>
      <span class="version-tag">v1.0.0</span>
    </a>

    <!-- Desktop Navigation Links -->
    <nav class="nav-menu" id="navMenu">
      <a href="/#features" class="nav-link <?= ($active === 'features') ? 'active' : '' ?>"><?= __('nav_features', 'Features') ?></a>
      <a href="/#benchmarks" class="nav-link <?= ($active === 'benchmarks') ? 'active' : '' ?>"><?= __('nav_benchmarks', 'Benchmarks') ?></a>
      <a href="/docs/" class="nav-link <?= ($active === 'docs') ? 'active' : '' ?>"><?= __('nav_docs', 'Docs') ?></a>
      <a href="/packages" class="nav-link <?= ($active === 'packages') ? 'active' : '' ?>">Packages</a>
      <a href="/frameworks/" class="nav-link <?= ($active === 'frameworks') ? 'active' : '' ?>"><?= __('nav_frameworks', 'Frameworks') ?></a>

      <!-- Dropdown in navbar to prevent overflow -->
      <div class="nav-dropdown" id="navMoreDropdown">
        <button type="button" class="nav-dropdown-toggle <?= in_array($active, ['packages', 'lib', 'lsp', 'playground', 'about', 'community']) ? 'active' : '' ?>" id="navMoreBtn" aria-expanded="false" aria-haspopup="true">
          <span><?= __('nav_more', 'More') ?></span>
          <svg class="dropdown-arrow" width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
            <path d="M6 9l6 6 6-6"/>
          </svg>
        </button>
        <div class="nav-dropdown-menu" id="navMoreMenu" role="menu" aria-labelledby="navMoreBtn">
          <a href="/packages" class="dropdown-item <?= ($active === 'packages' || $active === 'lib') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title"><?= __('nav_packages', 'Packages') ?> <span class="nav-badge">40+</span></div>
              <div class="dropdown-item-desc">Standard modules, tensors & AI kernels</div>
            </div>
          </a>
          <a href="/docs/lsp" class="dropdown-item <?= ($active === 'lsp') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title"><?= __('nav_lsp', 'LSP & IDEs') ?></div>
              <div class="dropdown-item-desc">VS Code, Neovim & language server</div>
            </div>
          </a>
          <a href="/#playground" class="dropdown-item <?= ($active === 'playground') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title"><?= __('nav_playground', 'Playground') ?></div>
              <div class="dropdown-item-desc">Interactive browser code runner</div>
            </div>
          </a>
          <div class="dropdown-divider"></div>
          <a href="/blog/" class="dropdown-item <?= ($active === 'blog') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title">Engineering Blog</div>
              <div class="dropdown-item-desc">Architecture, deep dives & benchmarks</div>
            </div>
          </a>
          <a href="/news" class="dropdown-item <?= ($active === 'news') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 20H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v1m2 13a2 2 0 0 1-2-2V7m2 13a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-2m-4-3H9M7 16h6M7 8h6v4H7V8z"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title">News &amp; Releases</div>
              <div class="dropdown-item-desc">Official company press &amp; launch notes</div>
            </div>
          </a>
          <div class="dropdown-divider"></div>
          <a href="/about/" class="dropdown-item <?= ($active === 'about') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title"><?= __('nav_about', 'About') ?></div>
              <div class="dropdown-item-desc">TezzCorp Pvt Ltd & Language Origin</div>
            </div>
          </a>
          <a href="/community" class="dropdown-item <?= ($active === 'community') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title">Community</div>
              <div class="dropdown-item-desc">Developer forum & release updates</div>
            </div>
          </a>
          <a href="/profile" class="dropdown-item <?= ($active === 'profile') ? 'active' : '' ?>" role="menuitem">
            <div class="dropdown-item-icon">
              <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
            </div>
            <div class="dropdown-item-content">
              <div class="dropdown-item-title">Developer Profile</div>
              <div class="dropdown-item-desc">Avatar, credentials &amp; community bio</div>
            </div>
          </a>
        </div>
      </div>

      <a href="/download/" class="nav-link <?= ($active === 'download') ? 'active' : '' ?>"><?= __('nav_downloads', 'Downloads') ?></a>
    </nav>

    <!-- Header Actions -->
    <div class="nav-actions">
      <!-- Language Selector -->
      <?= function_exists('tn_lang_switcher_html') ? tn_lang_switcher_html('desktop') : '' ?>

      <!-- Theme Switcher -->
      <button class="theme-toggle" id="themeToggle" title="Toggle Light/Dark Theme" aria-label="Toggle Theme">
        <!-- Sun Icon (for Dark mode) -->
        <svg class="sun-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="5"></circle>
          <line x1="12" y1="1" x2="12" y2="3"></line>
          <line x1="12" y1="21" x2="12" y2="23"></line>
          <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line>
          <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line>
          <line x1="1" y1="12" x2="3" y2="12"></line>
          <line x1="21" y1="12" x2="23" y2="12"></line>
          <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line>
          <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line>
        </svg>
        <!-- Moon Icon (for Light mode) -->
        <svg class="moon-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>
        </svg>
      </button>

      <!-- Primary Action CTA (Desktop only - hidden on mobile) -->
      <a href="/download/" class="btn btn-primary btn-sm btn-install-desktop">
        <svg class="btn-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <polyline points="4 17 10 11 4 5"></polyline>
          <line x1="12" y1="19" x2="20" y2="19"></line>
        </svg>
        <span><?= __('nav_install_cli', 'Install Tezz') ?></span>
      </a>

      <!-- Mobile Menu Toggle Button (Hamburger) -->
      <button class="mobile-toggle" id="mobileToggle" aria-label="Open Navigation Menu" aria-controls="mobileSidebar" aria-expanded="false">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <line x1="3" y1="12" x2="21" y2="12"></line>
          <line x1="3" y1="6" x2="21" y2="6"></line>
          <line x1="3" y1="18" x2="21" y2="18"></line>
        </svg>
      </button>
    </div>
  </div>
</header>

<!-- Mobile Sidebar Backdrop Overlay -->
<div class="sidebar-overlay" id="sidebarOverlay" aria-hidden="true"></div>

<!-- Mobile Off-Canvas Sidebar Drawer -->
<aside class="mobile-sidebar" id="mobileSidebar" aria-label="Mobile Navigation" aria-hidden="true">
  <div class="sidebar-header">
    <a href="/" class="brand-logo" aria-label="TezzNative Home">
      <div class="logo-symbol" style="display: flex; align-items: center; justify-content: center; width: 34px; height: 34px; border-radius: 9px; overflow: hidden; background: radial-gradient(circle at 30% 25%, #1e293b 0%, #0b1329 60%, #030712 100%); border: 1px solid rgba(56, 189, 248, 0.45); box-shadow: 0 4px 14px rgba(2, 6, 23, 0.28), inset 0 1px 1px rgba(255, 255, 255, 0.18); padding: 2px; flex-shrink: 0;">
        <img src="/assets/logo.png" alt="TezzNative Official Logo" style="width: 100%; height: 100%; object-fit: contain;" />
      </div>
      <div style="display: flex; flex-direction: column; line-height: 1.15;">
        <span class="brand-text" style="font-size: 1.15rem;">Tezz<span class="brand-accent">Native</span></span>
        <span style="font-size: 0.55rem; color: #94a3b8; font-weight: 600; letter-spacing: 0.4px; text-transform: uppercase;">TEZZCORP PRIVATE LIMITED</span>
      </div>
      <span class="version-tag">v1.0.0</span>
    </a>
    <button class="sidebar-close-btn" id="sidebarClose" aria-label="Close Navigation Menu">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <line x1="18" y1="6" x2="6" y2="18"></line>
        <line x1="6" y1="6" x2="18" y2="18"></line>
      </svg>
    </button>
  </div>

  <div class="sidebar-body">
    <div style="padding: 10px 16px; border-bottom: 1px solid var(--border-subtle); display: flex; align-items: center; justify-content: space-between;">
      <span style="font-size: 0.8rem; color: var(--text-tertiary); font-weight: 600; text-transform: uppercase;"><?= __('lang_selector', 'Language') ?></span>
      <?= function_exists('tn_lang_switcher_html') ? tn_lang_switcher_html('mobile') : '' ?>
    </div>

    <div style="padding: 10px 16px; border-bottom: 1px solid var(--border-subtle); display: flex; align-items: center; justify-content: space-between;">
      <span style="font-size: 0.8rem; color: var(--text-tertiary); font-weight: 600; text-transform: uppercase;">Theme Mode</span>
      <button class="theme-toggle" id="themeToggleMobile" title="Toggle Light/Dark Theme" aria-label="Toggle Theme" style="padding: 6px 12px; border-radius: 8px; display: inline-flex; align-items: center; gap: 6px; font-size: 0.82rem; font-weight: 600; cursor: pointer;">
        <svg class="sun-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"></circle><line x1="12" y1="1" x2="12" y2="3"></line><line x1="12" y1="21" x2="12" y2="23"></line><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line><line x1="1" y1="12" x2="3" y2="12"></line><line x1="21" y1="12" x2="23" y2="12"></line><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line></svg>
        <svg class="moon-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path></svg>
        <span>Toggle</span>
      </button>
    </div>

    <div class="sidebar-section-label" style="padding-top: 14px;">Navigation Menu</div>
    <nav class="sidebar-nav">
      <a href="/#features" class="sidebar-link <?= ($active === 'features') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>
        <span><?= __('nav_features', 'Features') ?></span>
      </a>
      <a href="/#benchmarks" class="sidebar-link <?= ($active === 'benchmarks') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="18" y1="20" x2="18" y2="10"/><line x1="12" y1="20" x2="12" y2="4"/><line x1="6" y1="20" x2="6" y2="14"/></svg>
        <span><?= __('nav_benchmarks', 'Benchmarks') ?></span>
      </a>
      <a href="/packages" class="sidebar-link <?= ($active === 'packages') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/></svg>
        <span><?= __('nav_packages', 'Packages') ?> <span class="sidebar-badge">67+</span></span>
      </a>
      <a href="/#playground" class="sidebar-link <?= ($active === 'playground') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>
        <span><?= __('nav_playground', 'Playground') ?></span>
      </a>
      <a href="/docs/lsp" class="sidebar-link <?= ($active === 'lsp') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
        <span><?= __('nav_lsp', 'LSP & IDEs') ?></span>
      </a>
      <a href="/docs/" class="sidebar-link <?= ($active === 'docs') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>
        <span><?= __('nav_docs', 'Documentation') ?></span>
      </a>
      <a href="/frameworks/" class="sidebar-link <?= ($active === 'frameworks') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg>
        <span><?= __('nav_frameworks', 'Frameworks') ?></span>
      </a>
      <a href="/about/" class="sidebar-link <?= ($active === 'about') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>
        <span><?= __('nav_about', 'About') ?></span>
      </a>
      <a href="/community" class="sidebar-link <?= ($active === 'community') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>
        <span>Community</span>
      </a>
      <a href="/profile" class="sidebar-link <?= ($active === 'profile') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
        <span>Developer Profile</span>
      </a>
      <a href="/support" class="sidebar-link <?= ($active === 'support') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>
        <span>Support</span>
      </a>
      <a href="/blog/" class="sidebar-link <?= ($active === 'blog') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>
        <span>Engineering Blog</span>
      </a>
      <a href="/news" class="sidebar-link <?= ($active === 'news') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 20H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v1m2 13a2 2 0 0 1-2-2V7m2 13a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-2m-4-3H9M7 16h6M7 8h6v4H7V8z"/></svg>
        <span>News &amp; Releases</span>
      </a>
      <a href="/download/" class="sidebar-link <?= ($active === 'download') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
        <span><?= __('nav_downloads', 'All Downloads') ?></span>
      </a>
    </nav>

    <!-- Prominent Mobile CTA Box -->
    <div class="sidebar-cta-box">
      <div class="sidebar-cta-title">Install TezzNative v1.0.0</div>
      <p class="sidebar-cta-desc">Standalone C speed, native tensors & async runtime on your machine.</p>
      <a href="/download/" class="btn btn-primary sidebar-btn-install">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <polyline points="4 17 10 11 4 5"></polyline>
          <line x1="12" y1="19" x2="20" y2="19"></line>
        </svg>
        <span><?= __('nav_install_cli', 'Install TezzNative (CLI)') ?></span>
      </a>
      <div class="sidebar-cta-links">
        <a href="/packages" class="sidebar-sub-link">Package Registry &rarr;</a>
        <a href="/download/tezznative-sdk-linux.tar.gz" class="sidebar-sub-link">Linux SDK (.tar.gz) &rarr;</a>
        <a href="/download/" class="sidebar-sub-link">Manifest & SHA256 &rarr;</a>
      </div>
    </div>
  </div>

  <div class="sidebar-footer">
    <div class="sidebar-footer-links">
      <a href="https://github.com/TezzCorp-Pvt-Ltd/TezzNative" target="_blank" rel="noopener">GitHub</a>
      <span>&bull;</span>
      <a href="/community/">Forum</a>
      <span>&bull;</span>
      <a href="/docs/">Docs</a>
    </div>
    <div class="sidebar-copy">&copy; <?= date('Y') ?> TEZZCORP PRIVATE LIMITED &bull; Production Ready</div>
  </div>
</aside>
<?php
}

function tn_page_shell_start(string $title, string $tagline = '', string $lead = ''): void {
?>
<section class="section page-shell-section">
  <div class="section-container">
    <div class="section-header page-shell-header">
      <span class="section-pill"><?= htmlspecialchars(strtoupper($tagline ?: 'TEZZNATIVE ECOSYSTEM')) ?></span>
      <h1 class="section-title page-shell-title"><?= htmlspecialchars($title) ?></h1>
      <?php if ($lead): ?>
        <p class="section-desc page-shell-desc"><?= htmlspecialchars($lead) ?></p>
      <?php endif; ?>
    </div>
<?php
}

function tn_page_shell_end(): void {
?>
  </div>
</section>
<?php
}

function tn_footer(): void {
  require __DIR__ . '/footer.php';
}

function tn_badge(string $label, string $color = 'blue'): string {
  $colorMap = [
    'blue'    => 'badge-info',
    'green'   => 'badge-success',
    'amber'   => 'badge-warning',
    'warning' => 'badge-warning',
    'red'     => 'badge-danger',
    'danger'  => 'badge-danger',
  ];
  $cls = $colorMap[strtolower($color)] ?? 'badge-info';
  return '<span class="badge ' . $cls . '">' . htmlspecialchars($label) . '</span>';
}

function tn_code(string $title, string $code): void {
?>
  <div class="tn-code-card" style="background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: var(--radius-lg); overflow: hidden; margin-bottom: 24px;">
    <div style="display: flex; align-items: center; justify-content: space-between; padding: 12px 18px; background: var(--bg-surface-raised); border-bottom: 1px solid var(--border-subtle);">
      <div style="display: flex; align-items: center; gap: 6px;">
        <span style="width: 10px; height: 10px; border-radius: 50%; background: #ef4444; display: inline-block;"></span>
        <span style="width: 10px; height: 10px; border-radius: 50%; background: #f59e0b; display: inline-block;"></span>
        <span style="width: 10px; height: 10px; border-radius: 50%; background: #10b981; display: inline-block;"></span>
        <span style="font-family: var(--font-mono); font-size: 0.8rem; font-weight: 600; color: var(--text-secondary); margin-left: 8px;"><?= htmlspecialchars($title) ?></span>
      </div>
      <button type="button" onclick="copySnippet(<?= htmlspecialchars(json_encode($code)) ?>, this)" class="btn btn-outline" style="padding: 3px 8px; font-size: 0.74rem;">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
        <span>Copy</span>
      </button>
    </div>
    <pre class="code-view" style="margin: 0; padding: 20px; font-family: var(--font-mono); font-size: 0.88rem; line-height: 1.6; overflow-x: auto;"><code><?= htmlspecialchars($code) ?></code></pre>
  </div>
<?php
}
?>
