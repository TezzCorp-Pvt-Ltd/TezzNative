#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${TEZZ_INSTALL_BASE:-https://tezznative.org/download}"
PORTAL_URL="${TEZZ_PORTAL_BASE:-https://tezznative.org}"
PORTAL_URL="${PORTAL_URL%/}"
MODE="${1:-${TEZZ_INSTALL_MODE:-install}}"
MODE="$(printf '%s' "$MODE" | tr '[:upper:]' '[:lower:]')"

if [[ "$MODE" == "gui" || "$MODE" == "--gui" ]]; then
  case "$PORTAL_URL" in https://*) ;; *) echo "install failed: installer downloads require an HTTPS portal" >&2; exit 1 ;; esac
  GUI_SCRIPT=""
  GUI_TMP=""
  if [[ -f "${BASH_SOURCE[0]:-}" ]]; then
    GUI_SCRIPT="$(dirname "${BASH_SOURCE[0]}")/install-gui.sh"
  fi
  if [[ ! -f "$GUI_SCRIPT" ]]; then
    GUI_TMP="$(mktemp "${TMPDIR:-/tmp}/tezznative-install-gui.XXXXXXXX.sh")"
    GUI_SCRIPT="$GUI_TMP"
    if command -v curl >/dev/null 2>&1; then
      if ! curl -fsSL --proto '=https' --proto-redir '=https' --tlsv1.2 --connect-timeout 15 --max-time 60 "${PORTAL_URL}/install-gui.sh?nocache=$(date +%s)" -o "$GUI_SCRIPT"; then
        rm -f "$GUI_TMP"
        echo "install failed: could not download the graphical installer" >&2
        exit 1
      fi
    elif command -v wget >/dev/null 2>&1; then
      if ! wget --https-only --timeout=30 --tries=2 -qO "$GUI_SCRIPT" "${PORTAL_URL}/install-gui.sh?nocache=$(date +%s)"; then
        rm -f "$GUI_TMP"
        echo "install failed: could not download the graphical installer" >&2
        exit 1
      fi
    else
      rm -f "$GUI_TMP"
      echo "install failed: curl or wget is required to download the installer GUI" >&2
      exit 1
    fi
  fi
  if [[ -n "$GUI_TMP" ]]; then
    if bash "$GUI_SCRIPT"; then gui_rc=0; else gui_rc=$?; fi
    rm -f "$GUI_TMP"
    exit "$gui_rc"
  fi
  exec bash "$GUI_SCRIPT"
fi

INSTALL_SCOPE="${TEZZ_INSTALL_SCOPE:-user}"
INSTALL_SCOPE="$(printf '%s' "$INSTALL_SCOPE" | tr '[:upper:]' '[:lower:]')"
if [[ "$INSTALL_SCOPE" != "user" && "$INSTALL_SCOPE" != "system" ]]; then
  INSTALL_SCOPE="user"
fi

DEST_DEFAULT="$HOME/TezzNative"
if [[ "$INSTALL_SCOPE" == "system" ]]; then
  DEST_DEFAULT="/opt/TezzNative"
fi
DEST="${TEZZ_INSTALL_DIR:-$DEST_DEFAULT}"
SDK_DIR="$DEST/sdk"
BIN_DIR="${TEZZ_INSTALL_BIN:-$HOME/.local/bin}"
TMP_ARCHIVE="${TMPDIR:-/tmp}/tezznative-sdk-linux-$RANDOM-$RANDOM.tar.gz"
trap 'rm -f "$TMP_ARCHIVE"' EXIT
PATH_MODE="${TEZZ_INSTALL_AUTO_PATH:-auto}"
if [[ "$PATH_MODE" == "auto" ]]; then
  if [[ "$INSTALL_SCOPE" == "system" ]]; then
    PATH_MODE="system"
  else
    PATH_MODE="user"
  fi
fi
if [[ -z "${TEZZ_INSTALL_BIN:-}" ]]; then
  if [[ "$PATH_MODE" == "system" ]]; then
    BIN_DIR="/usr/local/bin"
  else
    BIN_DIR="$HOME/.local/bin"
  fi
fi
CACHE_BUST="${TEZZ_INSTALL_CACHE_BUST:-$(date +%s)-$$}"

http_get() {
  local url="$1"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 30 "$url"
    return $?
  fi
  if command -v wget >/dev/null 2>&1; then
    wget --https-only --timeout=30 --tries=2 -qO- "$url"
    return $?
  fi
  return 1
}

download_file() {
  local url="$1"
  local out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 600 "$url" -o "$out"
    return $?
  fi
  if command -v wget >/dev/null 2>&1; then
    wget --https-only --timeout=60 --tries=3 -qO "$out" "$url"
    return $?
  fi
  return 1
}

hash_file() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print toupper($1)}'
    return 0
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print toupper($1)}'
    return 0
  fi
  return 1
}

verify_archive_checksum() {
  local archive="$1"
  local file_name="$2"
  local sha_file="${TMPDIR:-/tmp}/${file_name}.$RANDOM.$RANDOM.sha256"
  if ! download_file "$BASE_URL/${file_name}.sha256?nocache=$CACHE_BUST" "$sha_file"; then
    echo "install failed: could not download checksum for $file_name"
    rm -f "$sha_file"
    return 1
  fi
  local expected
  expected="$(awk '{print toupper($1)}' "$sha_file" | head -n1)"
  rm -f "$sha_file"
  if ! printf '%s' "$expected" | grep -Eq '^[0-9A-F]{64}$'; then
    echo "install failed: invalid checksum file for $file_name"
    return 1
  fi
  local actual
  actual="$(hash_file "$archive")"
  if [[ "$actual" != "$expected" ]]; then
    echo "install failed: checksum mismatch for $file_name"
    echo "  expected: $expected"
    echo "  actual:   $actual"
    return 1
  fi
  echo "Verified SHA-256: $actual"
}

json_version() {
  sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n1
}

get_remote_version() {
  local local_version
  local_version="$(get_local_version || true)"
  local api
  api="$PORTAL_URL/api/update_check.php?platform=linux-x64&version=$local_version&mode=$MODE"
  http_get "$api" | json_version && return 0
  http_get "$BASE_URL/sdk/version.json" | json_version || true
}

send_install_event() {
  local status="$1"
  local message="$2"
  local install_id="${HOSTNAME:-tezznative-cli}"
  local version="${remote_version:-unknown}"
  if command -v curl >/dev/null 2>&1; then
    curl -fsS -X POST "$PORTAL_URL/api/install_event.php" \
      -d "platform=linux-x64" \
      -d "version=$version" \
      -d "status=$status" \
      -d "install_id=$install_id" \
      -d "message=$message" >/dev/null 2>&1 || true
  fi
}

get_local_version() {
  if [[ -f "$SDK_DIR/version.json" ]]; then
    cat "$SDK_DIR/version.json" | json_version
    return 0
  fi
  if [[ -f "$SDK_DIR/tezz.mod" ]]; then
    sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$SDK_DIR/tezz.mod" | head -n1
    return 0
  fi
  echo ""
}

require_tools() {
  if ! command -v tar >/dev/null 2>&1; then
    echo "install failed: tar is required"
    exit 1
  fi
  if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    echo "install failed: curl or wget is required"
    exit 1
  fi
}

remove_install() {
  rm -rf "$DEST"
  rm -f "$BIN_DIR/tezz" "$BIN_DIR/tezzc"
}

normalize_sdk_layout() {
  local sdk_root="${1:-$SDK_DIR}"
  mkdir -p "$sdk_root/bin"
  if [[ -x "$sdk_root/build/tezzc" && ! -x "$sdk_root/bin/tezzc-linux-x64" ]]; then
    cp "$sdk_root/build/tezzc" "$sdk_root/bin/tezzc-linux-x64"
  fi
  if [[ -x "$sdk_root/tezzc" && ! -x "$sdk_root/bin/tezzc-linux-x64" ]]; then
    cp "$sdk_root/tezzc" "$sdk_root/bin/tezzc-linux-x64"
  fi
  chmod +x "$sdk_root/tezz" 2>/dev/null || true
  chmod +x "$sdk_root/bin/tezzc-linux-x64" 2>/dev/null || true
}

assert_sdk_integrity() {
  local sdk_root="${1:-$SDK_DIR}"
  local missing=0
  local required=(
    "$sdk_root/tezz"
    "$sdk_root/tezz.mod"
    "$sdk_root/tools/tezz.tn"
    "$sdk_root/tools/probes/tls_connect_ex_probe.tn"
    "$sdk_root/lib/std.tn"
    "$sdk_root/lib/io.tn"
    "$sdk_root/bin/tezzc-linux-x64"
  )
  for item in "${required[@]}"; do
    if [[ ! -e "$item" ]]; then
      echo "missing SDK file: $item"
      missing=1
    fi
  done
  if [[ $missing -ne 0 ]]; then
    return 1
  fi
  return 0
}

write_user_profile() {
  local profile="$HOME/.profile"
  local temp_profile="$profile.tezznative.$$"
  local bin_quoted sdk_quoted
  printf -v bin_quoted '%q' "$BIN_DIR"
  printf -v sdk_quoted '%q' "$SDK_DIR"
  mkdir -p "$(dirname "$profile")"
  touch "$profile"
  awk '
    $0 == "# >>> TezzNative managed environment >>>" { managed = 1; next }
    $0 == "# <<< TezzNative managed environment <<<" { managed = 0; next }
    !managed { print }
  ' "$profile" > "$temp_profile"
  {
    printf '\n# >>> TezzNative managed environment >>>\n'
    printf 'export PATH=%s:"$PATH"\n' "$bin_quoted"
    printf 'export TEZZ_SDK_ROOT=%s\n' "$sdk_quoted"
    printf '# <<< TezzNative managed environment <<<\n'
  } >> "$temp_profile"
  chmod --reference="$profile" "$temp_profile" 2>/dev/null || true
  mv "$temp_profile" "$profile"
}

write_system_profile() {
  local file="/etc/profile.d/tezznative.sh"
  local bin_quoted sdk_quoted payload
  printf -v bin_quoted '%q' "$BIN_DIR"
  printf -v sdk_quoted '%q' "$SDK_DIR"
  payload="export PATH=$bin_quoted:\$PATH\nexport TEZZ_SDK_ROOT=$sdk_quoted"
  if command -v sudo >/dev/null 2>&1; then
    printf '%b\n' "$payload" | sudo tee "$file" >/dev/null
  else
    printf '%b\n' "$payload" > "$file"
  fi
}

configure_path() {
  local mode="$PATH_MODE"
  case "$mode" in
    user|"") write_user_profile ;;
    system) write_system_profile ;;
    none)
      ;;
    *)
      write_user_profile
      ;;
  esac
}

install_shims() {
  if [[ -w "$BIN_DIR" ]]; then
    mkdir -p "$BIN_DIR"
    ln -sf "$SDK_DIR/tezz" "$BIN_DIR/tezz"
    if [[ -x "$SDK_DIR/bin/tezzc-linux-x64" ]]; then
      ln -sf "$SDK_DIR/bin/tezzc-linux-x64" "$BIN_DIR/tezzc"
    fi
  else
    if ! command -v sudo >/dev/null 2>&1; then
      echo "install failed: cannot write command links to $BIN_DIR (sudo is required)"
      return 1
    fi
    sudo install -d "$BIN_DIR"
    sudo ln -sf "$SDK_DIR/tezz" "$BIN_DIR/tezz"
    if [[ -x "$SDK_DIR/bin/tezzc-linux-x64" ]]; then
      sudo ln -sf "$SDK_DIR/bin/tezzc-linux-x64" "$BIN_DIR/tezzc"
    fi
  fi
}

smoke_test_install() {
  local tezz_bin="$BIN_DIR/tezz"
  if [[ ! -x "$tezz_bin" ]]; then
    tezz_bin="$SDK_DIR/tezz"
  fi
  "$tezz_bin" --version >/dev/null

  local probe="${TMPDIR:-/tmp}/tezznative_smoke_$RANDOM.tn"
  cat > "$probe" <<'TN'
fn main() -> int:
  say "tezz-smoke-ok"
  ret 0
TN
  local out
  out="$("$tezz_bin" "$probe" 2>/dev/null || true)"
  rm -f "$probe"
  if [[ "$out" != *"tezz-smoke-ok"* ]]; then
    echo "install failed: post-install smoke test failed"
    return 1
  fi
}

install_payload() {
  local remote_version="$1"
  require_tools

  case "$BASE_URL" in
    https://*) ;;
    *) echo "install failed: SDK downloads require an HTTPS source"; return 1 ;;
  esac
  if [[ "$BASE_URL" == *"@"* || "$BASE_URL" =~ [[:space:]] ]]; then
    echo "install failed: SDK source URL cannot contain credentials or whitespace"
    return 1
  fi

  local install_parent
  install_parent="$(dirname "$DEST")"
  local staging_dir="$install_parent/.tezznative-stage-$$-$RANDOM"
  local backup_dir="$install_parent/.tezznative-backup-$$-$RANDOM"
  local have_previous=0
  local system_write=0
  if [[ "$INSTALL_SCOPE" == "system" && ! -w "$install_parent" ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
      echo "install failed: system scope requires sudo"
      return 1
    fi
    system_write=1
  fi
  if [[ "$DEST" == "/" || "$DEST" == *$'\n'* || "$DEST" == *$'\r'* ]]; then
    echo "install failed: choose a dedicated install directory without line breaks"
    return 1
  fi
  if [[ -e "$DEST" ]]; then
    if [[ ! -d "$DEST" ]]; then
      echo "install failed: install location exists and is not a directory: $DEST"
      return 1
    fi
    if [[ -n "$(find "$DEST" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]] \
      && [[ ! -f "$SDK_DIR/tezz.mod" && ! -f "$SDK_DIR/version.json" ]]; then
      echo "install failed: $DEST is not an existing TezzNative installation. Choose an empty folder or an existing TezzNative folder."
      return 1
    fi
  fi

  echo "Downloading TezzNative SDK archive..."
  download_file "$BASE_URL/tezznative-sdk-linux.tar.gz?nocache=$CACHE_BUST" "$TMP_ARCHIVE"
  echo "Verifying the SDK archive checksum..."
  verify_archive_checksum "$TMP_ARCHIVE" "tezznative-sdk-linux.tar.gz"

  echo "Extracting into a temporary install folder..."
  if [[ "$system_write" -eq 1 ]]; then
    if ! sudo mkdir -p "$staging_dir" || ! sudo tar -xzf "$TMP_ARCHIVE" -C "$staging_dir"; then
      sudo rm -rf "$staging_dir" || true
      echo "install failed: could not extract the SDK archive"
      return 1
    fi
    local install_owner="${SUDO_USER:-${USER:-root}}"
    local install_group
    install_group="$(id -gn "$install_owner" 2>/dev/null || printf '%s' "$install_owner")"
    if ! sudo chown -R "$install_owner":"$install_group" "$staging_dir"; then
      sudo rm -rf "$staging_dir" || true
      echo "install failed: could not set install folder permissions"
      return 1
    fi
  else
    if ! mkdir -p "$staging_dir" || ! tar -xzf "$TMP_ARCHIVE" -C "$staging_dir"; then
      rm -rf "$staging_dir"
      echo "install failed: could not extract the SDK archive"
      return 1
    fi
  fi

  local stage_sdk="$staging_dir/sdk"
  echo "Preparing and checking SDK files..."
  if ! normalize_sdk_layout "$stage_sdk" || ! assert_sdk_integrity "$stage_sdk"; then
    if [[ "$system_write" -eq 1 ]]; then sudo rm -rf "$staging_dir"; else rm -rf "$staging_dir"; fi
    rm -f "$TMP_ARCHIVE"
    echo "install failed: the downloaded SDK is missing required files"
    return 1
  fi

  if [[ -e "$DEST" ]]; then
    have_previous=1
    if [[ "$system_write" -eq 1 ]]; then
      if ! sudo mv "$DEST" "$backup_dir"; then
        sudo rm -rf "$staging_dir" || true
        echo "install failed: could not prepare the existing installation for update"
        return 1
      fi
    else
      if ! mv "$DEST" "$backup_dir"; then
        rm -rf "$staging_dir"
        echo "install failed: could not prepare the existing installation for update"
        return 1
      fi
    fi
  fi
  if [[ "$system_write" -eq 1 ]]; then
    if ! sudo mv "$staging_dir" "$DEST"; then
      if [[ "$have_previous" -eq 1 ]]; then sudo mv "$backup_dir" "$DEST" || true; fi
      sudo rm -rf "$staging_dir" || true
      rm -f "$TMP_ARCHIVE"
      return 1
    fi
  else
    if ! mv "$staging_dir" "$DEST"; then
      if [[ "$have_previous" -eq 1 ]]; then mv "$backup_dir" "$DEST" || true; fi
      rm -rf "$staging_dir"
      rm -f "$TMP_ARCHIVE"
      return 1
    fi
  fi
  rm -f "$TMP_ARCHIVE"

  echo "Adding command links and PATH configuration..."
  if ! install_shims || ! configure_path; then
    echo "install failed: could not configure command links or PATH"
    if [[ "$system_write" -eq 1 ]]; then
      sudo rm -rf "$DEST"
      if [[ "$have_previous" -eq 1 ]]; then sudo mv "$backup_dir" "$DEST" || true; fi
    else
      rm -rf "$DEST"
      if [[ "$have_previous" -eq 1 ]]; then mv "$backup_dir" "$DEST" || true; fi
    fi
    return 1
  fi
  echo "Checking the installed compiler and launcher..."
  if ! smoke_test_install; then
    echo "install failed: the installed compiler or launcher did not start"
    if [[ "$system_write" -eq 1 ]]; then
      sudo rm -rf "$DEST"
      if [[ "$have_previous" -eq 1 ]]; then sudo mv "$backup_dir" "$DEST" || true; fi
    else
      rm -rf "$DEST"
      if [[ "$have_previous" -eq 1 ]]; then mv "$backup_dir" "$DEST" || true; fi
    fi
    return 1
  fi
  if [[ "$have_previous" -eq 1 ]]; then
    if [[ "$system_write" -eq 1 ]]; then sudo rm -rf "$backup_dir"; else rm -rf "$backup_dir"; fi
  fi

  echo ""
  echo "TezzNative installed."
  echo "Install root: $DEST"
  echo "SDK root: $SDK_DIR"
  echo "Bin dir: $BIN_DIR"
  echo ""
  echo "Next steps:"
  echo "  source ~/.profile"
  echo "  tezz --version"
  echo "  tezz my_script.tn"
  echo ""
  echo "Executable script mode (Python-like):"
  echo "  1. Add shebang: #!/usr/bin/env tezz"
  echo "  2. chmod +x your_script.tn"
  echo "  3. ./your_script.tn"

  if [[ -n "$remote_version" ]]; then
    echo "Installed version: $remote_version"
  fi
}

if [[ "$MODE" == "install" || "$MODE" == "update" || "$MODE" == "reinstall" ]]; then
  echo "Checking the official TezzNative release..."
fi
remote_version="$(get_remote_version)"
local_version="$(get_local_version)"

case "$MODE" in
  check)
    echo "Local version:  ${local_version:-not-installed}"
    echo "Remote version: ${remote_version:-unknown}"
    if [[ -n "$remote_version" && -n "$local_version" && "$remote_version" == "$local_version" ]]; then
      echo "Status: up-to-date"
    elif [[ -n "$local_version" ]]; then
      echo "Status: update-available"
    else
      echo "Status: not-installed"
    fi
    send_install_event "check" "local=$local_version remote=$remote_version"
    ;;
  uninstall)
    remove_install
    echo "TezzNative removed from $DEST"
    send_install_event "uninstall" "removed"
    ;;
  reinstall)
    remove_install
    install_payload "$remote_version"
    send_install_event "reinstall" "completed"
    ;;
  update)
    if [[ -z "$local_version" ]]; then
      install_payload "$remote_version"
      send_install_event "install" "installed from update mode"
    elif [[ -n "$remote_version" && "$local_version" == "$remote_version" ]]; then
      echo "TezzNative is already up-to-date ($local_version)."
      send_install_event "check" "already up-to-date"
    else
      install_payload "$remote_version"
      send_install_event "update" "updated"
    fi
    ;;
  install|"")
    install_payload "$remote_version"
    send_install_event "install" "completed"
    ;;
  *)
    echo "Unknown mode: $MODE"
    echo "Usage: install.sh [install|update|reinstall|uninstall|check]"
    exit 1
    ;;
esac
