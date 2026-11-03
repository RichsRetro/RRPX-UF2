"""Reject stale native COFF objects whose FTP layout guard is missing/mismatched."""
import pathlib,struct,sys

def symbols(path):
    b=pathlib.Path(path).read_bytes()
    if len(b)<20:raise ValueError('Truncated COFF object')
    machine,_,_,offset,count,_,_=struct.unpack_from('<HHIIIHH',b)
    if machine!=0x14c or offset+count*18+4>len(b):raise ValueError('Invalid i386 COFF symbol table')
    strings=offset+count*18;result=[];i=0
    while i<count:
        at=offset+i*18;raw=b[at:at+8]
        if raw[:4]==bytes(4):
            start=strings+struct.unpack_from('<I',raw,4)[0]
            if start>=len(b):raise ValueError('Invalid COFF string offset')
            end=b.find(bytes(1),start)
            if end<0:raise ValueError('Unterminated COFF symbol')
            name=b[start:end].decode('ascii','replace')
        else:name=raw.rstrip(bytes(1)).decode('ascii','replace')
        section=struct.unpack_from('<h',b,at+12)[0];aux=b[at+17]
        result.append((name,section));i+=1+aux
    return result

def check(main,server):
    callers={n for n,s in symbols(main) if 'verifyPicoFtpAbi' in n and s==0}
    definitions={n for n,s in symbols(server) if 'verifyPicoFtpAbi' in n and s>0}
    if len(callers)!=1 or callers!=definitions:
        raise ValueError('FTP ABI guard missing or mismatched: clean-rebuild all application objects')
    return next(iter(callers))

if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('Usage: check_ftp_abi.py Dashboard/main.obj Dashboard/Sources/ftpServer.obj')
    print('PASS native caller/server FTP ABI tag:',check(*sys.argv[1:]))
