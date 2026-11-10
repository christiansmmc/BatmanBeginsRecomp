import struct

import find_code_pointers as fcp

CODE_VA = 0x11000


def build_code() -> bytes:
    # 0x00 ret, 0x10 function A (known), 0x20 function B (unknown), 0x30 mid-instruction bytes of a function
    code = bytearray(b"\xCC" * 0x40)
    code[0x0F] = 0xC3
    code[0x10:0x13] = b"\x55\x8B\xEC"
    code[0x1F] = 0xCC
    code[0x20:0x23] = b"\x55\x8B\xEC"
    code[0x2F] = 0x8B  # not the end of a function
    code[0x30:0x33] = b"\x55\x8B\xEC"
    return bytes(code)


def data_with(*values) -> bytes:
    return b"".join(struct.pack("<I", value) for value in values)


def test_finds_unknown_function_pointer():
    data = data_with(CODE_VA + 0x20)
    assert fcp.missed_code_pointers(data, build_code(), CODE_VA, set()) == [CODE_VA + 0x20]


def test_ignores_known_function():
    data = data_with(CODE_VA + 0x10)
    assert fcp.missed_code_pointers(data, build_code(), CODE_VA, {CODE_VA + 0x10}) == []


def test_ignores_pointer_that_does_not_follow_a_function_end():
    assert fcp.missed_code_pointers(data_with(CODE_VA + 0x30), build_code(), CODE_VA, set()) == []


def test_ignores_values_outside_code():
    assert fcp.missed_code_pointers(data_with(0x10, CODE_VA + 0x100, 0xFFFFFFFF), build_code(), CODE_VA, set()) == []


def test_reports_each_address_once_sorted():
    data = data_with(CODE_VA + 0x20, CODE_VA + 0x20)
    assert fcp.missed_code_pointers(data, build_code(), CODE_VA, set()) == [CODE_VA + 0x20]


def test_known_functions_are_read_from_generated_mapping():
    source = '{ 0x11000, sub_00011000 },\n{ 0x000B0AE0, sub_000B0AE0 },\n'
    assert fcp.known_functions(source) == {0x11000, 0xB0AE0}


def test_finds_function_after_padding_whatever_its_first_byte():
    # the real miss: 0xB0AE0 starts with "mov al, [addr]" (A0) right after int3 padding
    code = bytearray(b"\xCC" * 0x20)
    code[0x10:0x15] = b"\xA0\x0A\xF3\x3E\x00"
    assert fcp.missed_code_pointers(data_with(CODE_VA + 0x10), bytes(code), CODE_VA, set()) == [CODE_VA + 0x10]


def test_after_a_ret_an_unusual_first_byte_is_not_enough():
    code = bytearray(b"\xCC" * 0x20)
    code[0x0F] = 0xC3
    code[0x10:0x12] = b"\xA0\x0A"
    assert fcp.missed_code_pointers(data_with(CODE_VA + 0x10), bytes(code), CODE_VA, set()) == []
