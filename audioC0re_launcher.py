#!/usr/bin/env python3
"""audioC0re_launcher.py — sends payload, shellcode, and library to AudioC0re.
"""

import argparse, datetime, os, platform, re, select, socket, struct, sys, threading, time
from pathlib import Path

DEFAULT_CONSOLE_IP       = "" # Fill with your PS4/PS5 IP
DEFAULT_LAUNCHER         = "audioC0re.lua"
DEFAULT_SHELLCODE        = "audioC0re.bin"
DEFAULT_LIBRARY          = "audioC0re-library"
DEFAULT_LIBRARY_FALLBACK = "library"

PAYLOAD_PORT       = 9026
LOG_PORT           = 9027
SC_PORT_LO         = 5001
SC_PORT_HI         = 5021
LIB_PORT_LO        = 5100
LIB_PORT_HI        = 5121
CHUNK              = 64 * 1024

IS_WINDOWS = os.name == "nt"
OS_NAME    = platform.system() or "Unknown"

# Global event to signal a quick shutdown across threads
shutdown_event = threading.Event()


def graceful_exit(msg="[*] Launcher stopped cleanly."):
    shutdown_event.set()
    print(f"\n{msg}")


def interruptible_sleep(seconds):
    """Sleeps for given seconds, but returns immediately if shutdown is requested."""
    end_time = time.time() + seconds
    while time.time() < end_time:
        if shutdown_event.is_set():
            return False
        time.sleep(0.1)
    return True


def connect_with_cancel(host, port, timeout=3.0):
    """Attempt non-blocking socket connection to allow immediate thread cancellation."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    except OSError:
        pass
    s.setblocking(False)

    err = s.connect_ex((host, port))
    if err == 0:
        s.setblocking(True)
        return s

    deadline = time.time() + timeout
    while time.time() < deadline and not shutdown_event.is_set():
        _, writable, _ = select.select([], [s], [], 0.2)
        if writable:
            sock_err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
            if sock_err == 0:
                s.setblocking(True)
                return s
            else:
                s.close()
                return None

    s.close()
    return None


def find_file(name, subdirs=("payloads", "lua", ".", "audioC0re", "bundle", os.path.join("bundle", "audioC0re"), os.path.join("bundle", "library"))):
    if os.path.isfile(name): return name
    for s in subdirs:
        c = os.path.join(s, os.path.basename(name))
        if os.path.isfile(c): return c
    return None


def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        try: return socket.gethostbyname(socket.gethostname())
        except OSError: return "127.0.0.1"
    finally: s.close()


def str_to_bool(v):
    if isinstance(v, bool): return v
    return str(v).strip().lower() in ("1","true","yes","on","y","t")


class LogServer(threading.Thread):
    """UDP debug-log listener + device-signal parser."""
    def __init__(self, port, verbose=True, max_wait_s=35.0):
        super().__init__(daemon=True)
        self.port = port
        self.verbose = verbose
        self.max_wait_s = max_wait_s
        self.bound_event = threading.Event()
        self.sock = None
        self.library_ready = threading.Event()
        self.lib_port = None
        self.lib_port_event = threading.Event()

    def run(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try: s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        except OSError: pass
        if hasattr(socket, "SO_REUSEPORT"):
            try: s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except OSError: pass

        deadline = time.time() + self.max_wait_s
        attempt = 0
        bound = False
        while time.time() < deadline and not shutdown_event.is_set():
            attempt += 1
            try:
                s.bind(("0.0.0.0", self.port))
                bound = True
                break
            except OSError as e:
                if self.verbose and (attempt == 1 or attempt % 10 == 0):
                    print(f"[log] bind UDP {self.port} attempt {attempt} failed: {e}")
                time.sleep(0.5)

        if not bound:
            if not shutdown_event.is_set():
                print(f"[log] FATAL: could not bind UDP {self.port} within {self.max_wait_s:.0f}s.")
            try: s.close()
            except OSError: pass
            self.bound_event.set()
            return

        s.settimeout(0.5)
        self.sock = s
        self.bound_event.set()
        if self.verbose:
            print(f"[log] UDP listening on 0.0.0.0:{self.port}", flush=True)

        while not shutdown_event.is_set():
            try:
                data, addr = s.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break

            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            msg = data.decode("utf-8", "replace").rstrip()
            msg = msg.encode("ascii", "replace").decode("ascii")
            if self.verbose:
                print(f"[{ts}] {addr[0]}  {msg}", flush=True)

            # Updated regex to capture LIBPORT regardless of string formatting variations
            m = re.search(r"LIBPORT[^\d]*(\d+)", msg, re.IGNORECASE)
            if m:
                try:
                    self.lib_port = int(m.group(1))
                    self.lib_port_event.set()
                except ValueError:
                    pass

            if "library listener fd" in msg or "blocking on accept" in msg:
                self.library_ready.set()

            # Auto-shutdown: the Lua payload prints this line only after
            # func_wrap(rx)(...) has returned — i.e. the shellcode has
            # exited and there is nothing left for us to monitor.
            if "returned from shellcode" in msg:
                if self.verbose:
                    print("[log] shellcode returned .. shutting down launcher",
                          flush=True)
                shutdown_event.set()

        try: s.close()
        except OSError: pass

    def stop(self):
        # Signal the run loop so it exits promptly, then close the socket.
        shutdown_event.set()
        if self.sock:
            try: self.sock.close()
            except OSError: pass


def send_lua(host, path, pc_ip=None, retries=5, conn_timeout=3.0):
    p = find_file(path)
    if not p:
        print(f"[!] missing {path}")
        return False
    try:
        with open(p, "r", encoding="utf-8") as f: text = f.read()
    except UnicodeDecodeError:
        with open(p, "r") as f: text = f.read()

    repl = pc_ip if pc_ip else ""
    decl = 'local PC_IP        = "__PC_IP__"'
    newd = 'local PC_IP        = "' + repl + '"'
    if decl in text:
        text = text.replace(decl, newd, 1)
    else:
        pat = r'(local\s+PC_IP\s*=\s*)"__PC_IP__"'
        text, n = re.subn(pat, r'\g<1>"' + repl + '"', text, count=1)

    data = text.encode("utf-8")
    print(f"[1] Sending {os.path.basename(p)} ({len(data):,} B)")

    for i in range(1, retries + 1):
        if shutdown_event.is_set(): return False
        s = connect_with_cancel(host, PAYLOAD_PORT, timeout=conn_timeout)
        if s:
            try:
                s.sendall(data)
                s.close()
                return True
            except Exception as e:
                try: s.close()
                except OSError: pass
                if i < retries:
                    print(f"[1] send error retry {i}/{retries}: {e}")
        else:
            if shutdown_event.is_set(): return False
            if i < retries:
                print(f"[1] connection timed out retry {i}/{retries}")

        if not interruptible_sleep(1.0): return False
    return False


def _send_sc_once(host, port, data, size):
    s = connect_with_cancel(host, port, timeout=0.5)
    if not s:
        return False

    print(f"[sc] connected {host}:{port}, sending {size:,} bytes")
    sent = 0; t0 = time.time()
    try:
        while sent < size and not shutdown_event.is_set():
            chunk = data[sent:sent+CHUNK]
            s.sendall(chunk); sent += len(chunk)
            pct = sent * 100 // size
            print(f"\r[sc] {sent:,}/{size:,} ({pct}%)", end="", flush=True)
        print()
        if shutdown_event.is_set():
            s.close()
            return False
        dt = max(time.time() - t0, 1e-6)
        print(f"[sc] done in {dt:.1f}s ({sent/dt/1024:.0f} KB/s)")
        s.close(); return True
    except OSError as e:
        print(f"\n[!] send broke: {e}")
        try: s.close()
        except OSError: pass
        return False


def stream_shellcode(host, path, timeout=25, retries=3):
    sc_path = find_file(path)
    if not sc_path:
        print(f"[!] missing {path}"); return False
    print(f"[sc] using shellcode: {sc_path}")
    with open(sc_path, "rb") as f: data = f.read()
    size = len(data)
    for attempt in range(1, retries + 1):
        if shutdown_event.is_set(): return False
        if attempt > 1:
            print(f"[sc] retry {attempt}/{retries}...")
            if not interruptible_sleep(1.5): return False
        print(f"[sc] scanning {SC_PORT_LO}..{SC_PORT_HI-1}")
        deadline = time.time() + timeout
        while time.time() < deadline and not shutdown_event.is_set():
            for port in range(SC_PORT_LO, SC_PORT_HI):
                if shutdown_event.is_set(): return False
                if _send_sc_once(host, port, data, size): return True
            time.sleep(0.3)
    return False


def find_library_dir(name, fallback_name=None):
    if not name: return None

    search_roots = [".", "audioC0re", "bundle", os.path.join("bundle", "audioC0re"), os.path.join("bundle", "library")]
    target_names = [name]
    if fallback_name and fallback_name != name:
        target_names.append(fallback_name)

    for root in search_roots:
        for target in target_names:
            candidates = [
                target, target.lower(), target.upper(),
                target.replace("-", "_"), target.replace("_", "-")
            ]
            for c in candidates:
                path = os.path.join(root, c)
                if os.path.isdir(path):
                    return path

            path = os.path.join(root, target)
            if os.path.isdir(path):
                for entry in os.listdir(path):
                    sub = os.path.join(path, entry)
                    if os.path.isdir(sub) and os.path.isfile(os.path.join(sub, "manifest.bin")):
                        return sub
    return None


def send_library(host, lib_dir, specific_port=None, scan_timeout=20, fallback_dir=DEFAULT_LIBRARY_FALLBACK):
    resolved_dir = find_library_dir(lib_dir, fallback_name=fallback_dir)
    if resolved_dir is None:
        print(f"[lib] folder '{lib_dir}' (and fallback '{fallback_dir}') not found — skipping")
        return False

    lib_path = Path(resolved_dir)
    if not (lib_path / "manifest.bin").exists():
        print(f"[lib] WARNING: {lib_path}/manifest.bin not found — sending anyway")
    else:
        print(f"[lib] using folder: {lib_path}")

    files = []
    for f in sorted(lib_path.rglob("*")):
        if f.is_file():
            rel = f.relative_to(lib_path).as_posix()
            files.append((f, rel))
    if not files:
        print("[lib] no files to send")
        return False

    s = None
    if specific_port is not None:
        print(f"[lib] connecting to device-reported port {specific_port}")
        s = connect_with_cancel(host, specific_port, timeout=2.0)
        if not s:
            print(f"[lib] connect to {specific_port} failed — falling back to scan")

    if s is None:
        print(f"[lib] scanning {LIB_PORT_HI-1}..{LIB_PORT_LO} (high->low, newest first)")
        deadline = time.time() + scan_timeout
        while time.time() < deadline and s is None and not shutdown_event.is_set():
            for port in range(LIB_PORT_HI - 1, LIB_PORT_LO - 1, -1):
                if shutdown_event.is_set(): return False
                s = connect_with_cancel(host, port, timeout=0.4)
                if s:
                    print(f"[lib] connected to {host}:{port}")
                    break
            if s is None:
                time.sleep(0.3)

        if s is None or shutdown_event.is_set():
            if not shutdown_event.is_set():
                print("[lib] no listener found — is the shellcode running?")
            return False

    print(f"[lib] sending {len(files)} files")
    total_bytes = sum(p.stat().st_size for p, _ in files)
    sent_bytes  = 0
    t0 = time.time()
    try:
        for p, rel in files:
            if shutdown_event.is_set():
                s.close()
                return False
            data = p.read_bytes()
            name_b = rel.encode("utf-8")[:200]
            hdr = struct.pack("<QH", len(data), len(name_b)) + name_b
            s.sendall(hdr)
            s.sendall(data)
            sent_bytes += len(data)
            pct = sent_bytes * 100 // total_bytes
            print(f"\r[lib] {sent_bytes:,}/{total_bytes:,} ({pct}%) {rel[:40]}", end="", flush=True)
        s.sendall(struct.pack("<QH", 0, 0))
        print()
        dt = max(time.time() - t0, 1e-6)
        print(f"[lib] done in {dt:.1f}s ({sent_bytes/dt/1024:.0f} KB/s)")
        s.close()
        return True
    except OSError as e:
        print(f"\n[!] library send broke: {e}")
        try: s.close()
        except OSError: pass
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", default=DEFAULT_CONSOLE_IP)
    ap.add_argument("--launcher",  "-l", default=DEFAULT_LAUNCHER)
    ap.add_argument("--shellcode", "-s", default=DEFAULT_SHELLCODE)
    ap.add_argument("--library",   "-L", default=DEFAULT_LIBRARY)
    ap.add_argument("--library-fallback", "-L2", default=DEFAULT_LIBRARY_FALLBACK)
    ap.add_argument("--debug-logs", type=str_to_bool, default=True, metavar="true|false")
    ap.add_argument("--local-ip",  default=None)
    ap.add_argument("--scport",    type=int, default=None)
    ap.add_argument("--no-library", action="store_true")
    ap.add_argument("--ready-timeout", type=int, default=5)
    ap.add_argument("--payload-timeout", type=float, default=3.0)
    a = ap.parse_args()

    if not a.host:
        print("Provide the console IP: audioC0re_launcher.py <PS5_IP>")
        return 1

    print("=" * 60)
    print(" AudioC0re launcher")
    print(f" Host OS: {OS_NAME}")
    print(f" Debug logs: {'ENABLED' if a.debug_logs else 'DISABLED'}")
    print(f" Library:   {a.library} (fallback: {a.library_fallback}){' (skipped)' if a.no_library else ''}")
    print("=" * 60)

    lua = find_file(a.launcher)
    sc  = find_file(a.shellcode)
    if not lua: print(f"[!] {a.launcher} not found"); return 1
    if not sc:  print(f"[!] {a.shellcode} not found"); return 1

    pc_ip = a.local_ip or get_local_ip()
    if pc_ip == "127.0.0.1":
        print("[*] WARN: no LAN route detected — pass --local-ip")

    print(f"[*] console: {a.host}")
    print(f"[*] PC IP:   {pc_ip}")

    log = LogServer(LOG_PORT, verbose=a.debug_logs)
    log.start()

    try:
        if not log.bound_event.wait(timeout=log.max_wait_s + 5):
            print("[!] LogServer never reported bind status — aborting")
            return 1
        if not log.sock:
            print("[!] LogServer failed to bind.")
            return 1

        if not send_lua(a.host, lua, pc_ip=pc_ip, conn_timeout=a.payload_timeout):
            return 1
        interruptible_sleep(1.0)

        ok = False
        if a.scport is not None:
            with open(sc, "rb") as f: data = f.read()
            ok = _send_sc_once(a.host, a.scport, data, len(data))
        else:
            ok = stream_shellcode(a.host, sc)
        if not ok or shutdown_event.is_set():
            if not shutdown_event.is_set():
                print("[!] shellcode send failed")
            return 1

        if not a.no_library and not shutdown_event.is_set():
            print(f"[lib] waiting for device LIBPORT (up to {a.ready_timeout}s)")
            # Poll log.lib_port directly rather than waiting on the
            # event.  The LIBPORT UDP message can arrive before the
            # LogServer thread has fully scheduled itself, which causes
            # the event-based wait to miss it and time out.
            port_from_log = None
            deadline = time.time() + a.ready_timeout
            while time.time() < deadline and not shutdown_event.is_set():
                if log.lib_port is not None:
                    port_from_log = log.lib_port
                    break
                time.sleep(0.05)
            if port_from_log is not None:
                print(f"[lib] device LIBPORT = {port_from_log}")
                interruptible_sleep(0.3)
            else:
                print(f"[lib] no LIBPORT in log — falling back to high->low scan")

            send_library(a.host, a.library, specific_port=port_from_log, fallback_dir=a.library_fallback)

        if not shutdown_event.is_set():
            print("\n[*] Watching logs. Stop anytime.")
            while log.is_alive() and not shutdown_event.is_set():
                time.sleep(0.5)

    except KeyboardInterrupt:
        graceful_exit("[*] Stopped by user.")
    finally:
        log.stop()

    print("[*] Stopped gracefully.")
    return 0


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[*] Stopped by user.")
