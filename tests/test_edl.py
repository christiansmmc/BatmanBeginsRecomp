"""All streams here are built by a small encoder below, so the tests hold no game data."""
import struct

import pytest

import edl

LITERALS_END = 0x100
FIRST_LENGTH_SYMBOL = 0x101


class BitWriter:
    def __init__(self):
        self.bits = []
        self.table_length = 0  # a code length carries over from the previous symbol, across tables and blocks

    def write(self, value, count):
        self.bits.extend((value >> index) & 1 for index in range(count))

    def write_table(self, lengths):
        self.write(len(lengths), 9)
        for length in lengths:
            if length == self.table_length:
                self.write(0, 1)
            else:
                self.write(1, 1)
                self.write(length, 4)
                self.table_length = length

    def write_code(self, codes, symbol):
        code, length = codes[symbol]
        for index in reversed(range(length)):
            self.write((code >> index) & 1, 1)

    def to_words(self, word_format):
        bits = self.bits + [0] * (-len(self.bits) % 32)
        words = (sum(bit << index for index, bit in enumerate(bits[start:start + 32]))
                 for start in range(0, len(bits), 32))
        return b"".join(struct.pack(word_format, word) for word in words)


def canonical_codes(lengths):
    """symbol -> (code, length), shortest codes first, then by symbol"""
    codes = {}
    code = 0
    previous = 0
    for length, symbol in sorted((length, symbol) for symbol, length in enumerate(lengths) if length):
        code <<= length - previous
        previous = length
        codes[symbol] = (code, length)
        code += 1
    return codes


def fixed_lengths(symbols, length):
    lengths = [0] * (max(symbols) + 1)
    for symbol in symbols:
        lengths[symbol] = length
    return lengths


def container(writer, size, big_endian=True):
    word_format, size_format, kind = (">I", ">II", 0x81) if big_endian else ("<I", "<II", 0x01)
    stream = writer.to_words(word_format)
    return b"EDL" + bytes([kind]) + struct.pack(size_format, 12 + len(stream), size) + stream


def stored_block(writer, payload, last=True):
    writer.write(0, 1)
    writer.write(len(payload), 15)
    for byte in payload:
        writer.write(byte, 8)
    writer.write(int(last), 1)


def huffman_tables(writer, literal_lengths, distance_lengths):
    writer.write(1, 1)
    writer.write_table(literal_lengths)
    writer.write_table(distance_lengths)
    return canonical_codes(literal_lengths), canonical_codes(distance_lengths)


def test_stored_block():
    writer = BitWriter()
    stored_block(writer, b"hello")
    assert edl.decompress(container(writer, 5)) == b"hello"


def test_little_endian_stream():
    writer = BitWriter()
    stored_block(writer, b"hello")
    assert edl.decompress(container(writer, 5, big_endian=False)) == b"hello"


def test_huffman_literals():
    writer = BitWriter()
    literals, _ = huffman_tables(writer, fixed_lengths([ord("a"), ord("b"), LITERALS_END], 2), [1])
    for symbol in (ord("a"), ord("b"), ord("a"), LITERALS_END):
        writer.write_code(literals, symbol)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 3)) == b"aba"


def test_codes_of_mixed_lengths_are_canonical():
    # longer codes are listed first by symbol, so a decoder that orders by symbol alone goes wrong
    lengths = [0] * (LITERALS_END + 1)
    lengths[ord("a")] = 3
    lengths[ord("b")] = 3
    lengths[ord("c")] = 2
    lengths[LITERALS_END] = 1
    writer = BitWriter()
    literals, _ = huffman_tables(writer, lengths, [1])
    for symbol in (ord("a"), ord("b"), ord("c"), LITERALS_END):
        writer.write_code(literals, symbol)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 3)) == b"abc"


def test_overlapping_match_without_extra_bits():
    # length symbol 0 means length 3; distance symbol 0 means distance 1
    length_symbol = FIRST_LENGTH_SYMBOL
    writer = BitWriter()
    literals, distances = huffman_tables(writer, fixed_lengths([ord("x"), LITERALS_END, length_symbol], 2), [1])
    writer.write_code(literals, ord("x"))
    writer.write_code(literals, length_symbol)
    writer.write_code(distances, 0)
    writer.write_code(literals, LITERALS_END)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 4)) == b"xxxx"


def test_match_with_extra_bits():
    # length symbol 9 has base 0xA + 3 and 1 extra bit; distance symbol 5 has base 6 + 1 and 1 extra bit
    length_symbol = FIRST_LENGTH_SYMBOL + 9
    distance_symbol = 5
    payload = b"abcdefghi"
    symbols = list(payload) + [LITERALS_END, length_symbol]
    writer = BitWriter()
    literals, distances = huffman_tables(writer, fixed_lengths(symbols, 4),
                                         fixed_lengths([distance_symbol], 1))
    for byte in payload:
        writer.write_code(literals, byte)
    writer.write_code(literals, length_symbol)
    writer.write(1, 1)  # length 0xA + 1 + 3 = 14
    writer.write_code(distances, distance_symbol)
    writer.write(1, 1)  # distance 6 + 1 + 1 = 8
    writer.write_code(literals, LITERALS_END)
    writer.write(1, 1)
    expected = payload + (b"bcdefghi" * 2)[:14]
    assert edl.decompress(container(writer, len(expected))) == expected


def test_longest_match_symbol_has_no_extra_bits():
    # length symbol 28 means length 0xFF + 3
    length_symbol = FIRST_LENGTH_SYMBOL + 28
    writer = BitWriter()
    literals, distances = huffman_tables(writer, fixed_lengths([ord("z"), LITERALS_END, length_symbol], 2), [1])
    writer.write_code(literals, ord("z"))
    writer.write_code(literals, length_symbol)
    writer.write_code(distances, 0)
    writer.write_code(literals, LITERALS_END)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 259)) == b"z" * 259


def test_empty_table_reuses_the_previous_one():
    writer = BitWriter()
    literals, _ = huffman_tables(writer, fixed_lengths([ord("a"), LITERALS_END], 1), [1])
    writer.write_code(literals, ord("a"))
    writer.write_code(literals, LITERALS_END)
    writer.write(0, 1)
    writer.write(1, 1)
    writer.write(0, 9)
    writer.write(0, 9)
    writer.write_code(literals, ord("a"))
    writer.write_code(literals, LITERALS_END)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 2)) == b"aa"


def test_code_length_carries_over_to_the_next_table():
    # the distance table's only entry has the literal table's last length, so it is written with no new length
    writer = BitWriter()
    literal_lengths = fixed_lengths([ord("x"), LITERALS_END, FIRST_LENGTH_SYMBOL], 2)
    literals, distances = huffman_tables(writer, literal_lengths, [2])
    writer.write_code(literals, ord("x"))
    writer.write_code(literals, FIRST_LENGTH_SYMBOL)
    writer.write_code(distances, 0)
    writer.write_code(literals, LITERALS_END)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 4)) == b"xxxx"


def test_stored_and_huffman_blocks_share_history():
    writer = BitWriter()
    stored_block(writer, b"ab", last=False)
    literals, distances = huffman_tables(writer, fixed_lengths([LITERALS_END, FIRST_LENGTH_SYMBOL], 1), [0, 1])
    writer.write_code(literals, FIRST_LENGTH_SYMBOL)
    writer.write_code(distances, 1)  # distance 2
    writer.write_code(literals, LITERALS_END)
    writer.write(1, 1)
    assert edl.decompress(container(writer, 5)) == b"ababa"


@pytest.mark.parametrize("data", [b"", b"EDL", b"XDL\x81" + bytes(8)])
def test_rejects_a_bad_magic(data):
    with pytest.raises(ValueError, match="EDL"):
        edl.decompress(data)


@pytest.mark.parametrize("kind", [0x00, 0x02, 0x80, 0x82])
def test_rejects_other_edl_types(kind):
    data = b"EDL" + bytes([kind]) + struct.pack(">II", 16, 1) + bytes(4)
    with pytest.raises(ValueError, match="type"):
        edl.decompress(data)


def test_rejects_a_compressed_size_past_the_end():
    writer = BitWriter()
    stored_block(writer, b"hello")
    with pytest.raises(ValueError, match="compressed size"):
        edl.decompress(container(writer, 5)[:-4])


def test_rejects_output_of_the_wrong_length():
    writer = BitWriter()
    stored_block(writer, b"hello")
    for declared in (4, 6):
        with pytest.raises(ValueError, match="length"):
            edl.decompress(container(writer, declared))


def test_rejects_a_stream_with_no_end():
    writer = BitWriter()
    stored_block(writer, b"hello", last=False)
    with pytest.raises(ValueError, match="truncated"):
        edl.decompress(container(writer, 5))


def test_rejects_a_code_with_no_table():
    writer = BitWriter()
    writer.write(1, 1)
    writer.write(0, 9)
    writer.write(0, 9)
    writer.write(0, 64)
    with pytest.raises(ValueError, match="code"):
        edl.decompress(container(writer, 1))


def test_rejects_a_match_before_the_start():
    writer = BitWriter()
    literals, distances = huffman_tables(writer, fixed_lengths([LITERALS_END, FIRST_LENGTH_SYMBOL], 1), [1])
    writer.write_code(literals, FIRST_LENGTH_SYMBOL)
    writer.write_code(distances, 0)
    writer.write(0, 64)
    with pytest.raises(ValueError, match="before the start"):
        edl.decompress(container(writer, 3))


def test_rejects_an_unknown_length_symbol():
    bad_symbol = FIRST_LENGTH_SYMBOL + 29
    writer = BitWriter()
    literals, distances = huffman_tables(writer, fixed_lengths([LITERALS_END, bad_symbol], 1), [1])
    writer.write_code(literals, bad_symbol)
    writer.write(0, 64)
    with pytest.raises(ValueError, match="length symbol"):
        edl.decompress(container(writer, 3))


def test_rejects_an_unknown_distance_symbol():
    writer = BitWriter()
    literals, distances = huffman_tables(writer, fixed_lengths([LITERALS_END, FIRST_LENGTH_SYMBOL], 1),
                                         fixed_lengths([30], 1))
    writer.write_code(literals, FIRST_LENGTH_SYMBOL)
    writer.write_code(distances, 30)
    writer.write(0, 64)
    with pytest.raises(ValueError, match="distance symbol"):
        edl.decompress(container(writer, 3))
