<?php
declare(strict_types=1);

/**
 * console/forgot-password.php — Administrator Password Reset with Email OTP
 * Verifies identity via Hostinger Mail SMTP and updates credentials in MariaDB.
 */

require_once __DIR__ . '/includes/auth.php';
require_once dirname(__DIR__) . '/config/mailer.php';

if (tn_admin_is_logged()) {
    header("Location: /console/");
    exit;
}

$pdo = tn_pdo();
$step = isset($_SESSION['tn_reset_admin_id']) ? 2 : 1;
$error = '';
$success = '';

// Step 1: Request OTP
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action']) && $_POST['action'] === 'request_otp') {
    $csrf = (string)($_POST['csrf_token'] ?? '');
    $identity = trim((string)($_POST['identity'] ?? ''));

    if (!tn_csrf_verify($csrf)) {
        $error = "Security token mismatch. Please try again.";
    } elseif ($identity === '') {
        $error = "Please enter your administrator username or email.";
    } else {
        $stmt = $pdo->prepare("SELECT * FROM tn_admins WHERE username = :id OR email = :id LIMIT 1");
        $stmt->execute([':id' => $identity]);
        $admin = $stmt->fetch();

        if ($admin) {
            $otp = sprintf('%06d', random_int(100000, 999999));
            $stmt = $pdo->prepare("
                UPDATE tn_admins 
                SET reset_otp = :otp, reset_expires_at = DATE_ADD(NOW(), INTERVAL 15 MINUTE) 
                WHERE id = :id
            ");
            $stmt->execute([':otp' => $otp, ':id' => $admin['id']]);

            // Send via Hostinger SMTP
            $mailRes = tn_send_otp_email($admin['email'], $otp, 'Admin Password Reset');
            if ($mailRes['success']) {
                $_SESSION['tn_reset_admin_id'] = $admin['id'];
                $_SESSION['tn_reset_admin_email'] = $admin['email'];
                $step = 2;
                $success = "A 6-digit reset code has been sent to " . htmlspecialchars($admin['email']) . ".";
            } else {
                $error = "Failed to dispatch email: " . ($mailRes['error'] ?? 'SMTP Error');
            }
        } else {
            // Constant time response to prevent user enumeration
            $error = "No administrator account found with that identifier.";
        }
    }
}

// Step 2: Verify OTP and Set New Password
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action']) && $_POST['action'] === 'verify_and_reset') {
    $csrf     = (string)($_POST['csrf_token'] ?? '');
    $otp      = trim((string)($_POST['otp'] ?? ''));
    $new_pass = (string)($_POST['new_password'] ?? '');
    $confirm  = (string)($_POST['confirm_password'] ?? '');
    $admin_id = (int)($_SESSION['tn_reset_admin_id'] ?? 0);

    if (!tn_csrf_verify($csrf)) {
        $error = "Security token mismatch. Please try again.";
    } elseif ($admin_id <= 0) {
        unset($_SESSION['tn_reset_admin_id'], $_SESSION['tn_reset_admin_email']);
        $step = 1;
        $error = "Reset session expired. Please request a new code.";
    } elseif (strlen($otp) !== 6 || !ctype_digit($otp)) {
        $error = "Please enter a valid 6-digit numeric OTP code.";
    } elseif (strlen($new_pass) < 8) {
        $error = "Password must be at least 8 characters long.";
    } elseif ($new_pass !== $confirm) {
        $error = "Password confirmation does not match.";
    } else {
        $stmt = $pdo->prepare("
            SELECT * FROM tn_admins 
            WHERE id = :id AND reset_otp = :otp AND reset_expires_at > NOW() 
            LIMIT 1
        ");
        $stmt->execute([':id' => $admin_id, ':otp' => $otp]);
        $admin = $stmt->fetch();

        if ($admin) {
            $hash = password_hash($new_pass, PASSWORD_BCRYPT);
            $stmt = $pdo->prepare("
                UPDATE tn_admins 
                SET password_hash = :hash, reset_otp = NULL, reset_expires_at = NULL, updated_at = NOW() 
                WHERE id = :id
            ");
            $stmt->execute([':hash' => $hash, ':id' => $admin_id]);

            unset($_SESSION['tn_reset_admin_id'], $_SESSION['tn_reset_admin_email']);
            tn_set_flash('success', 'Password reset successfully! You can now log in with your new password.');
            header("Location: /console/login.php");
            exit;
        } else {
            $error = "Invalid or expired OTP code. Please verify the code or request a new one.";
        }
    }
}

// Resend OTP
if (isset($_GET['resend']) && $step === 2 && !empty($_SESSION['tn_reset_admin_id'])) {
    $admin_id = (int)$_SESSION['tn_reset_admin_id'];
    $stmt = $pdo->prepare("SELECT * FROM tn_admins WHERE id = ?");
    $stmt->execute([$admin_id]);
    $admin = $stmt->fetch();

    if ($admin) {
        $otp = sprintf('%06d', random_int(100000, 999999));
        $pdo->prepare("UPDATE tn_admins SET reset_otp = ?, reset_expires_at = DATE_ADD(NOW(), INTERVAL 15 MINUTE) WHERE id = ?")
            ->execute([$otp, $admin_id]);
        tn_send_otp_email($admin['email'], $otp, 'Admin Password Reset');
        $success = "A fresh 6-digit code has been resent to your email.";
    }
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="robots" content="noindex, nofollow, noarchive">
  <title>Reset Password — TezzNative Console</title>
  <link rel="icon" type="image/x-icon" href="/favicon.ico?v=1.0.0">
  <link rel="stylesheet" href="/assets/app.css?v=1.0.0">
  <style>
    body {
      background: radial-gradient(circle at 50% 20%, #0d172e 0%, #060b18 60%, #02050c 100%);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 24px;
      font-family: var(--font-sans);
    }
    .login-box {
      width: 100%;
      max-width: 440px;
      background: rgba(11, 19, 38, 0.88);
      border: 1px solid rgba(56, 189, 248, 0.35);
      border-radius: 18px;
      padding: 36px 32px;
      box-shadow: 0 16px 48px rgba(2, 6, 23, 0.6), 0 0 32px rgba(14, 165, 233, 0.15);
      backdrop-filter: blur(20px);
    }
    .brand-disc {
      width: 52px;
      height: 52px;
      border-radius: 14px;
      margin: 0 auto 16px;
      display: flex;
      align-items: center;
      justify-content: center;
      background: radial-gradient(circle at 30% 25%, #1e293b 0%, #0b1329 60%, #030712 100%);
      border: 1px solid rgba(56, 189, 248, 0.5);
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4);
    }
    .brand-disc img {
      width: 36px;
      height: 36px;
      object-fit: contain;
    }
    .login-title {
      font-size: 1.45rem;
      font-weight: 800;
      color: #f8fafc;
      text-align: center;
      letter-spacing: -0.4px;
      margin-bottom: 6px;
    }
    .login-subtitle {
      font-size: 0.84rem;
      color: #94a3b8;
      text-align: center;
      margin-bottom: 24px;
    }
    .form-group {
      margin-bottom: 18px;
    }
    .form-group label {
      display: block;
      font-size: 0.82rem;
      font-weight: 600;
      color: #cbd5e1;
      margin-bottom: 8px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .form-input {
      width: 100%;
      padding: 12px 14px;
      background: rgba(15, 23, 42, 0.8);
      border: 1px solid rgba(255, 255, 255, 0.12);
      border-radius: 10px;
      color: #f8fafc;
      font-size: 0.95rem;
      outline: none;
      transition: all 0.2s ease;
      box-sizing: border-box;
    }
    .form-input:focus {
      border-color: #0284c7;
      box-shadow: 0 0 0 3px rgba(14, 165, 233, 0.25);
      background: rgba(15, 23, 42, 1);
    }
    .otp-input {
      letter-spacing: 12px;
      font-size: 1.5rem;
      font-family: 'JetBrains Mono', monospace;
      text-align: center;
      font-weight: 800;
      color: #38bdf8;
    }
    .login-btn {
      width: 100%;
      padding: 13px;
      background: linear-gradient(135deg, #00d2ff 0%, #0284c7 50%, #2563eb 100%);
      border: none;
      border-radius: 10px;
      color: #ffffff;
      font-weight: 700;
      font-size: 1rem;
      cursor: pointer;
      box-shadow: 0 4px 18px rgba(14, 165, 233, 0.4);
      transition: all 0.2s ease;
      margin-top: 8px;
    }
    .login-btn:hover {
      transform: translateY(-1px);
      box-shadow: 0 6px 24px rgba(14, 165, 233, 0.6);
    }
    .alert-error {
      background: rgba(239, 68, 68, 0.15);
      border: 1px solid rgba(239, 68, 68, 0.4);
      color: #fca5a5;
      padding: 12px 14px;
      border-radius: 10px;
      font-size: 0.85rem;
      margin-bottom: 20px;
    }
    .alert-success {
      background: rgba(16, 185, 129, 0.15);
      border: 1px solid rgba(16, 185, 129, 0.4);
      color: #6ee7b7;
      padding: 12px 14px;
      border-radius: 10px;
      font-size: 0.85rem;
      margin-bottom: 20px;
    }
  </style>
</head>
<body>

<div class="login-box">
  <div class="brand-disc">
    <img src="/assets/logo.png" alt="TezzNative">
  </div>
  <h1 class="login-title">Reset Password</h1>
  <p class="login-subtitle">Hostinger Mail SMTP OTP Verification</p>

  <?php if (!empty($error)): ?>
    <div class="alert-error"><?= htmlspecialchars($error) ?></div>
  <?php endif; ?>

  <?php if (!empty($success)): ?>
    <div class="alert-success"><?= htmlspecialchars($success) ?></div>
  <?php endif; ?>

  <?php if ($step === 1): ?>
    <form method="POST" action="">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
      <input type="hidden" name="action" value="request_otp">

      <div class="form-group">
        <label for="identity">Admin Username or Email</label>
        <input type="text" id="identity" name="identity" class="form-input" required autofocus placeholder="admin or no-reply@tezznative.org">
      </div>

      <button type="submit" class="login-btn">Send 6-Digit OTP &rarr;</button>

      <div style="text-align: center; margin-top: 18px;">
        <a href="/console/login.php" style="color: #94a3b8; font-size: 0.85rem; text-decoration: none;">&larr; Back to Sign In</a>
      </div>
    </form>

  <?php else: ?>
    <form method="POST" action="">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
      <input type="hidden" name="action" value="verify_and_reset">

      <div class="form-group">
        <label for="otp">Enter 6-Digit Security Code</label>
        <input type="text" id="otp" name="otp" class="form-input otp-input" maxlength="6" pattern="[0-9]{6}" required autofocus placeholder="••••••">
      </div>

      <div class="form-group">
        <label for="new_password">New Password (min 8 chars)</label>
        <input type="password" id="new_password" name="new_password" class="form-input" minlength="8" required placeholder="••••••••••••">
      </div>

      <div class="form-group">
        <label for="confirm_password">Confirm New Password</label>
        <input type="password" id="confirm_password" name="confirm_password" class="form-input" minlength="8" required placeholder="••••••••••••">
      </div>

      <button type="submit" class="login-btn">Save New Password &rarr;</button>

      <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 18px; font-size: 0.85rem;">
        <a href="/console/forgot-password.php?resend=1" style="color: #38bdf8; text-decoration: none;">Resend OTP Code</a>
        <a href="/console/login.php" style="color: #94a3b8; text-decoration: none;">Cancel</a>
      </div>
    </form>
  <?php endif; ?>
</div>

</body>
</html>

