"""Host integration: HTTP bounds, commands during a session, and private bond storage."""
import ctypes
import http.client
import json
import os
from pathlib import Path
import socket
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
ID = 'F46AD7BCB280-0'
ID2 = '112233445566-0'
ID3 = '223344556677-0'
ID4 = '334455667788-2'
ID5 = '445566778899-0'

def record():
    b = bytearray(64)
    b[:8] = b'XBSC0001'
    b[15:21] = b[54:60] = bytes.fromhex('80b2bcd76af4')
    b[22:38] = bytes(range(16))
    b[62] = 16
    return bytes(b)

def run():
    with tempfile.TemporaryDirectory(prefix='xinput-manager-') as temp:
        directory = Path(temp)
        bonds = directory / 'bonds'
        bonds.mkdir(mode=0o700)
        legacy = directory / 'bt-bond-trial.bin'
        legacy.write_bytes(record()); legacy.chmod(0o600)
        common = ['cc','-std=c11','-Wall','-Wextra','-Werror','-Isrc/app','-Isrc/platform','-Isrc/usb','-Isrc/bluetooth','-Itesting_tools/src/diagnostics','-Itesting_tools/build']
        subprocess.run(common + ['-shared','-fPIC','src/bluetooth/bt_store.c','testing_tools/tests/manager_stubs.c','-o',str(directory/'store.so')],cwd=ROOT,check=True)
        store = ctypes.CDLL(str(directory/'store.so'))
        path = lambda p: os.fsencode(p)
        assert store.bt_store_import(path(bonds),path(legacy)) == 1
        saved = bonds / (ID+'.bin')
        assert saved.read_bytes() == record() and saved.stat().st_mode & 0o777 == 0o600
        legacy.write_bytes(record()[:22]+b'Z'*16+record()[38:])
        assert store.bt_store_import(path(bonds),path(legacy)) == 1
        assert saved.read_bytes() == record(), 'Import must preserve existing bonds'
        out = ctypes.create_string_buffer(192)
        for invalid in [b'../../evil',b'f46ad7bcb280-0',b'F46AD7BCB280-3',b'F46AD7BCB280-0/']:
            assert store.bt_store_path(path(bonds),invalid,out,192) == 0
            assert store.bt_store_forget(path(bonds),invalid) == 0
        buffer = ctypes.create_string_buffer(64)
        assert store.bt_store_read(path(saved),buffer) == 1
        saved.chmod(0o644)
        assert store.bt_store_read(path(saved),buffer) == 0
        saved.chmod(0o600)
        link = directory/'link'; link.symlink_to(saved)
        assert store.bt_store_read(path(link),buffer) == 0
        assert store.bt_store_auto(path(bonds),ID.encode()) == 1
        auto = ctypes.create_string_buffer(15)
        assert store.bt_store_auto_read(path(bonds),auto) == 1 and auto.value == ID.encode()
        assert store.bt_store_auto(path(bonds),b'') == 1
        assert store.bt_store_auto_read(path(bonds),auto) == 0 and auto.value == b''

        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0)); port=reserve.getsockname()[1]
        for device_id in [ID2,ID3,ID4,ID5]:
            value=bytearray(record()); address=bytes.fromhex(device_id[:12])[::-1]
            value[15:21]=value[54:60]=address
            if device_id.endswith('-2'):
                value[:8]=b'XBCL0001';value[21]=value[60]=2;value[61]=4
            file=bonds/(device_id+'.bin');file.write_bytes(value);file.chmod(0o600)
        exe = directory/'service' 
        subprocess.run(common + ['-DMANAGER_HOST_TEST','-DMANAGER_USB=1',f'-DMANAGER_PORT={port}',f'-DMANAGER_STATE_DIR="{temp}"',
            'src/app/manager_service.c','src/app/manager_http.c','src/bluetooth/bt_store.c','src/bluetooth/bt_advert.c','testing_tools/tests/manager_stubs.c','-o',str(exe)],cwd=ROOT,check=True)
        native_request = directory/'native-request'
        drop_request = directory/'drop-slot'
        process = subprocess.Popen([str(exe)], env={**os.environ,
            'XINPUT_TEST_NATIVE_REQUEST': str(native_request), 'XINPUT_TEST_DROP_SLOT':str(drop_request)})
        try:
            def request(method,path='/',body=None,headers=None):
                client=http.client.HTTPConnection('127.0.0.1',port,timeout=2)
                client.request(method,path,body,headers or {})
                response=client.getresponse(); data=response.read(); code=response.status; client.close()
                return code,data
            for _ in range(100):
                try: code,data=request('GET','/api/status'); break
                except ConnectionRefusedError: time.sleep(.02)
            assert code == 200
            state=json.loads(data); token=state['token']
            assert state['usbEnabled'] and len(state['usb']) == 4
            assert [device['slot'] for device in state['usb']] == [1,2,3,4]
            assert ID in [device['id'] for device in state['devices']]
            assert len(token)==32 and 'ltk' not in data.decode().lower()
            headers={'X-XInput-Token':token,'Origin':f'http://127.0.0.1:{port}'}
            assert request('GET')[0]==200
            assert request('GET','/?open=123-456')[0]==200
            assert request('GET','/app.js?v=123')[0]==200
            assert b'/app.js?v=' in request('GET')[1]
            for asset in ['/app.css','/app.js']: assert request('GET',asset)[0]==200
            assert request('POST','/api/command','disconnect')[0]==403
            assert request('POST','/api/command','disconnect',{**headers,'Origin':'http://evil.example'})[0]==403
            assert request('GET','/api/status',headers={'Host':f'evil.example:{port}'})[0]==403
            assert request('POST','/api/command','forget ../../evil',headers)[0] in (400,404)
            assert request('POST','/api/command','x'*128,headers)[0]==413
            assert request('POST','/api/command','disconnect\x00',headers)[0]==400
            assert request('POST','/api/command','connect '+ID,headers)[0]==200
            time.sleep(.15)
            state=json.loads(request('GET','/api/status')[1])
            assert state['phase']=='Connected' and state['input']['reports']==42 and state['auto']==ID
            assert state['usbEnabled'] and state['usb'][0]['ready'] and state['usb'][0]['delivered']>0
            usb_before=state['usb'][0]['delivered']
            time.sleep(0.1)
            assert json.loads(request('GET','/api/status')[1])['usb'][0]['delivered']>usb_before, 'USB polling must continue during Bluetooth connection'
            process.send_signal(signal.SIGHUP)
            time.sleep(.05)
            assert process.poll() is None, 'Closing a launcher must not stop the controller service'
            # Search must not disconnect the first Xbox.
            assert request('POST','/api/command','scan-switch',headers)[0]==200
            time.sleep(.1)
            assert len(json.loads(request('GET','/api/status')[1])['bluetooth'])==1
            assert request('POST','/api/command','connect '+ID2,headers)[0]==200
            time.sleep(.15)
            state=json.loads(request('GET','/api/status')[1])
            assert [c['id'] for c in state['bluetooth']]==[ID,ID2]
            assert [c['input']['lx'] for c in state['bluetooth']]==[200,50]
            assert state['autos']==[ID,ID2,'','']
            assert request('POST','/api/command','connect '+ID,headers)[0]==409
            for device_id in [ID3,ID4]:
                assert request('POST','/api/command','connect '+device_id,headers)[0]==200
                time.sleep(.1)
            state=json.loads(request('GET','/api/status')[1])
            assert state['bluetoothCapacity']==4 and [c['id'] for c in state['bluetooth']]==[ID,ID2,ID3,ID4]
            assert state['autos']==[ID,ID2,ID3,ID4]
            for slot,device_id in enumerate([ID,ID2,ID3,ID4]):
                filename='auto.txt' if slot==0 else f'auto{slot+1}.txt'
                assert (bonds/filename).read_text()==device_id
                out=ctypes.create_string_buffer(15)
                assert store.bt_store_auto_read_slot(path(bonds),slot,out)==1 and out.value==device_id.encode()
            assert request('POST','/api/command','connect '+ID5,headers)[0]==409
            assert request('POST','/api/command','auto3 '+ID4,headers)[0]==409
            assert request('POST','/api/command','auto4 '+ID3,headers)[0]==409
            assert request('POST','/api/command','auto5 '+ID5,headers)[0]==400
            for device_id in [ID3,ID4]:
                assert request('POST','/api/command','disconnect '+device_id,headers)[0]==200
                time.sleep(.1)
            state=json.loads(request('GET','/api/status')[1])
            assert [c['id'] for c in state['bluetooth']]==[ID,ID2] and state['autos']==[ID,ID2,'','']
            assert request('POST','/api/command','auto2 '+ID,headers)[0]==409
            assert request('POST','/api/command','disconnect '+ID,headers)[0]==200
            time.sleep(.1)
            state=json.loads(request('GET','/api/status')[1])
            assert [c['id'] for c in state['bluetooth']]==[ID2] and state['autos']==['',ID2,'','']
            assert request('POST','/api/command','connect '+ID,headers)[0]==200
            time.sleep(.1)
            drop_request.write_text('1')
            time.sleep(.1)
            state=json.loads(request('GET','/api/status')[1])
            assert [c['id'] for c in state['bluetooth']]==[ID] and state['autos']==[ID,ID2,'','']
            time.sleep(5.2)
            state=json.loads(request('GET','/api/status')[1])
            assert len(state['bluetooth'])==2, 'Second Xbox must reconnect without disturbing the first'
            native_request.touch();time.sleep(.15)
            state=json.loads(request('GET','/api/status')[1])
            assert not state['bluetooth'] and state['phase']=='Bluetooth paused for DualSense'
            assert state['autos']==[ID,ID2,'','']
            assert request('POST','/api/command','connect '+ID,headers)[0]==200
            time.sleep(.1)
            assert request('POST','/api/command','disconnect',headers)[0]==200
            time.sleep(.15)
            state=json.loads(request('GET','/api/status')[1])
            assert state['phase']=='Ready' and state['auto']=='' and state['active']=='' and state['input']['lx']==128
            assert request('POST','/api/command','connect '+ID,headers)[0]==200
            time.sleep(.1)
            native_request.touch()
            time.sleep(.15)
            yielded=json.loads(request('GET','/api/status')[1])
            assert yielded['phase']=='Bluetooth paused for DualSense' and yielded['auto']==ID and yielded['active']==''
            usb_before=yielded['usb'][0]['delivered']
            time.sleep(2.2)
            yielded=json.loads(request('GET','/api/status')[1])
            assert yielded['phase']=='Bluetooth paused for DualSense', 'Native reconnect must pause automatic Bluetooth retry'
            assert yielded['usb'][0]['delivered']>usb_before, 'USB must continue while radio is yielded'
            assert request('POST','/api/command','connect '+ID,headers)[0]==200
            time.sleep(.1)
            assert json.loads(request('GET','/api/status')[1])['phase']=='Connected', 'Explicit Connect must resume a native-radio pause'
            assert request('POST','/api/command','disconnect',headers)[0]==200
            time.sleep(.1)
            assert request('POST','/api/command','scan',headers)[0]==200
            time.sleep(.1)
            scanning=json.loads(request('GET','/api/status')[1])
            assert scanning['scanning'] and scanning['busy'], 'Disconnect must remain available during a search'
            assert request('POST','/api/command','disconnect',headers)[0]==200
            time.sleep(.15)
            assert json.loads(request('GET','/api/status')[1])['phase']=='Ready'
            def raw(parts):
                with socket.create_connection(('127.0.0.1',port),timeout=4) as sock:
                    for part in parts: sock.sendall(part); time.sleep(.02)
                    data=b''
                    while True:
                        more=sock.recv(32768)
                        if not more: return data
                        data+=more
            prefix=f'POST /api/command HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nX-XInput-Token: {token}\r\n'.encode()
            assert b' 400 ' in raw([prefix+b'Content-Length: 10\r\nContent-Length: 10\r\n\r\ndisconnect'])
            assert b' 400 ' in raw([prefix+b'Transfer-Encoding: chunked\r\n\r\n'])
            assert b' 200 ' in raw([prefix+b'Content-Length: 10\r\n\r\ndi',b'scon',b'nect'])
            assert raw([b'GET /api/status HTTP/1.1\r\n'])==b'', 'Partial-header timeout must close client'
            with socket.create_connection(('127.0.0.1',port)) as sock:
                sock.sendall(f'GET /app.js HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n\r\n'.encode())
            time.sleep(.1)
            assert process.poll() is None and request('GET','/api/status')[0]==200
            assert request('POST','/api/command','forget '+ID,headers)[0]==200
            time.sleep(.15)
            assert not saved.exists() and legacy.exists()
            assert store.bt_store_import(path(bonds),path(legacy)) == 1
            assert not saved.exists(), 'Forgotten legacy bonds must not return on service restart'
        finally:
            process.terminate(); process.wait(timeout=3)
            assert process.returncode == 0, 'Service termination must take its cleanup path'
    print('Manager HTTP, session cancellation, reconnect preferences, and private bond storage tests passed')

if __name__=='__main__': run()
