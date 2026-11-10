import socket
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import gdb_stub  # noqa: E402


def test_a_packet_carries_its_checksum():
    assert gdb_stub.frame(b"m100,4") == b"$m100,4#5e"


def test_parse_skips_acknowledgements_and_waits_for_a_whole_packet():
    assert gdb_stub.parse_frame(b"+$OK#9") == (None, b"$OK#9")
    assert gdb_stub.parse_frame(b"+$OK#9a$more") == (b"OK", b"$more")
    assert gdb_stub.parse_frame(b"+") == (None, b"")


def test_read_asks_for_memory_and_decodes_the_reply():
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    asked = []

    def serve():
        connection, _ = server.accept()
        asked.append(connection.recv(100))
        connection.sendall(b"+" + gdb_stub.frame(b"01020304"))
        connection.recv(1)
        connection.close()

    thread = threading.Thread(target=serve)
    thread.start()
    stub = gdb_stub.GdbStub(port=server.getsockname()[1])
    assert stub.read(0x100, 4) == b"\x01\x02\x03\x04"
    stub.close()
    thread.join()
    server.close()
    assert asked == [gdb_stub.frame(b"m100,4")]


def serve_one(replies):
    """A stub that answers each packet it gets with the next reply and keeps what it was asked."""
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    asked = []

    def serve():
        connection, _ = server.accept()
        for reply in replies:
            asked.append(connection.recv(100))
            connection.sendall(b"+" + gdb_stub.frame(reply))
            connection.recv(1)
        connection.close()

    thread = threading.Thread(target=serve)
    thread.start()
    return server, thread, asked


def test_a_breakpoint_is_set_and_removed_by_address():
    server, thread, asked = serve_one([b"OK", b"OK"])
    stub = gdb_stub.GdbStub(port=server.getsockname()[1])
    stub.set_breakpoint(0x201F77)
    stub.remove_breakpoint(0x201F77)
    stub.close()
    thread.join()
    server.close()
    assert asked == [gdb_stub.frame(b"Z0,201f77,1"), gdb_stub.frame(b"z0,201f77,1")]


def test_running_to_a_breakpoint_returns_when_the_guest_stops():
    server, thread, asked = serve_one([b"T05thread:01;"])
    stub = gdb_stub.GdbStub(port=server.getsockname()[1])
    assert stub.run_until_stop() == b"T05thread:01;"
    stub.close()
    thread.join()
    server.close()
    assert asked == [gdb_stub.frame(b"c")]


def test_registers_come_in_gdb_order_little_endian():
    values = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x201F77]
    reply = b"".join(value.to_bytes(4, "little").hex().encode() for value in values) + b"00" * 8
    server, thread, _ = serve_one([reply])
    stub = gdb_stub.GdbStub(port=server.getsockname()[1])
    registers = stub.registers()
    stub.close()
    thread.join()
    server.close()
    assert registers["eax"] == 0x11
    assert registers["ebp"] == 0x66
    assert registers["eip"] == 0x201F77
