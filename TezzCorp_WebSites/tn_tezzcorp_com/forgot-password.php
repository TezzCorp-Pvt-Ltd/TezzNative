<?php
declare(strict_types=1);

/**
 * forgot-password.php — Community Password Reset via Hostinger Mail OTP
 */

require_once __DIR__ . '/config/db.php';
require_once __DIR__ . '/config/mailer.php';

$pdo = tn_pdo();
$step = isset($_SESSION['tn_public_reset_email']) ? 2 : 1;
$error = '';
$success = '';

if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

// Request OTP
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action']) && $_POST['action'] === 'request_otp') {
    $email = strtolower(trim((string)($_POST['email'] ?? '')));

    if ($email === '' || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $error = "Please enter a valid email address.";
    } else {
        $stmt = $pdo->prepare("SELECT id, display_name FROM tn_community_users WHERE email = ? LIMIT 1");
        $stmt->execute([$email]);
        $user = $stmt->fetch();

        if ($user) {
            $otp = sprintf('%06d', random_int(100000, 999999));
            $pdo->prepare("UPDATE tn_community_users SET reset_otp = ?, reset_expires_at = DATE_ADD(NOW(), INTERVAL 15 MINUTE) WHERE id = ?")
                ->execute([$otp, $user['id']]);

            $mailRes = tn_send_otp_email($email, $otp, 'Account Password Reset');
            if ($mailRes['success']) {
                $_SESSION['tn_public_reset_email'] = $email;
                $_SESSION['tn_public_reset_id'] = $user['id'];
                $step = 2;
                $success = "A 6-digit reset code has been sent to " . htmlspecialchars($email) . ".";
            } else {
                $error = "Could not send verification email. Please try again.";
            }
        } else {
            $error = "No community developer account found with that email address.";
        }
    }
}

// Verify OTP & Save New Password
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action']) && $_POST['action'] === 'verify_and_reset') {
    $otp      = trim((string)($_POST['otp'] ?? ''));
    $new_pass = (string)($_POST['new_password'] ?? '');
    $confirm  = (string)($_POST['confirm_password'] ?? '');
    $user_id  = (int)($_SESSION['tn_public_reset_id'] ?? 0);

    if ($user_id <= 0) {
        unset($_SESSION['tn_public_reset_email'], $_SESSION['tn_public_reset_id']);
        $step = 1;
        $error = "Reset session expired. Please enter your email again.";
    } elseif (strlen($otp) !== 6 || !ctype_digit($otp)) {
        $error = "Please enter a valid 6-digit numeric OTP code.";
    } elseif (strlen($new_pass) < 6) {
        $error = "Password must be at least 6 characters long.";
    } elseif ($new_pass !== $confirm) {
        $error = "Password confirmation does not match.";
    } else {
        $stmt = $pdo->prepare("SELECT id FROM tn_community_users WHERE id = ? AND reset_otp = ? AND reset_expires_at > NOW() LIMIT 1");
        $stmt->execute([$user_id, $otp]);
        if ($stmt->fetch()) {
            $hash = password_hash($new_pass, PASSWORD_BCRYPT);
            $pdo->prepare("UPDATE tn_community_users SET password_hash = ?, reset_otp = NULL, reset_expires_at = NULL WHERE id = ?")
                ->execute([$hash, $user_id]);

            unset($_SESSION['tn_public_reset_email'], $_SESSION['tn_public_reset_id']);
            header("Location: /login.php?reset_success=1");
            exit;
        } else {
            $error = "Invalid or expired verification code.";
        }
    }
}

$page_title = 'Reset Password — TezzNative';
$page_desc  = 'Reset your community developer account password with email OTP verification.';
$active_nav = 'community';
include __DIR__ . '/includes/header.php';
?>

<main id="main-content" style="padding: 60px 24px; min-height: 70vh; display: flex; align-items: center; justify-content: center;">
  <div style="width: 100%; max-width: 440px; background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius-lg); padding: 36px 30px; box-shadow: var(--shadow-md);">
    <div style="text-align: center; margin-bottom: 24px;">
      <h1 style="font-size: 1.6rem; font-weight: 800; color: var(--text); margin-bottom: 6px;">Reset Password</h1>
      <p style="font-size: 0.88rem; color: var(--text-secondary);">Enter your email to receive a secure 6-digit OTP code.</p>
    </div>

    <?php if (!empty($error)): ?>
      <div style="background: rgba(239, 68, 68, 0.12); border: 1px solid rgba(239, 68, 68, 0.3); color: #ef4444; padding: 12px 14px; border-radius: 8px; font-size: 0.85rem; margin-bottom: 20px;">
        <?= htmlspecialchars($error) ?>
      </div>
    <?php endif; ?>

    <?php if (!empty($success)): ?>
      <div style="background: rgba(16, 185, 129, 0.12); border: 1px solid rgba(16, 185, 129, 0.3); color: #10b981; padding: 12px 14px; border-radius: 8px; font-size: 0.85rem; margin-bottom: 20px;">
        <?= htmlspecialchars($success) ?>
      </div>
    <?php endif; ?>

    <?php if ($step === 1): ?>
      <form method="POST">
        <input type="hidden" name="action" value="request_otp">
        <div style="margin-bottom: 18px;">
          <label style="display: block; font-size: 0.82rem; font-weight: 700; color: var(--text); margin-bottom: 6px; text-transform: uppercase;">Your Email Address</label>
          <input type="email" name="email" required placeholder="developer@example.org" style="width: 100%; padding: 11px 14px; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.95rem; box-sizing: border-box; outline: none;">
        </div>
        <button type="submit" class="btn btn-primary" style="width: 100%; padding: 12px; font-size: 0.95rem;">Send 6-Digit OTP &rarr;</button>
        <div style="text-align: center; margin-top: 18px;">
          <a href="/login.php" style="color: var(--blue); font-size: 0.85rem; text-decoration: none;">&larr; Back to Login</a>
        </div>
      </form>
    <?php else: ?>
      <form method="POST">
        <input type="hidden" name="action" value="verify_and_reset">
        <div style="margin-bottom: 18px;">
          <label style="display: block; font-size: 0.82rem; font-weight: 700; color: var(--text); margin-bottom: 6px; text-transform: uppercase;">6-Digit OTP Code</label>
          <input type="text" name="otp" maxlength="6" pattern="[0-9]{6}" required placeholder="••••••" style="width: 100%; padding: 11px 14px; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--blue); font-size: 1.4rem; text-align: center; font-weight: 800; letter-spacing: 8px; box-sizing: border-box; font-family: monospace; outline: none;">
        </div>
        <div style="margin-bottom: 18px;">
          <label style="display: block; font-size: 0.82rem; font-weight: 700; color: var(--text); margin-bottom: 6px; text-transform: uppercase;">New Password</label>
          <input type="password" name="new_password" minlength="6" required placeholder="••••••••••••" style="width: 100%; padding: 11px 14px; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.95rem; box-sizing: border-box; outline: none;">
        </div>
        <div style="margin-bottom: 18px;">
          <label style="display: block; font-size: 0.82rem; font-weight: 700; color: var(--text); margin-bottom: 6px; text-transform: uppercase;">Confirm New Password</label>
          <input type="password" name="confirm_password" minlength="6" required placeholder="••••••••••••" style="width: 100%; padding: 11px 14px; background: var(--bg3); border: 1px solid var(--border); border-radius: var(--radius); color: var(--text); font-size: 0.95rem; box-sizing: border-box; outline: none;">
        </div>
        <button type="submit" class="btn btn-primary" style="width: 100%; padding: 12px; font-size: 0.95rem;">Save New Password &rarr;</button>
        <div style="text-align: center; margin-top: 18px;">
          <a href="/login.php" style="color: var(--dim); font-size: 0.85rem; text-decoration: none;">Cancel</a>
        </div>
      </form>
    <?php endif; ?>
  </div>
</main>

<?php include __DIR__ . '/includes/footer.php'; ?>

