"""Embed a separately executable GPL patcher with an integrity fingerprint."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def build():
    data=(ROOT/'built_boot/nkpatcher.xbe').read_bytes()
    metadata=json.loads((ROOT/'built_boot/manifest.json').read_text())
    digest=hashlib.sha256(data).hexdigest()
    if metadata['files']['nkpatcher.xbe']['sha256']!=digest:raise ValueError('Component fingerprint mismatch')
    (ROOT/'Dashboard/Includes/bootComponent.hpp').write_text('#pragma once\nextern const unsigned char boot_component[];\nextern const unsigned int boot_component_size;\nextern const char boot_component_sha256[];\n')
    source='#include "bootComponent.hpp"\n// Separate GPL-2.0-only executable data. See third_party/nkpatcher/COPYING.\n'
    source+=f'const unsigned int boot_component_size={len(data)};\nconst char boot_component_sha256[]="{digest}";\nconst unsigned char boot_component[]={{\n'
    source+='\n'.join(','.join(str(b) for b in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n'
    (ROOT/'Dashboard/Sources/bootComponent.cpp').write_text(source)
    print('Verified separate GPL executable resource:',len(data),'bytes')
if __name__=='__main__':build()
