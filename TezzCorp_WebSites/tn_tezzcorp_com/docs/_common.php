<?php
declare(strict_types=1);

require_once __DIR__ . '/../includes/nav.php';

function tn_doc_boot(string $path, string $active = 'docs'): array {
    $boot = tn_boot($path);
    $GLOBALS['tn_config'] = $boot['config'];
    $GLOBALS['tn_doc_active'] = $active;
    return $boot;
}

function tn_doc_nav(): void {
    $active = (string)($GLOBALS['tn_doc_active'] ?? '');
    $links = [
        ['docs', '/docs/', 'Overview'],
        ['getting-started', '/docs/getting-started', 'Getting Started'],
        ['language', '/docs/language', 'Language'],
        ['examples', '/docs/examples', 'Examples'],
        ['conformance', '/docs/conformance', 'Conformance'],
        ['developer-experience', '/docs/developer-experience', 'Developer Experience'],
        ['package-trust', '/docs/package-trust', 'Package Trust'],
        ['python-bridge', '/docs/python-bridge', 'Python Bridge'],
        ['actor-runtime', '/docs/actor-runtime', 'Actor Runtime'],
        ['sdk', '/docs/sdk', 'SDK Reference'],
        ['stdlib-core', '/docs/stdlib-core', 'Stdlib Core'],
        ['native-backend', '/docs/native-backend', 'Native Backend'],
        ['c-abi', '/docs/c-abi', 'C ABI'],
        ['benchmarks', '/docs/benchmarks', 'Benchmarks'],
        ['trust', '/docs/trust-baseline', 'Trust Baseline'],
        ['stability', '/docs/stability', 'Stability'],
        ['optimization', '/docs/optimization-plan', 'Optimization Plan'],
        ['backend', '/docs/backend', 'Backend'],
        ['tezzdb', '/docs/tezzdb', 'TezzDB'],
    ];
?>
<aside class="tn-sidebar" aria-label="Documentation navigation">
  <div class="tn-sidebar-header">
    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>
    <span>Documentation Index</span>
  </div>
  <div class="tn-sidebar-nav">
    <?php foreach ($links as [$key, $href, $label]): ?>
    <a href="<?= htmlspecialchars($href) ?>"<?= $active === $key ? ' class="active"' : '' ?>><?= htmlspecialchars($label) ?></a>
    <?php endforeach; ?>
  </div>
</aside>
<?php
}
