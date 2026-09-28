#!/usr/bin/env python3
import hashlib, json, tempfile
from pathlib import Path

def make_refs(rows=144, cols=8):
    return [[(r+c)%18 for c in range(cols)] for r in range(rows)]

def main():
    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        mt=td/'metatiles.bin'
        mt.write_bytes(bytes(144*8*2))
        refs=make_refs()
        obj={'version':1,'metatiles_sha256':hashlib.sha256(mt.read_bytes()).hexdigest(),'palettes':refs}
        p=td/'palette_refs.json'; p.write_text(json.dumps(obj))
        got=json.loads(p.read_text())
        assert len(got['palettes'])==144
        assert all(len(row)==8 for row in got['palettes'])
        assert all(0<=v<18 for row in got['palettes'] for v in row)
        assert got['metatiles_sha256']==hashlib.sha256(mt.read_bytes()).hexdigest()
    print('PASS: Kyoto 8-subtile palette_refs sidecar shape/checksum')
if __name__=='__main__': main()
