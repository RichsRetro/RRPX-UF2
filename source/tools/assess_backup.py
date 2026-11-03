"""Assess a verified console backup offline. Never writes to console boot files."""
import argparse,hashlib,importlib.util,json,pathlib,struct
ROOT=pathlib.Path(__file__).resolve().parents[1]
EXPECTED_LOADER='3ae2f3eae3917e0130d1e1a5c8e1c5df207ea31703646a174d98f7be9a769495'
def retail_signature(data):
    spec=importlib.util.spec_from_file_location('sign',ROOT/'pico/tools/habibi_sign.py')
    sign=importlib.util.module_from_spec(spec);spec.loader.exec_module(sign)
    _,_,header_size,_,_=sign.validate_layout(data)
    n,e,_=sign.key_pair();modulus=bytearray(n.to_bytes(256,'little'))
    modulus[-4:]=(int.from_bytes(modulus[-4:],'little')^0x2dd78bd6).to_bytes(4,'little')
    decoded=pow(int.from_bytes(data[4:260],'little'),e,int.from_bytes(modulus,'little')).to_bytes(256,'little')
    digest=sign.xbox_hash(data[0x104:header_size])[::-1]
    return decoded[:20]==digest and decoded[-2:]==b'\x01\0'
def assess(folder):
    folder=pathlib.Path(folder).resolve()
    manifest=json.loads((folder/'manifest.json').read_text())
    if type(manifest.get('schema')) is not int or manifest.get('schema')!=1 or manifest.get('status')!='verified-backup' or manifest.get('persistent_boot_enabled') is not False:
        raise ValueError('Not a complete preparation backup')
    records=manifest.get('files');seen=set();hashes={}
    if not isinstance(records,list) or len(records)<3:raise ValueError('Missing backup file table')
    for record in records:
        name=record['path'];relative=pathlib.PurePosixPath(name)
        if not isinstance(name,str) or '\\' in name or ':' in name or relative.is_absolute() or any(p in ('','..','.') for p in name.split('/')):
            raise ValueError('Unsafe file path')
        if name not in ('eeprom.bin','recovery.xbe') and not name.startswith('C/'):raise ValueError('Unexpected file namespace')
        if name in seen:raise ValueError('Duplicate file record')
        seen.add(name);file=folder/name
        if file.is_symlink() or folder not in file.resolve().parents:raise ValueError('Backup path escapes its directory')
        with file.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
        if file.stat().st_size!=record['bytes'] or digest!=record['sha256']:raise ValueError('Backup integrity mismatch: '+name)
        hashes[name]=digest
    actual={str(p.relative_to(folder)).replace('\\','/') for p in folder.rglob('*') if p.is_file() and p not in (folder/'manifest.json',folder/'EXPORT-VERIFIED.json')}
    if actual!=seen or not {'eeprom.bin','recovery.xbe'}.issubset(seen):raise ValueError('Incomplete or unexpected backup tree')
    eeprom=(folder/'eeprom.bin').read_bytes()
    if len(eeprom)!=256 or len(set(eeprom))<2:raise ValueError('Invalid EEPROM backup')
    console=manifest.get('console',{});kernel=console.get('kernel','unknown')
    profiles=json.loads((ROOT/'built_boot/manifest.json').read_text())['profiles']
    profile=profiles.get('K.'+kernel)
    candidates=[p for p,h in hashes.items() if h==EXPECTED_LOADER and p.startswith('C/')]
    compatible=[p for p in candidates if retail_signature((folder/p).read_bytes())]
    stock='C/xboxdash.xbe';stock_signed=False
    if stock in hashes:
        try:stock_signed=retail_signature((folder/stock).read_bytes())
        except (ValueError,struct.error):pass
    reasons=['Cold-boot profile and recovery procedure still require physical Xbox validation']
    if not profile or not profile.get('patcher_supported'):reasons.append('Unsupported or unknown exact kernel/QFE')
    if not compatible:reasons.append('Compatible Microsoft-signed boot loader is absent from this backup')
    if not stock_signed:reasons.append('Current stock boot executable could not be authenticated')
    return {'kernel':kernel,'backup_verified':True,'eeprom_sha256':hashes['eeprom.bin'],
        'compatible_loader_paths':compatible,'stock_boot_retail_signature_valid':stock_signed,
        'known_kernel_profile':bool(profile and profile.get('patcher_supported')),
        'ready_for_activation':False,'blocking_reasons':reasons}
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('backup',type=pathlib.Path);args=parser.parse_args()
    print(json.dumps(assess(args.backup),indent=2))
