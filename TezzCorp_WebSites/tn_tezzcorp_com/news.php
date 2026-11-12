<?php
declare(strict_types=1);

/**
 * news.php — News & Official Releases from TEZZCORP PRIVATE LIMITED
 * Fetches dynamic press releases from MariaDB tn_articles, falling back gracefully to static archive.
 */

require_once __DIR__ . '/config/db.php';

$page_title = 'News & Announcements — TEZZCORP PRIVATE LIMITED';
$page_desc  = 'Official releases, engineering milestones, and ecosystem updates for TezzNative from TEZZCORP PRIVATE LIMITED.';
$active_nav = 'news';

$fallback_news = [
    [
        'date'      => 'May 2026',
        'category'  => 'Product Launch',
        'cat_color' => 'var(--blue)',
        'cat_bg'    => 'rgba(59,130,246,.12)',
        'icon'      => 'release',
        'title'     => 'TezzNative v1.0.0 Released — A Systems Programming Language Made in India',
        'excerpt'   => 'TEZZCORP PRIVATE LIMITED officially releases TezzNative v1.0.0 for Windows x64. The production-grade systems programming language features a single-pass compiler, <50ms build times, built-in native GUI, and a complete ecosystem of zero-dependency frameworks.',
        'link'      => '/blog/?slug=tezznative-v1-launch',
    ],
    [
        'date'      => 'April 2026',
        'category'  => 'AI / Product',
        'cat_color' => 'var(--indigo)',
        'cat_bg'    => 'rgba(99,102,241,.12)',
        'icon'      => 'ai',
        'title'     => 'TezzLLM v2.0 Enterprise — Federated On-Device AI for Corporate Environments',
        'excerpt'   => 'TezzLLM Enterprise v2.0 launches with 1M+ parameters, 8-trainer parallel federated training, RoPE positional encoding, SwiGLU feedforward networks, and full corporate deployment support. Zero dependencies, 64MB RAM minimum.',
        'link'      => '/blog/?slug=tezzllm-federated-ai',
    ],
    [
        'date'      => 'March 2026',
        'category'  => 'Company',
        'cat_color' => 'var(--green)',
        'cat_bg'    => 'rgba(16,185,129,.12)',
        'icon'      => 'company',
        'title'     => 'TEZZCORP PRIVATE LIMITED Incorporated — CIN: U62011BR2026PTC083413',
        'excerpt'   => 'TEZZCORP PRIVATE LIMITED is officially incorporated in Bihar, India (Jhiliya, Bettiah) under the Companies Act, 2013. The company formalizes its software engineering, AI research, and product development operations.',
        'link'      => '/about.php',
    ],
    [
        'date'      => 'February 2026',
        'category'  => 'Framework',
        'cat_color' => 'var(--cyan)',
        'cat_bg'    => 'rgba(6,182,212,.12)',
        'icon'      => 'db',
        'title'     => 'TezzDB — Embedded SQL Database Engine Ships with TezzNative Ecosystem',
        'excerpt'   => 'TezzDB, a fully embedded SQL-compatible database engine with ACID transaction support and B-tree storage, is released as part of the TezzNative standard ecosystem — no external SQLite or database runtimes needed.',
        'link'      => '/frameworks/tezzdb.php',
    ],
    [
        'date'      => 'January 2026',
        'category'  => 'Framework',
        'cat_color' => 'var(--purple)',
        'cat_bg'    => 'rgba(139,92,246,.12)',
        'icon'      => 'gui',
        'title'     => 'TezzUI Native GUI Toolkit — Win32 Desktop Apps in Pure TezzNative',
        'excerpt'   => 'TezzUI releases as a stable native GUI framework for building full-featured Windows desktop applications using only TezzNative. Supports windows, controls, dialogs, custom rendering, and message loop management.',
        'link'      => '/frameworks/tezzui.php',
    ],
];

// Query DB for news
$news_items = [];
try {
    $pdo = tn_pdo();
    $stmt = $pdo->query("SELECT * FROM tn_articles WHERE type = 'news' AND status = 'published' ORDER BY featured DESC, published_at DESC");
    $db_items = $stmt->fetchAll();
    if (!empty($db_items)) {
        foreach ($db_items as $item) {
            $cat = $item['category'] ?? 'Release';
            $cat_color = 'var(--blue)';
            $cat_bg = 'rgba(59,130,246,.12)';
            $icon = 'release';

            if (stripos($cat, 'AI') !== false) {
                $cat_color = 'var(--indigo)';
                $cat_bg = 'rgba(99,102,241,.12)';
                $icon = 'ai';
            } elseif (stripos($cat, 'Company') !== false || stripos($cat, 'Corporate') !== false) {
                $cat_color = 'var(--green)';
                $cat_bg = 'rgba(16,185,129,.12)';
                $icon = 'company';
            } elseif (stripos($cat, 'Database') !== false || stripos($cat, 'DB') !== false) {
                $cat_color = 'var(--cyan)';
                $cat_bg = 'rgba(6,182,212,.12)';
                $icon = 'db';
            } elseif (stripos($cat, 'GUI') !== false || stripos($cat, 'UI') !== false) {
                $cat_color = 'var(--purple)';
                $cat_bg = 'rgba(139,92,246,.12)';
                $icon = 'gui';
            }

            $news_items[] = [
                'date'      => date('F Y', strtotime($item['published_at'] ?? 'now')),
                'category'  => $cat,
                'cat_color' => $cat_color,
                'cat_bg'    => $cat_bg,
                'icon'      => $icon,
                'title'     => $item['title'],
                'excerpt'   => $item['excerpt'] ?: substr(strip_tags($item['content']), 0, 240) . '...',
                'link'      => !empty($item['slug']) ? '/blog/?slug=' . urlencode($item['slug']) : '#',
            ];
        }
    }
} catch (Throwable $e) {}

if (empty($news_items)) {
    $news_items = $fallback_news;
}

include __DIR__ . '/includes/header.php';
?>

<main id="main-content">
  <!-- Page Hero -->
  <section style="padding: 4.5rem 0 3.5rem; border-bottom: 1px solid var(--border);">
    <div class="container">
      <div class="eyebrow" style="color: var(--blue); font-weight: 700; font-size: 0.82rem; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px;">Official Dispatch</div>
      <h1 id="news-page-heading" style="font-size: clamp(2rem, 3.5vw, 2.8rem); font-weight: 800; color: var(--text); letter-spacing: -0.5px;">News &amp; Releases</h1>
      <p style="font-size: 1.1rem; margin-top: 0.75rem; max-width: 600px; color: var(--text-secondary); line-height: 1.6;">
        Latest product launches, release milestones, and corporate announcements from TEZZCORP PRIVATE LIMITED.
      </p>
    </div>
  </section>

  <!-- News List -->
  <section class="section" aria-labelledby="news-page-heading" style="padding: 3.5rem 0 5rem;">
    <div class="container">
      <div style="max-width: 840px; margin: 0 auto; display: flex; flex-direction: column; gap: 2rem;">
        <?php foreach ($news_items as $i => $item): ?>
        <article
          class="card"
          id="news-item-<?= $i + 1 ?>"
          aria-labelledby="news-title-<?= $i + 1 ?>"
          style="display: flex; gap: 1.75rem; align-items: flex-start; flex-wrap: wrap; padding: 2rem; border-radius: 14px; background: var(--bg2); border: 1px solid var(--border);"
        >
          <!-- Icon -->
          <div style="width: 56px; height: 56px; min-width: 56px; border-radius: 14px; background: var(--bg3); border: 1px solid var(--border); display: flex; align-items: center; justify-content: center; color: var(--text-accent);">
            <?php
              $iconSvg = match($item['icon']) {
                'ai'      => '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="10" rx="2"/><circle cx="12" cy="5" r="2"/><path d="M12 7v4"/><line x1="8" y1="16" x2="8" y2="16"/><line x1="16" y1="16" x2="16" y2="16"/></svg>',
                'company' => '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 22V4a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v18Z"/><path d="M6 12H4a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h2"/><path d="M18 9h2a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2h-2"/><path d="M10 6h4"/><path d="M10 10h4"/><path d="M10 14h4"/><path d="M10 18h4"/></svg>',
                'db'      => '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/></svg>',
                'gui'     => '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>',
                default   => '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.7-2.13-.09-2.91a2.18 2.18 0 0 0-2.91-.09z"/><path d="m12 15-3-3a22 22 0 0 1 2-3.95A12.88 12.88 0 0 1 22 2c0 2.72-.78 7.5-6 11a22.35 22.35 0 0 1-4 2z"/></svg>',
              };
              echo $iconSvg;
            ?>
          </div>
          <!-- Body -->
          <div style="flex: 1; min-width: 220px;">
            <div style="display: flex; align-items: center; gap: 0.75rem; margin-bottom: 0.65rem; flex-wrap: wrap;">
              <span style="font-size: 0.68rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.08em; padding: 0.2rem 0.55rem; border-radius: 4px; background: <?= $item['cat_bg'] ?>; color: <?= $item['cat_color'] ?>;">
                <?= htmlspecialchars($item['category']) ?>
              </span>
              <span style="font-size: 0.81rem; color: var(--dim);"><?= htmlspecialchars($item['date']) ?></span>
            </div>
            <h2 id="news-title-<?= $i + 1 ?>" style="font-size: 1.2rem; margin-bottom: 0.625rem; font-weight: 700; color: var(--text); line-height: 1.4;">
              <?= htmlspecialchars($item['title']) ?>
            </h2>
            <p style="font-size: 0.92rem; color: var(--muted); margin-bottom: 1rem; line-height: 1.6;">
              <?= htmlspecialchars($item['excerpt']) ?>
            </p>
            <a href="<?= htmlspecialchars($item['link']) ?>"
               <?= str_starts_with($item['link'], 'http') ? 'target="_blank" rel="noopener"' : '' ?>
               style="display: inline-flex; align-items: center; gap: 4px; color: var(--blue); font-size: 0.88rem; font-weight: 600; text-decoration: none;">
              Read Full Notice
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="9 18 15 12 9 6"></polyline></svg>
            </a>
          </div>
        </article>
        <?php endforeach; ?>
      </div>
    </div>
  </section>

  <!-- Newsletter CTA (Zero CDN, Zero Emoji) -->
  <section class="section-sm" style="background: var(--bg2); border-top: 1px solid var(--border); padding: 4rem 0;">
    <div class="container">
      <div class="cta-box" style="max-width: 640px; margin: 0 auto; text-align: center;">
        <div class="eyebrow" style="color: var(--blue); font-weight: 700; font-size: 0.8rem; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px;">Stay Updated</div>
        <h2 style="font-size: 1.85rem; font-weight: 800; color: var(--text); letter-spacing: -0.5px;">Never Miss a TezzNative Milestone</h2>
        <p style="font-size: 0.98rem; color: var(--text-secondary); margin-top: 0.75rem; line-height: 1.6;">
          Subscribe to official company bulletins and be the first to know about compiler releases and benchmarks.
        </p>
        <form style="display: flex; gap: 0.75rem; flex-wrap: wrap; justify-content: center; margin-top: 1.75rem;" onsubmit="return handleNewsSub(event)">
          <input type="email" id="newsSubEmail" placeholder="developer@example.com" aria-label="Your email address" required
            style="flex: 1; min-width: 240px; max-width: 340px; padding: 0.7rem 1.1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.92rem; font-family: var(--font); outline: none;">
          <button class="btn btn-primary" type="submit" style="display: inline-flex; align-items: center; gap: 6px; padding: 0.7rem 1.25rem;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"></line><polygon points="22 2 15 22 11 13 2 9 22 2"></polygon></svg>
            Subscribe
          </button>
        </form>
        <div id="newsSubSuccess" style="display: none; margin-top: 12px; color: #10b981; font-size: 0.88rem; font-weight: 600;">
          Subscription confirmed. Thank you!
        </div>
      </div>
    </div>
  </section>
</main>

<script>
function handleNewsSub(e) {
  e.preventDefault();
  const input = document.getElementById('newsSubEmail');
  if (input && input.value) {
    input.value = '';
    const s = document.getElementById('newsSubSuccess');
    if (s) {
      s.style.display = 'block';
      setTimeout(() => s.style.display = 'none', 4000);
    }
  }
  return false;
}
</script>

<?php include __DIR__ . '/includes/footer.php'; ?>
