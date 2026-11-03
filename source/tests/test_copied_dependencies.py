import pathlib,tempfile,os,subprocess
source=pathlib.Path(__file__).resolve().parents[1]/'Dashboard/Makefile'
guard=source.read_text().split('# PicoX: copied .d files',1)[1];guard='# PicoX: copied .d files'+guard
with tempfile.TemporaryDirectory()as tmp:
 r=pathlib.Path(tmp);(r/'Includes').mkdir();(r/'sdk/lib/net/nforceif/include').mkdir(parents=True)
 files={'main.cpp':'source','main.obj':'cached old object','Includes/ftpServer.hpp':'new class layout','sdk/lib/net/nforceif/include/lwipopts.h':'config','foreign.d':'/some/other/checkout/main.obj: /some/other/checkout/Includes/ftpServer.hpp\n'}
 for n,b in files.items():(r/n).write_text(b)
 for n in files:os.utime(r/n,(100,100))
 os.utime(r/'Includes/ftpServer.hpp',(200,200))
 (r/'Makefile').write_text('SRCS := $(CURDIR)/main.cpp\nNXDK_DIR := $(CURDIR)/sdk\n-include foreign.d\n$(CURDIR)/main.obj: $(CURDIR)/main.cpp\n\t@echo REBUILD MAIN\n'+guard)
 os.utime(r/'Makefile',(50,50))
 result=subprocess.run(['make','-s','-n',str(r/'main.obj')],cwd=r,text=True,capture_output=True,check=True)
 assert 'REBUILD MAIN'in result.stdout,result.stdout
print('PASS copied foreign absolute dependency file cannot hide local FTP header changes from main.obj')
