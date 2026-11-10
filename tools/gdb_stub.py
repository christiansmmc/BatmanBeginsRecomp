"""Development aid: a minimal client of xemu's gdb stub (xemu -s, port 1234), to read the guest's memory.

The guest stops while a read is served. run_game.py --exe xemu --arg=-s starts xemu with the stub.
"""
from __future__ import annotations

import socket


def frame(payload: bytes) -> bytes:
    return b"$" + payload + b"#" + f"{sum(payload) % 256:02x}".encode()


def parse_frame(buffer: bytes) -> tuple[bytes | None, bytes]:
    """The first whole packet's payload and what follows it; None until one has arrived. Acknowledgements are skipped."""
    start = buffer.find(b"$")
    if start < 0:
        return None, b""
    end = buffer.find(b"#", start)
    if end < 0 or len(buffer) < end + 3:
        return None, buffer[start:]
    return buffer[start + 1:end], buffer[end + 3:]


class GdbStub:
    def __init__(self, host: str = "127.0.0.1", port: int = 1234, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.pending = b""

    def _receive(self) -> bytes:
        while True:
            payload, rest = parse_frame(self.pending)
            if payload is not None:
                self.pending = rest
                self.sock.sendall(b"+")
                return payload
            self.pending = rest + self.sock.recv(65536)

    def command(self, payload: bytes) -> bytes:
        self.sock.sendall(frame(payload))
        return self._receive()

    def stop(self) -> bytes:
        """Stops the guest."""
        self.sock.sendall(b"\x03")
        return self._receive()

    def resume(self) -> None:
        self.sock.sendall(frame(b"c"))

    def set_breakpoint(self, address: int) -> None:
        self.command(f"Z0,{address:x},1".encode())

    def remove_breakpoint(self, address: int) -> None:
        self.command(f"z0,{address:x},1".encode())

    def run_until_stop(self) -> bytes:
        """Resumes the guest and waits for it to stop at a breakpoint, however long that takes."""
        self.sock.settimeout(None)
        return self.command(b"c")

    def registers(self) -> dict[str, int]:
        """The guest must be stopped."""
        reply = bytes.fromhex(self.command(b"g").decode())
        names = "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "eip"
        return {name: int.from_bytes(reply[index * 4:index * 4 + 4], "little") for index, name in enumerate(names)}

    def read(self, address: int, size: int) -> bytes:
        """The guest must be stopped."""
        reply = self.command(f"m{address:x},{size:x}".encode())
        if reply.startswith(b"E"):
            raise OSError(f"cannot read {size} bytes at {address:#x}: {reply.decode()}")
        return bytes.fromhex(reply.decode())

    def close(self) -> None:
        self.sock.close()
