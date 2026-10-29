"""Recreate MSVC import libraries from the installed, matching fooyin DLLs."""
import pathlib, struct, subprocess, sys

output = pathlib.Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=True)
for module in ('core', 'gui', 'utils'):
    name = 'fooyin_' + module
    data = (pathlib.Path(sys.argv[2]) / (name + '.dll')).read_bytes()
    def u16(p): return struct.unpack_from('<H', data, p)[0]
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    pe = u32(60); optional = pe + 24
    assert u16(optional) == 0x20b, 'Expected x64 PE'
    sections = []
    for i in range(u16(pe + 6)):
        s = optional + u16(pe + 20) + i * 40
        sections.append((u32(s+12), max(u32(s+8),u32(s+16)), u32(s+20), u32(s+36)))
    def section(rva): return next(s for s in sections if s[0] <= rva < s[0]+s[1])
    def offset(rva):
        s = section(rva); return s[2] + rva - s[0]
    exp = offset(u32(optional + 112))
    functions, names, ordinals = (offset(u32(exp+p)) for p in (28,32,36))
    exports = []
    for i in range(u32(exp+24)):
        p = offset(u32(names+i*4)); symbol = data[p:data.index(0,p)].decode('ascii')
        rva = u32(functions+u16(ordinals+i*2)*4)
        suffix = '' if section(rva)[3] & 0x20000000 else ' DATA'
        exports.append('  ' + symbol + suffix)
    definition = output / (name+'.def')
    contents='LIBRARY '+name+'.dll\nEXPORTS\n'+'\n'.join(exports)+'\n'
    if definition.exists() and definition.read_text()==contents and (output/(name+'.lib')).exists():
        continue
    definition.write_text(contents)
    subprocess.run(['lib.exe','/nologo','/machine:x64','/def:'+str(definition),'/out:'+str(output/(name+'.lib'))], check=True)
    print(name, len(exports), 'exports')
