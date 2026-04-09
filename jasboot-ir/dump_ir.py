# dump_ir.py
import sys

with open(sys.argv[1], 'rb') as f:
    header = f.read(16)
    magic = header[0:4]
    version = header[4]
    flags = header[7]
    code_size = int.from_bytes(header[8:12], 'little')
    data_size = int.from_bytes(header[12:16], 'little')
    
    print(f"Header: Magic={magic}, Ver={version}, Flags={flags}, Code={code_size}, Data={data_size}")
    
    offset = 16
    if flags & 0x04: # IR_FLAG_IA_METADATA
        ia_size = int.from_bytes(f.read(4), 'little')
        f.read(ia_size)
        offset += 4 + ia_size
        print(f"Skipped IA Metadata: {ia_size} bytes")
    
    print(f"Code starts at offset {offset}")
    code = f.read(code_size)
    
    for i in range(0, len(code), 5):
        inst = code[i:i+5]
        if len(inst) < 5: break
        pc = offset + i
        print(f"PC={pc:04d} | Op={inst[0]:02X} Flags={inst[1]:02X} A={inst[2]:02X} B={inst[3]:02X} C={inst[4]:02X}")
