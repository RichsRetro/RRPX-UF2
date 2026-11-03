"""Rebuild the GPL boot components; no console access or Microsoft blobs."""
import hashlib,importlib.util,json,pathlib,subprocess,os,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent.parent
SOURCE=ROOT/'third_party/nkpatcher'
OUTPUT=ROOT/'built_boot'
local_nasm=WORKSPACE/'work/Rocky5/App Sources/NKPatcher/Main NKP11/nasm.exe'
NASM=os.environ.get('RRXD_NASM') or (str(local_nasm) if local_nasm.exists() else shutil.which('nasm'))
sign_spec=importlib.util.spec_from_file_location('sign',ROOT/'pico/tools/habibi_sign.py')
sign=importlib.util.module_from_spec(sign_spec);sign_spec.loader.exec_module(sign)
def assemble(source,output,*options):
    if not NASM:raise RuntimeError('Install NASM or set RRXD_NASM to its executable path')
    subprocess.run([str(NASM),*options,'-o',str(output),source],cwd=SOURCE,check=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def build():
    OUTPUT.mkdir(exist_ok=True)
    assemble('xboxapp.asm',OUTPUT/'nkpatcher-unsigned.xbe')
    signed=sign.sign_xbe(sign.populate_empty_hashes((OUTPUT/'nkpatcher-unsigned.xbe').read_bytes()))
    assert sign.verify_habibi(signed)
    (OUTPUT/'nkpatcher.xbe').write_bytes(signed)
    assemble('ernie.asm',OUTPUT/'ernie.xtf')
    assert b'\\Device\\Harddisk0\\Partition1\\RRXD-Lite;nkpatcher.xbe\0' in (OUTPUT/'ernie.xtf').read_bytes()
    profiles=json.loads((SOURCE/'font_profiles.json').read_text())
    supported={'3944','4034','4817','5101','5530','5713','5838'}
    for name,profile in profiles.items():
        target=OUTPUT/name
        assemble('font_stub.asm',target,f'-DKERNEL_GADGET={profile["font_gadget"]}')
        assert sha(target)==profile['upstream_font_sha256'],f'Font reproduction failed: {name}'
        profile['patcher_supported']=profile['kernel'].split('.')[2] in supported
    commit=(SOURCE/'SOURCE_REVISION.txt').read_text().strip()
    manifest={'component_license':'GPL-2.0-only','source_commit':commit,'upstream':'https://github.com/Rocky5/Xbox-Softmodding-Tool',
        'profiles':profiles,'files':{p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in OUTPUT.iterdir() if p.is_file() and p.name!='manifest.json'},
        'required_microsoft_loader_sha256':'3ae2f3eae3917e0130d1e1a5c8e1c5df207ea31703646a174d98f7be9a769495',
        'microsoft_loader_included':False,'persistent_activation_enabled':False,
        'reason':'Cold-boot profile and recovery procedure not yet hardware-validated; ShadowC disabled in the staging configuration.'}
    (OUTPUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print(f'PASS: signed patcher, isolated font-loader path, {len(profiles)} byte-exact font profiles; activation disabled')
if __name__=='__main__':build()
