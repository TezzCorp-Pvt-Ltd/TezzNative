<?php
declare(strict_types=1);

/**
 * console/database.php — TezzNative Database & Backup Infrastructure Manager
 */

require_once __DIR__ . '/includes/auth.php';
tn_admin_require();

$active_nav = 'database';
$console_title = 'Database Diagnostics & Backups';

$pdo = tn_pdo();

// ── Handle Export & Action Requests ───────────────────────────────────────────
if (isset($_GET['action'])) {
    $action = $_GET['action'];
    $csrf = $_GET['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/database.php');
        exit;
    }

    if ($action === 'export_packages_json') {
        $stmt = $pdo->query("SELECT * FROM tn_package_registry ORDER BY name ASC");
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

        header('Content-Type: application/json; charset=utf-8');
        header('Content-Disposition: attachment; filename="tezznative_packages_' . date('Ymd_His') . '.json"');
        echo json_encode([
            'exported_utc' => gmdate('Y-m-d\TH:i:s\Z'),
            'total_count'  => count($rows),
            'packages'     => $rows
        ], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
        exit;
    }

    if ($action === 'export_community_json') {
        $users = $pdo->query("SELECT id, display_name, email, github_url, role, is_verified, join_date, created_at FROM tn_community_users ORDER BY id ASC")->fetchAll(PDO::FETCH_ASSOC);
        $posts = $pdo->query("SELECT id, user_id, title, body, category, upvotes, created_at FROM tn_community_posts ORDER BY id ASC")->fetchAll(PDO::FETCH_ASSOC);

        header('Content-Type: application/json; charset=utf-8');
        header('Content-Disposition: attachment; filename="tezznative_community_' . date('Ymd_His') . '.json"');
        echo json_encode([
            'exported_utc' => gmdate('Y-m-d\TH:i:s\Z'),
            'users_count'  => count($users),
            'posts_count'  => count($posts),
            'users'        => $users,
            'posts'        => $posts,
        ], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
        exit;
    }

    if ($action === 'export_full_sql') {
        $dbname = $pdo->query("SELECT DATABASE()")->fetchColumn();
        $tables = ['tn_package_registry', 'tn_community_users', 'tn_community_posts', 'tn_admin_users'];

        header('Content-Type: text/plain; charset=utf-8');
        header('Content-Disposition: attachment; filename="tezznative_backup_' . date('Ymd_His') . '.sql"');

        echo "-- TezzNative Database Backup\n";
        echo "-- Exported on: " . gmdate('Y-m-d H:i:s') . " UTC\n";
        echo "-- Database: `{$dbname}`\n\n";
        echo "SET NAMES utf8mb4;\n";
        echo "SET FOREIGN_KEY_CHECKS = 0;\n\n";

        foreach ($tables as $table) {
            try {
                $createRow = $pdo->query("SHOW CREATE TABLE `{$table}`")->fetch(PDO::FETCH_NUM);
                if (!empty($createRow[1])) {
                    echo "-- Table structure for `{$table}`\n";
                    echo "DROP TABLE IF EXISTS `{$table}`;\n";
                    echo $createRow[1] . ";\n\n";

                    $rows = $pdo->query("SELECT * FROM `{$table}`")->fetchAll(PDO::FETCH_ASSOC);
                    if (!empty($rows)) {
                        echo "-- Data for `{$table}`\n";
                        foreach ($rows as $row) {
                            $cols = array_keys($row);
                            $escapedVals = array_map(function($val) use ($pdo) {
                                return ($val === null) ? 'NULL' : $pdo->quote((string)$val);
                            }, array_values($row));
                            echo "INSERT INTO `{$table}` (`" . implode('`, `', $cols) . "`) VALUES (" . implode(', ', $escapedVals) . ");\n";
                        }
                        echo "\n";
                    }
                }
            } catch (Throwable $e) {
                echo "-- Table `{$table}` could not be dumped: " . $e->getMessage() . "\n\n";
            }
        }

        echo "SET FOREIGN_KEY_CHECKS = 1;\n";
        echo "-- End of backup\n";
        exit;
    }
}

// ── Handle POST maintenance actions ──────────────────────────────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $csrf = $_POST['csrf_token'] ?? '';

    if (!tn_csrf_verify($csrf)) {
        tn_set_flash('error', 'Security token mismatch. Request rejected.');
        header('Location: /console/database.php');
        exit;
    }

    if ($action === 'optimize_tables') {
        $tables = ['tn_package_registry', 'tn_community_users', 'tn_community_posts'];
        $optimized = 0;
        foreach ($tables as $t) {
            try {
                $pdo->exec("OPTIMIZE TABLE `{$t}`");
                $optimized++;
            } catch (Throwable $e) {}
        }
        tn_set_flash('success', "Optimized and defragmented {$optimized} tables successfully.");
        header('Location: /console/database.php');
        exit;
    }
}

// ── Fetch Server Diagnostics ──────────────────────────────────────────────────
$t0 = microtime(true);
$version = 'Unknown';
$dbname = 'Unknown';
try {
    $version = (string)$pdo->query("SELECT VERSION()")->fetchColumn();
    $dbname = (string)$pdo->query("SELECT DATABASE()")->fetchColumn();
} catch (Throwable $e) {}
$latency_ms = round((microtime(true) - $t0) * 1000, 2);

// ── Fetch Table Metadata ──────────────────────────────────────────────────────
$tables_info = [];
$total_data_bytes = 0;
$total_index_bytes = 0;
$total_rows = 0;

try {
    $stmt = $pdo->prepare("
        SELECT 
            TABLE_NAME AS `name`,
            ENGINE AS `engine`,
            TABLE_ROWS AS `rows`,
            DATA_LENGTH AS `data_size`,
            INDEX_LENGTH AS `index_size`,
            TABLE_COLLATION AS `collation`
        FROM information_schema.TABLES 
        WHERE TABLE_SCHEMA = :schema
        ORDER BY TABLE_NAME ASC
    ");
    $stmt->execute([':schema' => $dbname]);
    $raw_tables = $stmt->fetchAll(PDO::FETCH_ASSOC);

    foreach ($raw_tables as $tbl) {
        $dsize = (int)($tbl['data_size'] ?? 0);
        $isize = (int)($tbl['index_size'] ?? 0);
        $rows = (int)($tbl['rows'] ?? 0);

        // For accurate count on active tables
        try {
            $exact = (int)$pdo->query("SELECT count(*) FROM `{$tbl['name']}`")->fetchColumn();
            $rows = $exact;
        } catch (Throwable $e) {}

        $total_data_bytes += $dsize;
        $total_index_bytes += $isize;
        $total_rows += $rows;

        $tables_info[] = [
            'name'       => $tbl['name'],
            'engine'     => $tbl['engine'] ?? 'InnoDB',
            'rows'       => $rows,
            'data_size'  => $dsize,
            'index_size' => $isize,
            'total_size' => $dsize + $isize,
            'collation'  => $tbl['collation'] ?? 'utf8mb4_unicode_ci',
        ];
    }
} catch (Throwable $e) {}

function format_db_size(int $bytes): string {
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
    <div class="kpi-label">Active Database</div>
    <div class="kpi-val" style="font-size: 1.1rem; color: #38bdf8; word-break: break-all;"><?= htmlspecialchars($dbname) ?></div>
    <div class="kpi-meta"><?= htmlspecialchars($version) ?></div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Total Records</div>
    <div class="kpi-val"><?= number_format($total_rows) ?></div>
    <div class="kpi-meta">Across <?= count($tables_info) ?> database tables</div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Total Storage Size</div>
    <div class="kpi-val"><?= format_db_size($total_data_bytes + $total_index_bytes) ?></div>
    <div class="kpi-meta">Data: <?= format_db_size($total_data_bytes) ?> &bull; Index: <?= format_db_size($total_index_bytes) ?></div>
  </div>
  <div class="kpi-card">
    <div class="kpi-label">Query Ping Latency</div>
    <div class="kpi-val" style="color: #10b981;"><?= $latency_ms ?> ms</div>
    <div class="kpi-meta">Local socket / loopback response time</div>
  </div>
</div>

<!-- Backup & Optimization Actions -->
<div class="panel" style="margin-bottom: 24px;">
  <div style="display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 14px;">
    <div>
      <h3 style="font-size: 1.1rem; font-weight: 700; color: #f8fafc; margin-bottom: 4px;">Database Maintenance &amp; Backup Hub</h3>
      <p style="font-size: 0.85rem; color: #94a3b8; margin: 0;">Export full tables, package metadata, and community user datasets.</p>
    </div>
    <div style="display: flex; gap: 10px; flex-wrap: wrap;">
      <a href="/console/database.php?action=export_packages_json&csrf_token=<?= tn_csrf_token() ?>" class="btn btn-outline">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
        Export Packages (.json)
      </a>

      <a href="/console/database.php?action=export_community_json&csrf_token=<?= tn_csrf_token() ?>" class="btn btn-outline">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
        Export Community (.json)
      </a>

      <a href="/console/database.php?action=export_full_sql&csrf_token=<?= tn_csrf_token() ?>" class="btn btn-primary">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><ellipse cx="12" cy="5" rx="9" ry="3"></ellipse><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"></path><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"></path></svg>
        Full SQL Backup (.sql)
      </a>

      <form method="POST" style="margin: 0;" onsubmit="return confirm('Defragment and optimize all database tables now?');">
        <input type="hidden" name="csrf_token" value="<?= tn_csrf_token() ?>">
        <input type="hidden" name="action" value="optimize_tables">
        <button type="submit" class="btn btn-outline">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"></path></svg>
          Optimize Tables
        </button>
      </form>
    </div>
  </div>
</div>

<!-- Database Tables Table -->
<div class="panel">
  <div class="panel-header">
    <div>
      <h3 class="panel-title">Database Tables &amp; Allocation</h3>
      <p style="font-size: 0.82rem; color: #94a3b8; margin-top: 4px;">Inspected from MySQL <code>information_schema</code>.</p>
    </div>
  </div>

  <div class="table-responsive">
    <table class="data-table">
      <thead>
        <tr>
          <th>Table Name</th>
          <th>Engine</th>
          <th>Records</th>
          <th>Data Size</th>
          <th>Index Size</th>
          <th>Total Footprint</th>
          <th>Collation</th>
        </tr>
      </thead>
      <tbody>
        <?php if (empty($tables_info)): ?>
          <tr><td colspan="7" style="text-align: center; color: #64748b; padding: 24px;">No database tables found in current schema.</td></tr>
        <?php else: ?>
          <?php foreach ($tables_info as $tbl): ?>
          <tr>
            <td>
              <div style="font-weight: 700; color: #f8fafc; font-family: 'JetBrains Mono', monospace;"><?= htmlspecialchars($tbl['name']) ?></div>
            </td>
            <td><span class="badge badge-info"><?= htmlspecialchars($tbl['engine']) ?></span></td>
            <td style="font-family: 'JetBrains Mono', monospace; font-weight: 700; color: #38bdf8;"><?= number_format($tbl['rows']) ?></td>
            <td style="font-size: 0.85rem; color: #cbd5e1;"><?= format_db_size($tbl['data_size']) ?></td>
            <td style="font-size: 0.85rem; color: #94a3b8;"><?= format_db_size($tbl['index_size']) ?></td>
            <td style="font-weight: 700; font-family: 'JetBrains Mono', monospace; color: #f8fafc;"><?= format_db_size($tbl['total_size']) ?></td>
            <td style="font-size: 0.78rem; color: #64748b;"><?= htmlspecialchars($tbl['collation']) ?></td>
          </tr>
          <?php endforeach; ?>
        <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
