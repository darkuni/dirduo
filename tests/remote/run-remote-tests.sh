#!/bin/bash
# Starts a throwaway FTP server and SSH server on localhost, then runs the
# remote-location tests against them through KIO.
#
#   tests/remote/run-remote-tests.sh <build-dir> [test arguments]
#
# Set LINOPUS_REMOTE_PROGRAM to run another program from the build directory
# against the same servers (linopus_snapshot for screenshots).
#
# Needs: python3-pyftpdlib, openssh-server, dbus (dbus-run-session), and a
# Linopus build with KIO. Run as a user that may start sshd (root in CI).
set -euo pipefail
BUILD=${1:-build}
WORK=$(mktemp -d)
cleanup() {
    [ -n "${FTP_PID:-}" ] && kill "$FTP_PID" 2>/dev/null || true
    [ -n "${SSH_PID:-}" ] && kill "$SSH_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

# FTP: user "deck", password "steam", serving $WORK/ftp.
mkdir -p "$WORK/ftp"
python3 - "$WORK/ftp" > "$WORK/ftp.log" 2>&1 <<'PY' &
import sys
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer
auth = DummyAuthorizer()
auth.add_user("deck", "steam", sys.argv[1], perm="elradfmwMT")
h = FTPHandler
h.authorizer = auth
h.passive_ports = range(30000, 30100)
FTPServer(("127.0.0.1", 2121), h).serve_forever()
PY
FTP_PID=$!

# SSH on port 2222, key login for the current user, host key pre-trusted.
# sshd and libssh find ~/.ssh through the account database, not $HOME (they
# differ in CI containers), so use that.
SSH_HOME=$(getent passwd "$(id -un)" | cut -d: -f6)
mkdir -p "$WORK/ssh" "$WORK/sftp" "$SSH_HOME/.ssh" /run/sshd
chmod 700 "$SSH_HOME/.ssh"
ssh-keygen -q -t ed25519 -N '' -f "$WORK/ssh/host_key"
[ -f "$SSH_HOME/.ssh/id_ed25519" ] || ssh-keygen -q -t ed25519 -N '' -f "$SSH_HOME/.ssh/id_ed25519"
cat "$SSH_HOME/.ssh/id_ed25519.pub" >> "$SSH_HOME/.ssh/authorized_keys"
chmod 600 "$SSH_HOME/.ssh/authorized_keys"
cat > "$WORK/ssh/sshd_config" <<CFG
Port 2222
ListenAddress 127.0.0.1
HostKey $WORK/ssh/host_key
PidFile $WORK/ssh/sshd.pid
PasswordAuthentication no
PubkeyAuthentication yes
PermitRootLogin prohibit-password
StrictModes no
Subsystem sftp internal-sftp
CFG
/usr/sbin/sshd -D -f "$WORK/ssh/sshd_config" -E "$WORK/ssh.log" &
SSH_PID=$!

for i in $(seq 1 50); do
    if (echo > /dev/tcp/127.0.0.1/2121) 2>/dev/null && (echo > /dev/tcp/127.0.0.1/2222) 2>/dev/null; then
        break
    fi
    sleep 0.2
done
ssh-keyscan -p 2222 127.0.0.1 2>/dev/null >> "$SSH_HOME/.ssh/known_hosts"
export HOME="$SSH_HOME"

export LINOPUS_TEST_FTP="ftp://deck:steam@127.0.0.1:2121"
export LINOPUS_TEST_FTP_DIR="$WORK/ftp"
export LINOPUS_TEST_SFTP="sftp://$(id -un)@127.0.0.1:2222"
export LINOPUS_TEST_SFTP_DIR="$WORK/sftp"
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="$WORK/runtime"
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

# No test may wait on a prompt forever.
export QTEST_FUNCTION_TIMEOUT=60000

# Output goes to a file, not a pipe: KIO's helper daemons outlive the test
# and would hold a pipe open forever.
set +e
dbus-run-session -- "$BUILD/${LINOPUS_REMOTE_PROGRAM:-tst_remote}" "${@:2}" > "$WORK/test.log" 2>&1 < /dev/null
rc=$?
set -e
cat "$WORK/test.log"
pkill -x kiod6 2>/dev/null || true
if [ "$rc" -ne 0 ] && [ -n "${GITHUB_ACTIONS:-}" ]; then
    # Failures as annotations: readable even when the full log is not.
    grep -E "^(FAIL|QFATAL|QWARN)|Actual|Expected|Loc:" "$WORK/test.log" | head -20 | while IFS= read -r line; do
        echo "::error::${line}"
    done
    tail -5 "$WORK/ssh.log" 2>/dev/null | while IFS= read -r line; do echo "::warning::sshd: ${line}"; done
fi
exit "$rc"
