"""Run the actual portable backup engine against disposable disk fixtures."""
import hashlib,json,pathlib,subprocess,sys,tempfile
binary=str(pathlib.Path(sys.argv[1]).resolve())
def call(*args,ok=True):
    p=subprocess.run([binary,*map(str,args)],capture_output=True,text=True)
    assert p.returncode in (0,1),(p.returncode,p.stdout,p.stderr)
    assert (p.returncode==0)==ok,(p.returncode,p.stdout,p.stderr)
    return p.stdout
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp); c=root/'C';e=root/'E';c.mkdir();e.mkdir();(c/'fonts').mkdir()
    (c/'xboxdash.xbe').write_bytes(bytes(range(256))*1200)
    (c/'fonts'/'Xbox Book.xtf').write_bytes(b'original font'*100)
    recovery=root/'application.xbe';recovery.write_bytes(b'recovery executable'*1000)
    before={str(p.relative_to(c)):hashlib.sha256(p.read_bytes()).hexdigest() for p in c.rglob('*') if p.is_file()}
    first=json.loads(call('backup',c,e,recovery));folder=pathlib.Path(first['folder']);assert first['files']==2
    manifest=json.loads((folder/'manifest.json').read_text());assert manifest['persistent_boot_enabled'] is False
    for entry in manifest['files']:
        assert entry['sha256']==hashlib.sha256((folder/entry['path']).read_bytes()).hexdigest()
    assert (folder/'eeprom.bin').read_bytes()==bytes(range(256))
    assert (folder/'recovery.xbe').read_bytes()==recovery.read_bytes()
    for name,digest in before.items(): assert hashlib.sha256((folder/'C'/name).read_bytes()).hexdigest()==digest
    call('matches',folder,c)
    (c/'fonts'/'Xbox Book.xtf').write_bytes(b'changed non-boot file');call('matches',folder,c,ok=False)
    (c/'fonts'/'Xbox Book.xtf').write_bytes(b'original font'*100)
    (c/'extra-file').write_bytes(b'new');call('matches',folder,c,ok=False);(c/'extra-file').unlink()
    (c/'extra-empty-folder').mkdir();call('matches',folder,c,ok=False);(c/'extra-empty-folder').rmdir()
    call('matches',folder,c)
    second=json.loads(call('backup',c,e,recovery));assert second['folder']!=first['folder'];call('verify',folder)
    assert call('latest',e)==second['folder']
    protected=folder/'C'/'xboxdash.xbe';original=protected.read_bytes();protected.write_bytes(b'corrupt');call('verify',folder,ok=False);protected.write_bytes(original)
    record=manifest['files'][0];oldpath=record['path'];record['path']='../escape';(folder/'manifest.json').write_text(json.dumps(manifest));call('verify',folder,ok=False);record['path']=oldpath
    (folder/'manifest.json').write_text(json.dumps(manifest));(folder/'unexpected.bin').write_bytes(b'x');call('verify',folder,ok=False);(folder/'unexpected.bin').unlink()
    call('verify',folder)
    invalid=dict(manifest);invalid['schema']='wrong type';(folder/'manifest.json').write_text(json.dumps(invalid));call('verify',folder,ok=False)
    (folder/'manifest.json').write_text(json.dumps(manifest))
    failed=json.loads(call('cancel',c,e,recovery,ok=False));assert not (pathlib.Path(failed['folder'])/'manifest.json').exists()
    assert call('latest',e)==second['folder']
    call('bad-eeprom',c,e,recovery,ok=False)
    call('backup',c,c/'nested',recovery,ok=False)
    call('backup',str(c)+'/',c/'nested',recovery,ok=False)
    (c/'linked').symlink_to(recovery);call('backup',c,e,recovery,ok=False);(c/'linked').unlink()
    after={str(p.relative_to(c)):hashlib.sha256(p.read_bytes()).hexdigest() for p in c.rglob('*') if p.is_file()}
    assert after==before,'Source disk was modified'
print('PASS: exact C/EEPROM/recovery backups, unique sessions, read-back hashes, corruption/traversal/extra-file rejection, cancellation, invalid EEPROM, overlap and symlink rejection; source unchanged')
