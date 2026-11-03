"""End-to-end passive FTP export using the actual recovery FTP server."""
import importlib.util,json,pathlib,socket,subprocess,sys,tempfile,time,hashlib,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('exporter',ROOT/'tools/export_backup.py')
exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
server=pathlib.Path(sys.argv[1]).resolve();backup_tool=pathlib.Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory() as t:
    root=pathlib.Path(t);remote=root/'server';c=root/'C';c.mkdir();(c/'fonts').mkdir();(c/'xboxdash.xbe').write_bytes(bytes(range(256))*2000);(c/'fonts/Xbox Book.xtf').write_bytes(b'original font'*300)
    parent=remote/'E/RRXD-Lite-Setup';parent.mkdir(parents=True);application=root/'application.xbe';application.write_bytes(b'fixture application'*4000)
    r=subprocess.run([str(backup_tool),'backup',str(c),str(parent),str(application)],check=True,capture_output=True,text=True)
    backup=pathlib.Path(json.loads(r.stdout)['folder']);manifest=json.loads((backup/'manifest.json').read_text())
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    process=subprocess.Popen([str(server),str(remote),str(port)],stdout=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:connection=socket.create_connection(('127.0.0.1',port),timeout=.1);connection.close();break
            except OSError:time.sleep(.03)
        else:raise AssertionError('FTP test server did not start')
        exported=exporter.export('127.0.0.1','Backup-000',root/'exported',port=port)
        assert (exported/'EXPORT-VERIFIED.json').exists()
        for record in manifest['files']:assert hashlib.sha256((exported/record['path']).read_bytes()).hexdigest()==record['sha256']
        with zipfile.ZipFile(root/'exported.zip') as z:assert z.testzip() is None
        try:exporter.export('127.0.0.1','Backup-000',exported,port=port)
        except FileExistsError:pass
        else:raise AssertionError('Existing local backup was replaced')
        (backup/'C/xboxdash.xbe').write_bytes(b'damaged')
        try:exporter.export('127.0.0.1','Backup-000',root/'damaged-export',port=port)
        except ValueError:pass
        else:raise AssertionError('Damaged remote backup accepted')
        assert not (root/'damaged-export/EXPORT-VERIFIED.json').exists()
        for bad in ['../escape','C/../../escape','C/CON','C/a\r\nDELE x','C/a:b','/absolute']:
            m=json.loads(json.dumps(manifest));m['files'][0]['path']=bad
            try:exporter.records_from(json.dumps(m).encode())
            except ValueError:pass
            else:raise AssertionError('Unsafe export path accepted: '+bad)
    finally:process.terminate();process.wait(timeout=5)
print('PASS: actual recovery FTP -> verified backup export + ZIP; spaces/binary files, no overwrite, remote corruption and unsafe paths rejected')
