<?php
declare(strict_types=1);

/**
 * console/profile.php — Admin Profile & Security Configuration
 * Enables admin photo upload, profile editing, password changes, and 2FA management.
 */

$active_nav = 'profile';
$console_title = 'Admin Profile & Security';

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$admin_id = $_SESSION['tn_admin_id'] ?? null;
$pdo = null;
try {
    $pdo = tn_pdo();
} catch (Throwable $e) {}

if (!$pdo) {
    die('Database connection required.');
}

if (!$admin_id && !empty($_SESSION['tn_admin_user'])) {
    $stmt = $pdo->prepare("SELECT id FROM tn_admins WHERE username = ?");
    $stmt->execute([$_SESSION['tn_admin_user']]);
    $col = $stmt->fetchColumn();
    if ($col) {
        $admin_id = (int)$col;
        $_SESSION['tn_admin_id'] = $admin_id;
    }
}

if (!$admin_id) {
    header("Location: /console/login");
    exit;
}

// Fetch current admin profile
$stmt = $pdo->prepare("SELECT id, username, email, full_name, avatar_url, role, two_factor_enabled, created_at, last_login FROM tn_admins WHERE id = ?");
$stmt->execute([$admin_id]);
$admin = $stmt->fetch(PDO::FETCH_ASSOC);

if (!$admin) {
    tn_set_flash('error', 'Admin user not found.');
    header("Location: /console/");
    exit;
}

// Handle POST submissions
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $token = $_POST['csrf_token'] ?? '';
    if (!tn_csrf_verify($token)) {
        tn_set_flash('error', 'CSRF verification failed.');
        header("Location: /console/profile");
        exit;
    }

    $action = $_POST['action'] ?? '';

    // 1. Update Profile & Avatar
    if ($action === 'update_profile') {
        $fullName = trim((string)($_POST['full_name'] ?? ''));
        $email = strtolower(trim((string)($_POST['email'] ?? '')));

        if (empty($fullName) || empty($email) || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
            tn_set_flash('error', 'Please provide a valid full name and email address.');
            header("Location: /console/profile");
            exit;
        }

        // Check for duplicate email
        $checkStmt = $pdo->prepare("SELECT id FROM tn_admins WHERE email = ? AND id != ?");
        $checkStmt->execute([$email, $admin_id]);
        if ($checkStmt->fetch()) {
            tn_set_flash('error', 'That email address is already registered to another admin.');
            header("Location: /console/profile");
            exit;
        }

        $avatar_url = $admin['avatar_url'];

        // Handle Avatar File Upload
        if (!empty($_FILES['avatar']['name']) && is_uploaded_file($_FILES['avatar']['tmp_name'])) {
            $file = $_FILES['avatar'];
            $maxBytes = 5 * 1024 * 1024; // 5MB

            if ($file['size'] > $maxBytes) {
                tn_set_flash('error', 'Avatar image must be under 5MB.');
                header("Location: /console/profile");
                exit;
            }

            $allowedMimes = ['image/jpeg', 'image/png', 'image/webp', 'image/gif'];
            $finfo = finfo_open(FILEINFO_MIME_TYPE);
            $mime = finfo_file($finfo, $file['tmp_name']);
            finfo_close($finfo);

            if (!in_array($mime, $allowedMimes, true)) {
                tn_set_flash('error', 'Only JPG, PNG, WEBP, and GIF images are accepted.');
                header("Location: /console/profile");
                exit;
            }

            $ext = match ($mime) {
                'image/jpeg' => 'jpg',
                'image/png'  => 'png',
                'image/webp' => 'webp',
                'image/gif'  => 'gif',
                default      => 'jpg'
            };

            $uploadDir = dirname(__DIR__) . '/uploads/avatars';
            if (!is_dir($uploadDir)) {
                mkdir($uploadDir, 0755, true);
            }

            $fileName = 'admin_' . time() . '_' . bin2hex(random_bytes(6)) . '.' . $ext;
            $destPath = $uploadDir . '/' . $fileName;

            if (move_uploaded_file($file['tmp_name'], $destPath)) {
                $avatar_url = '/uploads/avatars/' . $fileName;
                $_SESSION['tn_admin_avatar'] = $avatar_url;
            } else {
                tn_set_flash('error', 'Failed to save uploaded image. Check directory permissions.');
                header("Location: /console/profile");
                exit;
            }
        }

        try {
            $upStmt = $pdo->prepare("UPDATE tn_admins SET full_name = ?, email = ?, avatar_url = ?, updated_at = NOW() WHERE id = ?");
            $upStmt->execute([$fullName, $email, $avatar_url, $admin_id]);
            tn_set_flash('success', 'Profile updated successfully.');
        } catch (Throwable $e) {
            tn_set_flash('error', 'Database error: ' . $e->getMessage());
        }

        header("Location: /console/profile");
        exit;
    }

    // 2. Change Password
    if ($action === 'change_password') {
        $currentPass = (string)($_POST['current_password'] ?? '');
        $newPass = (string)($_POST['new_password'] ?? '');
        $confirmPass = (string)($_POST['confirm_password'] ?? '');

        // Verify current password
        $passStmt = $pdo->prepare("SELECT password_hash FROM tn_admins WHERE id = ?");
        $passStmt->execute([$admin_id]);
        $hashRow = $passStmt->fetch();

        if (!$hashRow || !password_verify($currentPass, (string)$hashRow['password_hash'])) {
            tn_set_flash('error', 'Current password is incorrect.');
            header("Location: /console/profile");
            exit;
        }

        if (strlen($newPass) < 8) {
            tn_set_flash('error', 'New password must be at least 8 characters long.');
            header("Location: /console/profile");
            exit;
        }

        if ($newPass !== $confirmPass) {
            tn_set_flash('error', 'New password and confirmation do not match.');
            header("Location: /console/profile");
            exit;
        }

        $newHash = password_hash($newPass, PASSWORD_DEFAULT);
        try {
            $upStmt = $pdo->prepare("UPDATE tn_admins SET password_hash = ?, updated_at = NOW() WHERE id = ?");
            $upStmt->execute([$newHash, $admin_id]);
            tn_set_flash('success', 'Password updated successfully.');
        } catch (Throwable $e) {
            tn_set_flash('error', 'Error updating password: ' . $e->getMessage());
        }

        header("Location: /console/profile");
        exit;
    }

    // 3. Toggle 2FA
    if ($action === 'toggle_2fa') {
        $newStatus = !empty($_POST['enable_2fa']) ? 1 : 0;
        try {
            $upStmt = $pdo->prepare("UPDATE tn_admins SET two_factor_enabled = ?, updated_at = NOW() WHERE id = ?");
            $upStmt->execute([$newStatus, $admin_id]);
            tn_set_flash('success', $newStatus ? 'Two-Factor Authentication is now ENABLED.' : 'Two-Factor Authentication is now DISABLED.');
        } catch (Throwable $e) {
            tn_set_flash('error', 'Error updating 2FA: ' . $e->getMessage());
        }

        header("Location: /console/profile");
        exit;
    }
}

require_once __DIR__ . '/includes/header.php';
?>

<div style="max-width: 980px; margin: 0 auto; display: flex; flex-direction: column; gap: 24px;">

  <!-- Profile Overview Card -->
  <div class="panel">
    <div class="panel-header">
      <div class="panel-title">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2.2"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"></path><circle cx="12" cy="7" r="4"></circle></svg>
        <span>Admin Profile &amp; Identity</span>
      </div>
    </div>

    <form method="POST" action="" enctype="multipart/form-data">
      <input type="hidden" name="action" value="update_profile">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">

      <div style="display: flex; gap: 32px; align-items: flex-start; flex-wrap: wrap;">
        <!-- Avatar Section -->
        <div style="display: flex; flex-direction: column; align-items: center; gap: 14px; min-width: 180px;">
          <div style="width: 110px; height: 110px; border-radius: 50%; overflow: hidden; border: 2px solid var(--c-accent); position: relative; background: radial-gradient(circle at 30% 25%, #1e293b 0%, #0b1329 60%, #030712 100%); display: flex; align-items: center; justify-content: center; box-shadow: 0 4px 14px rgba(0,0,0,0.25);">
            <?php if (!empty($admin['avatar_url'])): ?>
              <img id="avatarPreview" src="<?= htmlspecialchars($admin['avatar_url']) ?>" alt="Admin Avatar" style="width: 100%; height: 100%; object-fit: cover;">
            <?php else: ?>
              <img id="avatarPreview" src="" alt="Admin Avatar" style="width: 100%; height: 100%; object-fit: cover; display: none;">
              <span id="avatarPlaceholder" style="font-size: 2.2rem; font-weight: 800; color: #38bdf8;"><?= strtoupper(substr($admin['username'], 0, 2)) ?></span>
            <?php endif; ?>
          </div>

          <label class="c-btn c-btn-secondary c-btn-sm" style="cursor: pointer; display: inline-flex; align-items: center; gap: 6px;">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path><circle cx="12" cy="13" r="4"></circle></svg>
            <span>Change Photo</span>
            <input type="file" name="avatar" id="avatarFileInput" accept="image/*" style="display: none;" onchange="previewImage(this)">
          </label>
          <span style="font-size: 0.72rem; color: var(--c-text-muted); text-align: center;">JPG, PNG, WEBP &bull; Max 5MB</span>
        </div>

        <!-- Details Fields -->
        <div style="flex: 1; min-width: 260px; display: flex; flex-direction: column; gap: 16px;">
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
            <div>
              <label style="font-size: 0.8rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Full Name</label>
              <input type="text" name="full_name" class="c-input" value="<?= htmlspecialchars($admin['full_name'] ?? '') ?>" required>
            </div>
            <div>
              <label style="font-size: 0.8rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Email Address</label>
              <input type="email" name="email" class="c-input" value="<?= htmlspecialchars($admin['email'] ?? '') ?>" required>
            </div>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
            <div>
              <label style="font-size: 0.8rem; font-weight: 600; color: var(--c-text-muted); margin-bottom: 6px; display: block;">Username (Immutable)</label>
              <input type="text" class="c-input" value="<?= htmlspecialchars($admin['username']) ?>" disabled style="opacity: 0.65; cursor: not-allowed;">
            </div>
            <div>
              <label style="font-size: 0.8rem; font-weight: 600; color: var(--c-text-muted); margin-bottom: 6px; display: block;">Assigned Role</label>
              <input type="text" class="c-input" value="<?= strtoupper(htmlspecialchars($admin['role'] ?? 'superadmin')) ?>" disabled style="opacity: 0.65; cursor: not-allowed;">
            </div>
          </div>

          <div style="margin-top: 8px;">
            <button type="submit" class="c-btn c-btn-primary">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="20 6 9 17 4 12"></polyline></svg>
              <span>Save Profile Changes</span>
            </button>
          </div>
        </div>
      </div>
    </form>
  </div>

  <!-- Security Settings (2FA & Password) -->
  <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px;" class="security-grid">
    
    <!-- Change Password Card -->
    <div class="panel">
      <div class="panel-header">
        <div class="panel-title">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#f59e0b" stroke-width="2.2"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect><path d="M7 11V7a5 5 0 0 1 10 0v4"></path></svg>
          <span>Change Password</span>
        </div>
      </div>

      <form method="POST" action="" style="display: flex; flex-direction: column; gap: 14px;">
        <input type="hidden" name="action" value="change_password">
        <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Current Password</label>
          <input type="password" name="current_password" class="c-input" required autocomplete="current-password">
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">New Password (Min 8 chars)</label>
          <input type="password" name="new_password" class="c-input" required minlength="8" autocomplete="new-password">
        </div>

        <div>
          <label style="font-size: 0.78rem; font-weight: 600; color: var(--c-text-secondary); margin-bottom: 6px; display: block;">Confirm New Password</label>
          <input type="password" name="confirm_password" class="c-input" required minlength="8" autocomplete="new-password">
        </div>

        <div style="margin-top: 6px;">
          <button type="submit" class="c-btn c-btn-secondary" style="width: 100%;">
            <span>Update Password</span>
          </button>
        </div>
      </form>
    </div>

    <!-- Two-Factor Authentication Card -->
    <div class="panel">
      <div class="panel-header">
        <div class="panel-title">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2.2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg>
          <span>Two-Factor Authentication</span>
        </div>
      </div>

      <div style="display: flex; flex-direction: column; gap: 16px;">
        <div style="display: flex; align-items: center; justify-content: space-between; padding: 12px 14px; border-radius: 8px; background: rgba(16, 185, 129, 0.08); border: 1px solid rgba(16, 185, 129, 0.25);">
          <div>
            <div style="font-weight: 700; color: var(--c-text-primary); font-size: 0.92rem;">
              Status: 
              <?php if (!empty($admin['two_factor_enabled'])): ?>
                <span style="color: #10b981;">ENABLED (Active)</span>
              <?php else: ?>
                <span style="color: var(--c-danger);">DISABLED</span>
              <?php endif; ?>
            </div>
            <p style="font-size: 0.78rem; color: var(--c-text-muted); margin-top: 2px;">
              Email OTP delivered via official Hostinger SMTP.
            </p>
          </div>
          <div style="width: 12px; height: 12px; border-radius: 50%; background: <?= !empty($admin['two_factor_enabled']) ? '#10b981' : '#ef4444' ?>; box-shadow: 0 0 8px <?= !empty($admin['two_factor_enabled']) ? '#10b981' : '#ef4444' ?>;"></div>
        </div>

        <p style="font-size: 0.84rem; color: var(--c-text-secondary); line-height: 1.6;">
          When Two-Factor Authentication is active, logging in requires a 6-digit cryptographic verification code sent to <strong><?= htmlspecialchars($admin['email']) ?></strong>.
        </p>

        <form method="POST" action="">
          <input type="hidden" name="action" value="toggle_2fa">
          <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
          <input type="hidden" name="enable_2fa" value="<?= empty($admin['two_factor_enabled']) ? '1' : '0' ?>">
          
          <?php if (empty($admin['two_factor_enabled'])): ?>
            <button type="submit" class="c-btn c-btn-primary" style="width: 100%;">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg>
              <span>Enable 2FA Protection</span>
            </button>
          <?php else: ?>
            <button type="submit" class="c-btn c-btn-danger" style="width: 100%;" onclick="return confirm('Disabling 2FA will lower your account security. Are you sure?');">
              <span>Disable 2FA Protection</span>
            </button>
          <?php endif; ?>
        </form>
      </div>
    </div>

  </div>
</div>

<style>
@media (max-width: 768px) {
  .security-grid {
    grid-template-columns: 1fr !important;
  }
}
</style>

<script>
function previewImage(input) {
  if (input.files && input.files[0]) {
    const reader = new FileReader();
    reader.onload = function (e) {
      const preview = document.getElementById('avatarPreview');
      const placeholder = document.getElementById('avatarPlaceholder');
      if (preview) {
        preview.src = e.target.result;
        preview.style.display = 'block';
      }
      if (placeholder) {
        placeholder.style.display = 'none';
      }
    };
    reader.readAsDataURL(input.files[0]);
  }
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
