<?php
declare(strict_types=1);
$page_title = 'News & Announcements — TEZZCORP PRIVATE LIMITED';
$page_desc  = 'Latest news, press releases, and company announcements from TEZZCORP PRIVATE LIMITED — Bihar\'s leading technology company.';
$active_nav = 'news';
include 'includes/header.php';

$news_items = [
  [
    'date' => 'May 2026',
    'category' => 'Product Launch',
    'cat_color' => 'var(--blue)',
    'cat_bg' => 'rgba(59,130,246,.12)',
    'icon' => 'release',
    'title' => 'TezzNative v1.0.0 Released — A Systems Programming Language Made in India',
    'excerpt' => 'TezzCorp officially releases TezzNative v1.0.0 for Windows x64. The production-grade systems programming language features a single-pass compiler, <50ms build times, built-in native GUI, and a complete ecosystem of zero-dependency frameworks.',
    'link' => 'blog/index.php',
  ],
  [
    'date' => 'April 2026',
    'category' => 'AI / Product',
    'cat_color' => 'var(--indigo)',
    'cat_bg' => 'rgba(99,102,241,.12)',
    'icon' => 'ai',
    'title' => 'TezzLLM v2.0 Enterprise — Federated On-Device AI for Corporate Environments',
    'excerpt' => 'TezzLLM Enterprise v2.0 launches with 1M+ parameters, 8-trainer parallel federated training, RoPE positional encoding, SwiGLU feedforward networks, and full corporate deployment support. Zero dependencies, 64MB RAM minimum.',
    'link' => 'https://ai.tezzcorp.com',
  ],
  [
    'date' => 'March 2026',
    'category' => 'Company',
    'cat_color' => 'var(--green)',
    'cat_bg' => 'rgba(16,185,129,.12)',
    'icon' => 'company',
    'title' => 'TEZZCORP PRIVATE LIMITED Incorporated — CIN: U62011BR2026PTC083413',
    'excerpt' => 'TEZZCORP PRIVATE LIMITED is officially incorporated in Bihar, India (Jhiliya, Bettiah) under the Companies Act, 2013. The company formalizes its software engineering, AI research, and product development operations.',
    'link' => 'about.php',
  ],
  [
    'date' => 'February 2026',
    'category' => 'Framework',
    'cat_color' => 'var(--cyan)',
    'cat_bg' => 'rgba(6,182,212,.12)',
    'icon' => 'db',
    'title' => 'TezzDB — Embedded SQL Database Engine Ships with TezzNative Ecosystem',
    'excerpt' => 'TezzDB, a fully embedded SQL-compatible database engine with ACID transaction support and B-tree storage, is released as part of the TezzNative standard ecosystem — no external SQLite or database runtimes needed.',
    'link' => 'https://tezznative.org/frameworks/tezzdb.php',
  ],
  [
    'date' => 'January 2026',
    'category' => 'Framework',
    'cat_color' => 'var(--purple)',
    'cat_bg' => 'rgba(139,92,246,.12)',
    'icon' => 'gui',
    'title' => 'TezzUI Native GUI Toolkit — Win32 Desktop Apps in Pure TezzNative',
    'excerpt' => 'TezzUI releases as a stable native GUI framework for building full-featured Windows desktop applications using only TezzNative. Supports windows, controls, dialogs, custom rendering, and message loop management.',
    'link' => 'https://tezznative.org/frameworks/tezzui.php',
  ],
  [
    'date' => 'December 2025',
    'category' => 'Milestone',
    'cat_color' => 'var(--orange)',
    'cat_bg' => 'rgba(245,158,11,.12)',
    'icon' => 'target',
    'title' => '200+ Software Projects Delivered Across India — TezzCorp Milestone',
    'excerpt' => 'TezzCorp reaches the milestone of 200+ delivered software projects across Bihar and India. Projects span school ERP systems, business CRM platforms, API gateways, and custom web portals for clients across sectors.',
    'link' => 'portfolio.php',
  ],
];
?>

<main id="main-content">
  <!-- Page Hero -->
  <section style="padding:4.5rem 0 3.5rem;border-bottom:1px solid var(--border)">
    <div class="container">
      <div class="eyebrow">Company</div>
      <h1 id="news-page-heading">News &amp; Announcements</h1>
      <p style="font-size:1.125rem;margin-top:.75rem;max-width:560px">
        Latest updates, product launches, milestones, and official announcements from TEZZCORP PRIVATE LIMITED
      </p>
    </div>
  </section>

  <!-- News List -->
  <section class="section" aria-labelledby="news-page-heading">
    <div class="container">
      <div style="max-width:800px;margin:0 auto;display:flex;flex-direction:column;gap:2rem">
        <?php foreach ($news_items as $i => $item): ?>
        <article
          class="card"
          id="news-item-<?= $i + 1 ?>"
          aria-labelledby="news-title-<?= $i + 1 ?>"
          style="display:flex;gap:2rem;align-items:flex-start;flex-wrap:wrap"
        >
          <!-- Icon -->
          <div style="width:64px;height:64px;min-width:64px;border-radius:16px;background:var(--bg2);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;color:var(--text-accent)">
            <?php
              $iconSvg = match($item['icon']) {
                'ai'      => '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="10" rx="2"/><circle cx="12" cy="5" r="2"/><path d="M12 7v4"/><line x1="8" y1="16" x2="8" y2="16"/><line x1="16" y1="16" x2="16" y2="16"/></svg>',
                'company' => '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M6 22V4a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v18Z"/><path d="M6 12H4a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h2"/><path d="M18 9h2a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2h-2"/><path d="M10 6h4"/><path d="M10 10h4"/><path d="M10 14h4"/><path d="M10 18h4"/></svg>',
                'db'      => '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/></svg>',
                'gui'     => '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>',
                'target'  => '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><circle cx="12" cy="12" r="6"/><circle cx="12" cy="12" r="2"/></svg>',
                default   => '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.7-2.13-.09-2.91a2.18 2.18 0 0 0-2.91-.09z"/><path d="m12 15-3-3a22 22 0 0 1 2-3.95A12.88 12.88 0 0 1 22 2c0 2.72-.78 7.5-6 11a22.35 22.35 0 0 1-4 2z"/></svg>',
              };
              echo $iconSvg;
            ?>
          </div>
          <!-- Body -->
          <div style="flex:1;min-width:200px">
            <div style="display:flex;align-items:center;gap:.75rem;margin-bottom:.75rem;flex-wrap:wrap">
              <span style="font-size:.6875rem;font-weight:700;text-transform:uppercase;letter-spacing:.08em;padding:.2rem .5rem;border-radius:4px;background:<?= $item['cat_bg'] ?>;color:<?= $item['cat_color'] ?>">
                <?= htmlspecialchars($item['category']) ?>
              </span>
              <span style="font-size:.8125rem;color:var(--dim)"><?= htmlspecialchars($item['date']) ?></span>
            </div>
            <h2 id="news-title-<?= $i + 1 ?>" style="font-size:1.1875rem;margin-bottom:.625rem;font-family:var(--font2)">
              <?= htmlspecialchars($item['title']) ?>
            </h2>
            <p style="font-size:.9rem;color:var(--muted);margin-bottom:1rem">
              <?= htmlspecialchars($item['excerpt']) ?>
            </p>
            <a href="<?= htmlspecialchars($item['link']) ?>"
               <?= str_starts_with($item['link'], 'http') ? 'target="_blank" rel="noopener"' : '' ?>
               class="btn btn-ghost" style="padding:.375rem 0;color:var(--blue);font-size:.875rem">
              Read More <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="vertical-align:middle;margin-left:0.2rem"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
            </a>
          </div>
        </article>
        <?php endforeach; ?>
      </div>
    </div>
  </section>

  <!-- Newsletter CTA -->
  <section class="section-sm">
    <div class="container">
      <div class="cta-box" style="max-width:640px;margin:0 auto">
        <div class="eyebrow" style="color:var(--blue)">Stay Updated</div>
        <h2 style="font-size:1.75rem">Never miss an update</h2>
        <p>Subscribe to TezzCorp announcements and get notified first about product launches and milestones.</p>
        <form style="display:flex;gap:.75rem;flex-wrap:wrap;justify-content:center;margin-top:1.5rem" onsubmit="return false" id="news-newsletter">
          <input type="email" placeholder="your@email.com" aria-label="Your email address"
            style="flex:1;min-width:220px;padding:.6875rem 1rem;background:var(--bg3);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none">
          <button class="btn btn-primary" type="submit" id="news-subscribe-btn">
            <i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle">notifications</i> Subscribe
          </button>
        </form>
      </div>
    </div>
  </section>
</main>
<script>
document.getElementById('news-newsletter').addEventListener('submit',function(e){
  e.preventDefault();
  var i=this.querySelector('input');
  var b=document.getElementById('news-subscribe-btn');
  if(i.value){
    b.innerHTML='<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem">check</i> Subscribed!';
    i.value='';setTimeout(function(){b.innerHTML='<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem">notifications</i> Subscribe';},3000);
  }
});
</script>
<?php include 'includes/footer.php'; ?>
