<?php
declare(strict_types=1);

/**
 * console/team.php — Manage Active Core Development Team
 * Full CRUD interface with avatar upload, reordering, and instant footer sync.
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'team';
$console_title = 'Core Dev Team Management';
$pdo = tn_pdo();

// ── Handle Actions ─────────────────────────────────────────────────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $csrf   = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/team');
        exit;
    }

    $uploadDir = dirname(__DIR__) . '/uploads/avatars';
    if (!is_dir($uploadDir)) {
        @mkdir($uploadDir, 0755, true);
    }

    // Helper for avatar file upload
    $handleAvatarUpload = function() use ($uploadDir): ?string {
        if (!isset($_FILES['avatar']) || $_FILES['avatar']['error'] !== UPLOAD_ERR_OK) {
            return null;
        }
        $file = $_FILES['avatar'];
        $maxBytes = 4 * 1024 * 1024; // 4MB
        if ($file['size'] > $maxBytes) {
            return null;
        }
        $finfo = new finfo(FILEINFO_MIME_TYPE);
        $mime = $finfo->file($file['tmp_name']);
        $allowed = ['image/jpeg' => '.jpg', 'image/png' => '.png', 'image/webp' => '.webp'];
        if (!isset($allowed[$mime])) {
            return null;
        }
        $ext = $allowed[$mime];
        $filename = 'team_' . time() . '_' . bin2hex(random_bytes(6)) . $ext;
        $dest = $uploadDir . '/' . $filename;
        if (move_uploaded_file($file['tmp_name'], $dest)) {
            return '/uploads/avatars/' . $filename;
        }
        return null;
    };

    if ($action === 'add_member') {
        $name = trim((string)($_POST['name'] ?? ''));
        $role = trim((string)($_POST['role'] ?? ''));
        $since = trim((string)($_POST['since_text'] ?? ''));
        $initials = strtoupper(trim((string)($_POST['initials'] ?? '')));
        $gradient = trim((string)($_POST['avatar_gradient'] ?? 'linear-gradient(135deg, #00d2ff, #0284c7)'));
        $order = (int)($_POST['display_order'] ?? 0);
        $active = isset($_POST['is_active']) ? 1 : 0;

        if (empty($initials) && !empty($name)) {
            $parts = explode(' ', $name);
            $initials = strtoupper(substr($parts[0], 0, 1) . (isset($parts[1]) ? substr($parts[1], 0, 1) : ''));
        }

        $avatarUrl = $handleAvatarUpload();
        if (!$avatarUrl && !empty($_POST['avatar_url'])) {
            $avatarUrl = trim((string)$_POST['avatar_url']);
        }

        if ($name && $role) {
            $stmt = $pdo->prepare("INSERT INTO tn_team_members (name, role, since_text, avatar_url, initials, avatar_gradient, display_order, is_active) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            $stmt->execute([$name, $role, $since, $avatarUrl, $initials, $gradient, $order, $active]);
            tn_set_flash('success', "Team member '{$name}' added successfully.");
        } else {
            tn_set_flash('error', 'Name and Role are required.');
        }
        header('Location: /console/team');
        exit;
    }

    if ($action === 'edit_member') {
        $id = (int)($_POST['member_id'] ?? 0);
        $name = trim((string)($_POST['name'] ?? ''));
        $role = trim((string)($_POST['role'] ?? ''));
        $since = trim((string)($_POST['since_text'] ?? ''));
        $initials = strtoupper(trim((string)($_POST['initials'] ?? '')));
        $gradient = trim((string)($_POST['avatar_gradient'] ?? 'linear-gradient(135deg, #00d2ff, #0284c7)'));
        $order = (int)($_POST['display_order'] ?? 0);
        $active = isset($_POST['is_active']) ? 1 : 0;

        if ($id > 0 && $name && $role) {
            $newAvatar = $handleAvatarUpload();
            if ($newAvatar) {
                $stmt = $pdo->prepare("UPDATE tn_team_members SET name = ?, role = ?, since_text = ?, avatar_url = ?, initials = ?, avatar_gradient = ?, display_order = ?, is_active = ?, updated_at = NOW() WHERE id = ?");
                $stmt->execute([$name, $role, $since, $newAvatar, $initials, $gradient, $order, $active, $id]);
            } else {
                $manualUrl = trim((string)($_POST['avatar_url'] ?? ''));
                $stmt = $pdo->prepare("UPDATE tn_team_members SET name = ?, role = ?, since_text = ?, avatar_url = COALESCE(NULLIF(?, ''), avatar_url), initials = ?, avatar_gradient = ?, display_order = ?, is_active = ?, updated_at = NOW() WHERE id = ?");
                $stmt->execute([$name, $role, $since, $manualUrl, $initials, $gradient, $order, $active, $id]);
            }
            tn_set_flash('success', "Team member '{$name}' updated successfully.");
        }
        header('Location: /console/team');
        exit;
    }

    if ($action === 'delete_member') {
        $id = (int)($_POST['member_id'] ?? 0);
        if ($id > 0) {
            $pdo->prepare("DELETE FROM tn_team_members WHERE id = ?")->execute([$id]);
            tn_set_flash('success', "Team member #{$id} deleted.");
        }
        header('Location: /console/team');
        exit;
    }

    if ($action === 'toggle_active') {
        $id = (int)($_POST['member_id'] ?? 0);
        if ($id > 0) {
            $pdo->prepare("UPDATE tn_team_members SET is_active = 1 - is_active WHERE id = ?")->execute([$id]);
            tn_set_flash('success', "Status toggled for team member #{$id}.");
        }
        header('Location: /console/team');
        exit;
    }
}

// ── Fetch Team Members ─────────────────────────────────────────────────────────
$members = $pdo->query("SELECT * FROM tn_team_members ORDER BY display_order ASC, id ASC")->fetchAll(PDO::FETCH_ASSOC);

require_once __DIR__ . '/includes/header.php';
?>

<div class="panel">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Active Core Development Team</h3>
      <p class="panel-desc">Manage engineers, architects, and designers displayed in the official footer and community roster.</p>
    </div>
    <div style="display: flex; gap: 10px; align-items: center; flex-wrap: wrap;">
      <button type="button" class="btn btn-primary" onclick="openAddMemberModal()" style="display: inline-flex; align-items: center; gap: 6px;">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
        <span>Add Member</span>
      </button>
    </div>
  </div>

  <div class="table-responsive">
    <table class="console-table">
      <thead>
        <tr>
          <th style="width: 50px;">Order</th>
          <th style="width: 70px;">Avatar</th>
          <th>Name &amp; Role</th>
          <th>Tenure / Since</th>
          <th style="width: 100px;">Status</th>
          <th style="width: 160px; text-align: right;">Actions</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($members)): ?>
          <tr><td colspan="6" style="text-align: center; color: var(--c-text-tertiary); padding: 36px;">No team members registered yet. Click "Add Member" to create one.</td></tr>
        <?php else: ?>
          <?php foreach ($members as $m): ?>
          <tr>
            <td style="font-weight: 700; color: var(--c-text-tertiary);"><?= (int)$m['display_order'] ?></td>
            <td>
              <?php if (!empty($m['avatar_url'])): ?>
                <div style="width: 38px; height: 38px; border-radius: 50%; overflow: hidden; border: 1.5px solid var(--c-primary); flex-shrink: 0;">
                  <img src="<?= htmlspecialchars($m['avatar_url']) ?>" alt="<?= htmlspecialchars($m['name']) ?>" style="width: 100%; height: 100%; object-fit: cover; display: block;">
                </div>
              <?php else: ?>
                <div style="width: 38px; height: 38px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-weight: 700; font-size: 0.88rem; color: #fff; background: <?= htmlspecialchars($m['avatar_gradient'] ?: 'linear-gradient(135deg, #00d2ff, #0284c7)') ?>;">
                  <?= htmlspecialchars($m['initials'] ?: 'TM') ?>
                </div>
              <?php endif; ?>
            </td>
            <td>
              <div style="font-weight: 700; color: var(--c-text-primary); font-size: 0.95rem;"><?= htmlspecialchars($m['name']) ?></div>
              <div style="font-size: 0.8rem; color: #0284c7; font-weight: 600;"><?= htmlspecialchars($m['role']) ?></div>
            </td>
            <td style="color: var(--c-text-secondary); font-size: 0.88rem;">
              <?= htmlspecialchars($m['since_text']) ?>
            </td>
            <td>
              <form method="POST" style="margin: 0;">
                <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                <input type="hidden" name="action" value="toggle_active">
                <input type="hidden" name="member_id" value="<?= $m['id'] ?>">
                <button type="submit" class="status-badge" style="cursor: pointer; border: none; font-size: 0.74rem; <?= $m['is_active'] ? 'background: rgba(16, 185, 129, 0.15); color: #10b981; border: 1px solid rgba(16, 185, 129, 0.3);' : 'background: rgba(100, 116, 139, 0.15); color: #94a3b8; border: 1px solid rgba(100, 116, 139, 0.3);' ?>">
                  <?= $m['is_active'] ? 'Active' : 'Hidden' ?>
                </button>
              </form>
            </td>
            <td style="text-align: right;">
              <div style="display: inline-flex; align-items: center; gap: 6px;">
                <button type="button" class="btn btn-secondary btn-sm" onclick='openEditMemberModal(<?= json_encode($m, JSON_HEX_TAG | JSON_HEX_APOS | JSON_HEX_QUOT | JSON_HEX_AMP) ?>)'>Edit</button>
                <form method="POST" style="margin: 0;" onsubmit="return confirm('Delete <?= htmlspecialchars(addslashes($m['name'])) ?> from core team?');">
                  <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
                  <input type="hidden" name="action" value="delete_member">
                  <input type="hidden" name="member_id" value="<?= $m['id'] ?>">
                  <button type="submit" class="btn btn-danger btn-sm">Delete</button>
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

<!-- Add / Edit Member Modal -->
<div class="modal-backdrop" id="memberModal">
  <div class="modal-card" style="max-width: 580px;">
    <div class="modal-header">
      <h3 class="modal-title" id="memberModalTitle">Add Team Member</h3>
      <button type="button" class="modal-close" onclick="closeModal('memberModal')">&times;</button>
    </div>
    <form method="POST" id="memberForm" enctype="multipart/form-data">
      <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
      <input type="hidden" name="action" id="memberFormAction" value="add_member">
      <input type="hidden" name="member_id" id="memberFormId" value="0">

      <div class="modal-body" style="display: flex; flex-direction: column; gap: 16px;">
        <div>
          <label class="form-label" for="mem_name">Full Name *</label>
          <input type="text" name="name" id="mem_name" class="form-input" placeholder="e.g. Rohit Pathak" required>
        </div>

        <div>
          <label class="form-label" for="mem_role">Role / Job Title *</label>
          <input type="text" name="role" id="mem_role" class="form-input" placeholder="e.g. Creator & Lead Architect" required>
        </div>

        <div>
          <label class="form-label" for="mem_since">Tenure / Since Text *</label>
          <input type="text" name="since_text" id="mem_since" class="form-input" placeholder="e.g. Since 18 Oct 2022 • TezzNative v0.1 → v1.0.0" required>
        </div>

        <!-- Photo Upload & Preview -->
        <div style="background: var(--c-surface-raised); border: 1px solid var(--c-border-subtle); border-radius: 12px; padding: 16px;">
          <label class="form-label" style="margin-bottom: 8px;">Profile Photo / Avatar</label>
          <div style="display: flex; align-items: center; gap: 16px; margin-bottom: 12px;">
            <div id="avatarPreviewBox" style="width: 56px; height: 56px; border-radius: 50%; overflow: hidden; background: #0f172a; border: 2px solid var(--c-primary); display: flex; align-items: center; justify-content: center; font-weight: 700; color: #38bdf8; font-size: 1.1rem; flex-shrink: 0;">
              ?
            </div>
            <div style="flex: 1;">
              <input type="file" name="avatar" id="mem_avatar_file" accept="image/*" class="form-input" style="padding: 6px;" onchange="previewMemberPhoto(this)">
              <div style="font-size: 0.74rem; color: var(--c-text-tertiary); margin-top: 4px;">JPG, PNG, or WebP. Max 4MB. Square aspect recommended.</div>
            </div>
          </div>
          <div style="margin-top: 8px;">
            <label class="form-label" style="font-size: 0.78rem;">Or Remote Image URL (optional)</label>
            <input type="url" name="avatar_url" id="mem_avatar_url" class="form-input" placeholder="https://...">
          </div>
        </div>

        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
          <div>
            <label class="form-label" for="mem_initials">Initials Fallback</label>
            <input type="text" name="initials" id="mem_initials" class="form-input" maxlength="4" placeholder="e.g. RP">
          </div>
          <div>
            <label class="form-label" for="mem_order">Display Order</label>
            <input type="number" name="display_order" id="mem_order" class="form-input" value="1" min="0">
          </div>
        </div>

        <div style="display: flex; align-items: center; gap: 10px; margin-top: 4px;">
          <input type="checkbox" name="is_active" id="mem_active" value="1" checked style="width: 18px; height: 18px; cursor: pointer;">
          <label for="mem_active" style="cursor: pointer; font-size: 0.88rem; font-weight: 600; color: var(--c-text-primary);">
            Visible in Footer Core Development Team
          </label>
        </div>
      </div>

      <div class="modal-footer">
        <button type="button" class="btn btn-outline" onclick="closeModal('memberModal')">Cancel</button>
        <button type="submit" class="btn btn-primary" id="memberSubmitBtn">Save Member</button>
      </div>
    </form>
  </div>
</div>

<script>
function openAddMemberModal() {
  document.getElementById('memberForm').reset();
  document.getElementById('memberModalTitle').innerText = 'Add Team Member';
  document.getElementById('memberFormAction').value = 'add_member';
  document.getElementById('memberFormId').value = '0';
  document.getElementById('memberSubmitBtn').innerText = 'Save Member';
  document.getElementById('avatarPreviewBox').innerHTML = '?';
  document.getElementById('mem_active').checked = true;
  openModal('memberModal');
}

function openEditMemberModal(m) {
  document.getElementById('memberModalTitle').innerText = 'Edit ' + m.name;
  document.getElementById('memberFormAction').value = 'edit_member';
  document.getElementById('memberFormId').value = m.id;
  document.getElementById('memberSubmitBtn').innerText = 'Update Member';
  document.getElementById('mem_name').value = m.name || '';
  document.getElementById('mem_role').value = m.role || '';
  document.getElementById('mem_since').value = m.since_text || '';
  document.getElementById('mem_initials').value = m.initials || '';
  document.getElementById('mem_avatar_url').value = m.avatar_url || '';
  document.getElementById('mem_order').value = m.display_order || 0;
  document.getElementById('mem_active').checked = parseInt(m.is_active) === 1;

  var pBox = document.getElementById('avatarPreviewBox');
  if (m.avatar_url) {
    pBox.innerHTML = '<img src="' + m.avatar_url + '" style="width: 100%; height: 100%; object-fit: cover;">';
  } else {
    pBox.innerHTML = m.initials || m.name.substring(0, 2).toUpperCase();
  }
  openModal('memberModal');
}

function previewMemberPhoto(input) {
  if (input.files && input.files[0]) {
    var reader = new FileReader();
    reader.onload = function(e) {
      document.getElementById('avatarPreviewBox').innerHTML = '<img src="' + e.target.result + '" style="width: 100%; height: 100%; object-fit: cover;">';
    };
    reader.readAsDataURL(input.files[0]);
  }
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
