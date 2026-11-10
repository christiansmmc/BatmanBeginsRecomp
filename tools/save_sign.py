"""Development aid: signs a Batman Begins save for another machine, and reads or sets its widescreen option.

The game signs the first 0x800 bytes of a save with XCalculateSignature and keeps the 20 bytes right after them (0x20C6D4,
0x20CDA7). The signature is HMAC-SHA1 under the kernel's XboxSignatureKey, which comes from the title, not from the console
(flag 0, roamable). The SDK leaves that key at zero, so a save from the port is refused elsewhere until it is signed again.

The save also keeps the game's options as pairs of 16-bit code and value (0xF73D0). The code 0x3E is the widescreen option,
and it wins over what the console says when the save loads (docs/TECHNICAL.md, "Aspect ratio").

  save_sign.py <save> <key in hex> <output> [--widescreen 0|1]
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import struct
from pathlib import Path

SIGNED_BYTES = 0x800
SIGNATURE_BYTES = hashlib.sha1().digest_size
WIDESCREEN_CODE = 0x3E


def _signature(save: bytes, key: bytes) -> bytes:
    if len(save) < SIGNED_BYTES + SIGNATURE_BYTES:
        raise ValueError(f"{len(save)} bytes is too short to be a save")
    return hmac.new(key, save[:SIGNED_BYTES], hashlib.sha1).digest()


def is_signed_with(save: bytes, key: bytes) -> bool:
    return save[SIGNED_BYTES:SIGNED_BYTES + SIGNATURE_BYTES] == _signature(save, key)


def resign(save: bytes, key: bytes) -> bytes:
    return save[:SIGNED_BYTES] + _signature(save, key) + save[SIGNED_BYTES + SIGNATURE_BYTES:]


def _widescreen_offset(save: bytes) -> int:
    """Where the option's code sits: the table does not start at the same place in every save."""
    found = [o for o in range(0, SIGNED_BYTES - 4, 2) if struct.unpack_from("<H", save, o)[0] == WIDESCREEN_CODE]
    if len(found) != 1:
        raise ValueError(f"expected the widescreen entry once, found it at {[hex(o) for o in found]}")
    return found[0]


def widescreen_of(save: bytes) -> int:
    return struct.unpack_from("<H", save, _widescreen_offset(save) + 2)[0]


def set_widescreen(save: bytes, value: int) -> bytes:
    """The save with the option set. It needs signing again afterwards."""
    edited = bytearray(save)
    struct.pack_into("<H", edited, _widescreen_offset(save) + 2, value)
    return bytes(edited)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("save", type=Path)
    parser.add_argument("key", type=bytes.fromhex)
    parser.add_argument("output", type=Path)
    parser.add_argument("--widescreen", type=int, choices=(0, 1))
    args = parser.parse_args()
    save = args.save.read_bytes()
    if args.widescreen is not None:
        save = set_widescreen(save, args.widescreen)
    args.output.write_bytes(resign(save, args.key))


if __name__ == "__main__":
    main()
