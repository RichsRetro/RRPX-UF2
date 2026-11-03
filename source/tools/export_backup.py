"""Download and verify an RRXD backup over passive FTP; never writes to Xbox."""
import argparse,ftplib,hashlib,json,pathlib,re,shutil,zipfile
MAX_MANIFEST=4*1024*1024
def records_from(data):
    if len(data)>MAX_MANIFEST:raise ValueError('Backup manifest exceeds the size limit')
    m=json.loads(data)
    if not isinstance(m,dict) or type(m.get('schema')) is not int or m.get('schema')!=1 or m.get('status')!='verified-backup' or m.get('persistent_boot_enabled') is not False:
        raise ValueError('Not a complete RRXD preparation backup')
    records=m.get('files');seen=set()
    if not isinstance(records,list) or not 3<=len(records)<=10002:raise ValueError('Invalid file table')
    for r in records:
        if not isinstance(r,dict):raise ValueError('Invalid file record')
        name=r.get('path');size=r.get('bytes');digest=r.get('sha256')
        if not isinstance(name,str) or len(name)>210 or '\\' in name or ':' in name or any(p in ('','..','.') for p in name.split('/')) or any(ord(c)<32 or ord(c)==127 for c in name):
            raise ValueError('Unsafe backup path')
        if name not in ('eeprom.bin','recovery.xbe') and not name.startswith('C/'):raise ValueError('Unexpected backup path')
        if name in seen:raise ValueError('Duplicate backup file')
        seen.add(name)
        if isinstance(size,bool) or not isinstance(size,int) or not 0<=size<=4*1024**3:raise ValueError('Invalid file size')
        if not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest):raise ValueError('Invalid file hash')
        if name=='eeprom.bin' and size!=256:raise ValueError('Invalid EEPROM size')
    if not {'eeprom.bin','recovery.xbe'}.issubset(seen) or not any(n.startswith('C/') for n in seen):raise ValueError('Missing recovery data')
    # Avoid conflicting files/directories and aliases on Windows/FATX.
    keys=set()
    for name in sorted(seen,key=len):
        key=name.casefold()
        if key in keys or any('/'.join(key.split('/')[:i]) in keys for i in range(1,len(key.split('/')))):raise ValueError('Conflicting backup paths')
        keys.add(key)
        for part in name.split('/'):
            if part.endswith((' ','.')) or part.split('.')[0].upper() in {'CON','PRN','AUX','NUL',*[f'COM{i}' for i in range(1,10)],*[f'LPT{i}' for i in range(1,10)]}:
                raise ValueError('Filename cannot safely be exported on Windows')
    return m,records
def export(host,backup,output,*,port=21,user='xbox',password='xbox',archive=True):
    if not re.fullmatch(r'Backup-\d{3}',backup):raise ValueError('Use a backup name such as Backup-000')
    output=pathlib.Path(output).absolute()
    if output.exists():raise FileExistsError('Choose a new destination; existing backups are never replaced')
    if archive and output.with_name(output.name+'.zip').exists():raise FileExistsError('An export ZIP already exists at that destination')
    with ftplib.FTP() as ftp:
        ftp.connect(host,port,timeout=30);ftp.login(user,password);ftp.set_pasv(True)
        remote='/E/RRXD-Lite-Setup/'+backup
        manifest_data=bytearray()
        def append(block):
            manifest_data.extend(block)
            if len(manifest_data)>MAX_MANIFEST:raise ValueError('Manifest too large')
        ftp.retrbinary('RETR '+remote+'/manifest.json',append)
        manifest,records=records_from(manifest_data)
        total=sum(r['bytes'] for r in records)
        output.parent.mkdir(parents=True,exist_ok=True)
        if shutil.disk_usage(output.parent).free<total*(2 if archive else 1)+16*1024*1024:raise OSError('Not enough free space for a verified backup export')
        output.mkdir(mode=0o700)
        for r in records:
            target=output/r['path'];target.parent.mkdir(parents=True,exist_ok=True)
            partial=target.with_name(target.name+'.download-partial')
            digest=hashlib.sha256();count=0
            with partial.open('xb') as file:
                def receive(block):
                    nonlocal count
                    count+=len(block)
                    if count>r['bytes']:raise ValueError('Remote file exceeds its recorded size')
                    digest.update(block);file.write(block)
                ftp.retrbinary('RETR '+remote+'/'+r['path'],receive,blocksize=65536)
            if count!=r['bytes'] or digest.hexdigest()!=r['sha256']:raise ValueError('Backup hash mismatch: '+r['path'])
            partial.rename(target)
        (output/'manifest.json').write_bytes(manifest_data)
        if len(set((output/'eeprom.bin').read_bytes()))<2:raise ValueError('EEPROM backup is invalid')
        if archive:
            archive_path=output.parent/(output.name+'.zip')
            with zipfile.ZipFile(archive_path,'x',zipfile.ZIP_DEFLATED) as z:
                for r in records:z.write(output/r['path'],backup+'/'+r['path'])
                z.write(output/'manifest.json',backup+'/manifest.json')
            with zipfile.ZipFile(archive_path) as z:
                if z.testzip() is not None:raise ValueError('Export archive verification failed')
                for r in records:
                    with z.open(backup+'/'+r['path']) as file:
                        if hashlib.file_digest(file,'sha256').hexdigest()!=r['sha256']:raise ValueError('Archive content hash mismatch')
        (output/'EXPORT-VERIFIED.json').write_text(json.dumps({'backup':backup,'files':len(records),'bytes':total,'sha256_verified':True,'archive_verified':archive},indent=2))
    return output
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',required=True);p.add_argument('--backup',default='Backup-000');p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--port',type=int,default=21);p.add_argument('--user',default='xbox');p.add_argument('--password',default='xbox');p.add_argument('--no-zip',action='store_true');args=p.parse_args()
    try:result=export(args.host,args.backup,args.output,port=args.port,user=args.user,password=args.password,archive=not args.no_zip)
    except (OSError,ValueError,ftplib.Error,zipfile.BadZipFile) as error:p.exit(1,f'Export not verified: {error}\nPartial files, if any, are preserved.\n')
    print('Verified backup saved to',result)
