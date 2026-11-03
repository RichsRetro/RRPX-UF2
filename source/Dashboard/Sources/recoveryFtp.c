/* SPDX-License-Identifier: MIT
 * Small FTP service written for PicoLaunch. No external FTP server code used.
 */
#include "recoveryFtp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#ifdef NXDK
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <lwip/sockets.h>
#define close_socket lwip_close
struct pl_stat { unsigned st_mode; uint64_t st_size; };
static int pl_stat(const char *path,struct pl_stat *st) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path,GetFileExInfoStandard,&data)) return -1;
    st->st_mode=(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)?0x4000:0x8000;
    st->st_size=((uint64_t)data.nFileSizeHigh<<32)|data.nFileSizeLow;
    return 0;
}
#define stat pl_stat
#define S_ISDIR(mode) ((mode)==0x4000)
#define S_ISREG(mode) ((mode)==0x8000)
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define close_socket close
#endif

#define PATH_CAP 1024
#define LINE_CAP 1200
static volatile int running = 1, transfers, clients, listening;
static int listener = -1;
static const char *root_path;
typedef struct {
    int control, passive, authenticated, user_ok;
    struct sockaddr_in peer;
    char cwd[PATH_CAP], rename_from[PATH_CAP];
    uint64_t restart;
} session;

static int add_count(volatile int *value, int delta) {
#ifdef NXDK
    return delta > 0 ? InterlockedIncrement((volatile LONG *)value) :
                       InterlockedDecrement((volatile LONG *)value);
#else
    return __sync_add_and_fetch(value, delta);
#endif
}
int ftp_transfer_count(void) { return transfers; }
int ftp_client_count(void) { return clients; }
int ftp_is_listening(void) { return listening; }
void ftp_prepare(void) { running=1; }
void ftp_stop(void) {
    running = 0;
    if (listener >= 0) shutdown(listener, SHUT_RDWR);
}
static unsigned long milliseconds(void) {
#ifdef NXDK
 return GetTickCount();
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;
#endif
}
static int temporary_socket_error(void){return errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK||errno==ETIMEDOUT;}
static int send_all(int fd,const void *buffer,size_t length){
 const char*p=buffer;unsigned long last=milliseconds();
 while(length&&running){int count=send(fd,p,length,0);if(count<=0){if(count<0&&temporary_socket_error()&&milliseconds()-last<30000)continue;return -1;}p+=count;length-=count;last=milliseconds();}
 return length?-1:0;
}
static int receive_data(int fd,void*buffer,size_t size){unsigned long began=milliseconds();
 while(running){int n=recv(fd,buffer,size,0);if(n>=0)return n;if(!temporary_socket_error()||milliseconds()-began>=30000)return -1;}return -1;
}
static int reply(session *s, int code, const char *format, ...) {
    char body[900], line[1000];
    va_list args; va_start(args, format);
    vsnprintf(body, sizeof(body), format, args); va_end(args);
    snprintf(line, sizeof(line), "%d %s\r\n", code, body);
    return send_all(s->control, line, strlen(line));
}
static int receive_line(int fd, char *out, size_t capacity) {
    size_t used = 0;
    while (used + 1 < capacity) {
        char ch;
        int count = recv(fd, &ch, 1, 0);
        if(count<0&&temporary_socket_error()&&running)continue;
        if(!running||count<=0||ch=='\0')return -1;
        if (ch == '\n') { out[used] = 0; return 0; }
        if (ch != '\r') out[used++] = ch;
    }
    return -1;
}
/* A virtual /C, /D, /E... root. Canonicalise before translating to a drive. */
static int virtual_path(const char *cwd, const char *arg, char out[PATH_CAP]) {
    char joined[PATH_CAP], part[PATH_CAP];
    if (!arg || !*arg) arg = ".";
    int n = arg[0] == '/' ? snprintf(joined, sizeof(joined), "%s", arg) :
                           snprintf(joined, sizeof(joined), "%s/%s", cwd, arg);
    if (n < 0 || n >= (int)sizeof(joined)) return -1;
    strcpy(out, "/");
    const char *p = joined;
    while (*p) {
        while (*p == '/') ++p;
        size_t len = 0;
        while (*p && *p != '/') {
            unsigned char ch = (unsigned char)*p++;
            if (ch < 32 || ch == '\\' || ch == ':' || len + 1 >= sizeof(part)) return -1;
            part[len++] = (char)ch;
        }
        part[len] = 0;
        if (!len || !strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) {
            char *slash = strrchr(out, '/');
            if (slash == out) out[1] = 0; else *slash = 0;
            continue;
        }
        size_t used = strlen(out);
        if (used + len + 2 > PATH_CAP) return -1;
        if (used > 1) strcat(out, "/");
        strcat(out, part);
    }
    if (out[1]) {
        out[1] = (char)toupper((unsigned char)out[1]);
        if (!strchr("CDEFGXYZ", out[1]) || (out[2] && out[2] != '/')) return -1;
    }
    return 0;
}
static int native_path(const char *virt, char out[PATH_CAP]) {
    if (!virt[1]) return -1;
#ifdef NXDK
    int n = snprintf(out, PATH_CAP, "%c:\\%s", virt[1]=='D'?'R':virt[1], virt[2] ? virt+3 : "");
    for (char *p = out; *p; ++p) if (*p == '/') *p = '\\';
#else
    int n = snprintf(out, PATH_CAP, "%s%s", root_path, virt);
    if (n < 0 || n >= PATH_CAP) return -1;
    /* Host tests reject symlinks so virtual-root tests cannot escape their fixture. */
    char check[PATH_CAP]; strcpy(check, out);
    for (char *p = check + strlen(root_path); ; ++p) {
        if (*p == '/' || !*p) {
            char saved = *p; *p = 0;
            struct stat st;
            if (!lstat(check, &st) && S_ISLNK(st.st_mode)) return -1;
            *p = saved; if (!saved) break;
        }
    }
#endif
    return n >= 0 && n < PATH_CAP ? 0 : -1;
}
static void passive_close(session *s) {
    if (s->passive >= 0) { close_socket(s->passive); s->passive = -1; }
}
static void socket_timeout(int fd, int seconds) {
    struct timeval timeout = {seconds, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
static int passive_open(session *s, int extended) {
    passive_close(s);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return reply(s, 425, "Cannot open data socket");
    struct sockaddr_in local = {0}; socklen_t size = sizeof(local);
    if (getsockname(s->control, (struct sockaddr *)&local, &size)) {
        close_socket(fd); return reply(s, 425, "No local address");
    }
    local.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) || listen(fd, 1)) {
        close_socket(fd); return reply(s, 425, "Cannot listen for data");
    }
    size = sizeof(local); getsockname(fd, (struct sockaddr *)&local, &size);
    s->passive = fd;
    unsigned port = ntohs(local.sin_port), ip = ntohl(local.sin_addr.s_addr);
    if (extended) return reply(s, 229, "Entering Extended Passive Mode (|||%u|)", port);
    return reply(s, 227, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
                 ip>>24, (ip>>16)&255, (ip>>8)&255, ip&255, port>>8, port&255);
}
static int data_accept(session *s) {
    if (s->passive < 0) { reply(s, 425, "Use PASV or EPSV first"); return -1; }
    unsigned long began=milliseconds();int ready=0;
    while(running&&milliseconds()-began<30000){fd_set fds;FD_ZERO(&fds);FD_SET(s->passive,&fds);struct timeval timeout={1,0};
      int result=select(s->passive+1,&fds,NULL,NULL,&timeout);if(result>0){ready=1;break;}if(result<0&&errno!=EINTR)break;}
    if(!ready){passive_close(s);reply(s,425,"Data connection unavailable");return -1;}
    struct sockaddr_in peer; socklen_t length = sizeof(peer);
    int fd = accept(s->passive, (struct sockaddr *)&peer, &length);
    passive_close(s);
    if (fd < 0 || peer.sin_addr.s_addr != s->peer.sin_addr.s_addr) {
        if (fd >= 0) close_socket(fd);
        reply(s, 425, "Data peer mismatch"); return -1;
    }
    socket_timeout(fd, 1);
    return fd;
}
static int list_entry(int fd, const char *name, int directory, uint64_t size, int mode) {
    char line[1400];
    if (mode == 1) snprintf(line, sizeof(line), "%s\r\n", name);
    else if (mode == 2) snprintf(line, sizeof(line), "type=%s;size=%llu; %s\r\n",
                                directory ? "dir" : "file", (unsigned long long)size, name);
    else snprintf(line, sizeof(line), "%crw-rw-rw- 1 xbox xbox %llu Jan 01 2026 %s\r\n",
                  directory ? 'd' : '-', (unsigned long long)size, name);
    return send_all(fd, line, strlen(line));
}
static int list_directory(int fd, const char *virt, int mode) {
    if (!virt[1]) {
        for (const char *p = "CDEFGXYZ"; *p; ++p) {
            char entry[3] = {'/', *p, 0}, path[PATH_CAP]; struct stat st;
            if (!native_path(entry, path) && !stat(path, &st) && S_ISDIR(st.st_mode)) {
                char name[2] = {*p,0}; if (list_entry(fd, name, 1, 0, mode)) return -1;
            }
        }
        return 0;
    }
    char path[PATH_CAP]; if (native_path(virt, path)) return -1;
#ifdef NXDK
    char pattern[PATH_CAP]; if (snprintf(pattern, sizeof(pattern), "%s%s*", path, path[strlen(path)-1]=='\\'?"":"\\") >= PATH_CAP) return -1;
    WIN32_FIND_DATAA item; HANDLE find = FindFirstFileA(pattern, &item);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    int result = 0;
    do {
        if (!strcmp(item.cFileName, ".") || !strcmp(item.cFileName, "..")) continue;
        uint64_t size = ((uint64_t)item.nFileSizeHigh<<32) | item.nFileSizeLow;
        if (list_entry(fd, item.cFileName, !!(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), size, mode)) { result = -1; break; }
    } while (FindNextFileA(find, &item));
    FindClose(find); return result;
#else
    DIR *dir = opendir(path); if (!dir) return -1;
    int result = 0; struct dirent *item;
    while ((item = readdir(dir))) {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        char child[PATH_CAP]; struct stat st;
        if (snprintf(child, sizeof(child), "%s/%s", path, item->d_name) >= PATH_CAP) continue;
        if (lstat(child, &st) || S_ISLNK(st.st_mode)) continue;
        if (list_entry(fd, item->d_name, S_ISDIR(st.st_mode), st.st_size, mode)) { result = -1; break; }
    }
    closedir(dir); return result;
#endif
}
#ifdef NXDK
/* Native binary I/O avoids the observed stdio false-error-at-EOF behavior. */
static void transfer_native(session *s, const char *command, const char *virt) {
    int listing=!strcmp(command,"LIST")||!strcmp(command,"NLST")||!strcmp(command,"MLSD");
    int writing=!strcmp(command,"STOR")||!strcmp(command,"APPE");
    char path[PATH_CAP];HANDLE file=INVALID_HANDLE_VALUE;
    if(s->passive<0){reply(s,425,"Use PASV or EPSV first");return;}
    if(!listing){
        if(native_path(virt,path)||(virt[1]=='D'&&writing)){reply(s,550,"Invalid destination");return;}
        if(!writing){
            file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
            if(file==INVALID_HANDLE_VALUE){reply(s,550,"Cannot open source; error %lu",GetLastError());return;}
            if(s->restart){LARGE_INTEGER where,size;where.QuadPart=s->restart;
                if(!GetFileSizeEx(file,&size)||where.QuadPart>size.QuadPart||!SetFilePointerEx(file,where,NULL,FILE_BEGIN)){
                    CloseHandle(file);s->restart=0;reply(s,551,"Cannot seek to restart position");return;}
            }
        }
    }
    reply(s,150,"Opening binary data connection");int fd=data_accept(s);
    if(fd<0){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);return;}
    add_count(&transfers,1);int result=0;DWORD error=0;
    unsigned char *buffer=listing?NULL:malloc(65536);
    if(!listing&&!buffer){result=-1;error=ERROR_NOT_ENOUGH_MEMORY;}
    if(writing&&!result){
        DWORD began=GetTickCount();
        do {
            file=CreateFileA(path,GENERIC_WRITE,0,NULL,!strcmp(command,"APPE")?OPEN_ALWAYS:s->restart?OPEN_EXISTING:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
            if(file!=INVALID_HANDLE_VALUE)break;error=GetLastError();
            if(!running||(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION)||GetTickCount()-began>=3000)break;
            Sleep(25);
        }while(1);
        if(file==INVALID_HANDLE_VALUE)result=-1;
        if(!result&&(!strcmp(command,"APPE")||s->restart)){
            LARGE_INTEGER where,size;where.QuadPart=s->restart;
            if(!GetFileSizeEx(file,&size)||(s->restart&&where.QuadPart>size.QuadPart))result=-1;
            else {if(!strcmp(command,"APPE")&&!s->restart)where=size;
                if(!SetFilePointerEx(file,where,NULL,FILE_BEGIN))result=-1;}
            if(result)error=GetLastError();
        }
    }
    s->restart=0;
    if(!result&&listing)result=list_directory(fd,virt,!strcmp(command,"NLST")?1:!strcmp(command,"MLSD")?2:0);
    else if(!result&&writing){
        for(;;){int received=receive_data(fd,buffer,65536);if(!received)break;if(received<0){result=-1;break;}
            DWORD offset=0;while(offset<(DWORD)received){DWORD wrote=0;
                if(!WriteFile(file,buffer+offset,(DWORD)received-offset,&wrote,NULL)||!wrote){error=GetLastError();result=-1;break;}offset+=wrote;
            }if(result)break;
        }
        if(!result){IO_STATUS_BLOCK io;NTSTATUS status=NtFlushBuffersFile(file,&io);
            if(status==STATUS_PENDING){WaitForSingleObject(file,INFINITE);status=io.Status;}
            if(!NT_SUCCESS(status)){result=-1;error=(DWORD)status;}}
    }else if(!result){
        for(;;){DWORD count=0;if(!ReadFile(file,buffer,65536,&count,NULL)){error=GetLastError();result=-1;break;}
            if(!count)break;if(send_all(fd,buffer,count)){result=-1;break;}}
    }
    if(file!=INVALID_HANDLE_VALUE&&!CloseHandle(file)){result=-1;error=GetLastError();}
    free(buffer);close_socket(fd);add_count(&transfers,-1);
    if(result)reply(s,451,"Transfer failed; native error %lu",error);else reply(s,226,"Transfer complete");
}
#endif
static void transfer(session *s, const char *command, const char *virt) {
#ifdef NXDK
    transfer_native(s,command,virt);return;
#else

    int listing = !strcmp(command,"LIST") || !strcmp(command,"NLST") || !strcmp(command,"MLSD");
    int writing = !strcmp(command,"STOR") || !strcmp(command,"APPE");
    char path[PATH_CAP]; FILE *file = NULL;
    if (s->passive < 0) { reply(s,425,"Use PASV or EPSV first"); return; }
    if (!listing) {
        if (native_path(virt, path) || virt[1] == 'D' && writing) { reply(s,550,"Invalid destination"); return; }
        if (!writing) file = fopen(path, "rb");
        if (!writing && !file) { reply(s,550,"Cannot open file"); return; }
        if (!writing && s->restart && (s->restart > 0x7fffffff || fseek(file,(long)s->restart,SEEK_SET))) {
            fclose(file); s->restart=0; reply(s,551,"Cannot seek to restart position"); return;
        }
    }
    reply(s,150,"Opening binary data connection");
    int fd = data_accept(s);
    if (fd < 0) { if (file) fclose(file); return; }
    add_count(&transfers,1);
    if (writing) {
        file=fopen(path,!strcmp(command,"APPE")?"ab":s->restart?"r+b":"wb");
        if (!file || (s->restart && fseek(file,(long)s->restart,SEEK_SET))) {
            if (file) fclose(file);
            close_socket(fd); add_count(&transfers,-1); s->restart=0;
            reply(s,550,"Cannot open destination"); return;
        }
    }
    s->restart = 0;
    int result = 0;
    if (listing) result = list_directory(fd,virt,!strcmp(command,"NLST")?1:!strcmp(command,"MLSD")?2:0);
    else {
        char buffer[8192];
        if (writing) {
            int count;
            while ((count=receive_data(fd,buffer,sizeof(buffer))) > 0)
                if (fwrite(buffer,1,(size_t)count,file) != (size_t)count) { result=-1; break; }
            if (count < 0 || fflush(file)) result=-1;
        } else {
            size_t count;
            while ((count=fread(buffer,1,sizeof(buffer),file)))
                if (send_all(fd,buffer,count)) { result=-1; break; }
            if (ferror(file)) result=-1;
        }
    }
    if (file && fclose(file)) result=-1;
    close_socket(fd); add_count(&transfers,-1);
    reply(s,result?451:226,result?"Transfer failed":"Transfer complete");
#endif
}
static void handle_client(session *s) {
    char line[LINE_CAP]; strcpy(s->cwd,"/");
    socket_timeout(s->control,1); reply(s,220,"PicoLaunch FTP ready");
    while (running && !receive_line(s->control,line,sizeof(line))) {
        char *arg = strchr(line,' ');
        if (arg) { *arg++=0; while (*arg==' ') ++arg; } else arg=line+strlen(line);
        for (char *p=line; *p; ++p) *p=(char)toupper((unsigned char)*p);
        if (!strcmp(line,"USER")) { s->authenticated=0; s->user_ok=!strcmp(arg,"xbox"); reply(s,331,"Password required"); continue; }
        if (!strcmp(line,"PASS")) { s->authenticated=s->user_ok && !strcmp(arg,"xbox"); reply(s,s->authenticated?230:530,s->authenticated?"Logged in":"Login failed"); continue; }
        if (!strcmp(line,"QUIT")) { reply(s,221,"Goodbye"); break; }
        if (!strcmp(line,"SYST")) { reply(s,215,"UNIX Type: L8"); continue; }
        if (!strcmp(line,"FEAT")) { const char *features="211-Features\r\n EPSV\r\n PASV\r\n SIZE\r\n REST STREAM\r\n MLSD\r\n211 End\r\n"; send_all(s->control,features,strlen(features)); continue; }
        if (!s->authenticated) { reply(s,530,"Login first"); continue; }
        if (!strcmp(line,"TYPE") || !strcmp(line,"OPTS") || !strcmp(line,"NOOP")) { reply(s,200,"OK"); continue; }
        if (!strcmp(line,"PWD") || !strcmp(line,"XPWD")) { reply(s,257,"\"%s\"",s->cwd); continue; }
        if (!strcmp(line,"PASV") || !strcmp(line,"EPSV")) { passive_open(s,!strcmp(line,"EPSV")); continue; }
        if (!strcmp(line,"REST")) {
            char *end; unsigned long long position=strtoull(arg,&end,10);
            if (!*arg || *end || position>0x7fffffffULL) reply(s,501,"Invalid restart position");
            else { s->restart=position; reply(s,350,"Restart position accepted"); }
            continue;
        }
        char virt[PATH_CAP], path[PATH_CAP];
        const char *request = !strcmp(line,"CDUP") ? ".." : arg;
        if (!strcmp(line,"LIST") && arg[0]=='-') request=".";
        if (virtual_path(s->cwd,request,virt)) { reply(s,550,"Invalid path"); continue; }
        int native_ok = !native_path(virt,path);
        if (!strcmp(line,"CWD") || !strcmp(line,"CDUP")) {
            struct stat st;
            if (!virt[1] || native_ok && !stat(path,&st) && S_ISDIR(st.st_mode)) { strcpy(s->cwd,virt); reply(s,250,"Directory changed"); }
            else reply(s,550,"Directory unavailable");
        } else if (!strcmp(line,"LIST") || !strcmp(line,"NLST") || !strcmp(line,"MLSD") || !strcmp(line,"RETR") || !strcmp(line,"STOR") || !strcmp(line,"APPE")) transfer(s,line,virt);
        else if (!strcmp(line,"SIZE")) {
            struct stat st; if (native_ok && !stat(path,&st) && S_ISREG(st.st_mode)) reply(s,213,"%llu",(unsigned long long)st.st_size);
            else reply(s,550,"File unavailable");
        } else if (!strcmp(line,"MKD") || !strcmp(line,"XMKD")) {
#ifdef NXDK
            int ok=native_ok && CreateDirectoryA(path,NULL);
#else
            int ok=native_ok && !mkdir(path,0700);
#endif
            reply(s,ok?257:550,ok?"Directory created":"Cannot create directory");
        } else if (!strcmp(line,"RMD")) {
#ifdef NXDK
            int ok=native_ok && RemoveDirectoryA(path);
#else
            int ok=native_ok && !rmdir(path);
#endif
            reply(s,ok?250:550,ok?"Directory removed":"Cannot remove directory");
        } else if (!strcmp(line,"DELE")) reply(s,native_ok && !remove(path)?250:550,"Delete request processed");
        else if (!strcmp(line,"RNFR")) {
            struct stat st; if (native_ok && !stat(path,&st)) { strcpy(s->rename_from,path); reply(s,350,"Send RNTO"); }
            else reply(s,550,"Source unavailable");
        } else if (!strcmp(line,"RNTO")) {
            int ok=native_ok && s->rename_from[0] && !rename(s->rename_from,path);
            s->rename_from[0]=0; reply(s,ok?250:550,ok?"Renamed":"Rename failed");
        } else reply(s,502,"Command not implemented");
    }
    passive_close(s); close_socket(s->control);
}
#ifdef NXDK
static DWORD WINAPI worker(void *arg)
#else
static void *worker(void *arg)
#endif
{
    session *s=arg; handle_client(s); free(s); add_count(&clients,-1);
    return 0;
}
int ftp_run(const char *host_root, unsigned short port) {
    root_path=host_root;
    int fd=socket(AF_INET,SOCK_STREAM,0); if (fd<0) return -1;
    int yes=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_port=htons(port); addr.sin_addr.s_addr=INADDR_ANY;
    if (bind(fd,(struct sockaddr*)&addr,sizeof(addr)) || listen(fd,4)) { close_socket(fd); return -1; }
    listener=fd; listening=1;
    while (running) {
        session *s=calloc(1,sizeof(*s)); if (!s) break;
        socklen_t size=sizeof(s->peer); s->passive=-1;
        s->control=accept(fd,(struct sockaddr*)&s->peer,&size);
        if (s->control<0) { free(s); if (!running) break; continue; }
        if (add_count(&clients,1)>4) { reply(s,421,"Four clients maximum"); close_socket(s->control); free(s); add_count(&clients,-1); continue; }
#ifdef NXDK
        HANDLE thread=CreateThread(NULL,0,worker,s,0,NULL);
        if (thread) CloseHandle(thread);
        else { close_socket(s->control); free(s); add_count(&clients,-1); }
#else
        pthread_t thread;
        if (!pthread_create(&thread,NULL,worker,s)) pthread_detach(thread);
        else { close_socket(s->control); free(s); add_count(&clients,-1); }
#endif
    }
    listening=0;listener=-1;close_socket(fd);
    while(clients>0){
#ifdef NXDK
      Sleep(10);
#else
      struct timespec wait={0,10000000};nanosleep(&wait,NULL);
#endif
    }
    return 0;
}
