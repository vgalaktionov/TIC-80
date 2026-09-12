import pathlib, re, struct, sys
root = pathlib.Path(sys.argv[1]).resolve()
data = (root / 'tic80-pro.exe').read_bytes()
def unpack(fmt, offset):
    return struct.unpack_from('<' + fmt, data, offset)[0]
pe = unpack('I', 0x3c)
assert data[pe:pe + 4] == b'PE\0\0'
assert unpack('H', pe + 4) == 0x8664
optional = pe + 24
assert unpack('H', optional) == 0x20b
base = unpack('Q', optional + 24)
sections = optional + unpack('H', pe + 20)
def offset(address):
    rva = address - base
    for i in range(unpack('H', pe + 6)):
        section = sections + i * 40
        start = unpack('I', section + 12)
        size = unpack('I', section + 16)
        if start <= rva < start + size:
            return unpack('I', section + 20) + rva - start
    raise ValueError(hex(address))
symbols = (root / 'symbols.txt').read_text()
match = re.search(r'^([0-9a-f]+) [Dd] Scripts(?:\.lto_priv\.\d+)?$', symbols, re.M)
assert match, 'Runtime table symbol missing'
table = offset(int(match[1], 16))
names = []
for i in range(13):
    config = unpack('Q', table + 8 * i)
    if config == 0:
        break
    name_offset = offset(unpack('Q', offset(config) + 8))
    names.append(data[name_offset:data.index(b'\0', name_offset)].decode())
assert len(names) == 12, names
assert set(names) == {'lua', 'ruby', 'js', 'moon', 'yue', 'fennel',
                      'scheme', 'squirrel', 'wren', 'wasm', 'janet', 'python'}, names
print('Verified 12 non-null runtime registrations in the Windows executable:')
print(', '.join(names))
