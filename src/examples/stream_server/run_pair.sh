#!/bin/sh
# Fire up the simulation server, then the client, and take the server down when the client
# leaves.  The two are separate programs by design -- that is the whole point of this track --
# but they are always run together, and the ordering and the teardown are the two things that
# are easy to get wrong by hand.
#
#   ./src/examples/stream_server/run_pair.sh              # the default socket path
#   ./src/examples/stream_server/run_pair.sh /tmp/other.sock
#
# F12 quits the client; the server is then stopped and its socket removed.  Ctrl-C does the
# same, because the teardown is on a trap rather than on the happy path.
#
# THE SERVER'S OUTPUT GOES TO A FILE, NOT TO THE TERMINAL, and that is not tidiness: the
# server logs on every client attach and every control command, and anything it wrote to the
# terminal would land in the middle of the client's ncurses frame and corrupt the display.
# Its log is named at the end of the run instead.
#
# THE CLIENT NEEDS A REAL TERMINAL.  It calls initscr() through the window driver, so it must
# be on a tty -- piping it works only because ncurses gives up and reports ERR, which this
# script checks for up front rather than letting the client draw nothing in silence.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# THIS SCRIPT LIVES THREE LEVELS BELOW THE TREE ROOT -- src/examples/stream_server/ -- and the
# binaries it runs are in a build directory at that root.  Two `..` therefore, not one, and the
# build directory keeps its own name rather than following the rename: `build/harness` is what
# CMakeLists.txt, the probes and this README all say.
ROOT=$(cd "$HERE/../../.." && pwd)

SERVER=${SERVER:-$ROOT/build/harness/msp_server}
CLIENT=${CLIENT:-$ROOT/build/harness/ircitui_harness_client}
SOCK=${1:-/tmp/ircitui_sim_metrics.sock}
LOG=${LOG:-${TMPDIR:-/tmp}/msp_server.log}

SRV_PID=

# ALWAYS, on every path out.  `kill` on an empty PID is a no-op rather than an error, so this
# is safe whether or not the server ever started -- and it is a trap rather than a line at the
# end because Ctrl-C and a client crash must both take the server down with them.
cleanup() {
  if [ -n "$SRV_PID" ]; then
    kill -TERM "$SRV_PID" 2>/dev/null || true
    wait "$SRV_PID" 2>/dev/null || true
  fi
  # The server removes its own socket on the way out; this is the belt for the case where it
  # was killed so hard it could not.  Only a socket is ever removed.
  if [ -S "$SOCK" ]; then rm -f "$SOCK"; fi
}
trap cleanup EXIT INT TERM

if [ ! -x "$SERVER" ]; then
  printf 'no server binary at %s\n\nbuild the pair first:\n  cmake --preset dev -B build/harness -DIRCITUI_BUILD_HARNESS=ON\n  cmake --build build/harness -j\n' "$SERVER" >&2
  exit 1
fi
if [ ! -x "$CLIENT" ]; then
  printf 'no client binary at %s -- see the build commands above\n' "$CLIENT" >&2
  exit 1
fi

if [ ! -t 0 ] || [ ! -t 1 ]; then
  printf 'warning: not a terminal; the client will not be able to draw\n' >&2
fi

printf 'server: %s  ->  %s\n' "$SERVER" "$LOG"
"$SERVER" "$SOCK" >"$LOG" 2>&1 &
SRV_PID=$!

# WAIT FOR THE SOCKET, NOT FOR A SLEEP.  The client tolerates a missing server, so a fixed
# sleep would either be too short -- and the first frames would be missed -- or too long.  The
# server creates the path during bind, which is before it starts accepting, so a short retry
# after the path appears closes the rest of the gap.
i=0
while [ ! -S "$SOCK" ]; do
  i=$((i + 1))
  if [ "$i" -gt 100 ]; then
    printf 'the server did not create %s within 5s -- see %s\n' "$SOCK" "$LOG" >&2
    tail -5 "$LOG" >&2 || true
    exit 1
  fi

  # It may have died on the way: a path too long, a bind that lost a race, a socket owned by
  # somebody else.  Report the server's own message rather than timing out over it.
  if ! kill -0 "$SRV_PID" 2>/dev/null; then
    printf 'the server exited before it listened -- see %s\n' "$LOG" >&2
    tail -5 "$LOG" >&2 || true
    exit 1
  fi

  # sleep(1) in a POSIX shell is a second, which is far too coarse for a retry loop; the
  # fractional form is not portable but is present everywhere this runs.  The fallback keeps
  # the loop correct if it is not.
  sleep 0.05 2>/dev/null || sleep 1
done

printf 'client: %s\n' "$CLIENT"
printf 'F12 quits; the server will be stopped with it\n\n'

# THE CLIENT IS IN THE FOREGROUND AND OWNS THE TERMINAL.  It returns when the loop exits --
# F12, or a signal -- and the trap above then does the teardown.  A non-zero status is passed
# through rather than swallowed, so a script around this can act on it.
CLIENT_RC=0
"$CLIENT" "$SOCK" || CLIENT_RC=$?

printf '\nclient exited (%d); stopping the server\n' "$CLIENT_RC"
[ -s "$LOG" ] && { printf 'server log:\n'; sed 's/^/  /' "$LOG"; }
exit "$CLIENT_RC"
