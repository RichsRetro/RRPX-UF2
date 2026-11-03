"""Prepare a separate nxdk SDK for PicoX FTP (run on Linux/WSL).

Usage: python3 prepare_network_sdk.py ORIGINAL_NXDK NEW_SDK
The original SDK is read only. Build the network library AND dashboard using
NEW_SDK, rebuilding dashboard objects when switching SDK configuration.
"""
import json,pathlib,shutil,sys

CAPACITY_QUERY='''
/* PicoX: query capacity from the compiled socket table, not app headers. */
#if LWIP_SOCKET
unsigned picox_lwip_socket_capacity(void) { return sizeof(sockets) / sizeof(sockets[0]); }
#endif
'''

def prepare(source,dest):
    source=pathlib.Path(source).resolve();dest=pathlib.Path(dest).absolute()
    config=pathlib.Path(__file__).resolve().parents[1]/'third_party/network/lwipopts.h'
    if source==dest.resolve() or dest.resolve() in source.parents or source in dest.resolve().parents:
        raise ValueError('Use a separate sibling SDK directory')
    if not (source/'lib/net/lwip/src/api/sockets.c').is_file():
        raise ValueError('Source is not an nxdk SDK')
    marker=dest/'PICOX-NETWORK-SDK.json'
    if dest.exists():
        if dest.is_symlink() or not marker.is_file() or json.loads(marker.read_text()).get('source')!=str(source):
            raise ValueError('Destination exists without a matching PicoX SDK marker')
    else:
        dest.mkdir(parents=True)
        marker.write_text(json.dumps({'source':str(source),'sockets':32},indent=2))
    for child in source.iterdir():
        if child.name=='lib':continue
        target=dest/child.name
        if not target.exists():target.symlink_to(child,target_is_directory=child.is_dir())
    (dest/'lib').mkdir(exist_ok=True)
    for child in (source/'lib').iterdir():
        if child.name in ('net','libnxdk_net.lib'):continue
        target=dest/'lib'/child.name
        if not target.exists():target.symlink_to(child,target_is_directory=child.is_dir())
    if not (dest/'lib/net').exists():
        shutil.copytree(source/'lib/net',dest/'lib/net',ignore=shutil.ignore_patterns('*.obj','*.d','.git'))
    for target in [dest/'lib/net/nforceif/include/lwipopts.h',dest/'lib/net/lwip/src/api/sockets.c']:
        if target.is_symlink() or dest.resolve() not in target.resolve().parents:
            raise ValueError('Network output escaped dedicated SDK')
    target=dest/'lib/net/nforceif/include/lwipopts.h'
    if target.read_bytes()!=config.read_bytes():target.write_bytes(config.read_bytes())
    target=dest/'lib/net/lwip/src/api/sockets.c';text=target.read_text()
    if 'picox_lwip_socket_capacity' not in text:target.write_text(text+CAPACITY_QUERY)
    elif CAPACITY_QUERY.strip() not in text:raise ValueError('Unexpected existing capacity query')
    driver=dest/'lib/net/nvnetdrv/nvnetdrv_lwip.c'
    if driver.is_symlink() or dest.resolve() not in driver.resolve().parents:
        raise ValueError('Driver patch escaped dedicated SDK')
    replacement=(pathlib.Path(__file__).resolve().parents[1]/'third_party/network/rx_copy.c.inc').read_text()
    text=driver.read_text()
    start=text.index('void rx_callback (')
    body=text.index('{',start);end=body+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    # Keep the driver's original licence and all other functions unchanged.
    text=text[:start]+replacement[replacement.index('void rx_callback ('):].rstrip()+text[end:]
    query='unsigned picox_lwip_rx_copy_mode(void) { return 1; }'
    if 'picox_lwip_rx_copy_mode' not in text:text+='\n/* PicoX compiled driver-mode query. */\n'+query+'\n'
    elif query not in text:raise ValueError('Unexpected RX mode query')
    if driver.read_text()!=text:driver.write_text(text)
    print('Prepared isolated 32-socket SDK with owned-copy RX and compiled mode query:',dest)

if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit(__doc__)
    prepare(*sys.argv[1:])
