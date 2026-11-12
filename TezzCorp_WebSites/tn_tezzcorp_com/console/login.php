<?php
declare(strict_types=1);

/**
 * console/login.php — TezzNative Backend Console Login
 * Completely unlinked from public site; rate-limited and secured.
 */

require_once __DIR__ . '/includes/auth.php';

if (tn_admin_is_logged()) {
    header("Location: /console/");
    exit;
}

$error = '';
$ip = $_SERVER['REMOTE_ADDR'] ?? '127.0.0.1';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $user = trim((string)($_POST['username'] ?? ''));
    $pass = (string)($_POST['password'] ?? '');
    $token = (string)($_POST['csrf_token'] ?? '');

    if (!tn_csrf_verify($token)) {
        $error = "Security token expired. Please try again.";
    } elseif (!tn_check_rate_limit($ip)) {
        $error = "Too many failed attempts. Console locked for 15 minutes.";
    } else {
        // Valid credentials
        $validUsers = ['admin', 'rohit', 'tezzcorp'];
        $validPasswords = [
            'tezz-HvQtV0eefyCNQiTOyEeNOJ_J',
            'Rohit@8084',
            'TezzNativeByRohit@9608'
        ];

        $isValid = in_array(strtolower($user), $validUsers) && in_array($pass, $validPasswords);

        if ($isValid) {
            session_regenerate_id(true);
            $_SESSION['tn_admin_logged'] = true;
            $_SESSION['tn_admin_user'] = $user;
            $_SESSION['tn_admin_login_time'] = time();
            tn_clear_failed_attempts($ip);

            $redirect = $_GET['redirect'] ?? '/console/';
            header("Location: " . $redirect);
            exit;
        } else {
            tn_record_failed_attempt($ip);
            $error = "Invalid administrator credentials.";
        }
    }
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
      background: rgba(11, 19, 38, 0.85);
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
    }
    .form-input:focus {
      border-color: #0284c7;
      box-shadow: 0 0 0 3px rgba(14, 165, 233, 0.25);
      background: rgba(15, 23, 42, 1);
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
      display: flex;
      align-items: center;
      gap: 10px;
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
    <div class="alert-error">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line></svg>
      <span><?= htmlspecialchars($error) ?></span>
    </div>
  <?php endif; ?>

  <form method="POST" action="">
    <input type="hidden" name="csrf_token" value="<?= htmlspecialchars(tn_csrf_token()) ?>">
    
    <div class="form-group">
      <label for="username">Administrator Username</label>
      <input type="text" id="username" name="username" class="form-input" required autocomplete="username" autofocus placeholder="admin">
    </div>

    <div class="form-group">
      <label for="password">Security Key / Password</label>
      <input type="password" id="password" name="password" class="form-input" required autocomplete="current-password" placeholder="••••••••••••••••">
    </div>

    <button type="submit" class="login-btn">Authenticate &rarr;</button>
  </form>

  <p class="security-notice">
    Authorized access only. Session tokens are monitored and rate-limited.
    <br>&copy; <?= date('Y') ?> TEZZCORP PRIVATE LIMITED.
  </p>
</div>

</body>
</html>
