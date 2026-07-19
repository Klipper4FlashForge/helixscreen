#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""TCP proxy that drops the client-side connection on demand, to soak-test the
ESP32 K-Touch's Moonraker reconnect logic (Plan 4 Task 9: F5/F8/R3/R4) against
a stand-in for a server-side WebSocket disconnect, WITHOUT restarting the real
printer's services.

Point the device at this proxy (sdkconfig.local URL override) instead of the
Voron directly; the proxy forwards every byte both ways transparently and only
misbehaves when told to. Each "drop" tears down the whole proxied session (both
the K-Touch-facing socket and the current upstream socket) so the device sees
exactly what a server-side disconnect looks like — the real Voron's Moonraker
service itself is never touched or restarted; the proxy just opens a fresh
upstream connection for the device's next reconnect attempt.

Usage:
    # Drop the current client connection every 45s, forever:
    ./scripts/esp32_ws_chaos_proxy.py --listen-port 7125 \\
        --upstream-host 192.168.1.112 --upstream-port 7125 --drop-every 45

    # Drop on demand instead, from another terminal:
    kill -USR1 <pid>          # or: ./scripts/esp32_ws_chaos_proxy.py --pid-file /tmp/chaos.pid
                               #     kill -USR1 $(cat /tmp/chaos.pid)

Stdlib only (socket + threading + signal) — no external dependencies, matches
the brief's "host-side, stdlib-only Python" requirement.
"""
import argparse
import signal
import socket
import sys
import threading
import time

# Chunk size for the bidirectional pump. Moonraker JSON-RPC messages are well
# under this; large ones just take a few extra recv() calls, no framing here —
# this proxy is a dumb byte pipe.
BUF_SIZE = 65536


class ChaosProxy:
    def __init__(self, listen_host, listen_port, upstream_host, upstream_port):
        self.listen_host = listen_host
        self.listen_port = listen_port
        self.upstream_host = upstream_host
        self.upstream_port = upstream_port
        self.cycle = 0
        self._drop_requested = threading.Event()

    def request_drop(self):
        """Arm a drop of the CURRENT client connection (if any). Called from
        the SIGUSR1 handler or the --drop-every timer thread."""
        self._drop_requested.set()

    def _log(self, msg):
        ts = time.strftime("%Y-%m-%d %H:%M:%S")
        print(f"[{ts}] {msg}", flush=True)

    def _pump(self, src, dst, stop_event):
        try:
            while not stop_event.is_set():
                data = src.recv(BUF_SIZE)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        finally:
            stop_event.set()

    def _drop_watcher(self, client_sock, upstream_sock, stop_event):
        """Polls for a requested drop while this connection is active; the
        moment one is requested, tears down BOTH sides so the entire proxied
        session ends — matching a genuine server-side disconnect, where the
        device's socket AND the (now-stale) upstream socket both go away. The
        device opens a fresh TCP+WS connection on its next reconnect attempt,
        and the proxy opens a fresh upstream connection to the real Moonraker
        for it, same as a real session teardown/restart would look like.

        shutdown(SHUT_RDWR) on each socket — not a bare close()/SO_LINGER RST
        — is the reliable mechanism: each pump thread is concurrently blocked
        in a recv() on one of these two sockets, and closing an fd out from
        under a thread blocked in recv() on it is a well-known race (measured:
        an RST is silently swallowed and the remote peer sees nothing until
        ITS OWN multi-second timeout, defeating the point of a deterministic
        soak drop). shutdown() is documented-safe to call from another thread
        specifically to unblock a peer's in-progress blocking I/O, and is
        delivered to the remote socket immediately (measured: FIN observed
        client-side in <10ms). Both pump threads wake with EOF/an error, set
        stop_event, and handle_connection() closes both sockets once nothing
        else references them.
        """
        while not stop_event.is_set():
            if self._drop_requested.is_set():
                self._drop_requested.clear()
                self.cycle += 1
                self._log(f"DROP cycle={self.cycle} — tearing down proxied session")
                for sock in (client_sock, upstream_sock):
                    try:
                        sock.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                stop_event.set()
                return
            time.sleep(0.1)

    def handle_connection(self, client_sock, client_addr):
        # Runs in its own thread (see serve_forever) so a slow/hung teardown
        # of one connection can never block the accept loop from taking the
        # device's next reconnect.
        self._log(f"client connected: {client_addr}")
        try:
            upstream_sock = socket.create_connection(
                (self.upstream_host, self.upstream_port), timeout=10
            )
        except OSError as e:
            self._log(f"upstream connect failed: {e}")
            client_sock.close()
            return

        stop_event = threading.Event()
        threads = [
            threading.Thread(
                target=self._pump, args=(client_sock, upstream_sock, stop_event), daemon=True
            ),
            threading.Thread(
                target=self._pump, args=(upstream_sock, client_sock, stop_event), daemon=True
            ),
            threading.Thread(
                target=self._drop_watcher, args=(client_sock, upstream_sock, stop_event),
                daemon=True,
            ),
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        for sock in (client_sock, upstream_sock):
            try:
                sock.close()
            except OSError:
                pass
        self._log(f"client disconnected: {client_addr}")

    def serve_forever(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((self.listen_host, self.listen_port))
        listener.listen(5)
        self._log(
            f"listening on {self.listen_host}:{self.listen_port} -> "
            f"{self.upstream_host}:{self.upstream_port}"
        )
        try:
            while True:
                client_sock, client_addr = listener.accept()
                threading.Thread(
                    target=self.handle_connection, args=(client_sock, client_addr), daemon=True
                ).start()
        except KeyboardInterrupt:
            pass
        finally:
            listener.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--listen-host", default="0.0.0.0", help="Interface to listen on (default: all)")
    parser.add_argument("--listen-port", type=int, required=True, help="Port the device connects to")
    parser.add_argument("--upstream-host", required=True, help="Real Moonraker host (e.g. 192.168.1.112)")
    parser.add_argument("--upstream-port", type=int, required=True, help="Real Moonraker port (e.g. 7125)")
    parser.add_argument(
        "--drop-every", type=float, default=None,
        help="Seconds between automatic drops of the current client connection (omit for SIGUSR1-only)",
    )
    parser.add_argument("--pid-file", default=None, help="Write this process's PID here for `kill -USR1`")
    args = parser.parse_args()

    if args.pid_file:
        with open(args.pid_file, "w") as f:
            f.write(str(__import__("os").getpid()))

    proxy = ChaosProxy(args.listen_host, args.listen_port, args.upstream_host, args.upstream_port)

    def on_sigusr1(signum, frame):
        proxy.request_drop()

    signal.signal(signal.SIGUSR1, on_sigusr1)

    if args.drop_every:
        def timer_loop():
            while True:
                time.sleep(args.drop_every)
                proxy.request_drop()

        threading.Thread(target=timer_loop, daemon=True).start()
        proxy._log(f"auto-drop every {args.drop_every}s (also responds to SIGUSR1, pid={__import__('os').getpid()})")
    else:
        proxy._log(f"drop on SIGUSR1 only (pid={__import__('os').getpid()})")

    proxy.serve_forever()


if __name__ == "__main__":
    sys.exit(main())
