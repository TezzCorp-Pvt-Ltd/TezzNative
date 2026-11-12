<?php
declare(strict_types=1);

/**
 * console/packages.php — Package Registry Management (CRUD, Hashing, Metrics)
 */

$active_nav = 'packages';
$console_title = 'Package Registry Manager';

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$pdo = null;
try {
    $pdo = tn_pdo();
} catch (Throwable $e) {}

// Handle POST actions
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $token = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($token)) {
        tn_set_flash('error', 'CSRF verification failed. Please try again.');
        header("Location: /console/packages.php");
        exit;
    }

    if ($action === 'create' || $action === 'edit') {
        $name = strtolower(trim((string)($_POST['name'] ?? '')));
        $cleanName = preg_replace('/[^a-z0-9_]/', '', $name);
        $version = trim((string)($_POST['version'] ?? '1.0.0'));
        $category = trim((string)($_POST['category'] ?? 'Systems & OS'));
        $desc = trim((string)($_POST['description'] ?? ''));
        $tags = trim((string)($_POST['tags'] ?? ''));
        $license = trim((string)($_POST['license'] ?? 'MIT'));
        $author = trim((string)($_POST['author_name'] ?? 'Rohit Pathak (TEZZCORP PRIVATE LIMITED)'));
        $example = trim((string)($_POST['example_code'] ?? ''));
        $content = (string)($_POST['source_code'] ?? '');

        // File upload override
        if (!empty($_FILES['source_file']['tmp_name']) && is_uploaded_file($_FILES['source_file']['tmp_name'])) {
            $content = file_get_contents($_FILES['source_file']['tmp_name']);
        }

        if (empty($cleanName)) {
            tn_set_flash('error', 'Package name is required.');
            header("Location: /console/packages.php");
            exit;
        }

        // Save .tn file locally if content provided
        $checksum = '';
        if (!empty($content)) {
            $libPath = __DIR__ . '/../download/sdk/lib/' . $cleanName . '.tn';
            file_put_contents($libPath, $content);
            $checksum = hash('sha256', $content);
        }

        if ($pdo) {
            try {
                if ($action === 'create') {
                    // Check if already exists
                    $stmt = $pdo->prepare("SELECT id FROM tn_package_registry WHERE name = ?");
                    $stmt->execute([$cleanName]);
                    if ($stmt->fetch()) {
                        tn_set_flash('error', "Package '{$cleanName}' already exists. Use edit instead.");
                        header("Location: /console/packages.php");
                        exit;
                    }

                    $repoUrl = "https://tezznative.org/download/sdk/lib/{$cleanName}.tn";
                    $stmt = $pdo->prepare("
                        INSERT INTO tn_package_registry 
                        (name, version, description, category, author_name, repository_url, downloads, checksum, tags, license, example_code, created_at, updated_at)
                        VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, NOW(), NOW())
                    ");
                    $stmt->execute([$cleanName, $version, $desc, $category, $author, $repoUrl, $checksum, $tags, $license, $example]);
                    tn_set_flash('success', "Package '{$cleanName}' v{$version} created successfully!");
                } else {
                    // Update
                    $stmt = $pdo->prepare("
                        UPDATE tn_package_registry SET
                            version = ?,
                            description = ?,
                            category = ?,
                            author_name = ?,
                            tags = ?,
                            license = ?,
                            example_code = ?,
                            updated_at = NOW()
                            " . ($checksum ? ", checksum = ?" : "") . "
                        WHERE name = ?
                    ");
                    $params = [$version, $desc, $category, $author, $tags, $license, $example];
                    if ($checksum) $params[] = $checksum;
                    $params[] = $cleanName;
                    $stmt->execute($params);
                    tn_set_flash('success', "Package '{$cleanName}' updated successfully!");
                }
            } catch (Throwable $e) {
                tn_set_flash('error', 'Database error: ' . $e->getMessage());
            }
        } else {
            tn_set_flash('error', 'Database connection not available.');
        }

        header("Location: /console/packages.php");
        exit;
    } elseif ($action === 'delete') {
        $cleanName = preg_replace('/[^a-z0-9_]/', '', strtolower(trim((string)($_POST['name'] ?? ''))));
        if ($pdo && !empty($cleanName)) {
            try {
                $stmt = $pdo->prepare("DELETE FROM tn_package_registry WHERE name = ?");
                $stmt->execute([$cleanName]);
                tn_set_flash('success', "Package '{$cleanName}' deleted from registry.");
            } catch (Throwable $e) {
                tn_set_flash('error', 'Delete error: ' . $e->getMessage());
            }
        }
        header("Location: /console/packages.php");
        exit;
    }
}

// Fetch packages list
$packages = [];
if ($pdo) {
    try {
        $stmt = $pdo->query("SELECT * FROM tn_package_registry ORDER BY name ASC");
        $packages = $stmt->fetchAll(PDO::FETCH_ASSOC);
    } catch (Throwable $e) {}
}

if (empty($packages)) {
    $seed_file = __DIR__ . '/../config/packages_seed.json';
    if (file_exists($seed_file)) {
        $packages = json_decode((string)file_get_contents($seed_file), true) ?: [];
    }
}

// Categories list
$categories = [
    'GUI & Graphics',
    'AI & Deep Learning',
    'Systems & OS',
    'Networking & Cloud',
    'Database & Storage',
    'Embedded & Hardware',
    'DevOps & Tooling'
];

require_once __DIR__ . '/includes/header.php';
?>

<div class="panel">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2.2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path></svg>
        <span>Package Registry Directory (<?= count($packages) ?> total)</span>
      </h3>
      <p style="font-size: 0.82rem; color: var(--c-text-muted); margin-top: 4px;">
        Manage TezzNative standard libraries, inspect checksums, and publish new modules.
      </p>
    </div>

    <div class="panel-actions">
      <input type="text" id="tableSearchInput" class="c-input" placeholder="Filter packages..." style="width: 220px; padding: 7px 12px; font-size: 0.84rem;">
      <button type="button" class="c-btn c-btn-primary" data-modal-open="addPackageModal">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
        <span>Add Package</span>
      </button>
    </div>
  </div>

  <div class="table-wrapper">
    <table class="console-table">
      <thead>
        <tr>
          <th>Name</th>
          <th>Version</th>
          <th>Category</th>
          <th>Downloads</th>
          <th>SHA-256 Checksum</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody>
        <?php foreach ($packages as $pkg): ?>
          <tr class="filterable-row">
            <td>
              <a href="/package?name=<?= urlencode($pkg['name']) ?>" target="_blank" style="font-weight: 700; color: #38bdf8; text-decoration: none; display: flex; align-items: center; gap: 4px;">
                <code><?= htmlspecialchars($pkg['name']) ?></code>
                <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"></path><polyline points="15 3 21 3 21 9"></polyline><line x1="10" y1="14" x2="21" y2="3"></line></svg>
              </a>
              <div style="font-size: 0.74rem; color: var(--c-text-muted); max-width: 280px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">
                <?= htmlspecialchars($pkg['description'] ?? '') ?>
              </div>
            </td>
            <td>
              <span style="font-size: 0.72rem; padding: 2px 7px; border-radius: 9999px; background: rgba(14, 165, 233, 0.12); color: #0284c7; font-weight: 700; border: 1px solid rgba(14, 165, 233, 0.3);">
                v<?= htmlspecialchars($pkg['version'] ?? '1.0.0') ?>
              </span>
            </td>
            <td><span style="font-size: 0.82rem; color: var(--c-text-secondary);"><?= htmlspecialchars($pkg['category'] ?? 'Core') ?></span></td>
            <td><strong style="color: #10b981;"><?= number_format((int)($pkg['downloads'] ?? 0)) ?></strong></td>
            <td>
              <?php if (!empty($pkg['checksum'])): ?>
                <span title="<?= htmlspecialchars($pkg['checksum']) ?>" style="font-family: var(--font-mono); font-size: 0.74rem; color: var(--c-text-muted);">
                  <?= substr($pkg['checksum'], 0, 10) ?>...<?= substr($pkg['checksum'], -6) ?>
                </span>
              <?php else: ?>
                <span style="color: var(--c-text-muted); font-size: 0.74rem;">Pending</span>
              <?php endif; ?>
            </td>
            <td>
              <div style="display: flex; gap: 6px;">
                <button type="button" class="c-btn c-btn-secondary c-btn-sm" 
                        onclick="openEditModal(<?= htmlspecialchars(json_encode($pkg)) ?>)">
                  Edit
                </button>
                <form method="POST" action="" onsubmit="return confirm('Are you sure you want to delete <?= htmlspecialchars($pkg['name']) ?>?');">
                  <input type="hidden" name="action" value="delete">
                  <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
                  <input type="hidden" name="name" value="<?= htmlspecialchars($pkg['name']) ?>">
                  <button type="submit" class="c-btn c-btn-danger c-btn-sm" title="Delete Package">Del</button>
                </form>
              </div>
            </td>
          </tr>
        <?php endforeach; ?>
      </tbody>
    </table>
  </div>
</div>

<!-- Modal: Add New Package -->
<div class="c-modal-overlay" id="addPackageModal">
  <div class="c-modal-card">
    <div class="c-modal-header">
      <h3 class="c-modal-title">Publish New TezzNative Package</h3>
      <button type="button" class="c-btn c-btn-secondary c-btn-sm" data-modal-close>&times;</button>
    </div>
    <form method="POST" action="" enctype="multipart/form-data">
      <input type="hidden" name="action" value="create">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
      <div class="c-modal-body" style="display: flex; flex-direction: column; gap: 14px;">
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
          <div>
            <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Package Name (e.g. tznet)</label>
            <input type="text" name="name" class="c-input" required placeholder="my_library">
          </div>
          <div>
            <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Version</label>
            <input type="text" name="version" class="c-input" required value="1.0.0">
          </div>
        </div>

        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
          <div>
            <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Category</label>
            <select name="category" class="c-input">
              <?php foreach ($categories as $cat): ?>
                <option value="<?= htmlspecialchars($cat) ?>"><?= htmlspecialchars($cat) ?></option>
              <?php endforeach; ?>
            </select>
          </div>
          <div>
            <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">License</label>
            <input type="text" name="license" class="c-input" value="MIT">
          </div>
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Short Description</label>
          <input type="text" name="description" class="c-input" placeholder="High performance library for...">
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Example Usage Code (.tn)</label>
          <textarea name="example_code" class="c-input" rows="4" placeholder="import my_library&#10;&#10;fn main() -> int:&#10;  say &quot;Hello&quot;&#10;  ret 0"></textarea>
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Upload Source (.tn file)</label>
          <input type="file" name="source_file" accept=".tn" class="c-input" style="padding: 6px;">
        </div>
      </div>
      <div class="c-modal-footer">
        <button type="button" class="c-btn c-btn-secondary" data-modal-close>Cancel</button>
        <button type="submit" class="c-btn c-btn-primary">Publish to Registry</button>
      </div>
    </form>
  </div>
</div>

<!-- Modal: Edit Package -->
<div class="c-modal-overlay" id="editPackageModal">
  <div class="c-modal-card">
    <div class="c-modal-header">
      <h3 class="c-modal-title">Edit Package: <span id="editPkgTitle" style="color: #38bdf8;"></span></h3>
      <button type="button" class="c-btn c-btn-secondary c-btn-sm" data-modal-close>&times;</button>
    </div>
    <form method="POST" action="">
      <input type="hidden" name="action" value="edit">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
      <input type="hidden" name="name" id="editPkgName">
      <div class="c-modal-body" style="display: flex; flex-direction: column; gap: 14px;">
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
          <div>
            <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Version</label>
            <input type="text" name="version" id="editPkgVersion" class="c-input" required>
          </div>
          <div>
            <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Category</label>
            <select name="category" id="editPkgCategory" class="c-input">
              <?php foreach ($categories as $cat): ?>
                <option value="<?= htmlspecialchars($cat) ?>"><?= htmlspecialchars($cat) ?></option>
              <?php endforeach; ?>
            </select>
          </div>
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Description</label>
          <input type="text" name="description" id="editPkgDesc" class="c-input">
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Tags</label>
          <input type="text" name="tags" id="editPkgTags" class="c-input">
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Example Usage Code (.tn)</label>
          <textarea name="example_code" id="editPkgExample" class="c-input" rows="5"></textarea>
        </div>
      </div>
      <div class="c-modal-footer">
        <button type="button" class="c-btn c-btn-secondary" data-modal-close>Cancel</button>
        <button type="submit" class="c-btn c-btn-primary">Save Changes</button>
      </div>
    </form>
  </div>
</div>

<script>
function openEditModal(pkg) {
  document.getElementById('editPkgTitle').innerText = pkg.name;
  document.getElementById('editPkgName').value = pkg.name;
  document.getElementById('editPkgVersion').value = pkg.version || '1.0.0';
  document.getElementById('editPkgCategory').value = pkg.category || 'Systems & OS';
  document.getElementById('editPkgDesc').value = pkg.description || '';
  document.getElementById('editPkgTags').value = pkg.tags || '';
  document.getElementById('editPkgExample').value = pkg.example_code || '';
  document.getElementById('editPackageModal').classList.add('active');
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
