<?php
declare(strict_types=1);

/**
 * console/community.php — Community Developer & Forum Management
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'community';
$console_title = 'Community & Forum Manager';

$pdo = tn_pdo();

// Ensure community tables exist
if (function_exists('tn_community_boot')) {
    tn_community_boot($pdo);
}

// ── Handle Post Actions ───────────────────────────────────────────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $csrf = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/community.php');
        exit;
    }

    if ($action === 'update_user_role') {
        $user_id = (int)($_POST['user_id'] ?? 0);
        $role = trim((string)($_POST['role'] ?? 'developer'));
        $allowed = ['developer', 'contributor', 'tester', 'founder'];
        if ($user_id > 0 && in_array($role, $allowed, true)) {
            $stmt = $pdo->prepare("UPDATE tn_community_users SET role = :role, updated_at = NOW() WHERE id = :id");
            $stmt->execute([':role' => $role, ':id' => $user_id]);
            tn_set_flash('success', "User #{$user_id} role updated to '{$role}'.");
        }
        header('Location: /console/community.php');
        exit;
    }

    if ($action === 'toggle_verify') {
        $user_id = (int)($_POST['user_id'] ?? 0);
        $status = (int)($_POST['status'] ?? 0);
        if ($user_id > 0) {
            $stmt = $pdo->prepare("UPDATE tn_community_users SET is_verified = :v, updated_at = NOW() WHERE id = :id");
            $stmt->execute([':v' => $status ? 1 : 0, ':id' => $user_id]);
            tn_set_flash('success', "User #{$user_id} verification status toggled.");
        }
        header('Location: /console/community.php');
        exit;
    }

    if ($action === 'delete_user') {
        $user_id = (int)($_POST['user_id'] ?? 0);
        if ($user_id > 0) {
            $stmt = $pdo->prepare("DELETE FROM tn_community_users WHERE id = :id");
            $stmt->execute([':id' => $user_id]);
            tn_set_flash('success', "Community member #{$user_id} deleted.");
        }
        header('Location: /console/community.php');
        exit;
    }

    if ($action === 'delete_post') {
        $post_id = (int)($_POST['post_id'] ?? 0);
        if ($post_id > 0) {
            $stmt = $pdo->prepare("DELETE FROM tn_community_posts WHERE id = :id");
            $stmt->execute([':id' => $post_id]);
            tn_set_flash('success', "Forum thread #{$post_id} removed.");
        }
        header('Location: /console/community.php');
        exit;
    }

    if ($action === 'add_user') {
        $name = trim((string)($_POST['display_name'] ?? ''));
        $email = strtolower(trim((string)($_POST['email'] ?? '')));
        $pass = (string)($_POST['password'] ?? '');
        $role = trim((string)($_POST['role'] ?? 'developer'));
        $github = trim((string)($_POST['github_url'] ?? ''));
        $is_verified = !empty($_POST['is_verified']) ? 1 : 0;

        if (strlen($name) >= 2 && filter_var($email, FILTER_VALIDATE_EMAIL) && strlen($pass) >= 6) {
            try {
                $hash = password_hash($pass, PASSWORD_BCRYPT);
                $stmt = $pdo->prepare("INSERT INTO tn_community_users (display_name, email, password_hash, github_url, role, is_verified, join_date, created_at, updated_at) VALUES (:name, :email, :hash, :github, :role, :v, CURDATE(), NOW(), NOW())");
                $stmt->execute([
                    ':name'   => $name,
                    ':email'  => $email,
                    ':hash'   => $hash,
                    ':github' => $github ?: null,
                    ':role'   => $role,
                    ':v'      => $is_verified
                ]);
                tn_set_flash('success', "Member '{$name}' onboarded successfully.");
            } catch (Throwable $e) {
                tn_set_flash('error', "Database error: " . $e->getMessage());
            }
        } else {
            tn_set_flash('error', "Validation failed: Please ensure valid email and password >= 6 characters.");
        }
        header('Location: /console/community.php');
        exit;
    }
}

// ── Fetch KPIs ────────────────────────────────────────────────────────────────
$stats = [
    'users'    => 0,
    'verified' => 0,
    'posts'    => 0,
    'upvotes'  => 0,
];

try {
    $stats['users'] = (int)$pdo->query("SELECT count(*) FROM tn_community_users")->fetchColumn();
    $stats['verified'] = (int)$pdo->query("SELECT count(*) FROM tn_community_users WHERE is_verified = 1")->fetchColumn();
    $stats['posts'] = (int)$pdo->query("SELECT count(*) FROM tn_community_posts")->fetchColumn();
    $stats['upvotes'] = (int)$pdo->query("SELECT coalesce(sum(upvotes), 0) FROM tn_community_posts")->fetchColumn();
} catch (Throwable $e) {}

// ── Search & Filter Users ─────────────────────────────────────────────────────
$search = trim((string)($_GET['q'] ?? ''));
$role_filter = trim((string)($_GET['role'] ?? ''));

$sql = "SELECT id, display_name, email, bio, github_url, role, is_verified, join_date, created_at FROM tn_community_users WHERE 1=1";
$params = [];

if ($search !== '') {
    $sql .= " AND (display_name LIKE :q OR email LIKE :q OR github_url LIKE :q)";
    $params[':q'] = "%{$search}%";
}
if ($role_filter !== '' && in_array($role_filter, ['developer', 'contributor', 'tester', 'founder'], true)) {
    $sql .= " AND role = :role";
    $params[':role'] = $role_filter;
}
$sql .= " ORDER BY id DESC LIMIT 50";

$users = [];
try {
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $users = $stmt->fetchAll(PDO::FETCH_ASSOC);
} catch (Throwable $e) {}

// ── Fetch Recent Forum Posts ──────────────────────────────────────────────────
$posts = [];
try {
    $post_sql = "SELECT p.id, p.user_id, p.title, p.body, p.category, p.upvotes, p.created_at, u.display_name, u.role
                 FROM tn_community_posts p
                 LEFT JOIN tn_community_users u ON p.user_id = u.id
                 ORDER BY p.id DESC LIMIT 30";
    $posts = $pdo->query($post_sql)->fetchAll(PDO::FETCH_ASSOC);
} catch (Throwable $e) {}

require_once __DIR__ . '/includes/header.php';
?>

<!-- KPI Stat Overview -->
<div class="kpi-grid">
  <div class="kpi-card">
    <div class="kpi-label">Registered Developers</div>
    <div class="kpi-val"><?= $stats['users'] ?></div>
    <div class="kpi-meta">Active TezzNative community accounts</div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Verified Devs</div>
    <div class="kpi-val" style="color: #38bdf8;"><?= $stats['verified'] ?></div>
    <div class="kpi-meta">Certified contributors &amp; maintainers</div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Forum Threads</div>
    <div class="kpi-val"><?= $stats['posts'] ?></div>
    <div class="kpi-meta">Community discussions &amp; showcases</div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Total Upvotes</div>
    <div class="kpi-val" style="color: #10b981;"><?= $stats['upvotes'] ?></div>
    <div class="kpi-meta">Aggregated developer reactions</div>
  </div>
</div>

<!-- Community Users Panel -->
<div class="panel" style="margin-bottom: 24px;">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Community Members Directory</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Review developer registrations, manage contributor credentials, and enforce access roles.</p>
    </div>
    <div style="display: flex; gap: 10px; flex-wrap: wrap;">
      <button type="button" class="btn btn-primary" onclick="openModal('addUserModal')">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M16 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"></path><circle cx="9" cy="7" r="4"></circle><line x1="20" y1="8" x2="20" y2="14"></line><line x1="23" y1="11" x2="17" y2="11"></line></svg>
        Add Member
      </button>
    </div>
  </div>

  <!-- Search & Role Bar -->
  <form method="GET" style="display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap;">
    <input type="text" name="q" value="<?= htmlspecialchars($search) ?>" placeholder="Search by name, email, GitHub..." class="form-control" style="flex: 1; min-width: 220px;">
    <select name="role" class="form-control" style="width: auto; min-width: 160px;" onchange="this.form.submit()">
      <option value="">All Roles</option>
      <option value="developer" <?= $role_filter === 'developer' ? 'selected' : '' ?>>Developer</option>
      <option value="contributor" <?= $role_filter === 'contributor' ? 'selected' : '' ?>>Contributor</option>
      <option value="tester" <?= $role_filter === 'tester' ? 'selected' : '' ?>>Tester</option>
      <option value="founder" <?= $role_filter === 'founder' ? 'selected' : '' ?>>Founder</option>
    </select>
    <button type="submit" class="btn btn-outline">Filter</button>
    <?php if ($search !== '' || $role_filter !== ''): ?>
      <a href="/console/community.php" class="btn btn-outline">Clear</a>
    <?php endif; ?>
  </form>

  <div class="table-responsive">
    <table class="data-table">
      <thead>
        <tr>
          <th>ID</th>
          <th>Developer</th>
          <th>Email</th>
          <th>Role</th>
          <th>Verified</th>
          <th>Joined</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($users)): ?>
          <tr><td colspan="7" style="text-align: center; color: #64748b; padding: 28px;">No community members found matching query.</td></tr>
        <?php else: ?>
          <?php foreach ($users as $u): ?>
          <tr>
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.8rem; color: #64748b;">#<?= $u['id'] ?></td>
            <td>
              <div style="font-weight: 700; color: #f8fafc;"><?= htmlspecialchars($u['display_name']) ?></div>
              <?php if (!empty($u['github_url'])): ?>
                <a href="<?= htmlspecialchars($u['github_url']) ?>" target="_blank" rel="noopener" style="font-size: 0.75rem; color: #38bdf8; text-decoration: none;">GitHub Profile &rarr;</a>
              <?php endif; ?>
            </td>
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.82rem; color: #94a3b8;"><?= htmlspecialchars($u['email']) ?></td>
            <td>
              <form method="POST" style="margin: 0; display: inline-flex; align-items: center; gap: 6px;">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="update_user_role">
                <input type="hidden" name="user_id" value="<?= $u['id'] ?>">
                <select name="role" class="form-control" style="padding: 2px 8px; font-size: 0.75rem; width: auto;" onchange="this.form.submit()">
                  <option value="developer" <?= $u['role'] === 'developer' ? 'selected' : '' ?>>developer</option>
                  <option value="contributor" <?= $u['role'] === 'contributor' ? 'selected' : '' ?>>contributor</option>
                  <option value="tester" <?= $u['role'] === 'tester' ? 'selected' : '' ?>>tester</option>
                  <option value="founder" <?= $u['role'] === 'founder' ? 'selected' : '' ?>>founder</option>
                </select>
              </form>
            </td>
            <td>
              <form method="POST" style="margin: 0; display: inline;">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="toggle_verify">
                <input type="hidden" name="user_id" value="<?= $u['id'] ?>">
                <input type="hidden" name="status" value="<?= $u['is_verified'] ? '0' : '1' ?>">
                <?php if ($u['is_verified']): ?>
                  <button type="submit" class="badge badge-success" style="cursor: pointer; border: none;" title="Click to unverify">&#10003; Verified</button>
                <?php else: ?>
                  <button type="submit" class="badge badge-warning" style="cursor: pointer; border: none;" title="Click to verify">Unverified</button>
                <?php endif; ?>
              </form>
            </td>
            <td style="font-size: 0.8rem; color: #94a3b8; white-space: nowrap;"><?= htmlspecialchars($u['join_date'] ?? substr($u['created_at'], 0, 10)) ?></td>
            <td>
              <form method="POST" style="margin: 0; display: inline;" onsubmit="return confirm('Delete community member #<?= $u['id'] ?> (<?= htmlspecialchars(addslashes($u['display_name'])) ?>)? This cannot be undone.');">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="delete_user">
                <input type="hidden" name="user_id" value="<?= $u['id'] ?>">
                <button type="submit" class="btn btn-danger" style="padding: 3px 8px; font-size: 0.72rem;">Delete</button>
              </form>
            </td>
          </tr>
          <?php endforeach; ?>
        <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<!-- Forum Posts Panel -->
<div class="panel">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Forum Threads &amp; Discussions</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Monitor developer discussions, bug reports, and showcase topics.</p>
    </div>
  </div>

  <div class="table-responsive">
    <table class="data-table">
      <thead>
        <tr>
          <th>ID</th>
          <th>Topic &amp; Excerpt</th>
          <th>Author</th>
          <th>Category</th>
          <th>Upvotes</th>
          <th>Published</th>
          <th>Action</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($posts)): ?>
          <tr><td colspan="7" style="text-align: center; color: #64748b; padding: 28px;">No forum posts recorded yet.</td></tr>
        <?php else: ?>
          <?php foreach ($posts as $p): ?>
          <tr>
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.8rem; color: #64748b;">#<?= $p['id'] ?></td>
            <td style="max-width: 320px;">
              <div style="font-weight: 700; color: #f8fafc; font-size: 0.9rem;"><?= htmlspecialchars($p['title']) ?></div>
              <div style="font-size: 0.78rem; color: #94a3b8; margin-top: 2px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;"><?= htmlspecialchars(substr($p['body'], 0, 90)) ?>...</div>
            </td>
            <td>
              <div style="font-size: 0.85rem; color: #e2e8f0; font-weight: 600;"><?= htmlspecialchars($p['display_name'] ?? 'Anonymous') ?></div>
              <span class="badge badge-info" style="font-size: 0.65rem;"><?= htmlspecialchars($p['role'] ?? 'dev') ?></span>
            </td>
            <td>
              <span class="badge badge-info"><?= htmlspecialchars($p['category']) ?></span>
            </td>
            <td style="font-family: 'JetBrains Mono', monospace; color: #10b981; font-weight: 700;">+<?= (int)$p['upvotes'] ?></td>
            <td style="font-size: 0.8rem; color: #94a3b8; white-space: nowrap;"><?= htmlspecialchars(substr($p['created_at'], 0, 16)) ?></td>
            <td>
              <form method="POST" style="margin: 0; display: inline;" onsubmit="return confirm('Delete forum post #<?= $p['id'] ?>?');">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="delete_post">
                <input type="hidden" name="post_id" value="<?= $p['id'] ?>">
                <button type="submit" class="btn btn-danger" style="padding: 3px 8px; font-size: 0.72rem;">Delete</button>
              </form>
            </td>
          </tr>
          <?php endforeach; ?>
        <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<!-- Add Member Modal -->
<div class="modal-backdrop" id="addUserModal">
  <div class="modal-card">
    <div class="modal-header">
      <h3 class="modal-title">Onboard New Community Member</h3>
      <button type="button" class="modal-close" onclick="closeModal('addUserModal')">&times;</button>
    </div>
    <form method="POST">
      <div class="modal-body">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" value="add_user">

        <div class="form-group">
          <label class="form-label" for="display_name">Display Name *</label>
          <input type="text" id="display_name" name="display_name" class="form-control" placeholder="e.g. Alex Vance" required>
        </div>

        <div class="form-group">
          <label class="form-label" for="email">Developer Email *</label>
          <input type="email" id="email" name="email" class="form-control" placeholder="alex@example.org" required>
        </div>

        <div class="form-group">
          <label class="form-label" for="password">Initial Password *</label>
          <input type="password" id="password" name="password" class="form-control" placeholder="Minimum 6 characters" required>
        </div>

        <div class="form-group">
          <label class="form-label" for="github_url">GitHub URL</label>
          <input type="url" id="github_url" name="github_url" class="form-control" placeholder="https://github.com/username">
        </div>

        <div class="form-group">
          <label class="form-label" for="role">Assign Role</label>
          <select id="role" name="role" class="form-control">
            <option value="developer">Developer</option>
            <option value="contributor">Contributor</option>
            <option value="tester">Tester</option>
            <option value="founder">Founder</option>
          </select>
        </div>

        <div class="form-group">
          <label style="display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: #e2e8f0; cursor: pointer;">
            <input type="checkbox" name="is_verified" value="1" checked>
            Mark as Verified Developer immediately
          </label>
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-outline" onclick="closeModal('addUserModal')">Cancel</button>
        <button type="submit" class="btn btn-primary">Create Account</button>
      </div>
    </form>
  </div>
</div>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
