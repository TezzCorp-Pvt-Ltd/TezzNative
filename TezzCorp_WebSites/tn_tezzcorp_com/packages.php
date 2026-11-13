<?php
// packages.php — Official TezzNative Package Registry & Standard Libraries Directory
$page_title = "Official Package Registry — TezzNative Standard Libraries";
$page_desc  = "Search, download, and include standard & community packages for TezzNative. Featuring 67 native libraries for GUI, AI/Tensors, async coroutines, and OS systems.";
$active_nav = "packages";
$canonical_path = "/packages";

require_once __DIR__ . '/includes/header.php';
require_once __DIR__ . '/config/db.php';

function fetch_packages(): array {
    $seed_file = __DIR__ . '/config/packages_seed.json';
    try {
        $pdo = tn_pdo();
        $stmt = $pdo->query("SELECT * FROM tn_package_registry ORDER BY downloads DESC, name ASC");
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);
        if (!empty($rows)) {
            return $rows;
        }
    } catch (Throwable $e) {
        // Fallback to static seed
    }

    if (file_exists($seed_file)) {
        $data = json_decode(file_get_contents($seed_file), true);
        if (is_array($data)) return $data;
    }
    return [];
}

$packages = fetch_packages();
$total_packages = count($packages);
$total_downloads = array_sum(array_column($packages, 'downloads'));

$categories = [
    'All' => 'All Packages',
    'GUI & Graphics' => 'GUI & Graphics',
    'AI & Deep Learning' => 'AI & Deep Learning',
    'Systems & OS' => 'Systems & OS',
    'Networking & Cloud' => 'Networking & Cloud',
    'Database & Storage' => 'Database & Storage',
    'Embedded & Hardware' => 'Embedded & Hardware',
    'DevOps & Tooling' => 'DevOps & Tooling',
];
?>

<main class="site-main">
  <!-- Registry Hero Banner -->
  <section class="section" style="padding: 48px 0 28px; background: radial-gradient(circle at 10% 0%, rgba(14, 165, 233, 0.12) 0%, transparent 60%);">
    <div class="section-container">
      <div style="text-align: left; max-width: 860px; margin: 0 0 28px;">
        <span class="section-pill" style="display: inline-flex; align-items: center; gap: 6px; padding: 4px 14px; border-radius: 9999px; background: rgba(14, 165, 233, 0.12); color: #0284c7; border: 1px solid rgba(14, 165, 233, 0.3); font-size: 0.76rem; font-weight: 700; letter-spacing: 0.8px; text-transform: uppercase;">
          <span style="width: 7px; height: 7px; border-radius: 50%; background: #00d2ff; box-shadow: 0 0 8px #00d2ff;"></span>
          Official TezzNative Package Ecosystem &bull; v1.0.0
        </span>
        <h1 style="font-size: clamp(2rem, 5vw, 3rem); font-weight: 800; letter-spacing: -0.8px; margin: 18px 0 12px; line-height: 1.15;">
          Discover &amp; Install <span style="background: var(--brand-gradient); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">TezzNative Packages</span>
        </h1>
        <p style="font-size: 1.05rem; color: var(--text-secondary); line-height: 1.6; margin-bottom: 24px; max-width: 760px;">
          Explore <?= $total_packages ?> high-performance, zero-overhead native libraries engineered for GUI, AI tensors, hardware control, and systems software by <strong>TEZZCORP PRIVATE LIMITED</strong>.
        </p>

        <!-- Registry Stats Row -->
        <div style="display: flex; flex-wrap: wrap; justify-content: flex-start; gap: 20px; margin-bottom: 32px;">
          <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); padding: 12px 24px; border-radius: 12px; text-align: left;">
            <div style="font-size: 1.5rem; font-weight: 800; color: #0284c7;"><?= $total_packages ?></div>
            <div style="font-size: 0.78rem; color: var(--text-tertiary); text-transform: uppercase; font-weight: 600;">Verified Packages</div>
          </div>
          <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); padding: 12px 24px; border-radius: 12px; text-align: left;">
            <div style="font-size: 1.5rem; font-weight: 800; color: #10b981;"><?= number_format($total_downloads) ?>+</div>
            <div style="font-size: 0.78rem; color: var(--text-tertiary); text-transform: uppercase; font-weight: 600;">Package Downloads</div>
          </div>
          <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); padding: 12px 24px; border-radius: 12px; text-align: left;">
            <div style="font-size: 1.5rem; font-weight: 800; color: #f59e0b;">v1.0.0</div>
            <div style="font-size: 0.78rem; color: var(--text-tertiary); text-transform: uppercase; font-weight: 600;">Stable API Baseline</div>
          </div>
        </div>

        <!-- Live Search Input -->
        <div style="position: relative; max-width: 640px; margin: 0;">
          <input type="text" id="pkgSearchInput" placeholder="Search by library name, category, or keyword (e.g. gui, tensor, net, font)..." 
                 style="width: 100%; padding: 14px 48px 14px 18px; border-radius: 12px; background: var(--bg-surface); border: 1px solid var(--border-medium); color: var(--text-primary); font-size: 1rem; box-shadow: var(--shadow-sm); outline: none; transition: border-color 0.2s;"
                 onfocus="this.style.borderColor='rgba(14, 165, 233, 0.7)'" onblur="this.style.borderColor='var(--border-medium)'" oninput="filterPackages()">
          <svg style="position: absolute; right: 16px; top: 50%; transform: translateY(-50%); width: 20px; height: 20px; color: var(--text-tertiary); pointer-events: none;" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="11" cy="11" r="8"></circle>
            <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
          </svg>
        </div>
      </div>
    </div>
  </section>

  <!-- Categories Filter Bar -->
  <section style="border-top: 1px solid var(--border-subtle); border-bottom: 1px solid var(--border-subtle); background: var(--bg-surface); padding: 14px 0;">
    <div class="section-container">
      <div style="display: flex; gap: 8px; overflow-x: auto; padding-bottom: 4px; scrollbar-width: none;" id="categoryFilterContainer">
        <?php foreach ($categories as $catKey => $catLabel): ?>
          <button type="button" class="cat-pill <?= ($catKey === 'All') ? 'active' : '' ?>" onclick="selectCategory('<?= htmlspecialchars($catKey) ?>', this)"
                  style="white-space: nowrap; padding: 7px 16px; border-radius: 9999px; font-size: 0.85rem; font-weight: 600; cursor: pointer; transition: all 0.2s; border: 1px solid <?= ($catKey === 'All') ? 'rgba(14, 165, 233, 0.6)' : 'var(--border-subtle)' ?>; background: <?= ($catKey === 'All') ? 'rgba(14, 165, 233, 0.14)' : 'transparent' ?>; color: <?= ($catKey === 'All') ? '#0284c7' : 'var(--text-secondary)' ?>;">
            <?= htmlspecialchars($catLabel) ?>
          </button>
        <?php endforeach; ?>
      </div>
    </div>
  </section>

  <!-- Package Grid Listing -->
  <section class="section" style="padding: 40px 0 60px;">
    <div class="section-container">
      
      <!-- Quick Info Bar -->
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; flex-wrap: wrap; gap: 12px;">
        <div style="font-size: 0.95rem; color: var(--text-secondary);" id="matchCount">
          Showing <strong><?= $total_packages ?></strong> official packages
        </div>
        <div style="font-size: 0.85rem; color: var(--text-tertiary); display: flex; align-items: center; gap: 8px;">
          <span>Command format:</span>
          <code style="background: var(--code-bg); color: #38bdf8; padding: 2px 8px; border-radius: 4px; border: 1px solid var(--border-subtle);">tezz install &lt;package&gt;</code>
        </div>
      </div>

      <!-- Package Cards Grid -->
      <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(350px, 1fr)); gap: 20px;" id="packagesGrid">
        <?php foreach ($packages as $pkg): ?>
          <div class="pkg-card" data-name="<?= htmlspecialchars(strtolower($pkg['name'])) ?>" data-desc="<?= htmlspecialchars(strtolower($pkg['description'] ?? '')) ?>" data-category="<?= htmlspecialchars($pkg['category'] ?? 'Systems & OS') ?>" data-tags="<?= htmlspecialchars(strtolower($pkg['tags'] ?? '')) ?>"
               style="display: flex; flex-direction: column; background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 14px; padding: 22px; transition: transform 0.2s ease, border-color 0.2s ease, box-shadow 0.2s ease;">
            
            <!-- Top card info -->
            <div style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 12px;">
              <div>
                <a href="/package?name=<?= urlencode($pkg['name']) ?>" style="font-size: 1.25rem; font-weight: 800; color: var(--text-primary); text-decoration: none; display: inline-flex; align-items: center; gap: 6px;">
                  <code><?= htmlspecialchars($pkg['name']) ?></code>
                </a>
                <span class="version-tag" style="margin-left: 6px; font-size: 0.7rem; font-weight: 700; padding: 2px 8px; border-radius: 9999px; background: rgba(14, 165, 233, 0.12); color: #0284c7; border: 1px solid rgba(14, 165, 233, 0.3);">
                  v<?= htmlspecialchars($pkg['version'] ?? '1.0.0') ?>
                </span>
              </div>
              <span style="font-size: 0.72rem; font-weight: 600; padding: 3px 9px; border-radius: 6px; background: var(--bg-surface-raised); color: var(--text-tertiary); border: 1px solid var(--border-subtle);">
                <?= htmlspecialchars($pkg['category'] ?? 'Core') ?>
              </span>
            </div>

            <!-- Description -->
            <p style="font-size: 0.92rem; color: var(--text-secondary); line-height: 1.5; margin-bottom: 18px; flex-grow: 1;">
              <?= htmlspecialchars($pkg['description'] ?? 'Official TezzNative library.') ?>
            </p>

            <!-- Install Command Box -->
            <div style="display: flex; align-items: center; justify-content: space-between; background: var(--code-bg); border: 1px solid var(--border-subtle); border-radius: 8px; padding: 8px 12px; margin-bottom: 16px;">
              <code style="font-family: var(--font-mono); font-size: 0.82rem; color: #38bdf8;">tezz install <?= htmlspecialchars($pkg['name']) ?></code>
              <button type="button" onclick="copySnippet('tezz install <?= htmlspecialchars($pkg['name']) ?>', this)" title="Copy command"
                      style="background: transparent; border: none; color: var(--text-tertiary); cursor: pointer; padding: 4px; border-radius: 4px; display: flex; align-items: center; justify-content: center; transition: color 0.2s;">
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
              </button>
            </div>

            <!-- Card Footer -->
            <div style="display: flex; align-items: center; justify-content: space-between; border-top: 1px solid var(--border-subtle); padding-top: 14px; font-size: 0.8rem; color: var(--text-tertiary);">
              <div style="display: flex; align-items: center; gap: 6px;">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
                <span><?= number_format((int)($pkg['downloads'] ?? 0)) ?> downloads</span>
              </div>
              <div style="display: flex; align-items: center; gap: 10px;">
                <a href="/api/download_package.php?name=<?= urlencode($pkg['name']) ?>" class="btn-dl" title="Download .tn file directly"
                   style="color: #0284c7; text-decoration: none; font-weight: 600; display: inline-flex; align-items: center; gap: 4px;">
                  <span>.tn</span>
                  <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
                </a>
                <a href="/package?name=<?= urlencode($pkg['name']) ?>" style="color: var(--text-primary); text-decoration: none; font-weight: 600; display: inline-flex; align-items: center; gap: 4px;">
                  <span>Details</span>
                  <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="9 18 15 12 9 6"></polyline></svg>
                </a>
              </div>
            </div>

          </div>
        <?php endforeach; ?>
      </div>

      <!-- No match fallback -->
      <div id="noMatchBox" style="display: none; text-align: center; padding: 60px 20px; background: var(--bg-surface); border: 1px dashed var(--border-medium); border-radius: 14px; margin-top: 20px;">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="var(--text-tertiary)" stroke-width="1.5" style="margin-bottom: 14px;"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
        <h3 style="font-size: 1.2rem; font-weight: 700; color: var(--text-primary); margin-bottom: 6px;">No packages found</h3>
        <p style="color: var(--text-secondary); font-size: 0.95rem; margin-bottom: 18px;">Try searching for a different keyword or select another category.</p>
        <button type="button" onclick="clearFilters()" class="btn btn-secondary" style="padding: 8px 18px; font-size: 0.88rem;">Reset Filters</button>
      </div>

    </div>
  </section>

  <!-- Usage & Developer Documentation Section -->
  <section class="section" style="border-top: 1px solid var(--border-subtle); background: var(--bg-surface); padding: 50px 0;">
    <div class="section-container">
      <div style="text-align: left; margin-bottom: 36px;">
        <h2 style="font-size: 1.8rem; font-weight: 800; margin-bottom: 8px;">How Package Management Works in TezzNative</h2>
        <p style="color: var(--text-secondary); font-size: 1rem;">Simple, deterministic, zero-friction library inclusion with SHA-256 verification.</p>
      </div>

      <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 24px;">
        <div style="background: var(--bg-base); border: 1px solid var(--border-subtle); border-radius: 12px; padding: 22px;">
          <div style="display: flex; align-items: center; gap: 10px; margin-bottom: 12px;">
            <div style="width: 32px; height: 32px; border-radius: 8px; background: rgba(14, 165, 233, 0.15); color: #0284c7; display: flex; align-items: center; justify-content: center; font-weight: 800;">1</div>
            <h4 style="font-size: 1.05rem; font-weight: 700; margin: 0;">Install via CLI</h4>
          </div>
          <p style="font-size: 0.88rem; color: var(--text-secondary); margin-bottom: 12px; line-height: 1.5;">Run the tezz package manager to fetch and pin any library from our official registry into your project.</p>
          <pre style="background: var(--code-bg); padding: 10px; border-radius: 6px; font-family: var(--font-mono); font-size: 0.82rem; color: #38bdf8; overflow-x: auto;">tezz install gui</pre>
        </div>

        <div style="background: var(--bg-base); border: 1px solid var(--border-subtle); border-radius: 12px; padding: 22px;">
          <div style="display: flex; align-items: center; gap: 10px; margin-bottom: 12px;">
            <div style="width: 32px; height: 32px; border-radius: 8px; background: rgba(16, 185, 129, 0.15); color: #10b981; display: flex; align-items: center; justify-content: center; font-weight: 800;">2</div>
            <h4 style="font-size: 1.05rem; font-weight: 700; margin: 0;">Declare in tezz.mod</h4>
          </div>
          <p style="font-size: 0.88rem; color: var(--text-secondary); margin-bottom: 12px; line-height: 1.5;">Specify exact semantic version pins in your manifest to guarantee reproducible compilation.</p>
          <pre style="background: var(--code-bg); padding: 10px; border-radius: 6px; font-family: var(--font-mono); font-size: 0.82rem; color: #38bdf8; overflow-x: auto;">dep.gui = 1.0.0
dep.tensor = 1.0.0</pre>
        </div>

        <div style="background: var(--bg-base); border: 1px solid var(--border-subtle); border-radius: 12px; padding: 22px;">
          <div style="display: flex; align-items: center; gap: 10px; margin-bottom: 12px;">
            <div style="width: 32px; height: 32px; border-radius: 8px; background: rgba(245, 158, 11, 0.15); color: #f59e0b; display: flex; align-items: center; justify-content: center; font-weight: 800;">3</div>
            <h4 style="font-size: 1.05rem; font-weight: 700; margin: 0;">Include in Code</h4>
          </div>
          <p style="font-size: 0.88rem; color: var(--text-secondary); margin-bottom: 12px; line-height: 1.5;">Zero C dependencies or header wrangling. Directly import modules with Pythonic clarity.</p>
          <pre style="background: var(--code-bg); padding: 10px; border-radius: 6px; font-family: var(--font-mono); font-size: 0.82rem; color: #38bdf8; overflow-x: auto;">import gui
import tensor</pre>
        </div>
      </div>
    </div>
  </section>
</main>

<script>
let currentCategory = 'All';

function selectCategory(cat, btn) {
  currentCategory = cat;
  const pills = document.querySelectorAll('.cat-pill');
  pills.forEach(p => {
    p.style.background = 'transparent';
    p.style.borderColor = 'var(--border-subtle)';
    p.style.color = 'var(--text-secondary)';
    p.classList.remove('active');
  });

  btn.style.background = 'rgba(14, 165, 233, 0.14)';
  btn.style.borderColor = 'rgba(14, 165, 233, 0.6)';
  btn.style.color = '#0284c7';
  btn.classList.add('active');

  filterPackages();
}

function filterPackages() {
  const query = (document.getElementById('pkgSearchInput').value || '').trim().toLowerCase();
  const cards = document.querySelectorAll('.pkg-card');
  let matchCount = 0;

  cards.forEach(card => {
    const name = card.getAttribute('data-name') || '';
    const desc = card.getAttribute('data-desc') || '';
    const cat = card.getAttribute('data-category') || '';
    const tags = card.getAttribute('data-tags') || '';

    const matchesCat = (currentCategory === 'All' || cat.toLowerCase() === currentCategory.toLowerCase());
    const matchesQuery = !query || name.includes(query) || desc.includes(query) || tags.includes(query);

    if (matchesCat && matchesQuery) {
      card.style.display = 'flex';
      matchCount++;
    } else {
      card.style.display = 'none';
    }
  });

  const countEl = document.getElementById('matchCount');
  if (countEl) {
    countEl.innerHTML = `Showing <strong>${matchCount}</strong> official package${matchCount === 1 ? '' : 's'}`;
  }

  const noMatchBox = document.getElementById('noMatchBox');
  if (noMatchBox) {
    noMatchBox.style.display = (matchCount === 0) ? 'block' : 'none';
  }
}

function clearFilters() {
  const input = document.getElementById('pkgSearchInput');
  if (input) input.value = '';
  const allPill = document.querySelector('.cat-pill');
  if (allPill) selectCategory('All', allPill);
}

function copySnippet(text, btn) {
  navigator.clipboard.writeText(text).then(() => {
    const orig = btn.innerHTML;
    btn.innerHTML = '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2.5"><polyline points="20 6 9 17 4 12"></polyline></svg>';
    setTimeout(() => { btn.innerHTML = orig; }, 1800);
  });
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>

