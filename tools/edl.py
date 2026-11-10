"""Decompresses Eurocom "EuroPack" EDL files of type 1: Huffman-coded LZ77 blocks and stored blocks.

Layout: "EDL", a kind byte (low bits 1 for this type, 0x80 for big-endian), then the file's compressed and decompressed
sizes as 32-bit integers. The bit stream follows at offset 12 as 32-bit words in the file's byte order, read from each
word's lowest bit. Each block starts with a bit that says Huffman (1) or stored (0), and ends with a bit that is 1 on
the last block. Length and distance symbols use the same tables as deflate."""
from __future__ import annotations

import struct

MAGIC = b"EDL"
HEADER_SIZE = 12
BIG_ENDIAN_FLAG = 0x80
HUFFMAN_TYPE = 1
MAX_CODE_LENGTH = 15
END_OF_BLOCK = 0x100
FIRST_LENGTH_SYMBOL = 0x101
MIN_MATCH = 3

# (base value, extra bits) for each length and distance symbol
LENGTHS = ([(value, 0) for value in range(8)]
           + [((4 + index) << bits, bits) for bits in range(1, 6) for index in range(4)]
           + [(0xFF, 0)])
DISTANCES = ([(value, 0) for value in range(4)]
             + [(base << bits, bits) for bits in range(1, 14) for base in (2, 3)])


class BitReader:
    def __init__(self, stream: bytes, word_format: str):
        # a short last word is padded, which leaves its bytes first in the stream of a little-endian file
        padded = stream + bytes(-len(stream) % 4)
        self.words = (word for word, in struct.iter_unpack(word_format, padded))
        self.bits = 0
        self.count = 0

    def read(self, count: int) -> int:
        while self.count < count:
            word = next(self.words, None)
            if word is None:
                raise ValueError("EDL stream is truncated")
            self.bits |= word << self.count
            self.count += 32
        value = self.bits & ((1 << count) - 1)
        self.bits >>= count
        self.count -= count
        return value


def read_code_lengths(reader: BitReader, count: int, length: int) -> tuple[list[int], int]:
    """A set bit brings a new 4-bit length; otherwise the symbol has the same length as the one before it."""
    lengths = []
    for _ in range(count):
        if reader.read(1):
            length = reader.read(4)
        lengths.append(length)
    return lengths, length


def build_decoder(lengths: list[int]) -> dict[int, int]:
    """Maps a code, with a set bit in front of it to keep its leading zeros, to its symbol."""
    decoder = {}
    code = 0
    previous = 0
    for length, symbol in sorted((length, symbol) for symbol, length in enumerate(lengths) if length):
        code <<= length - previous
        previous = length
        decoder[(1 << length) | code] = symbol
        code += 1
    return decoder


def read_symbol(reader: BitReader, decoder: dict[int, int]) -> int:
    code = 1
    for _ in range(MAX_CODE_LENGTH):
        code = (code << 1) | reader.read(1)
        symbol = decoder.get(code)
        if symbol is not None:
            return symbol
    raise ValueError("EDL stream has an invalid Huffman code")


def read_match_value(reader: BitReader, table: list[tuple[int, int]], index: int, name: str) -> int:
    if not 0 <= index < len(table):
        raise ValueError(f"EDL stream has an unknown {name} symbol")
    base, extra_bits = table[index]
    return base + reader.read(extra_bits)


def decompress(data: bytes) -> bytes:
    if len(data) < HEADER_SIZE or data[:len(MAGIC)] != MAGIC:
        raise ValueError("not an EDL file")
    kind = data[len(MAGIC)]
    if kind & ~BIG_ENDIAN_FLAG != HUFFMAN_TYPE:
        raise ValueError(f"unsupported EDL type: {kind:#x}")
    big_endian = kind & BIG_ENDIAN_FLAG
    compressed_size, size = struct.unpack_from(">II" if big_endian else "<II", data, len(MAGIC) + 1)
    if not HEADER_SIZE <= compressed_size <= len(data):
        raise ValueError(f"EDL compressed size is out of range: {compressed_size:#x}")

    reader = BitReader(data[HEADER_SIZE:compressed_size], ">I" if big_endian else "<I")
    output = bytearray()
    literals: dict[int, int] = {}
    distances: dict[int, int] = {}
    code_length = 0  # kept across tables and blocks

    while True:
        if reader.read(1):
            count = reader.read(9)
            if count:
                lengths, code_length = read_code_lengths(reader, count, code_length)
                literals = build_decoder(lengths)
            count = reader.read(9)
            if count:
                lengths, code_length = read_code_lengths(reader, count, code_length)
                distances = build_decoder(lengths)
            while (symbol := read_symbol(reader, literals)) != END_OF_BLOCK:
                if symbol < END_OF_BLOCK:
                    output.append(symbol)
                else:
                    length = MIN_MATCH + read_match_value(reader, LENGTHS, symbol - FIRST_LENGTH_SYMBOL, "length")
                    distance = 1 + read_match_value(reader, DISTANCES, read_symbol(reader, distances), "distance")
                    if distance > len(output):
                        raise ValueError("EDL match reaches before the start of the output")
                    for _ in range(length):
                        output.append(output[-distance])
                if len(output) > size:
                    raise ValueError("EDL output is past its declared length")
        else:
            for _ in range(reader.read(15)):
                output.append(reader.read(8))
            if len(output) > size:
                raise ValueError("EDL output is past its declared length")
        if reader.read(1):
            break

    if len(output) != size:
        raise ValueError(f"EDL output length is {len(output):#x}, expected {size:#x}")
    return bytes(output)
