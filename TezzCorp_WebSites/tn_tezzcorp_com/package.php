<?php
// package.php — TezzNative Single Package Details & Documentation Page
require_once __DIR__ . '/config/db.php';

$name = isset($_GET['name']) ? trim((string)$_GET['name']) : '';
$cleanName = preg_replace('/[^a-zA-Z0-9_]/', '', $name);

function get_package_detail(string $pkgName): ?array {
    $seed_file = __DIR__ . '/config/packages_seed.json';
    try {
        $pdo = tn_pdo();
        $stmt = $pdo->prepare("SELECT * FROM tn_package_registry WHERE name = ? LIMIT 1");
        $stmt->execute([$pkgName]);
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        if ($row) return $row;
    } catch (Throwable $e) {}

    if (file_exists($seed_file)) {
        $data = json_decode(file_get_contents($seed_file), true);
        if (is_array($data)) {
            foreach ($data as $item) {
                if (strcasecmp($item['name'], $pkgName) === 0) {
                    return $item;
                }
            }
        }
    }
    return null;
}

function get_related_packages(string $cat, string $excludeName): array {
    $seed_file = __DIR__ . '/config/packages_seed.json';
    try {
        $pdo = tn_pdo();
        $stmt = $pdo->prepare("SELECT name, version, description, category, downloads FROM tn_package_registry WHERE category = ? AND name != ? ORDER BY downloads DESC LIMIT 4");
        $stmt->execute([$cat, $excludeName]);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);
        if (!empty($rows)) return $rows;
    } catch (Throwable $e) {}

    if (file_exists($seed_file)) {
        $data = json_decode(file_get_contents($seed_file), true);
        if (is_array($data)) {
            $matched = [];
            foreach ($data as $item) {
                if ($item['name'] !== $excludeName && ($item['category'] ?? '') === $cat) {
                    $matched[] = $item;
                    if (count($matched) >= 4) break;
                }
            }
            return $matched;
        }
    }
    return [];
}

$pkg = get_package_detail($cleanName);

if (!$pkg) {
    http_response_code(404);
    $page_title = "Package Not Found — TezzNative Registry";
    $page_desc  = "The requested package could not be found in the official TezzNative registry.";
    require_once __DIR__ . '/includes/header.php';
    ?>
    <main class="site-main" style="padding: 80px 20px; text-align: center;">
      <div style="max-width: 600px; margin: 0 auto; background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 16px; padding: 40px;">
        <h1 style="font-size: 2rem; font-weight: 800; margin-bottom: 12px;">Package Not Found</h1>
        <p style="color: var(--text-secondary); margin-bottom: 24px;">The library <code><?= htmlspecialchars($name) ?></code> does not exist in the official registry.</p>
        <a href="/packages" class="btn btn-primary" style="padding: 10px 22px; text-decoration: none;">Browse All Packages &rarr;</a>
      </div>
    </main>
    <?php
    require_once __DIR__ . '/includes/footer.php';
    exit;
}

$page_title = "{$pkg['name']} (v{$pkg['version']}) — TezzNative Package";
$page_desc  = htmlspecialchars($pkg['description'] ?? "Official TezzNative library {$pkg['name']}");
$active_nav = "packages";
$canonical_path = "/package/" . urlencode($pkg['name']);

require_once __DIR__ . '/includes/header.php';

$related = get_related_packages($pkg['category'] ?? 'Systems & OS', $pkg['name']);
$githubUrl = "https://github.com/TezzCorp-Pvt-Ltd/TezzNative/blob/main/lib/{$pkg['name']}.tn";
$dlUrl = "/api/download_package.php?name=" . urlencode($pkg['name']);
?>

<main class="site-main">
  <!-- Package Header -->
  <section class="section" style="padding: 48px 0 28px; background: radial-gradient(circle at 50% 0%, rgba(14, 165, 233, 0.12) 0%, transparent 60%); border-bottom: 1px solid var(--border-subtle);">
    <div class="section-container" style="max-width: 1100px; margin: 0 auto; padding: 0 20px;">
      
      <!-- Breadcrumb -->
      <nav style="display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: var(--text-tertiary); margin-bottom: 20px;">
        <a href="/" style="color: var(--text-secondary); text-decoration: none;">Home</a>
        <span>/</span>
        <a href="/packages" style="color: var(--text-secondary); text-decoration: none;">Packages</a>
        <span>/</span>
        <span style="color: #0284c7; font-weight: 600;"><?= htmlspecialchars($pkg['name']) ?></span>
      </nav>

      <div style="display: flex; justify-content: space-between; align-items: flex-start; flex-wrap: wrap; gap: 20px;">
        <div>
          <div style="display: flex; align-items: center; gap: 12px; margin-bottom: 8px;">
            <h1 style="font-size: 2.4rem; font-weight: 800; letter-spacing: -0.5px; margin: 0; font-family: var(--font-mono); color: var(--text-primary);">
              <?= htmlspecialchars($pkg['name']) ?>
            </h1>
            <span class="version-tag" style="font-size: 0.82rem; font-weight: 700; padding: 4px 12px; border-radius: 9999px; background: rgba(14, 165, 233, 0.12); color: #0284c7; border: 1px solid rgba(14, 165, 233, 0.3);">
              v<?= htmlspecialchars($pkg['version'] ?? '1.0.0') ?>
            </span>
            <span style="font-size: 0.78rem; font-weight: 600; padding: 4px 10px; border-radius: 6px; background: var(--bg-surface-raised); color: var(--text-secondary); border: 1px solid var(--border-subtle);">
              <?= htmlspecialchars($pkg['category'] ?? 'Standard Library') ?>
            </span>
          </div>
          <p style="font-size: 1.12rem; color: var(--text-secondary); max-width: 720px; line-height: 1.6; margin-bottom: 14px;">
            <?= htmlspecialchars($pkg['description'] ?? 'Official high-performance TezzNative standard library module.') ?>
          </p>
          <div style="display: flex; flex-wrap: wrap; gap: 18px; font-size: 0.86rem; color: var(--text-tertiary);">
            <span>Publisher: <strong style="color: var(--text-primary);"><?= htmlspecialchars($pkg['author_name'] ?? 'TEZZCORP PRIVATE LIMITED') ?></strong></span>
            <span>&bull;</span>
            <span>License: <strong style="color: var(--text-primary);"><?= htmlspecialchars($pkg['license'] ?? 'MIT') ?></strong></span>
            <span>&bull;</span>
            <span>Downloads: <strong style="color: #10b981;"><?= number_format((int)($pkg['downloads'] ?? 0)) ?></strong></span>
          </div>
        </div>

        <!-- Action CTAs -->
        <div style="display: flex; flex-direction: column; gap: 10px; min-width: 220px;">
          <a href="<?= $dlUrl ?>" class="btn btn-primary" style="display: flex; align-items: center; justify-content: center; gap: 8px; padding: 12px 20px; text-decoration: none; font-weight: 700;">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
            <span>Download .tn Source</span>
          </a>
          <a href="<?= $githubUrl ?>" target="_blank" rel="noopener" class="btn btn-secondary" style="display: flex; align-items: center; justify-content: center; gap: 8px; padding: 10px 18px; text-decoration: none; font-weight: 600;">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 19c-5 1.5-5-2.5-7-3m14 6v-3.87a3.37 3.37 0 0 0-.94-2.61c3.14-.35 6.44-1.54 6.44-7A5.44 5.44 0 0 0 20 4.77 5.07 5.07 0 0 0 19.91 1S18.73.65 16 2.48a13.38 13.38 0 0 0-7 0C6.27.65 5.09 1 5.09 1A5.07 5.07 0 0 0 5 4.77a5.44 5.44 0 0 0-1.5 3.78c0 5.42 3.3 6.61 6.44 7A3.37 3.37 0 0 0 9 18.13V22"></path></svg>
            <span>View on GitHub</span>
          </a>
        </div>
      </div>

    </div>
  </section>

  <!-- Package Content & Instructions -->
  <section class="section" style="padding: 40px 0 60px;">
    <div class="section-container" style="max-width: 1100px; margin: 0 auto; padding: 0 20px;">
      <div style="display: grid; grid-template-columns: 2fr 1fr; gap: 32px;" class="pkg-detail-grid">
        
        <!-- Left Column: Code & Documentation -->
        <div>
          <!-- Install Box -->
          <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 14px; padding: 24px; margin-bottom: 28px;">
            <h3 style="font-size: 1.15rem; font-weight: 800; margin-bottom: 12px; display: flex; align-items: center; gap: 8px;">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#0284c7" stroke-width="2.5"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
              <span>Installation &amp; Inclusion</span>
            </h3>
            
            <p style="font-size: 0.92rem; color: var(--text-secondary); margin-bottom: 10px;">Run from your project root:</p>
            <div style="display: flex; align-items: center; justify-content: space-between; background: var(--code-bg); border: 1px solid var(--border-subtle); border-radius: 8px; padding: 12px 16px; margin-bottom: 18px;">
              <code style="font-family: var(--font-mono); font-size: 0.95rem; color: #38bdf8;">tezz install <?= htmlspecialchars($pkg['name']) ?></code>
              <button type="button" onclick="copySnippet('tezz install <?= htmlspecialchars($pkg['name']) ?>', this)" title="Copy command"
                      style="background: transparent; border: none; color: var(--text-tertiary); cursor: pointer; padding: 4px; display: flex; align-items: center;">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
              </button>
            </div>

            <p style="font-size: 0.92rem; color: var(--text-secondary); margin-bottom: 10px;">Or add directly to <code>tezz.mod</code>:</p>
            <div style="background: var(--code-bg); border: 1px solid var(--border-subtle); border-radius: 8px; padding: 12px 16px;">
              <code style="font-family: var(--font-mono); font-size: 0.88rem; color: #10b981;">dep.<?= htmlspecialchars($pkg['name']) ?> = <?= htmlspecialchars($pkg['version'] ?? '1.0.0') ?></code>
            </div>
          </div>

          <!-- Example Usage Box -->
          <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 14px; padding: 24px; margin-bottom: 28px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px;">
              <h3 style="font-size: 1.15rem; font-weight: 800; margin: 0; display: flex; align-items: center; gap: 8px;">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2.5"><polyline points="16 18 22 12 16 6"></polyline><polyline points="8 6 2 12 8 18"></polyline></svg>
                <span>Example Usage in TezzNative</span>
              </h3>
              <button type="button" onclick="copySnippet(document.getElementById('codeExample').innerText, this)"
                      style="background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-secondary); padding: 5px 12px; border-radius: 6px; font-size: 0.78rem; font-weight: 600; cursor: pointer; display: flex; align-items: center; gap: 6px;">
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                <span>Copy Code</span>
              </button>
            </div>

            <pre style="background: var(--code-bg); border: 1px solid var(--border-subtle); border-radius: 8px; padding: 18px; font-family: var(--font-mono); font-size: 0.88rem; line-height: 1.5; color: #f8fafc; overflow-x: auto;"><code id="codeExample"><?= htmlspecialchars($pkg['example_code'] ?? "import {$pkg['name']}\n\nfn main() -> int:\n  say \"Using {$pkg['name']} v1.0.0\"\n  ret 0") ?></code></pre>
          </div>

          <!-- Integrity & Checksum -->
          <?php if (!empty($pkg['checksum'])): ?>
            <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 14px; padding: 22px;">
              <h4 style="font-size: 0.95rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.6px; color: var(--text-tertiary); margin-bottom: 10px;">
                Cryptographic Integrity (SHA-256)
              </h4>
              <div style="background: var(--code-bg); border: 1px solid var(--border-subtle); border-radius: 8px; padding: 10px 14px; font-family: var(--font-mono); font-size: 0.8rem; color: #38bdf8; word-break: break-all; display: flex; align-items: center; justify-content: space-between; gap: 12px;">
                <span><?= htmlspecialchars($pkg['checksum']) ?></span>
                <button type="button" onclick="copySnippet('<?= htmlspecialchars($pkg['checksum']) ?>', this)" title="Copy checksum"
                        style="background: transparent; border: none; color: var(--text-tertiary); cursor: pointer; padding: 2px;">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                </button>
              </div>
            </div>
          <?php endif; ?>
        </div>

        <!-- Right Column: Metadata & Related -->
        <div>
          <!-- Metadata Card -->
          <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 14px; padding: 22px; margin-bottom: 24px;">
            <h4 style="font-size: 0.85rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-tertiary); margin-bottom: 16px;">
              Package Metadata
            </h4>
            <ul style="list-style: none; padding: 0; margin: 0; display: flex; flex-direction: column; gap: 12px; font-size: 0.9rem;">
              <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--border-subtle); padding-bottom: 8px;">
                <span style="color: var(--text-tertiary);">Version:</span>
                <strong style="color: var(--text-primary);">v<?= htmlspecialchars($pkg['version'] ?? '1.0.0') ?></strong>
              </li>
              <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--border-subtle); padding-bottom: 8px;">
                <span style="color: var(--text-tertiary);">Category:</span>
                <strong style="color: var(--text-primary);"><?= htmlspecialchars($pkg['category'] ?? 'Core') ?></strong>
              </li>
              <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--border-subtle); padding-bottom: 8px;">
                <span style="color: var(--text-tertiary);">License:</span>
                <strong style="color: var(--text-primary);"><?= htmlspecialchars($pkg['license'] ?? 'MIT') ?></strong>
              </li>
              <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--border-subtle); padding-bottom: 8px;">
                <span style="color: var(--text-tertiary);">Downloads:</span>
                <strong style="color: #10b981;"><?= number_format((int)($pkg['downloads'] ?? 0)) ?></strong>
              </li>
              <li style="display: flex; justify-content: space-between; border-bottom: 1px solid var(--border-subtle); padding-bottom: 8px;">
                <span style="color: var(--text-tertiary);">Source:</span>
                <strong style="color: var(--text-primary);"><?= htmlspecialchars($pkg['name']) ?>.tn</strong>
              </li>
              <li style="display: flex; justify-content: space-between;">
                <span style="color: var(--text-tertiary);">Company:</span>
                <strong style="color: var(--text-primary);">TEZZCORP PRIVATE LIMITED</strong>
              </li>
            </ul>
          </div>

          <!-- Related Libraries in Category -->
          <?php if (!empty($related)): ?>
            <div style="background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: 14px; padding: 22px;">
              <h4 style="font-size: 0.85rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-tertiary); margin-bottom: 14px;">
                Related in <?= htmlspecialchars($pkg['category']) ?>
              </h4>
              <div style="display: flex; flex-direction: column; gap: 10px;">
                <?php foreach ($related as $rel): ?>
                  <a href="/package?name=<?= urlencode($rel['name']) ?>" style="display: block; text-decoration: none; padding: 10px 12px; border-radius: 8px; background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); transition: border-color 0.2s;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                      <code style="font-weight: 700; color: #0284c7; font-size: 0.95rem;"><?= htmlspecialchars($rel['name']) ?></code>
                      <span style="font-size: 0.72rem; color: var(--text-tertiary);">v<?= htmlspecialchars($rel['version'] ?? '1.0.0') ?></span>
                    </div>
                    <div style="font-size: 0.8rem; color: var(--text-secondary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">
                      <?= htmlspecialchars($rel['description'] ?? '') ?>
                    </div>
                  </a>
                <?php endforeach; ?>
              </div>
            </div>
          <?php endif; ?>
        </div>

      </div>
    </div>
  </section>
</main>

<script>
function copySnippet(text, btn) {
  navigator.clipboard.writeText(text).then(() => {
    const orig = btn.innerHTML;
    btn.innerHTML = '<span style="color: #10b981; font-weight: 700;">Copied!</span>';
    setTimeout(() => { btn.innerHTML = orig; }, 1800);
  });
}
</script>

<style>
@media (max-width: 820px) {
  .pkg-detail-grid {
    grid-template-columns: 1fr !important;
  }
}
</style>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
