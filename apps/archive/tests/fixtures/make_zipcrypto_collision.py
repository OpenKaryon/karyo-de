#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a tiny synthetic ZIP whose wrong password passes the header byte.

The archive is stored, not compressed. Both passwords pass the one-byte header
check; only fixture-password yields the correct data CRC. No third-party data.
Regenerate with Python's standard library; verify with its public zipfile API.
Format reference: https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
"""
import binascii
from pathlib import Path
import struct
import zipfile

MASK = 0xffffffff

def crc_byte(crc, value):
    for _ in range(8):
        low = (crc ^ value) & 1
        crc >>= 1
        if low:
            crc ^= 0xedb88320
        value >>= 1
    return crc

class Cipher:
    def __init__(self, password):
        self.keys = [0x12345678, 0x23456789, 0x34567890]
        for value in password:
            self.update(value)

    def update(self, value):
        a, b, c = self.keys
        a = crc_byte(a, value)
        b = ((b + (a & 255)) * 134775813 + 1) & MASK
        c = crc_byte(c, b >> 24)
        self.keys = [a, b, c]

    def transform(self, data, encrypt):
        result = bytearray()
        for value in data:
            temp = self.keys[2] | 2
            changed = value ^ (((temp * (temp ^ 1)) >> 8) & 255)
            self.update(value if encrypt else changed)
            result.append(changed)
        return bytes(result)

def generate():
    payload = b"private test fixture"
    name = b"secret.txt"
    crc = binascii.crc32(payload)
    for nonce in range(100000):
        header = nonce.to_bytes(11, "little") + bytes([crc >> 24])
        encrypted = Cipher(b"fixture-password").transform(header + payload, True)
        if Cipher(b"wrong").transform(encrypted[:12], False)[11] == crc >> 24:
            break
    else:
        raise RuntimeError("No collision in deterministic search")
    local = struct.pack("<I5H3I2H", 0x04034b50, 20, 1, 0, 0, 33, crc,
                        len(encrypted), len(payload), len(name), 0) + name + encrypted
    central = struct.pack("<I6H3I5H2I", 0x02014b50, 20, 20, 1, 0, 0, 33, crc,
                          len(encrypted), len(payload), len(name), 0, 0, 0, 0, 0, 0) + name
    end = struct.pack("<I4H2IH", 0x06054b50, 0, 0, 1, 1, len(central), len(local), 0)
    target = Path(__file__).with_name("zipcrypto-header-collision.zip")
    target.write_bytes(local + central + end)
    with zipfile.ZipFile(target) as archive:
        assert archive.read("secret.txt", pwd=b"fixture-password") == payload
        try:
            archive.read("secret.txt", pwd=b"wrong")
        except zipfile.BadZipFile as error:
            assert "CRC" in str(error)
        else:
            raise AssertionError("Wrong password must fail after the header check")
    damaged = target.with_name("zip-unencrypted-bad-crc.zip")
    info = zipfile.ZipInfo("secret.txt", date_time=(1980, 1, 1, 0, 0, 0))
    with zipfile.ZipFile(damaged, "w") as archive:
        archive.writestr(info, payload)
    data = bytearray(damaged.read_bytes())
    data[30 + len(name)] ^= 1
    damaged.write_bytes(data)
    print(target.name, "nonce", nonce)

if __name__ == "__main__":
    generate()
