#!/usr/bin/env python3
import collections


def main():
    codepoint_to_char_map = {}
    with open("cp437-map.tsv") as f:
        for ln in f.readlines():
            ch_s, _, _, cp_s = ln.split("\t")
            codepoint_to_char_map[int(cp_s, 16)] = int(ch_s)

    hash_map = collections.defaultdict(list)
    for cp, ch in codepoint_to_char_map.items():
        if cp < 0x7f:
            continue
        hash_map[cp & 0xFF].append((cp & ~0xff) | ch)

    max_len = max(len(v) for v in hash_map.values())

    with open("cp437_map.h", "w") as f:
        print("#include <stdint.h>\n", file=f)
        print(f"#define CP437_ENTRY_LEN {max_len}\n", file=f)
        print(f"uint16_t cp437_map[][{max_len}] = {{", file=f)
        for low in range(256):
            vs = [0] * max_len
            for i, v in enumerate(hash_map[low]):
                vs[i] = v
            vals = ", ".join(f"0x{v:04x}" for v in vs)
            print(f"  {{{vals}}},", file=f)
        print("};", file=f)


if __name__ == "__main__":
    main()
