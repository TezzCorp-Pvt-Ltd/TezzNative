#!/usr/bin/env bash
set -euo pipefail

PORTAL_URL="${TEZZ_PORTAL_BASE:-https://tezznative.org}"
PORTAL_URL="${PORTAL_URL%/}"
BASE_URL="${TEZZ_INSTALL_BASE:-$PORTAL_URL/download}"
case "$PORTAL_URL" in
  https://*) ;;
  *) printf 'TezzNative GUI installer: the installer portal must use HTTPS\n' >&2; exit 1 ;;
esac
GUI_TOOL=""
if command -v zenity >/dev/null 2>&1; then
  GUI_TOOL="zenity"
elif command -v yad >/dev/null 2>&1; then
  GUI_TOOL="yad"
fi

show_error() {
  if [[ "$GUI_TOOL" == "zenity" || "$GUI_TOOL" == "yad" ]]; then
    "$GUI_TOOL" --error --title="TezzNative Setup" --width=440 --text="$1" 2>/dev/null || true
  else
    printf 'TezzNative GUI installer: %s\n' "$1" >&2
  fi
}

if [[ -z "$GUI_TOOL" ]]; then
  show_error "A desktop dialog program is needed. Install Zenity (or YAD), then run install-gui.sh again. You can also install from a terminal with: curl -fsSL $PORTAL_URL/install.sh | bash"
  exit 1
fi
if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
  show_error "No graphical desktop session was detected. Run this installer from your desktop, or use the terminal install command shown on the website."
  exit 1
fi

case "$BASE_URL" in
  https://*) ;;
  *) show_error "The download source must use HTTPS."; exit 1 ;;
esac
if [[ "$BASE_URL" =~ [[:space:]] || "$BASE_URL" == *"@"* ]]; then
  show_error "Enter a valid HTTPS download source URL without credentials or spaces."
  exit 1
fi

USER_DEFAULT_DEST="${TEZZ_INSTALL_DIR:-$HOME/TezzNative}"
if [[ "$GUI_TOOL" == "zenity" ]]; then
  SCOPE="$(zenity --list --radiolist --title="TezzNative Setup" --width=520 --height=230 --text="Choose who can use this installation." --column="" --column="Install for" TRUE "Current user" FALSE "All users (admin prompt)")" || exit 0
  DEFAULT_DEST="$USER_DEFAULT_DEST"
  if [[ "$SCOPE" == "All users (admin prompt)" && -z "${TEZZ_INSTALL_DIR:-}" ]]; then DEFAULT_DEST="/opt/TezzNative"; fi
  DEST="$(zenity --entry --title="TezzNative Setup" --width=540 --text="Where should TezzNative be installed?" --entry-text="$DEFAULT_DEST")" || exit 0
  BASE_URL="$(zenity --entry --title="TezzNative Setup" --width=620 --text="SDK download source (HTTPS)" --entry-text="$BASE_URL")" || exit 0
  if zenity --question --title="TezzNative Setup" --width=440 --text="Add the TezzNative command-line tools to PATH?"; then
    ADD_PATH="yes"
  else
    ADD_PATH="no"
  fi
else
  SCOPE="$(yad --list --radiolist --title="TezzNative Setup" --width=520 --height=230 --text="Choose who can use this installation." --column="" --column="Install for" TRUE "Current user" FALSE "All users (admin prompt)")" || exit 0
  DEFAULT_DEST="$USER_DEFAULT_DEST"
  if [[ "$SCOPE" == "All users (admin prompt)" && -z "${TEZZ_INSTALL_DIR:-}" ]]; then DEFAULT_DEST="/opt/TezzNative"; fi
  DEST="$(yad --entry --title="TezzNative Setup" --width=540 --text="Where should TezzNative be installed?" --entry-text="$DEFAULT_DEST")" || exit 0
  BASE_URL="$(yad --entry --title="TezzNative Setup" --width=620 --text="SDK download source (HTTPS)" --entry-text="$BASE_URL")" || exit 0
  if yad --question --title="TezzNative Setup" --width=440 --text="Add the TezzNative command-line tools to PATH?"; then
    ADD_PATH="yes"
  else
    ADD_PATH="no"
  fi
fi

if [[ -z "$DEST" ]]; then
  show_error "Enter an install folder."
  exit 1
fi
case "$DEST" in
  "~") DEST="$HOME" ;;
  "~/"*) DEST="$HOME/${DEST#~/}" ;;
esac
if [[ "$DEST" != /* ]]; then DEST="$PWD/$DEST"; fi
if [[ "$DEST" != "/" ]]; then DEST="${DEST%/}"; fi
if [[ "$DEST" == "/" ]]; then
  show_error "The filesystem root cannot be used as the install folder. Choose a dedicated TezzNative folder."
  exit 1
fi
case "$BASE_URL" in
  https://*) ;;
  *) show_error "The download source must use HTTPS."; exit 1 ;;
esac
if [[ "$BASE_URL" =~ [[:space:]] || "$BASE_URL" == *"@"* ]]; then
  show_error "Enter a valid HTTPS download source URL without credentials or spaces."
  exit 1
fi
BASE_URL="${BASE_URL%/}"

if [[ "$SCOPE" == "All users (admin prompt)" ]]; then
  INSTALL_SCOPE="system"
  PATH_MODE="system"
else
  INSTALL_SCOPE="user"
  PATH_MODE="user"
fi
if [[ "$ADD_PATH" != "yes" ]]; then
  PATH_MODE="none"
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tezznative-gui.XXXXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
CLI_SCRIPT=""
if [[ -f "${BASH_SOURCE[0]:-}" ]]; then
  CLI_SCRIPT="$(dirname "${BASH_SOURCE[0]}")/install.sh"
fi
if [[ ! -f "$CLI_SCRIPT" ]]; then
  CLI_SCRIPT="$WORK_DIR/install.sh"
  if command -v curl >/dev/null 2>&1; then
    if ! curl -fsSL --proto '=https' --proto-redir '=https' --tlsv1.2 --connect-timeout 15 --max-time 60 "$PORTAL_URL/install.sh?nocache=$(date +%s)" -o "$CLI_SCRIPT"; then
      show_error "Could not download the TezzNative install steps from the official portal. Check your connection and try again."
      exit 1
    fi
  elif command -v wget >/dev/null 2>&1; then
    if ! wget --https-only --timeout=30 --tries=2 -qO "$CLI_SCRIPT" "$PORTAL_URL/install.sh?nocache=$(date +%s)"; then
      show_error "Could not download the TezzNative install steps from the official portal. Check your connection and try again."
      exit 1
    fi
  else
    show_error "curl or wget is required to download the TezzNative installer."
    exit 1
  fi
fi

LOG_FILE="$WORK_DIR/install.log"
INSTALL_CMD=(env \
  "TEZZ_INSTALL_BASE=$BASE_URL" \
  "TEZZ_INSTALL_DIR=$DEST" \
  "TEZZ_INSTALL_SCOPE=$INSTALL_SCOPE" \
  "TEZZ_INSTALL_AUTO_PATH=$PATH_MODE" \
  bash "$CLI_SCRIPT" install)
if [[ "$INSTALL_SCOPE" == "system" ]]; then
  if command -v pkexec >/dev/null 2>&1; then
    INSTALL_CMD=(pkexec env \
      "TEZZ_INSTALL_BASE=$BASE_URL" \
      "TEZZ_INSTALL_DIR=$DEST" \
      "TEZZ_INSTALL_SCOPE=$INSTALL_SCOPE" \
      "TEZZ_INSTALL_AUTO_PATH=$PATH_MODE" \
      bash "$CLI_SCRIPT" install)
  elif command -v sudo >/dev/null 2>&1; then
    if [[ "$GUI_TOOL" == "zenity" ]]; then
      zenity --info --title="TezzNative Setup" --width=500 --text="This system-wide install will request your sudo password in the terminal that launched the installer." || exit 0
    else
      yad --info --title="TezzNative Setup" --width=500 --text="This system-wide install will request your sudo password in the terminal that launched the installer." || exit 0
    fi
    INSTALL_CMD=(sudo env \
      "TEZZ_INSTALL_BASE=$BASE_URL" \
      "TEZZ_INSTALL_DIR=$DEST" \
      "TEZZ_INSTALL_SCOPE=$INSTALL_SCOPE" \
      "TEZZ_INSTALL_AUTO_PATH=$PATH_MODE" \
      bash "$CLI_SCRIPT" install)
  else
    show_error "System-wide setup needs pkexec or sudo. Choose Current user or install an administrator prompt tool."
    exit 1
  fi
fi

if [[ "$GUI_TOOL" == "zenity" ]]; then
  set +e
  "${INSTALL_CMD[@]}" 2>&1 \
      | tee "$LOG_FILE" \
      | awk '{ print "# " $0; fflush(); }' \
      | zenity --progress --pulsate --auto-close --no-cancel --title="Installing TezzNative" --width=520 --text="Downloading and verifying the SDK..."
  result=${PIPESTATUS[0]}
  set -e
else
  set +e
  "${INSTALL_CMD[@]}" 2>&1 \
      | tee "$LOG_FILE" \
      | awk '{ print "# " $0; fflush(); }' \
      | yad --progress --pulsate --auto-close --no-buttons --title="Installing TezzNative" --width=520 --text="Downloading and verifying the SDK..."
  result=${PIPESTATUS[0]}
  set -e
fi

if [[ $result -eq 0 ]]; then
  if [[ "$GUI_TOOL" == "zenity" ]]; then
    zenity --info --title="TezzNative Setup" --width=480 --text="TezzNative is installed in:\n$DEST\n\nOpen a new terminal, then run: tezz --version" || true
  else
    yad --info --title="TezzNative Setup" --width=480 --text="TezzNative is installed in:\n$DEST\n\nOpen a new terminal, then run: tezz --version" || true
  fi
else
  details="The installation did not complete (exit code $result). The SDK archive is checked before extraction. Review the installer output and try again."
  if [[ "$GUI_TOOL" == "zenity" ]]; then
    zenity --error --title="TezzNative Setup" --width=500 --text="$details" || true
    if zenity --question --title="TezzNative Setup" --text="Would you like to view the install log?"; then
      zenity --text-info --title="TezzNative install log" --width=760 --height=520 --filename="$LOG_FILE" || true
    fi
  else
    yad --error --title="TezzNative Setup" --width=500 --text="$details" || true
    if yad --question --title="TezzNative Setup" --text="Would you like to view the install log?"; then
      yad --text-info --title="TezzNative install log" --width=760 --height=520 --filename="$LOG_FILE" || true
    fi
  fi
  exit "$result"
fi
