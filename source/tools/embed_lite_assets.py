"""Rebuild the self-contained dashboard's font, logo and default-config arrays."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def build():
    files={'font':ROOT/'branding/Share-Bold.ttf','logo':ROOT/'branding/rich-retro-xdash-lite-logo.jpg','config':ROOT/'config-hardmod.json'}
    header='#pragma once\n';source='#include "liteAssets.hpp"\n'
    for name,file in files.items():
        data=file.read_bytes();header+=f'extern const unsigned char lite_{name}[];\nextern const unsigned int lite_{name}_size;\n'
        source+=f'const unsigned int lite_{name}_size={len(data)};\nconst unsigned char lite_{name}[]={{\n'
        source+='\n'.join(','.join(str(b) for b in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n'
    (ROOT/'Dashboard/Includes/liteAssets.hpp').write_text(header)
    (ROOT/'Dashboard/Sources/liteAssets.cpp').write_text(source)
    print('Embedded font, logo and configuration rebuilt')
if __name__=='__main__':build()
