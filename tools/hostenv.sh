# hostenv.sh - host differences of the tooling scripts (sourced, POSIX sh).
#   $PY          the Python interpreter (.tools/venv if present, else python3/python)
#   $HOST_OS     windows | macos | linux
#   $BAT         ".bat" on Windows, "" elsewhere (Ghidra's launcher scripts)
#   native PATH  a path as the host's native tools want it (cygpath -w on Windows)
#   find_java    sets JAVA_HOME to a JDK 21 (Ghidra) when it is not set
case "$(uname -s)" in
  Darwin) HOST_OS=macos ;;
  Linux)  HOST_OS=linux ;;
  *)      HOST_OS=windows ;;
esac
if [ "$HOST_OS" = windows ]; then BAT=.bat; else BAT=; fi
if [ -z "$PY" ]; then
  if [ -x "$ROOT/.tools/venv/bin/python" ]; then PY="$ROOT/.tools/venv/bin/python"
  elif command -v python3 >/dev/null 2>&1; then PY=python3
  else PY=python; fi
fi
native() { if [ "$HOST_OS" = windows ]; then cygpath -w "$1"; else printf '%s\n' "$1"; fi; }
find_java() {
  [ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/java" ] && return 0
  case "$HOST_OS" in
  windows) JAVA_HOME=$(ls -d "/c/Program Files/Eclipse Adoptium/jdk-21"* 2>/dev/null | head -1) ;;
  macos)
    JAVA_HOME=$(/usr/libexec/java_home -v 21 2>/dev/null || true)
    if [ -z "$JAVA_HOME" ] && command -v brew >/dev/null 2>&1; then
      # Homebrew's openjdk@21 is keg-only (not registered with java_home)
      J=$(brew --prefix openjdk@21 2>/dev/null)/libexec/openjdk.jdk/Contents/Home
      [ -x "$J/bin/java" ] && JAVA_HOME=$J
    fi ;;
  *) JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$(command -v java)")")") ;;
  esac
  [ -n "$JAVA_HOME" ] || { echo "no JDK 21 found (set JAVA_HOME)"; return 1; }
  export JAVA_HOME
}
