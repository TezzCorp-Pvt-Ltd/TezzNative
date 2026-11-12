<?php
declare(strict_types=1);

/**
 * console/community.php — Community Developer & Forum Management
 * Supports Community Member management with Photo/Avatar Upload and Database Synchronization.
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'community';
$console_title = 'Community & Forum Manager';

$pdo = tn_pdo();

// Helper to handle avatar image upload
function tn_handle_avatar_upload(?array $file): ?string {
    if (!$file || ($file['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) {
        return null;
    }
    $allowed = ['image/jpeg', 'image/png', 'image/webp', 'image/gif'];
    $finfo = finfo_open(FILEINFO_MIME_TYPE);
    $mime = finfo_file($finfo, $file['tmp_name']);
    finfo_close($finfo);

    if (!in_array($mime, $allowed, true)) {
        return null;
    }
    if ($file['size'] > 5 * 1024 * 1024) { // 5MB max
        return null;
    }

    $ext = match($mime) {
        'image/jpeg' => 'jpg',
        'image/png'  => 'png',
        'image/webp' => 'webp',
        'image/gif'  => 'gif',
        default      => 'jpg',
    };

    $uploadDir = __DIR__ . '/../../uploads/avatars';
    if (!is_dir($uploadDir)) {
        @mkdir($uploadDir, 0755, true);
    }

    $filename = 'avatar_' . time() . '_' . bin2hex(random_bytes(6)) . '.' . $ext;
    $target = $uploadDir . '/' . $filename;

    if (move_uploaded_file($file['tmp_name'], $target)) {
        return '/uploads/avatars/' . $filename;
    }
    return null;
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
        $bio = trim((string)($_POST['bio'] ?? ''));
        $github = trim((string)($_POST['github_url'] ?? ''));
        $is_verified = !empty($_POST['is_verified']) ? 1 : 0;

        $avatar_url = null;
        if (isset($_FILES['avatar'])) {
            $avatar_url = tn_handle_avatar_upload($_FILES['avatar']);
        }

        if (strlen($name) >= 2 && filter_var($email, FILTER_VALIDATE_EMAIL) && strlen($pass) >= 6) {
            try {
                $hash = password_hash($pass, PASSWORD_BCRYPT);
                $stmt = $pdo->prepare("INSERT INTO tn_community_users (display_name, email, password_hash, avatar_url, bio, github_url, role, is_verified, join_date, created_at, updated_at) VALUES (:name, :email, :hash, :avatar, :bio, :github, :role, :v, CURDATE(), NOW(), NOW())");
                $stmt->execute([
                    ':name'   => $name,
                    ':email'  => $email,
                    ':hash'   => $hash,
                    ':avatar' => $avatar_url,
                    ':bio'    => $bio ?: null,
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

    if ($action === 'edit_user') {
        $user_id = (int)($_POST['user_id'] ?? 0);
        $name = trim((string)($_POST['display_name'] ?? ''));
        $email = strtolower(trim((string)($_POST['email'] ?? '')));
        $role = trim((string)($_POST['role'] ?? 'developer'));
        $bio = trim((string)($_POST['bio'] ?? ''));
        $github = trim((string)($_POST['github_url'] ?? ''));
        $is_verified = !empty($_POST['is_verified']) ? 1 : 0;
        $pass = (string)($_POST['password'] ?? '');

        if ($user_id > 0 && strlen($name) >= 2 && filter_var($email, FILTER_VALIDATE_EMAIL)) {
            try {
                $avatar_url = null;
                if (isset($_FILES['avatar']) && $_FILES['avatar']['error'] === UPLOAD_ERR_OK) {
                    $avatar_url = tn_handle_avatar_upload($_FILES['avatar']);
                }

                if ($avatar_url !== null) {
                    $stmt = $pdo->prepare("UPDATE tn_community_users SET display_name = :name, email = :email, role = :role, bio = :bio, github_url = :github, is_verified = :v, avatar_url = :avatar, updated_at = NOW() WHERE id = :id");
                    $stmt->execute([
                        ':name'   => $name,
                        ':email'  => $email,
                        ':role'   => $role,
                        ':bio'    => $bio ?: null,
                        ':github' => $github ?: null,
                        ':v'      => $is_verified,
                        ':avatar' => $avatar_url,
                        ':id'     => $user_id
                    ]);
                } else {
                    $stmt = $pdo->prepare("UPDATE tn_community_users SET display_name = :name, email = :email, role = :role, bio = :bio, github_url = :github, is_verified = :v, updated_at = NOW() WHERE id = :id");
                    $stmt->execute([
                        ':name'   => $name,
                        ':email'  => $email,
                        ':role'   => $role,
                        ':bio'    => $bio ?: null,
                        ':github' => $github ?: null,
                        ':v'      => $is_verified,
                        ':id'     => $user_id
                    ]);
                }

                if (strlen($pass) >= 6) {
                    $hash = password_hash($pass, PASSWORD_BCRYPT);
                    $pdo->prepare("UPDATE tn_community_users SET password_hash = ? WHERE id = ?")->execute([$hash, $user_id]);
                }

                tn_set_flash('success', "Member '{$name}' updated successfully.");
            } catch (Throwable $e) {
                tn_set_flash('error', "Database error: " . $e->getMessage());
            }
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

$sql = "SELECT id, display_name, email, bio, github_url, avatar_url, role, is_verified, join_date, created_at FROM tn_community_users WHERE 1=1";
$params = [];

if ($search !== '') {
    $sql .= " AND (display_name LIKE :q OR email LIKE :q OR github_url LIKE :q)";
    $params[':q'] = "%{$search}%";
}
if ($role_filter !== '' && in_array($role_filter, ['developer', 'contributor', 'tester', 'founder'], true)) {
    $sql .= " AND role = :role";
    $params[':role'] = $role_filter;
}
$sql .= " ORDER BY (role = 'founder') DESC, id DESC LIMIT 50";

$users = [];
try {
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $users = $stmt->fetchAll(PDO::FETCH_ASSOC);
} catch (Throwable $e) {}

// ── Fetch Recent Forum Posts ──────────────────────────────────────────────────
$posts = [];
try {
    $stmt = $pdo->query("SELECT p.id, p.title, p.body, p.category, p.upvotes, p.created_at, u.display_name AS author_name 
                         FROM tn_community_posts p 
                         LEFT JOIN tn_community_users u ON p.user_id = u.id 
                         ORDER BY p.id DESC LIMIT 15");
    $posts = $stmt->fetchAll(PDO::FETCH_ASSOC);
} catch (Throwable $e) {}

require_once __DIR__ . '/includes/header.php';
?>

<!-- KPI Overview Grid -->
<div class="kpi-grid" style="margin-bottom: 24px;">
  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Community Members</span>
      <div class="kpi-icon-badge" style="background: rgba(14, 165, 233, 0.15); color: #38bdf8;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"></path><circle cx="9" cy="7" r="4"></circle><path d="M23 21v-2a4 4 0 0 0-3-3.87"></path><path d="M16 3.13a4 4 0 0 1 0 7.75"></path></svg>
      </div>
    </div>
    <div class="kpi-value"><?= number_format($stats['users']) ?></div>
    <div class="kpi-footer">Registered developers</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Verified Developers</span>
      <div class="kpi-icon-badge" style="background: rgba(16, 185, 129, 0.15); color: #10b981;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #10b981;"><?= number_format($stats['verified']) ?></div>
    <div class="kpi-footer">Authenticated badge active</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Forum Threads</span>
      <div class="kpi-icon-badge" style="background: rgba(245, 158, 11, 0.15); color: #f59e0b;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #f59e0b;"><?= number_format($stats['posts']) ?></div>
    <div class="kpi-footer">Discussion topics created</div>
  </div>

  <div class="kpi-card">
    <div class="kpi-header">
      <span class="kpi-title">Total Thread Upvotes</span>
      <div class="kpi-icon-badge" style="background: rgba(168, 85, 247, 0.15); color: #c084fc;">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 9V5a3 3 0 0 0-3-3l-4 9v11h11.28a2 2 0 0 0 2-1.7l1.38-9a2 2 0 0 0-2-2.3zM7 22H4a2 2 0 0 1-2-2v-7a2 2 0 0 1 2-2h3"></path></svg>
      </div>
    </div>
    <div class="kpi-value" style="color: #c084fc;"><?= number_format($stats['upvotes']) ?></div>
    <div class="kpi-footer">Community engagement</div>
  </div>
</div>

<!-- Community Members Panel -->
<div class="panel" style="margin-bottom: 24px;">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Community Registry</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Manage accounts, roles, verified badges, and avatar photos displayed publicly on <code>/community.php</code>.</p>
    </div>
    <button type="button" class="btn btn-primary" onclick="openModal('addUserModal')">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
      Onboard Member
    </button>
  </div>

  <!-- Search & Role Bar -->
  <form method="GET" style="display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap;">
    <input type="text" name="q" value="<?= htmlspecialchars($search) ?>" placeholder="Search by name, email, GitHub..." class="form-control" style="flex: 1; min-width: 220px;">
    <select name="role" class="form-control" style="width: auto; min-width: 160px;" onchange="this.form.submit()">
      <option value="">All Roles</option>
      <option value="founder" <?= $role_filter === 'founder' ? 'selected' : '' ?>>Founder</option>
      <option value="developer" <?= $role_filter === 'developer' ? 'selected' : '' ?>>Developer</option>
      <option value="contributor" <?= $role_filter === 'contributor' ? 'selected' : '' ?>>Contributor</option>
      <option value="tester" <?= $role_filter === 'tester' ? 'selected' : '' ?>>Tester</option>
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
          <th>Photo</th>
          <th>Developer</th>
          <th>Email</th>
          <th>Role</th>
          <th>Verified</th>
          <th>Joined</th>
          <th style="text-align: right; width: 150px;">Actions</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($users)): ?>
          <tr><td colspan="8" style="text-align: center; color: #64748b; padding: 28px;">No community members found matching query.</td></tr>
        <?php else: ?>
          <?php foreach ($users as $u): ?>
          <tr>
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.8rem; color: #64748b;">#<?= $u['id'] ?></td>
            <td>
              <?php if (!empty($u['avatar_url'])): ?>
                <img src="<?= htmlspecialchars($u['avatar_url']) ?>" alt="<?= htmlspecialchars($u['display_name']) ?>" style="width: 36px; height: 36px; border-radius: 50%; object-fit: cover; border: 1px solid rgba(56, 189, 248, 0.4); display: block;">
              <?php else: ?>
                <div style="width: 36px; height: 36px; border-radius: 50%; background: linear-gradient(135deg, #0284c7, #00d2ff); color: #fff; font-weight: 800; display: flex; align-items: center; justify-content: center; font-size: 0.82rem;">
                  <?= strtoupper(substr($u['display_name'], 0, 2)) ?>
                </div>
              <?php endif; ?>
            </td>
            <td>
              <div style="font-weight: 700; color: #f8fafc;"><?= htmlspecialchars($u['display_name']) ?></div>
              <?php if (!empty($u['github_url'])): ?>
                <a href="<?= htmlspecialchars($u['github_url']) ?>" target="_blank" rel="noopener" style="font-size: 0.75rem; color: #38bdf8; text-decoration: none;">GitHub Profile &rarr;</a>
              <?php endif; ?>
              <?php if (!empty($u['bio'])): ?>
                <div style="font-size: 0.72rem; color: #64748b;"><?= htmlspecialchars(substr($u['bio'], 0, 50)) ?></div>
              <?php endif; ?>
            </td>
            <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.82rem; color: #94a3b8;"><?= htmlspecialchars($u['email']) ?></td>
            <td>
              <span class="badge" style="background: rgba(14, 165, 233, 0.15); color: #38bdf8; text-transform: uppercase; font-size: 0.72rem; font-weight: 700;">
                <?= htmlspecialchars($u['role']) ?>
              </span>
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
            <td style="text-align: right; white-space: nowrap;">
              <div style="display: inline-flex; align-items: center; gap: 6px;">
                <button type="button" class="btn btn-secondary" style="padding: 3px 8px; font-size: 0.72rem;" onclick='openEditModal(<?= json_encode($u, JSON_HEX_TAG | JSON_HEX_APOS | JSON_HEX_QUOT | JSON_HEX_AMP) ?>)'>Edit</button>
                <form method="POST" style="margin: 0; display: inline;" onsubmit="return confirm('Delete community member #<?= $u['id'] ?> (<?= htmlspecialchars(addslashes($u['display_name'])) ?>)? This cannot be undone.');">
                  <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                  <input type="hidden" name="action" value="delete_user">
                  <input type="hidden" name="user_id" value="<?= $u['id'] ?>">
                  <button type="submit" class="btn btn-danger" style="padding: 3px 8px; font-size: 0.72rem;">Delete</button>
                </form>
              </div>
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
            <td style="font-size: 0.85rem; color: #cbd5e1;"><?= htmlspecialchars($p['author_name'] ?? 'Anonymous') ?></td>
            <td><span class="badge badge-info"><?= htmlspecialchars($p['category']) ?></span></td>
            <td style="font-weight: 700; color: #38bdf8; font-family: 'JetBrains Mono', monospace;"><?= $p['upvotes'] ?></td>
            <td style="font-size: 0.8rem; color: #94a3b8; white-space: nowrap;"><?= date('M d, Y', strtotime($p['created_at'])) ?></td>
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
    <form method="POST" enctype="multipart/form-data">
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
          <label class="form-label" for="avatar">Member Photo / Avatar (Upload)</label>
          <input type="file" id="avatar" name="avatar" class="form-control" accept="image/*">
          <small style="color: #64748b; font-size: 0.75rem;">JPG, PNG, WEBP (Max 5MB)</small>
        </div>

        <div class="form-group">
          <label class="form-label" for="password">Initial Password *</label>
          <input type="password" id="password" name="password" class="form-control" placeholder="Minimum 6 characters" required>
        </div>

        <div class="form-group">
          <label class="form-label" for="bio">Bio / Headline</label>
          <input type="text" id="bio" name="bio" class="form-control" placeholder="e.g. Systems Engineer &amp; Compiler Contributor">
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

<!-- Edit Member Modal -->
<div class="modal-backdrop" id="editUserModal">
  <div class="modal-card">
    <div class="modal-header">
      <h3 class="modal-title">Edit Community Member</h3>
      <button type="button" class="modal-close" onclick="closeModal('editUserModal')">&times;</button>
    </div>
    <form method="POST" enctype="multipart/form-data">
      <div class="modal-body">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" value="edit_user">
        <input type="hidden" name="user_id" id="edit_user_id" value="0">

        <div class="form-group">
          <label class="form-label" for="edit_display_name">Display Name *</label>
          <input type="text" id="edit_display_name" name="display_name" class="form-control" required>
        </div>

        <div class="form-group">
          <label class="form-label" for="edit_email">Developer Email *</label>
          <input type="email" id="edit_email" name="email" class="form-control" required>
        </div>

        <div class="form-group">
          <label class="form-label" for="edit_avatar">Change Photo / Avatar (Upload new image)</label>
          <input type="file" id="edit_avatar" name="avatar" class="form-control" accept="image/*">
          <small style="color: #64748b; font-size: 0.75rem;">Leave empty to keep current photo</small>
        </div>

        <div class="form-group">
          <label class="form-label" for="edit_password">New Password (leave blank to keep unchanged)</label>
          <input type="password" id="edit_password" name="password" class="form-control" placeholder="Leave empty to keep current password">
        </div>

        <div class="form-group">
          <label class="form-label" for="edit_bio">Bio / Headline</label>
          <input type="text" id="edit_bio" name="bio" class="form-control">
        </div>

        <div class="form-group">
          <label class="form-label" for="edit_github_url">GitHub URL</label>
          <input type="url" id="edit_github_url" name="github_url" class="form-control">
        </div>

        <div class="form-group">
          <label class="form-label" for="edit_role">Assign Role</label>
          <select id="edit_role" name="role" class="form-control">
            <option value="developer">Developer</option>
            <option value="contributor">Contributor</option>
            <option value="tester">Tester</option>
            <option value="founder">Founder</option>
          </select>
        </div>

        <div class="form-group">
          <label style="display: flex; align-items: center; gap: 8px; font-size: 0.85rem; color: #e2e8f0; cursor: pointer;">
            <input type="checkbox" id="edit_is_verified" name="is_verified" value="1">
            Verified Developer
          </label>
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-outline" onclick="closeModal('editUserModal')">Cancel</button>
        <button type="submit" class="btn btn-primary">Save Changes</button>
      </div>
    </form>
  </div>
</div>

<script>
function openEditModal(data) {
  document.getElementById('edit_user_id').value = data.id;
  document.getElementById('edit_display_name').value = data.display_name || '';
  document.getElementById('edit_email').value = data.email || '';
  document.getElementById('edit_bio').value = data.bio || '';
  document.getElementById('edit_github_url').value = data.github_url || '';
  document.getElementById('edit_role').value = data.role || 'developer';
  document.getElementById('edit_is_verified').checked = (parseInt(data.is_verified) === 1);
  document.getElementById('edit_password').value = '';
  openModal('editUserModal');
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
