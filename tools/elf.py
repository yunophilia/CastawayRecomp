"""Reads the symbol table of the game's linker output (files/tsc_wii_release_dvd.elf).

The disc ships the ELF that main.dol was made from (its sections are byte-identical to
the DOL's), with the full symbol table, so function names and extents come from there.
"""
import struct


class Symbol:
    __slots__ = ('name', 'addr', 'size', 'kind')

    def __init__(self, name, addr, size, kind):
        self.name = name
        self.addr = addr
        self.size = size
        self.kind = kind   # 'func' or 'object'


class Elf:
    def __init__(self, path):
        d = open(path, 'rb').read()
        if d[:4] != b'\x7fELF' or d[5] != 2:
            raise ValueError(f'{path}: not a big-endian ELF')
        shoff, = struct.unpack_from('>I', d, 32)
        shnum, shstrndx = struct.unpack_from('>HH', d, 48)
        self.sections = [struct.unpack_from('>10I', d, shoff + i * 40) for i in range(shnum)]
        shstr = self.sections[shstrndx]

        def cstr(base, off):
            p = base + off
            return d[p:d.index(b'\0', p)].decode('latin1')

        self.section_names = [cstr(shstr[4], s[0]) for s in self.sections]
        self.symbols = []
        for s in self.sections:
            if s[1] != 2:   # SHT_SYMTAB
                continue
            strtab = self.sections[s[6]]
            for k in range(s[5] // 16):
                st_name, value, size, info, _, shndx = struct.unpack_from('>IIIBBH', d, s[4] + k * 16)
                typ = info & 0xf
                if typ not in (1, 2) or not st_name or not value:
                    continue
                self.symbols.append(Symbol(cstr(strtab[4], st_name), value, size,
                                           'func' if typ == 2 else 'object'))

    def functions(self):
        """{address: name} of every function symbol with code (size > 0)."""
        out = {}
        for s in self.symbols:
            if s.kind == 'func' and s.size:
                out.setdefault(s.addr, s.name)
        return out

    def by_name(self):
        """{name: Symbol}; a name defined more than once maps to None."""
        out = {}
        for s in self.symbols:
            if s.name not in out:
                out[s.name] = s
            elif out[s.name] is not None and out[s.name].addr != s.addr:
                out[s.name] = None
        return out
