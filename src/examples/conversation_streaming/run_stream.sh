#!/bin/sh
# Start the conversation stream server, read the stream, and take the server down when the
# reader leaves.  The server prints the port it bound and logs every attach, so its output goes
# to a FILE rather than the terminal -- anything it wrote to the terminal would land in the
# middle of the frames this script is printing.
#
#   ./src/examples/conversation_streaming/run_stream.sh              # 13 frames, the whole burst
#   FRAMES=5 ./src/examples/conversation_streaming/run_stream.sh     # how many frames to read
#
# THE PORT IS NOT AN ARGUMENT. The server takes none: it starts at 6667 and walks forward, so a
# busy 6667 needs nothing from the caller, and this script reads back whichever port it reports.
# Steering it would mean a server-side option that does not exist, and a knob that silently does
# nothing is worse than no knob.
#
# The frames go to STDOUT, raw and unmodified, one per line -- so the output of two runs can be
# compared, or piped to `jq`.  The per-frame summary goes to STDERR, so it never contaminates
# that.  Ctrl-C goes down the same trap path as a clean exit.
#
# THE READER IS NOT THE CLIENT.  The client is a later gate; what reads here is a socket dump
# with a summary, which is enough to see the wire and not enough to draw anything.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# This script lives three levels below the tree root -- src/examples/conversation_streaming/ --
# and the binary it runs is in the build directory the README names.  Two `..` therefore, not one.
ROOT=$(cd "$HERE/../../.." && pwd)

SERVER=${SERVER:-$ROOT/build/conv-stream/conversation_stream_server}
FRAMES=${FRAMES:-13}
LOG=${LOG:-${TMPDIR:-/tmp}/conv_stream_server.log}

SRV_PID=

# ALWAYS, on every path out: `kill` on an empty PID is a no-op rather than an error, and this is
# a trap rather than a line at the end because Ctrl-C and a reader that dies must both take the
# server down with them.
cleanup() {
  if [ -n "$SRV_PID" ]; then
    kill -TERM "$SRV_PID" 2>/dev/null || true
    wait "$SRV_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [ "$#" -gt 0 ]; then
  printf 'this script takes no arguments (got %s).\n  FRAMES=<n> %s   # how many frames to read\n' "$1" "$0" >&2
  exit 2
fi

if [ ! -x "$SERVER" ]; then
  printf 'no server binary at %s\n\nbuild it first:\n  cmake -S . -B build/conv-stream -DIRCITUI_BUILD_CONVERSATION_STREAM=ON -DIRCITUI_BUILD_EXAMPLE=OFF\n  cmake --build build/conv-stream --target conversation_stream_server\n' "$SERVER" >&2
  exit 1
fi

"$SERVER" >"$LOG" 2>&1 &
SRV_PID=$!

# WAIT FOR THE PORT THE SERVER REPORTS, NOT FOR A SLEEP.  A fixed sleep would be either too
# short -- and the first frames missed -- or too long, and the port is not known in advance
# because the server falls forward to the next free one.
bound=""
i=0
while [ -z "$bound" ]; do
  i=$((i + 1))
  if [ "$i" -gt 100 ]; then
    printf 'the server did not report a port within 5s -- see %s\n' "$LOG" >&2
    tail -5 "$LOG" >&2 || true
    exit 1
  fi
  # It may have died on the way: every port in the range taken, or a bind that lost a race.
  # Report the server's own message rather than timing out over it.
  if ! kill -0 "$SRV_PID" 2>/dev/null; then
    printf 'the server exited before it listened -- see %s\n' "$LOG" >&2
    tail -5 "$LOG" >&2 || true
    exit 1
  fi
  bound=$(sed -n 's/.*listening on 0\.0\.0\.0:\([0-9][0-9]*\).*/\1/p' "$LOG" 2>/dev/null | head -1)
  [ -n "$bound" ] || sleep 0.05 2>/dev/null || sleep 1
done

printf 'server: %s  ->  %s\n' "$SERVER" "$LOG" >&2
printf 'reading %s frame(s) from 127.0.0.1:%s\n' "$FRAMES" "$bound" >&2

python3 - "$bound" "$FRAMES" <<'PY'
import json
import socket
import sys

port, want = int(sys.argv[1]), int(sys.argv[2])
sock = socket.create_connection(("127.0.0.1", port), timeout=10)
sock.settimeout(5)

buf, frames = b"", []
while len(frames) < want:
    try:
        chunk = sock.recv(65536)
    except socket.timeout:
        print("the stream went quiet before %d frame(s) -- the server sends a burst and waits"
              % want, file=sys.stderr)
        break
    if not chunk:
        print("the server closed the connection", file=sys.stderr)
        break
    buf += chunk
    while b"\n" in buf and len(frames) < want:
        line, buf = buf.split(b"\n", 1)
        frames.append(line.decode("utf-8", errors="replace"))

# The wire itself, unmodified, on stdout.
for f in frames:
    print(f)

# And what it was, on stderr.
print("-- %d frame(s)" % len(frames), file=sys.stderr)
for i, f in enumerate(frames):
    try:
        kind = json.loads(f).get("frame_type", "?")
    except ValueError:
        kind = "UNPARSEABLE"
    print("   %2d  %-15s %5d bytes" % (i, kind, len(f)), file=sys.stderr)
PY

printf 'done; the server log is %s\n' "$LOG" >&2
