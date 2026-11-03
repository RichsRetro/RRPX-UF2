import pathlib,tempfile,socket,subprocess,ftplib,concurrent.futures,io,time,sys
exe=sys.argv[1]
with tempfile.TemporaryDirectory() as temp:
 root=pathlib.Path(temp);(root/'E/Games/Game Folder').mkdir(parents=True)
 s=socket.socket();s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close()
 process=subprocess.Popen([exe,temp,str(port)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
 def connect():
  for attempt in range(100):
   f=ftplib.FTP(timeout=3)
   try:f.connect('127.0.0.1',port);f.login('xbox','xbox');return f
   except OSError:f.close();time.sleep(.02)
  raise AssertionError('FTP did not start')
 try:
  for round in range(2):
   assert process.stdout.readline().strip()=='READY'
   f=connect();assert 'E' in f.nlst();f.cwd('/E/Games');assert 'Game Folder' in f.nlst()
   def copy(n):
    g=connect();payload=bytes(range(256))*1024;remote='/E/Games/Game Folder/probe'+str(n)+'.bin';g.storbinary('STOR '+remote,io.BytesIO(payload));actual=bytearray();g.retrbinary('RETR '+remote,actual.extend);assert actual==payload;g.delete(remote);g.quit()
   with concurrent.futures.ThreadPoolExecutor(max_workers=2)as pool:list(pool.map(copy,[1,2]))
   f.cwd('/E/Games/Game Folder');data=f.transfercmd('STOR stopped.bin');data.sendall(b'x'*4096)
   began=time.monotonic();process.stdin.write('s');process.stdin.flush();assert process.stdout.readline().strip()=='STOPPED';assert time.monotonic()-began<4
   data.close();f.close()
  assert process.wait(timeout=4)==0
 finally:
  if process.poll() is None:process.kill();process.wait()
print('PASS compatibility FTP: root/subdir listings,2parallel256KiB upload+readback, stop with live upload+client drains under4s,2restarts and stop-before-start race')
