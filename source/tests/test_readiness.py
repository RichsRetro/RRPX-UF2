"""Validate real signatures and fail-closed readiness against disposable backups."""
import importlib.util,json,pathlib,shutil,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('assess',ROOT/'tools/assess_backup.py')
assess=importlib.util.module_from_spec(spec);spec.loader.exec_module(assess)
binary=pathlib.Path(sys.argv[1]).resolve();reference=pathlib.Path(sys.argv[2]).resolve()
data=reference.read_bytes();assert assess.retail_signature(data)
changed=bytearray(data);changed[4]^=1;assert not assess.retail_signature(changed)
assert not assess.retail_signature((ROOT/'built_boot/nkpatcher.xbe').read_bytes())
with tempfile.TemporaryDirectory() as t:
    root=pathlib.Path(t);c=root/'C';e=root/'E';c.mkdir();e.mkdir();(c/'xodash').mkdir()
    shutil.copy2(reference,c/'xboxdash.xbe');shutil.copy2(reference,c/'xodash/xonlinedash.xbe')
    recovery=root/'recovery.xbe';recovery.write_bytes(b'fixture recovery')
    r=subprocess.run([str(binary),'backup',str(c),str(e),str(recovery)],check=True,text=True,capture_output=True)
    folder=pathlib.Path(json.loads(r.stdout)['folder']);report=assess.assess(folder)
    assert report['backup_verified'] and report['known_kernel_profile']
    assert report['compatible_loader_paths'] and report['stock_boot_retail_signature_valid']
    assert report['ready_for_activation'] is False,'Untested cold boot must never be enabled'
    manifest=json.loads((folder/'manifest.json').read_text());manifest['console']['kernel']='1.0.9999.1'
    (folder/'manifest.json').write_text(json.dumps(manifest));report=assess.assess(folder)
    assert not report['known_kernel_profile'] and any('kernel' in r for r in report['blocking_reasons'])
    (folder/'eeprom.bin').write_bytes(bytes(256))
    try:assess.assess(folder)
    except ValueError:pass
    else:raise AssertionError('Corrupt EEPROM was accepted')
print('PASS: genuine retail signature, signature corruption and Habibi/retail distinction; matching loader/kernel detection, unsupported kernel and corrupt backup rejection; activation remains disabled')
