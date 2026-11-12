<?php
declare(strict_types=1);

/**
 * console/releases.php — TezzNative Releases & Toolchain Artifact Manager
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'releases';
$console_title = 'Releases & SDK Toolchain';

$download_dir = realpath(__DIR__ . '/../download');
$manifest_path = $download_dir ? $download_dir . '/release_manifest.json' : '';

// ── Handle Actions ────────────────────────────────────────────────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $csrf = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/releases.php');
        exit;
    }

    if ($action === 'regenerate_manifest') {
        if (!$download_dir || !is_dir($download_dir)) {
            tn_set_flash('error', 'Download directory not accessible.');
            header('Location: /console/releases.php');
            exit;
        }

        $artifacts = [];
        $files = [
            'install.cmd',
            'install.ps1',
            'install.sh',
            'tezzc.exe',
            'tezzc-windows-x64.exe',
            'tezzc-windows-arm64.exe',
            'tezzc-linux-x64',
            'tezzc-linux-x64.gz',
            'tezznative-sdk.zip',
            'tezznative-sdk-linux.tar.gz',
            'tezz_lsp.exe',
            'tzgui.dll',
            'tzgguf.dll',
            'tzgpu.dll',
            'tzgpu_rtx3060.dll',
            'tzgpu_stub.dll',
            'tzcpu.dll',
            'tztokenizer.dll',
            'tzsafetensors.dll',
            'tzmnist.dll',
            'tzcheckpoint.dll'
        ];

        // Also scan sdk directory if exists
        $sdk_dir = $download_dir . '/sdk';
        if (is_dir($sdk_dir)) {
            $iterator = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($sdk_dir));
            foreach ($iterator as $f) {
                if ($f->isFile()) {
                    $rel = str_replace('\\', '/', substr($f->getPathname(), strlen($download_dir) + 1));
                    $files[] = $rel;
                }
            }
        }

        $files = array_unique($files);
        sort($files);

        foreach ($files as $rel) {
            $fpath = $download_dir . '/' . $rel;
            if (is_file($fpath)) {
                $hash = strtoupper(hash_file('sha256', $fpath));
                $bytes = (int)filesize($fpath);
                $artifacts[] = [
                    'path'   => 'download/' . $rel,
                    'url'    => 'https://tezznative.org/download/' . $rel,
                    'bytes'  => $bytes,
                    'sha256' => $hash,
                ];

                // Write companion .sha256 file for root artifacts
                if (!str_contains($rel, '/')) {
                    @file_put_contents($fpath . '.sha256', $hash . "  " . basename($rel) . "\n");
                }
            }
        }

        $manifestData = [
            'schema'        => 'tezznative.release-manifest.v1',
            'generated_utc' => gmdate('Y-m-d\TH:i:s\Z'),
            'project'       => 'TezzNative',
            'version'       => '1.0.0',
            'channel'       => 'release',
            'api'           => 'v1',
            'base_url'      => 'https://tezznative.org',
            'policy'        => [
                'checksum_algorithm' => 'SHA-256',
                'install_rule'       => 'Installers must fail closed when archive checksums do not match.',
                'telemetry_rule'     => 'No install telemetry is required for compiler/runtime use; portal events are best-effort and non-blocking.'
            ],
            'artifacts'     => $artifacts,
        ];

        $jsonStr = json_encode($manifestData, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
        file_put_contents($manifest_path, $jsonStr);
        $manifestHash = strtoupper(hash('sha256', $jsonStr));
        file_put_contents($manifest_path . '.sha256', $manifestHash . "  release_manifest.json\n");

        tn_set_flash('success', 'Release manifest regenerated successfully! Indexed ' . count($artifacts) . ' binaries & archives.');
        header('Location: /console/releases.php');
        exit;
    }

    if ($action === 'upload_artifact') {
        if (!isset($_FILES['artifact_file']) || $_FILES['artifact_file']['error'] !== UPLOAD_ERR_OK) {
            tn_set_flash('error', 'File upload error or no file submitted.');
            header('Location: /console/releases.php');
            exit;
        }

        $orig_name = basename($_FILES['artifact_file']['name']);
        // Sanitize filename
        $clean_name = preg_replace('/[^a-zA-Z0-9_\-\.]/', '', $orig_name);
        if (!$clean_name) {
            tn_set_flash('error', 'Invalid artifact filename.');
            header('Location: /console/releases.php');
            exit;
        }

        $target_path = $download_dir . '/' . $clean_name;
        if (move_uploaded_file($_FILES['artifact_file']['tmp_name'], $target_path)) {
            $sha256 = strtoupper(hash_file('sha256', $target_path));
            @file_put_contents($target_path . '.sha256', $sha256 . "  " . $clean_name . "\n");
            tn_set_flash('success', "Artifact '{$clean_name}' successfully uploaded and SHA256 indexed ({$sha256}).");
        } else {
            tn_set_flash('error', "Failed to write file to download repository.");
        }

        header('Location: /console/releases.php');
        exit;
    }
}

// ── Read Manifest Info ────────────────────────────────────────────────────────
$manifest_info = [
    'generated_utc' => 'Unknown',
    'version'       => '1.0.0',
    'total_count'   => 0,
    'total_bytes'   => 0,
];

if (is_file($manifest_path)) {
    $raw = @file_get_contents($manifest_path);
    if ($raw) {
        $parsed = json_decode($raw, true);
        if ($parsed) {
            $manifest_info['generated_utc'] = $parsed['generated_utc'] ?? 'Unknown';
            $manifest_info['version'] = $parsed['version'] ?? '1.0.0';
            $manifest_info['total_count'] = count($parsed['artifacts'] ?? []);
            $bytes = 0;
            foreach ($parsed['artifacts'] ?? [] as $art) {
                $bytes += (int)($art['bytes'] ?? 0);
            }
            $manifest_info['total_bytes'] = $bytes;
        }
    }
}

// ── Define Catalog of Key Tracked Artifacts ──────────────────────────────────
$tracked_files = [
    [
        'category' => 'Compilers & Core CLI',
        'items' => [
            ['name' => 'tezzc-windows-x64.exe', 'desc' => 'TezzNative Native Compiler for Windows x64 (PE64)', 'os' => 'Windows x64', 'badge' => 'x86_64'],
            ['name' => 'tezzc-windows-arm64.exe', 'desc' => 'TezzNative Native Compiler for Windows ARM64 (PE64)', 'os' => 'Windows ARM64', 'badge' => 'ARM64'],
            ['name' => 'tezzc.exe', 'desc' => 'Primary Compiler Dispatcher (x64 Default)', 'os' => 'Windows x64', 'badge' => 'CLI'],
            ['name' => 'tezzc-linux-x64', 'desc' => 'TezzNative Native ELF Compiler for Linux x86_64', 'os' => 'Linux x64', 'badge' => 'ELF64'],
            ['name' => 'tezz_lsp.exe', 'desc' => 'Language Server Protocol Daemon (LSP v3.17)', 'os' => 'Windows x64', 'badge' => 'LSP Daemon'],
        ]
    ],
    [
        'category' => 'SDK Bundles & Archives',
        'items' => [
            ['name' => 'tezznative-sdk.zip', 'desc' => 'Full TezzNative Production SDK for Windows (Standard Libraries, Headers, Libs)', 'os' => 'Windows', 'badge' => 'ZIP Bundle'],
            ['name' => 'tezznative-sdk-linux.tar.gz', 'desc' => 'Full TezzNative Production SDK for Linux (POSIX Toolchain & Libs)', 'os' => 'Linux', 'badge' => 'TAR.GZ'],
        ]
    ],
    [
        'category' => 'Native Hardware & Runtime Modules',
        'items' => [
            ['name' => 'tzgui.dll', 'desc' => 'Hardware-Accelerated Native GUI Module (DirectX / Win32)', 'os' => 'Windows x64', 'badge' => 'GUI Core'],
            ['name' => 'tzgguf.dll', 'desc' => 'Native GGUF LLM Inference Engine with Quantization Support', 'os' => 'Windows x64', 'badge' => 'AI Core'],
            ['name' => 'tzgpu_rtx3060.dll', 'desc' => 'NVIDIA CUDA Optimized Kernel Driver (RTX Series)', 'os' => 'Windows x64', 'badge' => 'CUDA Driver'],
            ['name' => 'tzgpu.dll', 'desc' => 'Generic GPU Dispatch Driver', 'os' => 'Windows x64', 'badge' => 'Vulkan/GPU'],
            ['name' => 'tzcpu.dll', 'desc' => 'AVX2 / AVX-512 Native Vector Math Acceleration Engine', 'os' => 'Windows x64', 'badge' => 'SIMD'],
            ['name' => 'tztokenizer.dll', 'desc' => 'High-Speed BPE / WordPiece Tokenizer Module', 'os' => 'Windows x64', 'badge' => 'NLP/LLM'],
            ['name' => 'tzsafetensors.dll', 'desc' => 'Zero-Copy SafeTensors Weights Parser', 'os' => 'Windows x64', 'badge' => 'Weights'],
        ]
    ],
    [
        'category' => 'Installers & Scripts',
        'items' => [
            ['name' => 'install.ps1', 'desc' => 'PowerShell One-Line Installer Script for Windows', 'os' => 'Windows', 'badge' => 'PowerShell'],
            ['name' => 'install.sh', 'desc' => 'POSIX Shell Automated Installer for Linux / macOS', 'os' => 'Linux / macOS', 'badge' => 'Shell'],
            ['name' => 'install.cmd', 'desc' => 'Legacy Command Prompt Bootstrap', 'os' => 'Windows', 'badge' => 'Batch'],
            ['name' => 'release_manifest.json', 'desc' => 'Cryptographic Release Manifest Index (v1.0.0)', 'os' => 'Universal', 'badge' => 'JSON'],
        ]
    ]
];

function format_bytes(int $bytes): string {
    if ($bytes >= 1048576) {
        return number_format($bytes / 1048576, 2) . ' MB';
    } elseif ($bytes >= 1024) {
        return number_format($bytes / 1024, 2) . ' KB';
    }
    return $bytes . ' B';
}

require_once __DIR__ . '/includes/header.php';
?>

<!-- KPI Stat Overview -->
<div class="kpi-grid">
  <div class="kpi-card">
    <div class="kpi-label">Manifest Status</div>
    <div class="kpi-val" style="color: #10b981;">v<?= htmlspecialchars($manifest_info['version']) ?></div>
    <div class="kpi-meta">Last synced <?= htmlspecialchars($manifest_info['generated_utc']) ?></div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Indexed Artifacts</div>
    <div class="kpi-val"><?= $manifest_info['total_count'] ?></div>
    <div class="kpi-meta">Compiler, SDK, and standard dynamic DLLs</div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Total Repository Size</div>
    <div class="kpi-val"><?= format_bytes($manifest_info['total_bytes']) ?></div>
    <div class="kpi-meta">All compressed binary assets &amp; tarballs</div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Integrity Algorithm</div>
    <div class="kpi-val" style="font-size: 1.25rem;">SHA-256</div>
    <div class="kpi-meta">Every binary accompanied by .sha256 digest</div>
  </div>
</div>

<!-- Quick Actions Panel -->
<div class="panel" style="margin-bottom: 24px;">
  <div style="display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 14px;">
    <div>
      <h3 style="font-size: 1.1rem; font-weight: 700; color: #f8fafc; margin-bottom: 4px;">Manifest &amp; Toolchain Operations</h3>
      <p style="font-size: 0.85rem; color: #94a3b8; margin: 0;">Recalculate SHA-256 digests and synchronize <code style="color: #38bdf8;">release_manifest.json</code> across all distributions.</p>
    </div>
    <div style="display: flex; gap: 10px; flex-wrap: wrap;">
      <form method="POST" style="margin: 0;" onsubmit="return confirm('Regenerate release_manifest.json and recalculate all SHA256 hashes?');">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" value="regenerate_manifest">
        <button type="submit" class="btn btn-primary">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
          Regenerate Manifest &amp; Hashes
        </button>
      </form>
      <button type="button" class="btn btn-outline" onclick="openModal('uploadModal')">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="17 8 12 3 7 8"></polyline><line x1="12" y1="3" x2="12" y2="15"></line></svg>
        Upload Artifact
      </button>
    </div>
  </div>
</div>

<!-- Tracked Artifacts Tables -->
<?php foreach ($tracked_files as $section): ?>
<div class="panel" style="margin-bottom: 24px;">
  <div class="panel-header">
    <h3 class="panel-title"><?= htmlspecialchars($section['category']) ?></h3>
  </div>
  <div class="table-responsive">
    <table class="data-table">
      <thead>
        <tr>
          <th>Artifact</th>
          <th>Type / Platform</th>
          <th>Size</th>
          <th>SHA-256 Checksum</th>
          <th>Last Modified</th>
          <th>Status</th>
          <th>Action</th>
        </tr>
      </thead>
      <tbody>
        <?php foreach ($section['items'] as $item):
          $fpath = $download_dir . '/' . $item['name'];
          $exists = is_file($fpath);
          $size = $exists ? format_bytes((int)filesize($fpath)) : '—';
          $sha256 = $exists ? strtoupper(hash_file('sha256', $fpath)) : '—';
          $mtime = $exists ? date('Y-m-d H:i:s', (int)filemtime($fpath)) : '—';
        ?>
        <tr>
          <td>
            <div style="font-weight: 700; color: #f8fafc; font-family: 'JetBrains Mono', monospace;"><?= htmlspecialchars($item['name']) ?></div>
            <div style="font-size: 0.78rem; color: #64748b; margin-top: 2px;"><?= htmlspecialchars($item['desc']) ?></div>
          </td>
          <td>
            <span class="badge badge-info"><?= htmlspecialchars($item['badge']) ?></span>
            <span style="font-size: 0.76rem; color: #94a3b8; display: block; margin-top: 2px;"><?= htmlspecialchars($item['os']) ?></span>
          </td>
          <td style="font-family: 'JetBrains Mono', monospace; font-size: 0.85rem; color: #cbd5e1;"><?= $size ?></td>
          <td>
            <?php if ($exists): ?>
              <div style="display: flex; align-items: center; gap: 6px;">
                <code style="font-size: 0.75rem; color: #38bdf8; max-width: 160px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;" title="<?= $sha256 ?>"><?= substr($sha256, 0, 16) ?>...</code>
                <button type="button" class="btn btn-outline" style="padding: 2px 6px; font-size: 0.7rem;" onclick="copyToClipboard('<?= $sha256 ?>', this)" title="Copy SHA256">Copy</button>
              </div>
            <?php else: ?>
              <span style="color: #64748b;">—</span>
            <?php endif; ?>
          </td>
          <td style="font-size: 0.8rem; color: #94a3b8; white-space: nowrap;"><?= $mtime ?></td>
          <td>
            <?php if ($exists): ?>
              <span class="badge badge-success">Available</span>
            <?php else: ?>
              <span class="badge badge-warning">Missing</span>
            <?php endif; ?>
          </td>
          <td>
            <?php if ($exists): ?>
              <a href="/download/<?= urlencode($item['name']) ?>" class="btn btn-outline" style="padding: 4px 10px; font-size: 0.75rem;" download>
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
                Download
              </a>
            <?php else: ?>
              <button class="btn btn-outline" style="padding: 4px 10px; font-size: 0.75rem; opacity: 0.5;" disabled>Unavailable</button>
            <?php endif; ?>
          </td>
        </tr>
        <?php endforeach; ?>
      </tbody>
    </table>
  </div>
</div>
<?php endforeach; ?>

<!-- Upload Modal -->
<div class="modal-backdrop" id="uploadModal">
  <div class="modal-card">
    <div class="modal-header">
      <h3 class="modal-title">Upload / Update Toolchain Artifact</h3>
      <button type="button" class="modal-close" onclick="closeModal('uploadModal')">&times;</button>
    </div>
    <form method="POST" enctype="multipart/form-data">
      <div class="modal-body">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" value="upload_artifact">

        <div class="form-group">
          <label class="form-label" for="artifact_file">Select Binary / Script / Archive *</label>
          <input type="file" id="artifact_file" name="artifact_file" class="form-control" required style="padding: 8px;">
          <small style="color: #94a3b8; display: block; margin-top: 4px;">Supported: .exe, .zip, .tar.gz, .dll, .ps1, .sh, .cmd. The file will overwrite any existing file of the same name in <code style="color: #38bdf8;">/download/</code> and update its SHA-256 hash.</small>
        </div>
      </div>
      <div class="modal-footer">
        <button type="button" class="btn btn-outline" onclick="closeModal('uploadModal')">Cancel</button>
        <button type="submit" class="btn btn-primary">Upload &amp; Re-hash</button>
      </div>
    </form>
  </div>
</div>

<?php require_once __DIR__ . '/includes/footer.php'; ?>

