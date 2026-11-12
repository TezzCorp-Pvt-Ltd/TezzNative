<?php
declare(strict_types=1);

/**
 * config/mailer.php — Zero-Dependency Native SMTP Mailer for TezzNative
 * Uses direct SSL socket connection to Hostinger Mail (smtp.hostinger.com:465).
 */

function tn_send_email(string $to, string $subject, string $htmlBody, string $plainText = ''): array {
    $host = 'ssl://smtp.hostinger.com';
    $port = 465;
    $user = 'no-reply@tezznative.org';
    $pass = 'Native#TezzByRohit@9608$';
    $fromName = 'TezzNative Security';
    $fromEmail = 'no-reply@tezznative.org';

    $timeout = 10;
    $socket = @fsockopen($host, $port, $errno, $errstr, $timeout);
    if (!$socket) {
        return ['success' => false, 'error' => "SMTP Connection failed: {$errstr} ({$errno})"];
    }

    $readResponse = function() use ($socket): string {
        $data = '';
        while ($line = fgets($socket, 515)) {
            $data .= $line;
            if (substr($line, 3, 1) === ' ') break;
        }
        return $data;
    };

    $sendCommand = function(string $cmd, string $expectedCode) use ($socket, $readResponse): bool {
        fputs($socket, $cmd . "\r\n");
        $res = $readResponse();
        return strpos($res, $expectedCode) === 0 || strpos($res, $expectedCode) !== false;
    };

    try {
        $readResponse(); // Greeting

        if (!$sendCommand("EHLO tezznative.org", "250")) {
            throw new RuntimeException("EHLO failed");
        }

        if (!$sendCommand("AUTH LOGIN", "334")) {
            throw new RuntimeException("AUTH LOGIN failed");
        }

        if (!$sendCommand(base64_encode($user), "334")) {
            throw new RuntimeException("Username rejected");
        }

        if (!$sendCommand(base64_encode($pass), "235")) {
            throw new RuntimeException("Password rejected");
        }

        if (!$sendCommand("MAIL FROM:<{$fromEmail}>", "250")) {
            throw new RuntimeException("MAIL FROM rejected");
        }

        if (!$sendCommand("RCPT TO:<{$to}>", "250")) {
            throw new RuntimeException("RCPT TO rejected for {$to}");
        }

        if (!$sendCommand("DATA", "354")) {
            throw new RuntimeException("DATA command rejected");
        }

        // Email boundary and content
        $boundary = "----=_Part_" . bin2hex(random_bytes(8));
        $date = date('r');
        $headers = [];
        $headers[] = "From: {$fromName} <{$fromEmail}>";
        $headers[] = "To: <{$to}>";
        $headers[] = "Subject: {$subject}";
        $headers[] = "Date: {$date}";
        $headers[] = "MIME-Version: 1.0";
        $headers[] = "Content-Type: multipart/alternative; boundary=\"{$boundary}\"";
        $headers[] = "X-Mailer: TezzNative Native Engine v1.0.0";

        $body = implode("\r\n", $headers) . "\r\n\r\n";
        
        // Plaintext alternative
        $plain = $plainText ?: strip_tags($htmlBody);
        $body .= "--{$boundary}\r\n";
        $body .= "Content-Type: text/plain; charset=UTF-8\r\n";
        $body .= "Content-Transfer-Encoding: 7bit\r\n\r\n";
        $body .= $plain . "\r\n\r\n";

        // HTML alternative
        $body .= "--{$boundary}\r\n";
        $body .= "Content-Type: text/html; charset=UTF-8\r\n";
        $body .= "Content-Transfer-Encoding: 8bit\r\n\r\n";
        $body .= $htmlBody . "\r\n\r\n";
        $body .= "--{$boundary}--\r\n";
        $body .= ".";

        fputs($socket, $body . "\r\n");
        $sendRes = $readResponse();
        
        $sendCommand("QUIT", "221");
        fclose($socket);

        if (strpos($sendRes, "250") !== false) {
            return ['success' => true, 'message' => 'Email delivered successfully'];
        }
        return ['success' => false, 'error' => "Server did not acknowledge message: {$sendRes}"];

    } catch (Throwable $e) {
        @fclose($socket);
        return ['success' => false, 'error' => $e->getMessage()];
    }
}

/**
 * Sends a high-contrast, responsive OTP verification email.
 */
function tn_send_otp_email(string $to, string $otp, string $purpose = 'Two-Factor Authentication'): array {
    $subject = "Your {$purpose} Code: {$otp} — TezzNative";
    $year = date('Y');

    $html = <<<HTML
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>{$subject}</title>
</head>
<body style="margin: 0; padding: 0; background-color: #060b18; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; color: #f1f5f9;">
  <div style="max-width: 540px; margin: 30px auto; background: #0c1427; border: 1px solid rgba(56, 189, 248, 0.28); border-radius: 16px; overflow: hidden; box-shadow: 0 16px 40px rgba(0,0,0,0.5);">
    <div style="background: linear-gradient(135deg, #0284c7 0%, #0369a1 100%); padding: 24px 30px; text-align: center;">
      <h1 style="margin: 0; font-size: 24px; font-weight: 800; color: #ffffff; letter-spacing: -0.5px;">Tezz<span style="color: #67e8f9;">Native</span></h1>
      <p style="margin: 4px 0 0; font-size: 13px; color: #e0f2fe; text-transform: uppercase; letter-spacing: 1px; font-weight: 600;">Security &amp; Identity Verification</p>
    </div>
    
    <div style="padding: 32px 30px;">
      <p style="font-size: 15px; color: #94a3b8; margin: 0 0 16px; line-height: 1.6;">
        You have requested a secure verification code for <strong>{$purpose}</strong> on the TezzNative management portal.
      </p>

      <div style="background: #060a12; border: 1px dashed rgba(56, 189, 248, 0.4); border-radius: 12px; padding: 24px; text-align: center; margin: 24px 0;">
        <div style="font-size: 12px; font-weight: 700; color: #38bdf8; text-transform: uppercase; letter-spacing: 1.5px; margin-bottom: 8px;">One-Time Security Code</div>
        <div style="font-family: 'JetBrains Mono', monospace, Consolas, Courier, monospace; font-size: 38px; font-weight: 800; letter-spacing: 10px; color: #ffffff; text-shadow: 0 0 20px rgba(14, 165, 233, 0.6); margin-left: 10px;">
          {$otp}
        </div>
        <div style="font-size: 12px; color: #64748b; margin-top: 10px;">Valid for 10 minutes &bull; Do not share this code with anyone</div>
      </div>

      <p style="font-size: 13px; color: #64748b; line-height: 1.5; margin: 0;">
        If you did not initiate this login or password reset request, please secure your account immediately or notify <a href="mailto:security@tezznative.org" style="color: #38bdf8; text-decoration: none;">security@tezznative.org</a>.
      </p>
    </div>

    <div style="background: #070c18; padding: 18px 30px; border-top: 1px solid rgba(255, 255, 255, 0.06); text-align: center; font-size: 12px; color: #475569;">
      &copy; {$year} TEZZCORP PRIVATE LIMITED &bull; Bettiah, Bihar, India &bull; All rights reserved.
    </div>
  </div>
</body>
</html>
HTML;

    $text = "Your TezzNative {$purpose} code is: {$otp}\n\nThis code is valid for 10 minutes.\nIf you did not request this code, please ignore this email.\n\nTEZZCORP PRIVATE LIMITED";

    return tn_send_email($to, $subject, $html, $text);
}
