<?php
declare(strict_types=1);

/**
 * profile.php — Developer Profile & Identity Hub
 * Allows community members to view and edit profile details, upload avatars, and manage credentials.
 */

require_once __DIR__ . '/includes/nav.php';
$boot = tn_boot('/profile');
$pdo = $boot['pdo'];

if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

$notice = '';
$noticeType = '';

// Check if user is logged in
$logged_user_id = $_SESSION['tn_user_id'] ?? null;
$view_user_id = isset($_GET['id']) ? (int)$_GET['id'] : null;
$view_username = isset($_GET['u']) ? trim((string)$_GET['u']) : null;

// Handle Logout
if (isset($_GET['action']) && $_GET['action'] === 'logout') {
    unset($_SESSION['tn_user_id'], $_SESSION['tn_user_email'], $_SESSION['tn_user_name'], $_SESSION['tn_user_avatar']);
    header("Location: /profile");
    exit;
}

// Handle Form Submissions (Login, Register, Update Profile, Change Password)
if ($_SERVER['REQUEST_METHOD'] === 'POST' && $pdo) {
    $action = $_POST['action'] ?? '';
    $token = $_POST['csrf_token'] ?? '';

    // 1. User Sign In
    if ($action === 'login') {
        $email = strtolower(trim((string)($_POST['email'] ?? '')));
        $password = (string)($_POST['password'] ?? '');

        if ($email && $password) {
            $stmt = $pdo->prepare("SELECT id, display_name, email, password_hash, avatar_url, role FROM tn_community_users WHERE email = ? LIMIT 1");
            $stmt->execute([$email]);
            $u = $stmt->fetch(PDO::FETCH_ASSOC);

            if ($u && password_verify($password, (string)$u['password_hash'])) {
                $_SESSION['tn_user_id'] = (int)$u['id'];
                $_SESSION['tn_user_email'] = $u['email'];
                $_SESSION['tn_user_name'] = $u['display_name'];
                $_SESSION['tn_user_avatar'] = $u['avatar_url'] ?? '';
                header("Location: /profile");
                exit;
            } else {
                $notice = 'Invalid email address or password.';
                $noticeType = 'error';
            }
        } else {
            $notice = 'Please provide both your email and password.';
            $noticeType = 'error';
        }
    }

    // 2. User Registration
    elseif ($action === 'register') {
        $name = trim((string)($_POST['name'] ?? ''));
        $email = strtolower(trim((string)($_POST['email'] ?? '')));
        $password = (string)($_POST['password'] ?? '');

        if (!$name || !$email || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
            $notice = 'Please provide a valid name and email address.';
            $noticeType = 'error';
        } elseif (strlen($password) < 6) {
            $notice = 'Password must be at least 6 characters.';
            $noticeType = 'error';
        } else {
            // Check if email already exists
            $stmt = $pdo->prepare("SELECT id FROM tn_community_users WHERE email = ?");
            $stmt->execute([$email]);
            if ($stmt->fetch()) {
                $notice = 'This email is already registered. Please sign in instead.';
                $noticeType = 'error';
            } else {
                $hash = password_hash($password, PASSWORD_DEFAULT);
                $ins = $pdo->prepare("
                    INSERT INTO tn_community_users (display_name, email, password_hash, role, reputation, is_verified, join_date, created_at, updated_at)
                    VALUES (?, ?, ?, 'developer', 20, 0, CURDATE(), NOW(), NOW())
                ");
                $ins->execute([$name, $email, $hash]);
                $newId = (int)$pdo->lastInsertId();

                $_SESSION['tn_user_id'] = $newId;
                $_SESSION['tn_user_email'] = $email;
                $_SESSION['tn_user_name'] = $name;
                $_SESSION['tn_user_avatar'] = '';

                header("Location: /profile");
                exit;
            }
        }
    }

    // 3. Update Profile & Avatar (Logged-in Only)
    elseif ($action === 'update_profile' && $logged_user_id) {
        $displayName = trim((string)($_POST['display_name'] ?? ''));
        $bio = trim((string)($_POST['bio'] ?? ''));
        $githubUrl = trim((string)($_POST['github_url'] ?? ''));

        if (!$displayName) {
            $notice = 'Display name cannot be empty.';
            $noticeType = 'error';
        } else {
            // Fetch current avatar
            $currStmt = $pdo->prepare("SELECT avatar_url FROM tn_community_users WHERE id = ?");
            $currStmt->execute([$logged_user_id]);
            $curr = $currStmt->fetch();
            $avatar_url = $curr['avatar_url'] ?? null;

            // Handle Avatar File Upload
            if (!empty($_FILES['avatar']['name']) && is_uploaded_file($_FILES['avatar']['tmp_name'])) {
                $file = $_FILES['avatar'];
                $maxBytes = 5 * 1024 * 1024; // 5MB

                $allowedMimes = ['image/jpeg', 'image/png', 'image/webp'];
                $finfo = finfo_open(FILEINFO_MIME_TYPE);
                $mime = finfo_file($finfo, $file['tmp_name']);
                finfo_close($finfo);

                if ($file['size'] > $maxBytes) {
                    $notice = 'Avatar file size must be less than 5MB.';
                    $noticeType = 'error';
                } elseif (!in_array($mime, $allowedMimes, true)) {
                    $notice = 'Only JPG, PNG, and WEBP images are supported.';
                    $noticeType = 'error';
                } else {
                    $ext = match ($mime) {
                        'image/jpeg' => 'jpg',
                        'image/png'  => 'png',
                        'image/webp' => 'webp',
                        default      => 'jpg'
                    };

                    $uploadDir = __DIR__ . '/uploads/avatars';
                    if (!is_dir($uploadDir)) {
                        mkdir($uploadDir, 0755, true);
                    }

                    $fileName = 'avatar_' . time() . '_' . bin2hex(random_bytes(6)) . '.' . $ext;
                    $destPath = $uploadDir . '/' . $fileName;

                    if (move_uploaded_file($file['tmp_name'], $destPath)) {
                        $avatar_url = '/uploads/avatars/' . $fileName;
                        $_SESSION['tn_user_avatar'] = $avatar_url;
                    } else {
                        $notice = 'Failed to save avatar image.';
                        $noticeType = 'error';
                    }
                }
            }

            if ($noticeType !== 'error') {
                $up = $pdo->prepare("
                    UPDATE tn_community_users SET
                        display_name = ?,
                        bio = ?,
                        github_url = ?,
                        avatar_url = ?,
                        updated_at = NOW()
                    WHERE id = ?
                ");
                $up->execute([$displayName, $bio, $githubUrl, $avatar_url, $logged_user_id]);
                $_SESSION['tn_user_name'] = $displayName;
                $notice = 'Profile updated successfully!';
                $noticeType = 'success';
            }
        }
    }

    // 4. Change Password (Logged-in Only)
    elseif ($action === 'change_password' && $logged_user_id) {
        $currentPass = (string)($_POST['current_password'] ?? '');
        $newPass = (string)($_POST['new_password'] ?? '');
        $confirmPass = (string)($_POST['confirm_password'] ?? '');

        $currStmt = $pdo->prepare("SELECT password_hash FROM tn_community_users WHERE id = ?");
        $currStmt->execute([$logged_user_id]);
        $row = $currStmt->fetch();

        if (!$row || !password_verify($currentPass, (string)$row['password_hash'])) {
            $notice = 'Current password is incorrect.';
            $noticeType = 'error';
        } elseif (strlen($newPass) < 6) {
            $notice = 'New password must be at least 6 characters long.';
            $noticeType = 'error';
        } elseif ($newPass !== $confirmPass) {
            $notice = 'New password and confirmation do not match.';
            $noticeType = 'error';
        } else {
            $newHash = password_hash($newPass, PASSWORD_DEFAULT);
            $up = $pdo->prepare("UPDATE tn_community_users SET password_hash = ?, updated_at = NOW() WHERE id = ?");
            $up->execute([$newHash, $logged_user_id]);
            $notice = 'Password changed successfully.';
            $noticeType = 'success';
        }
    }
}

// Fetch Profile Data
$profileUser = null;
$isOwnProfile = false;

if ($logged_user_id && !$view_user_id && !$view_username) {
    $stmt = $pdo->prepare("SELECT * FROM tn_community_users WHERE id = ?");
    $stmt->execute([$logged_user_id]);
    $profileUser = $stmt->fetch(PDO::FETCH_ASSOC);
    $isOwnProfile = true;
} elseif ($view_user_id) {
    $stmt = $pdo->prepare("SELECT * FROM tn_community_users WHERE id = ?");
    $stmt->execute([$view_user_id]);
    $profileUser = $stmt->fetch(PDO::FETCH_ASSOC);
    $isOwnProfile = ($logged_user_id && (int)$logged_user_id === (int)$view_user_id);
} elseif ($view_username) {
    $stmt = $pdo->prepare("SELECT * FROM tn_community_users WHERE display_name = ? OR email LIKE ? LIMIT 1");
    $stmt->execute([$view_username, $view_username . '%']);
    $profileUser = $stmt->fetch(PDO::FETCH_ASSOC);
    $isOwnProfile = ($logged_user_id && $profileUser && (int)$logged_user_id === (int)$profileUser['id']);
}

$page_title = $profileUser ? htmlspecialchars($profileUser['display_name']) . ' — Developer Profile' : 'Developer Profile & Identity';
$page_desc = 'Manage your TezzNative developer profile, upload avatar photos, and showcase your community contributions.';
$active_nav = 'community';
require_once __DIR__ . '/includes/header.php';
?>

<main id="main-content" style="padding: 4rem 1.5rem 6rem;">
  <div class="container" style="max-width: 960px; margin: 0 auto;">

    <!-- Flash Notices -->
    <?php if ($notice): ?>
      <div style="padding: 14px 18px; border-radius: 10px; margin-bottom: 24px; font-weight: 600; font-size: 0.92rem; display: flex; align-items: center; gap: 10px; <?= $noticeType === 'success' ? 'background: rgba(16, 185, 129, 0.12); color: #10b981; border: 1px solid rgba(16, 185, 129, 0.3);' : 'background: rgba(239, 68, 68, 0.12); color: #f87171; border: 1px solid rgba(239, 68, 68, 0.3);' ?>">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <?= $noticeType === 'success' ? '<path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline>' : '<circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line>' ?>
        </svg>
        <span><?= htmlspecialchars($notice) ?></span>
      </div>
    <?php endif; ?>

    <?php if ($profileUser): ?>
      <!-- Profile Header Hero Card -->
      <div class="card" style="padding: 32px; border-radius: 18px; background: var(--bg2); border: 1px solid var(--border); margin-bottom: 30px; position: relative; overflow: hidden;">
        <div style="display: flex; gap: 28px; align-items: center; flex-wrap: wrap;">
          
          <!-- Avatar Display -->
          <div style="width: 104px; height: 104px; border-radius: 50%; overflow: hidden; border: 2px solid var(--blue); background: radial-gradient(circle at 30% 25%, #1e293b 0%, #0b1329 60%, #030712 100%); display: flex; align-items: center; justify-content: center; flex-shrink: 0; box-shadow: 0 4px 16px rgba(0,0,0,0.25);">
            <?php if (!empty($profileUser['avatar_url'])): ?>
              <img id="avatarDisplayImg" src="<?= htmlspecialchars($profileUser['avatar_url']) ?>" alt="<?= htmlspecialchars($profileUser['display_name']) ?>" style="width: 100%; height: 100%; object-fit: cover;">
            <?php else: ?>
              <span style="font-size: 2.2rem; font-weight: 800; color: #38bdf8;"><?= strtoupper(substr($profileUser['display_name'], 0, 2)) ?></span>
            <?php endif; ?>
          </div>

          <!-- User Header Details -->
          <div style="flex: 1; min-width: 240px;">
            <div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap; margin-bottom: 6px;">
              <h1 style="font-size: 1.85rem; font-weight: 800; color: var(--text); margin: 0;"><?= htmlspecialchars($profileUser['display_name']) ?></h1>
              <?php if (!empty($profileUser['is_verified'])): ?>
                <span title="Verified Developer" style="display: inline-flex; align-items: center; justify-content: center; width: 22px; height: 22px; border-radius: 50%; background: #38bdf8; color: #020817; font-weight: 800; font-size: 0.75rem;">&#10003;</span>
              <?php endif; ?>
              <span style="font-size: 0.76rem; font-weight: 700; text-transform: uppercase; padding: 3px 10px; border-radius: 9999px; background: rgba(56, 189, 248, 0.15); color: #38bdf8; border: 1px solid rgba(56, 189, 248, 0.3);">
                <?= htmlspecialchars(ucfirst($profileUser['role'] ?? 'Developer')) ?>
              </span>
            </div>

            <p style="font-size: 0.95rem; color: var(--muted); margin: 0 0 12px; line-height: 1.5;">
              <?= htmlspecialchars($profileUser['bio'] ?? 'TezzNative systems developer and ecosystem contributor.') ?>
            </p>

            <div style="display: flex; align-items: center; gap: 18px; flex-wrap: wrap; font-size: 0.85rem; color: var(--dim);">
              <span>Joined <?= date('M d, Y', strtotime($profileUser['join_date'] ?? $profileUser['created_at'] ?? 'now')) ?></span>
              <span>&bull;</span>
              <strong style="color: #10b981;"><?= number_format((int)($profileUser['reputation'] ?? 10)) ?> Reputation</strong>
              <?php if (!empty($profileUser['github_url'])): ?>
                <span>&bull;</span>
                <a href="<?= htmlspecialchars($profileUser['github_url']) ?>" target="_blank" rel="noopener" style="color: var(--blue); text-decoration: none; display: inline-flex; align-items: center; gap: 4px;">
                  <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 19c-5 1.5-5-2.5-7-3m14 6v-3.87a3.37 3.37 0 0 0-.94-2.61c3.14-.35 6.44-1.54 6.44-7A5.44 5.44 0 0 0 20 4.77 5.07 5.07 0 0 0 19.91 1S18.73.65 16 2.48a13.38 13.38 0 0 0-7 0C6.27.65 5.09 1 5.09 1A5.07 5.07 0 0 0 5 4.77a5.44 5.44 0 0 0-1.5 3.78c0 5.42 3.3 6.61 6.44 7A3.37 3.37 0 0 0 9 18.13V22"></path></svg>
                  GitHub
                </a>
              <?php endif; ?>
            </div>
          </div>

          <!-- Actions -->
          <?php if ($isOwnProfile): ?>
            <div>
              <a href="/profile?action=logout" class="btn btn-secondary" style="font-size: 0.84rem; padding: 7px 14px; display: inline-flex; align-items: center; gap: 6px;">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"></path><polyline points="16 17 21 12 16 7"></polyline><line x1="21" y1="12" x2="9" y2="12"></line></svg>
                Sign Out
              </a>
            </div>
          <?php endif; ?>
        </div>
      </div>

      <?php if ($isOwnProfile): ?>
        <!-- Edit Profile & Avatar Form -->
        <div style="display: grid; grid-template-columns: 3fr 2fr; gap: 24px;" class="profile-layout-grid">
          
          <!-- Column 1: Details & Photo Upload -->
          <div class="card" style="padding: 28px; border-radius: 16px; background: var(--bg2); border: 1px solid var(--border);">
            <h3 style="font-size: 1.25rem; font-weight: 700; color: var(--text); margin-bottom: 20px; display: flex; align-items: center; gap: 8px;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg>
              Edit Profile &amp; Avatar
            </h3>

            <form method="POST" action="" enctype="multipart/form-data" style="display: flex; flex-direction: column; gap: 18px;">
              <input type="hidden" name="action" value="update_profile">

              <!-- Avatar Uploader -->
              <div>
                <label style="font-size: 0.84rem; font-weight: 600; color: var(--text); margin-bottom: 8px; display: block;">Upload Avatar Image</label>
                <div style="display: flex; align-items: center; gap: 14px;">
                  <label class="btn btn-secondary" style="cursor: pointer; font-size: 0.85rem; padding: 8px 16px; display: inline-flex; align-items: center; gap: 6px;">
                    <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path><circle cx="12" cy="13" r="4"></circle></svg>
                    <span>Choose Photo</span>
                    <input type="file" name="avatar" id="avatarFileInput" accept="image/png, image/jpeg, image/webp" style="display: none;" onchange="previewUserAvatar(this)">
                  </label>
                  <span id="avatarFileName" style="font-size: 0.82rem; color: var(--dim);">JPG, PNG, WEBP (Max 5MB)</span>
                </div>
              </div>

              <!-- Display Name -->
              <div>
                <label style="font-size: 0.84rem; font-weight: 600; color: var(--text); margin-bottom: 8px; display: block;">Display Name</label>
                <input type="text" name="display_name" value="<?= htmlspecialchars($profileUser['display_name']) ?>" required
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; font-family: var(--font); outline: none;">
              </div>

              <!-- Bio -->
              <div>
                <label style="font-size: 0.84rem; font-weight: 600; color: var(--text); margin-bottom: 8px; display: block;">Bio / Headline</label>
                <textarea name="bio" rows="3" style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; font-family: var(--font); outline: none; resize: vertical;"><?= htmlspecialchars($profileUser['bio'] ?? '') ?></textarea>
              </div>

              <!-- GitHub URL -->
              <div>
                <label style="font-size: 0.84rem; font-weight: 600; color: var(--text); margin-bottom: 8px; display: block;">GitHub Profile URL</label>
                <input type="url" name="github_url" placeholder="https://github.com/username" value="<?= htmlspecialchars($profileUser['github_url'] ?? '') ?>"
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; font-family: var(--font); outline: none;">
              </div>

              <div style="margin-top: 6px;">
                <button type="submit" class="btn btn-primary" style="display: inline-flex; align-items: center; gap: 6px; padding: 0.75rem 1.5rem;">
                  <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="20 6 9 17 4 12"></polyline></svg>
                  <span>Save Profile</span>
                </button>
              </div>
            </form>
          </div>

          <!-- Column 2: Password Change -->
          <div class="card" style="padding: 28px; border-radius: 16px; background: var(--bg2); border: 1px solid var(--border); height: fit-content;">
            <h3 style="font-size: 1.25rem; font-weight: 700; color: var(--text); margin-bottom: 20px; display: flex; align-items: center; gap: 8px;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#f59e0b" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect><path d="M7 11V7a5 5 0 0 1 10 0v4"></path></svg>
              Change Password
            </h3>

            <form method="POST" action="" style="display: flex; flex-direction: column; gap: 16px;">
              <input type="hidden" name="action" value="change_password">

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Current Password</label>
                <input type="password" name="current_password" required
                  style="width: 100%; padding: 0.7rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.88rem; outline: none;">
              </div>

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">New Password</label>
                <input type="password" name="new_password" required minlength="6"
                  style="width: 100%; padding: 0.7rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.88rem; outline: none;">
              </div>

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Confirm New Password</label>
                <input type="password" name="confirm_password" required minlength="6"
                  style="width: 100%; padding: 0.7rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.88rem; outline: none;">
              </div>

              <div style="margin-top: 6px;">
                <button type="submit" class="btn btn-secondary" style="width: 100%; padding: 0.7rem;">
                  Update Password
                </button>
              </div>
            </form>
          </div>
        </div>
      <?php endif; ?>

    <?php else: ?>
      <!-- Guest State: Login & Signup Portals -->
      <div style="max-width: 800px; margin: 0 auto;">
        <div style="text-align: center; margin-bottom: 40px;">
          <h1 style="font-size: clamp(2rem, 3.5vw, 2.6rem); font-weight: 800; color: var(--text); letter-spacing: -0.5px;">Developer Portal</h1>
          <p style="font-size: 1.05rem; color: var(--text-secondary); max-width: 540px; margin: 10px auto 0; line-height: 1.6;">
            Sign in to manage your community profile, upload your custom avatar, and participate in technical discussions.
          </p>
        </div>

        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px;" class="auth-grid">
          <!-- Sign In Card -->
          <div class="card" style="padding: 32px; border-radius: 16px; background: var(--bg2); border: 1px solid var(--border);">
            <h3 style="font-size: 1.35rem; font-weight: 700; color: var(--text); margin-bottom: 18px;">Sign In</h3>
            <form method="POST" action="" style="display: flex; flex-direction: column; gap: 16px;">
              <input type="hidden" name="action" value="login">

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Email Address</label>
                <input type="email" name="email" required placeholder="developer@example.com"
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; outline: none;">
              </div>

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Password</label>
                <input type="password" name="password" required placeholder="••••••••"
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; outline: none;">
              </div>

              <div style="display: flex; justify-content: flex-end;">
                <a href="/forgot-password" style="font-size: 0.8rem; color: var(--blue); text-decoration: none;">Forgot password?</a>
              </div>

              <button type="submit" class="btn btn-primary" style="width: 100%; padding: 0.8rem;">
                Sign In to Profile
              </button>
            </form>
          </div>

          <!-- Register Card -->
          <div class="card" style="padding: 32px; border-radius: 16px; background: var(--bg2); border: 1px solid var(--border);">
            <h3 style="font-size: 1.35rem; font-weight: 700; color: var(--text); margin-bottom: 18px;">Create Profile</h3>
            <form method="POST" action="" style="display: flex; flex-direction: column; gap: 16px;">
              <input type="hidden" name="action" value="register">

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Full Name</label>
                <input type="text" name="name" required placeholder="Rohit Pathak"
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; outline: none;">
              </div>

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Email Address</label>
                <input type="email" name="email" required placeholder="developer@example.com"
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; outline: none;">
              </div>

              <div>
                <label style="font-size: 0.82rem; font-weight: 600; color: var(--text); margin-bottom: 6px; display: block;">Create Password</label>
                <input type="password" name="password" required minlength="6" placeholder="Min 6 characters"
                  style="width: 100%; padding: 0.75rem 1rem; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.9rem; outline: none;">
              </div>

              <button type="submit" class="btn btn-secondary" style="width: 100%; padding: 0.8rem; margin-top: 22px;">
                Join TezzNative Community
              </button>
            </form>
          </div>
        </div>
      </div>
    <?php endif; ?>

  </div>
</main>

<style>
@media (max-width: 800px) {
  .profile-layout-grid,
  .auth-grid {
    grid-template-columns: 1fr !important;
  }
}
</style>

<script>
function previewUserAvatar(input) {
  if (input.files && input.files[0]) {
    const file = input.files[0];
    const label = document.getElementById('avatarFileName');
    if (label) label.innerText = file.name;

    const reader = new FileReader();
    reader.onload = function (e) {
      const img = document.getElementById('avatarDisplayImg');
      if (img) img.src = e.target.result;
    };
    reader.readAsDataURL(file);
  }
}
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
