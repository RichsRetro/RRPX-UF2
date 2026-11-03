"""Regression tests for isolated SDK preparation; run on Linux/WSL."""
import importlib.util,pathlib,tempfile,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('network',root/'tools/prepare_network_sdk.py')
network=importlib.util.module_from_spec(spec);spec.loader.exec_module(network)
with tempfile.TemporaryDirectory() as temporary:
    base=pathlib.Path(temporary);source=base/'original';dest=base/'dedicated'
    for p,data in {'lib/net/lwip/src/api/sockets.c':'/* original BSD source fixture */\n',
                   'lib/net/nvnetdrv/nvnetdrv_lwip.c':'/* Original BSD driver fixture */\nvoid rx_callback (void *buffer, uint16_t length) { original_zero_copy(buffer, length); }\n',
                   'lib/net/nforceif/include/lwipopts.h':'/* original config */\n',
                   'lib/net/lwip/src/api/sockets.obj':'OLD OBJECT',
                   'lib/libnxdk_net.lib':'OLD NETWORK LIBRARY',
                   'lib/libhal.lib':'UNCHANGED HAL', 'bin/compiler':'tool'}.items():
        f=source/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(data)
    before={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()}
    network.prepare(source,dest)
    config=dest/'lib/net/nforceif/include/lwipopts.h'
    assert '#define MEMP_NUM_NETCONN 32' in config.read_text()
    assert 'sizeof(sockets) / sizeof(sockets[0])' in (dest/'lib/net/lwip/src/api/sockets.c').read_text()
    assert not (dest/'lib/net/lwip/src/api/sockets.obj').exists()
    assert not (dest/'lib/libnxdk_net.lib').exists()
    driver=dest/'lib/net/nvnetdrv/nvnetdrv_lwip.c'
    assert 'pbuf_take(p,' in driver.read_text()
    assert 'unsigned picox_lwip_rx_copy_mode(void) { return 1; }' in driver.read_text()
    driver_stamp=driver.stat().st_mtime_ns
    stamp=config.stat().st_mtime_ns
    network.prepare(source,dest)
    assert config.stat().st_mtime_ns==stamp
    assert driver.stat().st_mtime_ns==driver_stamp
    after={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()}
    assert before==after
    for unsafe in [source,source/'nested',base,base/'unmarked']:
        if unsafe.name=='unmarked':unsafe.mkdir()
        try:network.prepare(source,unsafe)
        except ValueError:pass
        else:raise AssertionError('Unsafe destination accepted')
print('PASS: source SDK unchanged, stale network binaries excluded, 32-slot config/query, repeatable setup and unsafe destination rejection')
