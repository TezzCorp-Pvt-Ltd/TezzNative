<?php
declare(strict_types=1);

/**
 * blog/index.php — TezzNative Official Engineering & Research Blog
 * Dynamic MariaDB-driven article reader & feed with zero-CDN, zero-emoji, responsive Day/Night UI.
 */

require_once __DIR__ . '/../config/db.php';

$requested_slug = trim((string)($_GET['slug'] ?? ''));

// ── Fallback static posts if DB is ever unavailable ────────────────────────────
$fallback_posts = [
    [
        'id'           => 1,
        'slug'         => 'tezznative-v1-launch',
        'category'     => 'Language',
        'published_at' => '2026-05-15 10:00:00',
        'author'       => 'Rohit Pathak',
        'read_time'    => '8 min',
        'title'        => 'Introducing TezzNative v1.0 — A New Era of Systems Programming from India',
        'excerpt'      => 'TezzNative reaches its first stable release: a high-performance compiled language with a single-pass compiler, <50ms build times, native Win32 GUI, and a full ecosystem — all zero-dependency. Here is the story behind it.',
        'content'      => "## The Vision: True Zero-Dependency Systems Programming\n\nFor decades, systems software development has been fractured between low-level speed with cumbersome toolchains and high-level productivity hampered by bloated garbage collectors.\n\nTezzNative was founded on 18 October 2022 by **Rohit Pathak** at **TEZZCORP PRIVATE LIMITED** to prove that engineers can have both: the clean syntax of Python paired with bare-metal x64 machine code generation.\n\n### Breakthrough Architectural Pillars:\n1. **Single-Pass Optimizing Direct PE/ELF Generator**: Emits standalone `.exe` binaries directly from syntax trees in under 50 milliseconds without invoking external linkers or C compilers.\n2. **First-Class 4D Tensor Arithmetic**: Native infix `@` matrix multiplication and element-wise SIMD AVX2 acceleration built directly into language grammar.\n3. **Deterministic Scoped RAII Lifetimes**: Zero garbage collection pauses, instant deterministic destruction via `defer`, and compile-time lifetime verification.\n4. **Hardware-Accelerated GUI Toolkit (`tzgui`)**: High-performance Win32, Direct2D, and OpenGL rendering loop at locked 60 FPS.",
    ],
    [
        'id'           => 2,
        'slug'         => 'tezzllm-federated-ai',
        'category'     => 'AI / ML',
        'published_at' => '2026-04-28 14:00:00',
        'author'       => 'Rohit Pathak',
        'read_time'    => '12 min',
        'title'        => 'How TezzLLM Achieves Zero-Dependency On-Device AI with Federated Training',
        'excerpt'      => 'A deep technical dive into the architecture of TezzLLM v2.0: byte-level BPE tokenization, RoPE positional encoding, SwiGLU feedforward networks, federated averaging across 8 parallel trainers, and running 1M+ parameters in 64MB RAM.',
        'content'      => "## On-Device AI Without the Runtime Bloat\n\nMost modern AI runtimes demand gigabytes of Python packages, CUDA runtimes, and gigabytes of memory just to execute a forward pass.\n\n**TezzLLM** takes the opposite approach: engineered in pure TezzNative, it implements the full transformer stack down to byte-level BPE tokenization, Rotary Positional Embeddings (RoPE), SwiGLU activations, and KV-cache optimization in a zero-dependency binary running smoothly in 64MB of system RAM.",
    ],
    [
        'id'           => 3,
        'slug'         => 'why-zero-dependencies',
        'category'     => 'Engineering',
        'published_at' => '2026-02-20 09:30:00',
        'author'       => 'Rohit Pathak',
        'read_time'    => '10 min',
        'title'        => 'Why Zero Dependencies Should Be Your Default — The Case for TezzNative\'s Philosophy',
        'excerpt'      => 'Modern software engineering has a dependency crisis. TezzNative was built around the radical idea that an entire production stack — compiler, GUI, network, database, AI — should ship as a single binary with no external requirements.',
        'content'      => "## Reclaiming Control Over Software Supply Chains\n\nWhen every project depends on thousands of transient npm or pip modules, security audits become impossible and build reproducibility degrades.\n\nTezzNative ships with its entire standard library, package manager, and framework ecosystem as self-contained, native code.",
    ],
];

// Helper to render markdown to simple HTML safely
function tn_render_markdown(string $md): string {
    $html = htmlspecialchars($md);
    // Headings
    $html = preg_replace('/^### (.*?)$/m', '<h3 style="font-size: 1.3rem; margin: 1.5rem 0 0.75rem; color: var(--text);">$1</h3>', $html);
    $html = preg_replace('/^## (.*?)$/m', '<h2 style="font-size: 1.6rem; margin: 2rem 0 1rem; color: var(--text); border-bottom: 1px solid var(--border); padding-bottom: 0.5rem;">$1</h2>', $html);
    $html = preg_replace('/^# (.*?)$/m', '<h1 style="font-size: 2rem; margin: 2.2rem 0 1.2rem; color: var(--text);">$1</h1>', $html);
    // Bold & italic
    $html = preg_replace('/\*\*(.*?)\*\*/', '<strong>$1</strong>', $html);
    $html = preg_replace('/\*(.*?)\*/', '<em>$1</em>', $html);
    // Code blocks
    $html = preg_replace('/```(.*?)```/s', '<pre class="code-view" style="margin: 1.25rem 0; padding: 1.25rem; border-radius: 8px; overflow-x: auto; font-family: var(--font-mono); font-size: 0.9rem;"><code>$1</code></pre>', $html);
    $html = preg_replace('/`([^`]+)`/', '<code style="background: var(--bg2); padding: 2px 6px; border-radius: 4px; font-family: var(--font-mono); font-size: 0.88em; border: 1px solid var(--border);">$1</code>', $html);
    // Unordered lists
    $html = preg_replace('/^\- (.*?)$/m', '<li style="margin-left: 1.5rem; margin-bottom: 0.5rem; color: var(--text-secondary);">$1</li>', $html);
    // Ordered lists
    $html = preg_replace('/^\d+\. (.*?)$/m', '<li style="margin-left: 1.5rem; margin-bottom: 0.5rem; color: var(--text-secondary);">$1</li>', $html);
    // Paragraphs
    $html = nl2br($html);
    return $html;
}

// ── Case 1: Display Single Article ─────────────────────────────────────────────
if ($requested_slug !== '') {
    $article = null;
    try {
        $pdo = tn_pdo();
        $stmt = $pdo->prepare("SELECT * FROM tn_articles WHERE slug = ? AND status = 'published' LIMIT 1");
        $stmt->execute([$requested_slug]);
        $article = $stmt->fetch();
        if ($article) {
            $pdo->prepare("UPDATE tn_articles SET views = views + 1 WHERE id = ?")->execute([$article['id']]);
        }
    } catch (Throwable $e) {}

    // Fallback check
    if (!$article) {
        foreach ($fallback_posts as $fp) {
            if ($fp['slug'] === $requested_slug) {
                $article = $fp;
                break;
            }
        }
    }

    if ($article) {
        $page_title = htmlspecialchars($article['title']) . ' — TezzNative Blog';
        $page_desc  = htmlspecialchars($article['excerpt'] ?? '');
        $active_nav = 'blog';
        include __DIR__ . '/../includes/header.php';
        ?>
        <main id="main-content">
          <article class="section" style="padding: 3.5rem 0 5rem;">
            <div class="container" style="max-width: 860px; margin: 0 auto;">
              <!-- Back to blog index -->
              <div style="margin-bottom: 2rem;">
                <a href="/blog/" style="display: inline-flex; align-items: center; gap: 8px; color: var(--blue); font-size: 0.9rem; font-weight: 600; text-decoration: none; transition: gap 0.2s;">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="19" y1="12" x2="5" y2="12"></line><polyline points="12 19 5 12 12 5"></polyline></svg>
                  Back to Engineering Blog
                </a>
              </div>

              <!-- Header meta -->
              <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap; margin-bottom: 1.25rem;">
                <span class="badge" style="background: rgba(14, 165, 233, 0.15); color: #0284c7; font-weight: 700; font-size: 0.78rem; text-transform: uppercase; padding: 4px 10px; border-radius: 9999px;">
                  <?= htmlspecialchars($article['category'] ?? 'General') ?>
                </span>
                <span style="font-size: 0.85rem; color: var(--dim);">
                  Published <?= date('F d, Y', strtotime($article['published_at'] ?? 'now')) ?>
                </span>
                <span style="font-size: 0.85rem; color: var(--dim);">&bull;</span>
                <span style="font-size: 0.85rem; color: var(--dim);">
                  <?= htmlspecialchars($article['read_time'] ?? '5 min') ?> read
                </span>
              </div>

              <!-- Article Title -->
              <h1 style="font-size: clamp(2rem, 4vw, 2.75rem); line-height: 1.2; font-weight: 800; color: var(--text); margin-bottom: 1.25rem; letter-spacing: -0.5px;">
                <?= htmlspecialchars($article['title']) ?>
              </h1>

              <!-- Excerpt lead -->
              <?php if (!empty($article['excerpt'])): ?>
                <p style="font-size: 1.18rem; line-height: 1.6; color: var(--text-secondary); margin-bottom: 2.5rem; padding-bottom: 2rem; border-bottom: 1px solid var(--border);">
                  <?= htmlspecialchars($article['excerpt']) ?>
                </p>
              <?php endif; ?>

              <!-- Article Body -->
              <div class="article-content" style="font-size: 1.05rem; line-height: 1.8; color: var(--text-secondary);">
                <?= tn_render_markdown($article['content'] ?? '') ?>
              </div>

              <!-- Author Card & Footer -->
              <div style="margin-top: 3.5rem; padding: 2rem; border-radius: 12px; background: var(--bg2); border: 1px solid var(--border); display: flex; align-items: center; gap: 1.5rem; flex-wrap: wrap;">
                <div style="width: 56px; height: 56px; border-radius: 50%; background: linear-gradient(135deg, #00d2ff 0%, #0284c7 100%); display: flex; align-items: center; justify-content: center; font-weight: 800; color: #ffffff; font-size: 1.25rem; flex-shrink: 0;">
                  RP
                </div>
                <div style="flex: 1; min-width: 220px;">
                  <div style="font-size: 1.1rem; font-weight: 700; color: var(--text);">
                    <?= htmlspecialchars($article['author'] ?? 'Rohit Pathak') ?>
                  </div>
                  <div style="font-size: 0.85rem; color: var(--dim); margin-top: 2px;">
                    Founder &amp; Chief Architect &bull; TEZZCORP PRIVATE LIMITED
                  </div>
                  <p style="font-size: 0.88rem; color: var(--text-secondary); margin-top: 6px;">
                    Designing next-generation zero-dependency programming languages, optimizing compilers, and high-performance native systems software.
                  </p>
                </div>
              </div>
            </div>
          </article>
        </main>
        <?php
        include __DIR__ . '/../includes/footer.php';
        exit;
    }
}

// ── Case 2: Display Blog Feed ──────────────────────────────────────────────────
$posts = [];
try {
    $pdo = tn_pdo();
    $stmt = $pdo->query("SELECT * FROM tn_articles WHERE type = 'blog' AND status = 'published' ORDER BY featured DESC, published_at DESC");
    $posts = $stmt->fetchAll();
} catch (Throwable $e) {}

if (empty($posts)) {
    $posts = $fallback_posts;
}

$page_title = 'Engineering Blog — TezzNative Programming Language';
$page_desc  = 'Official engineering blog: compiler internals, zero-GC memory models, native Win32 GUI, 4D tensor arithmetic, and on-device AI from TEZZCORP PRIVATE LIMITED.';
$active_nav = 'blog';
include __DIR__ . '/../includes/header.php';
?>

<main id="main-content">
  <!-- Hero Section -->
  <section style="padding: 4.5rem 0 3.5rem; border-bottom: 1px solid var(--border);">
    <div class="container">
      <div style="display: flex; align-items: flex-end; justify-content: space-between; flex-wrap: wrap; gap: 1.5rem;">
        <div>
          <div class="eyebrow" style="color: var(--blue); font-weight: 700; font-size: 0.82rem; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px;">Engineering &amp; Research</div>
          <h1 id="blog-page-heading" style="font-size: clamp(2rem, 3.5vw, 2.8rem); font-weight: 800; color: var(--text); letter-spacing: -0.5px;">TezzNative Blog</h1>
          <p style="font-size: 1.08rem; margin-top: 0.75rem; max-width: 580px; color: var(--text-secondary); line-height: 1.6;">
            In-depth architecture articles on native compiler design, deterministic memory lifetimes, tensor acceleration, and systems engineering by Rohit Pathak.
          </p>
        </div>

        <div style="display: flex; gap: 0.75rem; align-items: center;">
          <div style="position: relative; width: 240px;">
            <input type="search" placeholder="Search articles..." aria-label="Search blog posts" id="blog-search-input"
              style="width: 100%; padding: 0.65rem 1rem 0.65rem 2.2rem; background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.88rem; font-family: var(--font); outline: none; transition: border-color 0.2s;">
            <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="position: absolute; left: 10px; top: 50%; transform: translateY(-50%); color: var(--dim); pointer-events: none;"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
          </div>
        </div>
      </div>
    </div>
  </section>

  <!-- Posts List Section -->
  <section class="section" aria-labelledby="blog-page-heading" style="padding: 3.5rem 0 5rem;">
    <div class="container">
      <div class="article-horizontal-list" id="blog-posts-grid">
        <?php foreach ($posts as $i => $p): ?>
        <article class="article-horizontal-card" id="blog-post-<?= $i + 1 ?>" data-title="<?= htmlspecialchars(strtolower($p['title'])) ?>">
          
          <!-- Left Side Visual / Graphic Banner -->
          <div class="article-visual-side">
            <?php if (!empty($p['image_url'])): ?>
              <img src="<?= htmlspecialchars($p['image_url']) ?>" alt="<?= htmlspecialchars($p['title']) ?>" class="article-banner-img">
            <?php else: ?>
              <div class="article-banner-graphic">
                <div class="banner-glow-orb"></div>
                <div class="banner-badge-tag"><?= htmlspecialchars($p['category'] ?? 'Engineering') ?></div>
                <div class="banner-brand-watermark">
                  <img src="/assets/logo.png" alt="TezzNative" class="banner-logo-img">
                  <div class="banner-title-preview">TezzNative &bull; Architecture</div>
                </div>
              </div>
            <?php endif; ?>
          </div>

          <!-- Right Side Content & Metadata -->
          <div class="article-content-side">
            <div>
              <div class="article-meta-row" style="margin-bottom: 0.65rem;">
                <span class="article-category-badge">
                  <?= htmlspecialchars($p['category'] ?? 'Engineering') ?>
                </span>
                <span class="article-date">
                  <?= date('M d, Y', strtotime($p['published_at'] ?? 'now')) ?>
                </span>
                <span class="article-dot">&bull;</span>
                <span class="article-read-time">
                  <?= htmlspecialchars($p['read_time'] ?? '6 min read') ?>
                </span>
              </div>

              <h2 class="article-title" id="blog-title-<?= $i + 1 ?>">
                <a href="/blog/?slug=<?= urlencode($p['slug']) ?>"><?= htmlspecialchars($p['title']) ?></a>
              </h2>

              <p class="article-excerpt">
                <?= htmlspecialchars($p['excerpt'] ?? '') ?>
              </p>
            </div>

            <div class="article-footer-row">
              <div class="article-author">
                <div class="article-author-avatar">RP</div>
                <div>
                  <div class="article-author-name"><?= htmlspecialchars($p['author'] ?? 'Rohit Pathak') ?></div>
                  <div class="article-author-role">Founder, TEZZCORP</div>
                </div>
              </div>

              <a href="/blog/?slug=<?= urlencode($p['slug']) ?>" class="article-read-btn">
                <span>Read Full Article</span>
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="9 18 15 12 9 6"></polyline></svg>
              </a>
            </div>
          </div>

        </article>
        <?php endforeach; ?>
      </div>

      <p id="blog-no-results" style="display: none; text-align: center; color: var(--dim); padding: 3rem 0; font-size: 1rem;">
        No articles found matching your search.
      </p>
    </div>
  </section>

  <!-- Newsletter CTA (Zero CDN, Zero Emoji) -->
  <section class="section-sm" style="background: var(--bg2); border-top: 1px solid var(--border); padding: 4rem 0;">
    <div class="container">
      <div class="cta-box" style="max-width: 640px; margin: 0 auto; text-align: center;">
        <div class="eyebrow" style="color: var(--blue); font-weight: 700; font-size: 0.8rem; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px;">Stay Updated</div>
        <h2 style="font-size: 1.85rem; font-weight: 800; color: var(--text); letter-spacing: -0.5px;">TezzNative Engineering Digest</h2>
        <p style="font-size: 0.98rem; color: var(--text-secondary); margin-top: 0.75rem; line-height: 1.6;">
          Receive updates on release versions, compiler benchmarks, and architecture papers directly from the development team at TEZZCORP PRIVATE LIMITED.
        </p>
        <form style="display: flex; gap: 0.75rem; flex-wrap: wrap; justify-content: center; margin-top: 1.75rem;" onsubmit="return handleSubscribe(event)">
          <input type="email" id="nlEmail" placeholder="developer@example.com" aria-label="Email for newsletter" required
            style="flex: 1; min-width: 240px; max-width: 340px; padding: 0.7rem 1.1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.92rem; font-family: var(--font); outline: none;">
          <button class="btn btn-primary" type="submit" id="nlBtn" style="display: inline-flex; align-items: center; gap: 6px; padding: 0.7rem 1.25rem;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"></line><polygon points="22 2 15 22 11 13 2 9 22 2"></polygon></svg>
            Subscribe
          </button>
        </form>
        <div id="nlSuccess" style="display: none; margin-top: 12px; color: #10b981; font-size: 0.88rem; font-weight: 600;">
          Subscription confirmed. Thank you for following TezzNative!
        </div>
      </div>
    </div>
  </section>
</main>

<script>
// Search filtering
const searchInput = document.getElementById('blog-search-input');
const noResults = document.getElementById('blog-no-results');
if (searchInput) {
  searchInput.addEventListener('input', function() {
    const q = this.value.toLowerCase().trim();
    const cards = document.querySelectorAll('#blog-posts-grid .blog-card');
    let found = 0;
    cards.forEach(card => {
      const title = card.dataset.title || '';
      const show = !q || title.includes(q);
      card.style.display = show ? '' : 'none';
      if (show) found++;
    });
    if (noResults) noResults.style.display = (found === 0) ? 'block' : 'none';
  });
}

function handleSubscribe(e) {
  e.preventDefault();
  const email = document.getElementById('nlEmail').value;
  if (email) {
    document.getElementById('nlEmail').value = '';
    document.getElementById('nlSuccess').style.display = 'block';
    setTimeout(() => {
      const msg = document.getElementById('nlSuccess');
      if (msg) msg.style.display = 'none';
    }, 4000);
  }
  return false;
}
</script>

<?php include __DIR__ . '/../includes/footer.php'; ?>
