#!/usr/bin/env bash
# ==========================================================================
#  run_servers.sh -- launch the three OblivRec server processes.
#
#      bash scripts/run_servers.sh start     # launch, wait for readiness
#      bash scripts/run_servers.sh stop      # terminate by recorded PID
#      bash scripts/run_servers.sh demo      # start, run the demo, stop
#
#  Three genuinely separate OS processes, not threads. That was a deliberate
#  choice (Decisions Log 2026-09-10): it is the stronger claim, and because
#  each server serves one client to completion there is nothing concurrent to
#  express, so no threading appears anywhere in this project.
#
#  ------------------------------------------------------------------------
#  READINESS IS POLLED, NEVER SLEPT ON.
#
#  Each server prints "listening on <port>" and then accepts. A fixed `sleep 1`
#  would be both slower than necessary and unreliable under load -- the classic
#  way an integration script becomes flaky. We poll the log for that line and
#  give up with a clear message rather than hanging.
#
#  PIDs are captured at launch and used for teardown, so `stop` cannot kill an
#  unrelated process that happens to share a name.
# ==========================================================================
set -u

cd "$(dirname "$0")/.." || exit 1

BUILD=build
RUNDIR=build/run
PORTS=(7000 7001 7002)
TRANSCRIPT=bench/results/transcript.jsonl
READY_TIMEOUT=15

start_servers() {
  if [ ! -x "$BUILD/server.exe" ]; then
    echo "server.exe not built. Run: mingw32-make apps" >&2
    exit 1
  fi
  mkdir -p "$RUNDIR" "$(dirname "$TRANSCRIPT")"
  : > "$RUNDIR/pids"

  for p in 0 1 2; do
    port=${PORTS[$p]}
    log="$RUNDIR/p$p.log"
    : > "$log"
    ./"$BUILD"/server.exe --party "$p" --port "$port" \
        --transcript "$TRANSCRIPT" > "$log" 2>&1 &
    echo $! >> "$RUNDIR/pids"
    echo "  launched P$p on port $port (pid $!)"
  done

  # Poll for the readiness line rather than sleeping.
  for p in 0 1 2; do
    log="$RUNDIR/p$p.log"
    waited=0
    while ! grep -q "listening on" "$log" 2>/dev/null; do
      if ! kill -0 "$(sed -n "$((p+1))p" "$RUNDIR/pids")" 2>/dev/null; then
        echo "  P$p died before it was ready:" >&2
        sed 's/^/      /' "$log" >&2
        stop_servers
        exit 1
      fi
      waited=$((waited + 1))
      if [ "$waited" -gt $((READY_TIMEOUT * 10)) ]; then
        echo "  P$p did not become ready within ${READY_TIMEOUT}s" >&2
        sed 's/^/      /' "$log" >&2
        stop_servers
        exit 1
      fi
      sleep 0.1
    done
    echo "  P$p ready: $(grep -m1 'listening on' "$log")"
  done
}

stop_servers() {
  if [ -f "$RUNDIR/pids" ]; then
    while read -r pid; do
      [ -n "$pid" ] || continue
      if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null && echo "  stopped pid $pid"
      fi
    done < "$RUNDIR/pids"
    rm -f "$RUNDIR/pids"
  fi
}

case "${1:-start}" in
  start)
    start_servers
    echo
    echo "Servers up. Run:"
    echo "  ./build/demo.exe --user 42 --k 10 --connect 127.0.0.1:7000,127.0.0.1:7001,127.0.0.1:7002"
    echo "Then: bash scripts/run_servers.sh stop"
    ;;
  stop)
    stop_servers
    ;;
  demo)
    # The transcript is truncated per run so an analysis never mixes runs.
    : > "$TRANSCRIPT"
    start_servers
    echo
    ./"$BUILD"/demo.exe --user 42 --k 10 \
        --connect 127.0.0.1:7000,127.0.0.1:7001,127.0.0.1:7002 \
        --transcript "$TRANSCRIPT"
    rc=$?
    echo
    stop_servers
    exit $rc
    ;;
  *)
    echo "usage: $0 {start|stop|demo}" >&2
    exit 2
    ;;
esac
