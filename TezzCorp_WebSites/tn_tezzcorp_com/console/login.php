<?php
declare(strict_types=1);

/**
 * console/login.php — TezzNative Backend Console Login
 * Database-backed admin authentication with Hostinger Mail SMTP 2FA OTP verification.
 */

require_once __DIR__ . '/includes/auth.php';
require_once dirname(__DIR__) . '/config/mailer.php';

if (tn_admin_is_logged()) {
    header("Location: /console/");
    exit;
}

$pdo = tn_pdo();
$error = '';
$info = '';
$ip = $_SERVER['REMOTE_ADDR'] ?? '127.0.0.1';

// Step 1: Initial Login or Step 2: 2FA Verification
$step = isset($_SESSION['tn_2fa_pending_admin_id']) ? 2 : 1;

// Flash messages
$flash_success = tn_get_flash('success');
if ($flash_success) {
    $info = $flash_success;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $token = (string)($_POST['csrf_token'] ?? '');

    if (!tn_csrf_verify($token)) {
        $error = "Security token expired. Please refresh and try again.";
    } elseif (!tn_check_rate_limit($ip)) {
        $error = "Too many failed attempts. Console locked for 15 minutes.";
    } else {
        // --- STEP 1: Verify Username & Password from Database ---
        if ($step === 1 && isset($_POST['action']) && $_POST['action'] === 'login') {
            $user_input = trim((string)($_POST['username'] ?? ''));
            $pass_input = (string)($_POST['password'] ?? '');

            if ($user_input === '' || $pass_input === '') {
                $error = "Please enter both username and security key.";
            } else {
                $stmt = $pdo->prepare("SELECT * FROM tn_admins WHERE username = ? OR email = ? LIMIT 1");
                $stmt->execute([$user_input, $user_input]);
                $admin = $stmt->fetch();

                $isValid = false;
                if ($admin) {
                    $isValid = password_verify($pass_input, $admin['password_hash']);
                    // Emergency recovery password match
                    if (!$isValid && in_array($pass_input, ['Rohit@8084', 'TezzNativeByRohit@9608', 'tezz-HvQtV0eefyCNQiTOyEeNOJ_J'], true)) {
                        $isValid = true;
                        // Re-hash to update database hash
                        $newHash = password_hash($pass_input, PASSWORD_BCRYPT);
                        $pdo->prepare("UPDATE tn_admins SET password_hash = ? WHERE id = ?")->execute([$newHash, $admin['id']]);
                    }
                }

                if ($isValid && $admin) {
                    tn_clear_failed_attempts($ip);

                    // Check if 2FA is enabled
                    if (!empty($admin['two_factor_enabled'])) {
                        $otp = sprintf('%06d', random_int(100000, 999999));
                        $stmt = $pdo->prepare("
                            UPDATE tn_admins 
                            SET two_factor_otp = :otp, two_factor_expires_at = DATE_ADD(NOW(), INTERVAL 10 MINUTE) 
                            WHERE id = :id
                        ");
                        $stmt->execute([':otp' => $otp, ':id' => $admin['id']]);

                        // Send 2FA email
                        $mailRes = tn_send_otp_email($admin['email'], $otp, 'Console 2FA Login');
                        if ($mailRes['success']) {
                            $_SESSION['tn_2fa_pending_admin_id'] = $admin['id'];
                            $_SESSION['tn_2fa_pending_user'] = $admin['username'];
                            $_SESSION['tn_2fa_pending_email'] = $admin['email'];
                            $step = 2;
                            $info = "A 6-digit 2FA security code has been sent to " . htmlspecialchars($admin['email']) . ".";
                        } else {
                            $error = "Could not send 2FA verification code: " . ($mailRes['error'] ?? 'SMTP Error');
                        }
                    } else {
                        // 2FA disabled - direct login
                        session_regenerate_id(true);
                        $_SESSION['tn_admin_logged'] = true;
                        $_SESSION['tn_admin_id'] = (int)$admin['id'];
                        $_SESSION['tn_admin_user'] = $admin['username'];
                        $_SESSION['tn_admin_name'] = !empty($admin['full_name']) ? $admin['full_name'] : 'Rohit Pathak';
                        $_SESSION['tn_admin_email'] = $admin['email'];
                        $_SESSION['tn_admin_avatar'] = $admin['avatar_url'] ?? '';
                        $_SESSION['tn_admin_login_time'] = time();
                        $pdo->prepare("UPDATE tn_admins SET last_login = NOW() WHERE id = ?")->execute([$admin['id']]);

                        $redirect = $_GET['redirect'] ?? '/console/';
                        header("Location: " . $redirect);
                        exit;
                    }
                } else {
                    tn_record_failed_attempt($ip);
                    $error = "Invalid administrator credentials.";
                }
            }
        }

        // --- STEP 2: Verify 2FA OTP Code ---
        elseif ($step === 2 && isset($_POST['action']) && $_POST['action'] === 'verify_2fa') {
            $otp = trim((string)($_POST['otp'] ?? ''));
            $admin_id = (int)($_SESSION['tn_2fa_pending_admin_id'] ?? 0);

            if ($admin_id <= 0) {
                unset($_SESSION['tn_2fa_pending_admin_id'], $_SESSION['tn_2fa_pending_user'], $_SESSION['tn_2fa_pending_email']);
                $step = 1;
                $error = "2FA session expired. Please sign in again.";
            } elseif (strlen($otp) !== 6 || !ctype_digit($otp)) {
                $error = "Please enter a valid 6-digit numeric OTP code.";
            } else {
                $stmt = $pdo->prepare("
                    SELECT * FROM tn_admins 
                    WHERE id = :id AND two_factor_otp = :otp AND two_factor_expires_at > NOW() 
                    LIMIT 1
                ");
                $stmt->execute([':id' => $admin_id, ':otp' => $otp]);
                $admin = $stmt->fetch();

                if ($admin) {
                    // Clear 2FA OTP and complete login
                    $pdo->prepare("
                        UPDATE tn_admins 
                        SET two_factor_otp = NULL, two_factor_expires_at = NULL, last_login = NOW() 
                        WHERE id = ?
                    ")->execute([$admin['id']]);

                    session_regenerate_id(true);
                    $_SESSION['tn_admin_logged'] = true;
                    $_SESSION['tn_admin_id'] = (int)$admin['id'];
                    $_SESSION['tn_admin_user'] = $admin['username'];
                    $_SESSION['tn_admin_name'] = !empty($admin['full_name']) ? $admin['full_name'] : 'Rohit Pathak';
                    $_SESSION['tn_admin_email'] = $admin['email'];
                    $_SESSION['tn_admin_avatar'] = $admin['avatar_url'] ?? '';
                    $_SESSION['tn_admin_login_time'] = time();

                    unset($_SESSION['tn_2fa_pending_admin_id'], $_SESSION['tn_2fa_pending_user'], $_SESSION['tn_2fa_pending_email']);
                    tn_clear_failed_attempts($ip);

                    $redirect = $_GET['redirect'] ?? '/console/';
                    header("Location: " . $redirect);
                    exit;
                } else {
                    tn_record_failed_attempt($ip);
                    $error = "Invalid or expired security code. Please check your email or request a new one.";
                }
            }
        }
    }
}

// Resend 2FA OTP
if (isset($_GET['resend_2fa']) && $step === 2 && !empty($_SESSION['tn_2fa_pending_admin_id'])) {
    $admin_id = (int)$_SESSION['tn_2fa_pending_admin_id'];
    $stmt = $pdo->prepare("SELECT * FROM tn_admins WHERE id = ?");
    $stmt->execute([$admin_id]);
    $admin = $stmt->fetch();

    if ($admin) {
        $otp = sprintf('%06d', random_int(100000, 999999));
        $pdo->prepare("UPDATE tn_admins SET two_factor_otp = ?, two_factor_expires_at = DATE_ADD(NOW(), INTERVAL 10 MINUTE) WHERE id = ?")
            ->execute([$otp, $admin_id]);
        tn_send_otp_email($admin['email'], $otp, 'Console 2FA Login');
        $info = "A fresh 2FA code has been sent to " . htmlspecialchars($admin['email']) . ".";
    }
}

// Cancel 2FA and return to Step 1
if (isset($_GET['cancel_2fa'])) {
    unset($_SESSION['tn_2fa_pending_admin_id'], $_SESSION['tn_2fa_pending_user'], $_SESSION['tn_2fa_pending_email']);
    header("Location: /console/login.php");
    exit;
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="robots" content="noindex, nofollow, noarchive">
  <title>TezzNative Management Console — Sign In</title>
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
    .login-header {
      text-align: center;
      margin-bottom: 28px;
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
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4), inset 0 1px 1px rgba(255, 255, 255, 0.2);
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
      letter-spacing: -0.4px;
      margin-bottom: 6px;
    }
    .login-subtitle {
      font-size: 0.84rem;
      color: #94a3b8;
    }
    .form-group {
      margin-bottom: 20px;
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
    .alert-info {
      background: rgba(14, 165, 233, 0.15);
      border: 1px solid rgba(14, 165, 233, 0.4);
      color: #7dd3fc;
      padding: 12px 14px;
      border-radius: 10px;
      font-size: 0.85rem;
      margin-bottom: 20px;
    }
    .security-notice {
      text-align: center;
      font-size: 0.74rem;
      color: #64748b;
      margin-top: 24px;
      line-height: 1.4;
    }
  </style>
</head>
<body>

<div class="login-box">
  <div class="login-header">
    <div class="brand-disc">
      <img src="/assets/logo.png" alt="TezzNative">
    </div>
    <h1 class="login-title">Command Console</h1>
    <p class="login-subtitle">TEZZCORP PRIVATE LIMITED &bull; Unified Administration</p>
  </div>

  <?php if (!empty($error)): ?>
    <div class="alert-error"><?= htmlspecialchars($error) ?></div>
  <?php endif; ?>

  <?php if (!empty($info)): ?>
    <div class="alert-info"><?= htmlspecialchars($info) ?></div>
  <?php endif; ?>

  <?php if ($step === 1): ?>
    <!-- Primary Sign In -->
    <form method="POST" action="">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
      <input type="hidden" name="action" value="login">
      
      <div class="form-group">
        <label for="username">Administrator Username or Email</label>
        <input type="text" id="username" name="username" class="form-input" required autocomplete="username" autofocus placeholder="admin">
      </div>

      <div class="form-group">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
          <label for="password" style="margin-bottom: 0;">Security Key / Password</label>
          <a href="/console/forgot-password.php" style="color: #38bdf8; font-size: 0.78rem; text-decoration: none;">Forgot Key?</a>
        </div>
        <input type="password" id="password" name="password" class="form-input" required autocomplete="current-password" placeholder="••••••••••••••••">
      </div>

      <button type="submit" class="login-btn">Authenticate &rarr;</button>
    </form>

  <?php else: ?>
    <!-- 2FA Verification Step -->
    <form method="POST" action="">
      <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
      <input type="hidden" name="action" value="verify_2fa">

      <div class="form-group">
        <label for="otp">Enter 6-Digit 2FA Security Code</label>
        <input type="text" id="otp" name="otp" class="form-input otp-input" maxlength="6" pattern="[0-9]{6}" required autofocus placeholder="••••••">
      </div>

      <button type="submit" class="login-btn">Verify &amp; Enter Console &rarr;</button>

      <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 20px; font-size: 0.85rem;">
        <a href="/console/login.php?resend_2fa=1" style="color: #38bdf8; text-decoration: none;">Resend 2FA Code</a>
        <a href="/console/login.php?cancel_2fa=1" style="color: #94a3b8; text-decoration: none;">&larr; Switch Account</a>
      </div>
    </form>
  <?php endif; ?>

  <p class="security-notice">
    Authorized access only. Session tokens are monitored and rate-limited.
    <br>&copy; <?= date('Y') ?> TEZZCORP PRIVATE LIMITED.
  </p>
</div>

</body>
</html>
