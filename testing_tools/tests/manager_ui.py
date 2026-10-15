"""Exercise real browser rendering with a simulated service; no radio commands."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'testing_tools/.tools/host-testlibs'))
from playwright.sync_api import sync_playwright

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser',required=True)
    args=parser.parse_args()
    state={'token':'a'*32,'phase':'Ready','error':'','active':'','auto':'','scanning':False,'scanSeconds':0,'busy':False,'bluetooth':[],'bluetoothCapacity':4,'autos':['','','',''],
        'devices':[{'id':'F46AD7BCB280-0','name':'Xbox controller','paired':True,'candidate':True,'rssi':0},
                   {'id':'112233445566-0','name':'<script>bad</script>','paired':False,'candidate':True,'rssi':-40}],
        'input':{'lx':128,'ly':128,'rx':128,'ry':128,'lt':0,'rt':0,'buttons':0,'reports':0,'delivered':0}}
    commands=[]; errors=[]
    with sync_playwright() as p:
        browser=p.chromium.launch(executable_path=args.browser,headless=True)
        page=browser.new_page(viewport={'width':1920,'height':1080})
        def route(request):
            path=request.request.url.split('http://127.0.0.1:19030',1)[1]
            if path=='/api/status': request.fulfill(content_type='application/json',body=json.dumps(state))
            elif path=='/api/command':
                assert request.request.headers['x-xinput-token']=='a'*32
                commands.append(request.request.post_data)
                request.fulfill(content_type='text/plain',body='Command accepted')
            else:
                filename={'/':'index.html','/app.css':'app.css','/app.js':'app.js'}.get(path)
                if filename: request.fulfill(path=str(ROOT/'manager'/filename),content_type={'/':'text/html','/app.css':'text/css','/app.js':'application/javascript'}[path])
                else: request.fulfill(status=404,body='Not found')
        page.route('http://127.0.0.1:19030/**',route)
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.goto('http://127.0.0.1:19030/')
        page.wait_for_selector('#connect-F46AD7BCB280-0')
        assert page.locator('#nearby h3').inner_text()=='<script>bad</script>'
        page.locator('#connect-F46AD7BCB280-0').focus()
        page.evaluate("window.savedCard=document.getElementById('connect-F46AD7BCB280-0')")
        page.wait_for_timeout(650)
        assert page.evaluate('document.activeElement.id')=='connect-F46AD7BCB280-0'
        assert page.evaluate("window.savedCard===document.getElementById('connect-F46AD7BCB280-0')"), 'Refresh must not rebuild unchanged buttons'
        page.locator('#forget-F46AD7BCB280-0').click()
        page.wait_for_timeout(650)
        assert page.evaluate('document.activeElement.id')=='dialog-cancel'
        page.locator('#dialog-cancel').click()
        assert commands==[], 'Cancel must not forget'
        page.evaluate("window.mockButtons=Array.from({length:16},()=>({pressed:false}));navigator.getGamepads=()=>[{mapping:'standard',buttons:window.mockButtons,axes:[0,0]}]")
        page.locator('#forget-F46AD7BCB280-0').click()
        page.evaluate('window.mockButtons[0].pressed=true;window.mockButtons[1].pressed=true')
        page.wait_for_timeout(250)
        assert page.locator('#dialog').is_visible(), 'Gamepad polling must not duplicate native browser activation or close the dialog'
        page.evaluate('window.mockButtons[0].pressed=false;window.mockButtons[1].pressed=false')
        page.locator('#dialog-confirm').click();page.wait_for_timeout(100)
        assert commands[-1]=='forget F46AD7BCB280-0'
        page.locator('#search-switch').click();page.wait_for_timeout(100)
        assert commands[-1]=='scan-switch'
        page.locator('#pair-112233445566-0').click()
        page.locator('#dialog-confirm').click()
        page.wait_for_timeout(100)
        assert commands[-1]=='pair 112233445566-0'
        page.locator('#auto').focus(); page.evaluate("focusMove('right')")
        assert page.locator('#auto').input_value()=='F46AD7BCB280-0'
        page.locator('#save-auto').click(); page.wait_for_timeout(100)
        assert commands[-1]=='auto F46AD7BCB280-0'
        state['phase']='Connected';state['active']='F46AD7BCB280-0';state['busy']=True
        state['input']['buttons']=65536|16384;state['input']['lx']=255
        page.wait_for_timeout(650)
        state['bluetooth']=[{'slot':1,'id':'F46AD7BCB280-0','connected':True,'phase':'Connected','input':state['input'].copy()}]
        page.wait_for_timeout(650)
        assert not page.locator('#search').is_disabled()
        assert not page.locator('#pair-112233445566-0').is_disabled()
        assert page.locator('#connect-F46AD7BCB280-0').is_disabled()
        assert 'Home' in page.locator('#buttons').inner_text()
        state['devices'][1]['paired']=True
        second=state['input'].copy();second['buttons']=8192;second['lx']=0
        state['bluetooth'].append({'slot':2,'id':'112233445566-0','connected':True,'phase':'Connected','input':second})
        state['autos']=['F46AD7BCB280-0','112233445566-0','','']
        page.wait_for_timeout(650)
        assert not page.locator('#search').is_disabled()
        for slot,device_id in [(3,'223344556677-0'),(4,'334455667788-2')]:
            state['devices'].append({'id':device_id,'name':'Ultimate 2C Bluetooth' if slot==4 else 'Xbox '+str(slot),'paired':True,'candidate':True,'rssi':0})
            values=second.copy();values['buttons']=32768 if slot==3 else 4096
            state['bluetooth'].append({'slot':slot,'id':device_id,'connected':True,'phase':'Connected','input':values})
            state['autos'][slot-1]=device_id
        page.wait_for_timeout(650)
        assert page.locator('#search').is_disabled()
        assert page.locator('#search-switch').is_disabled()
        assert page.locator('#auto-third').input_value()=='223344556677-0'
        assert page.locator('#auto-fourth').input_value()=='334455667788-2'
        page.locator('#save-auto-third').click();page.wait_for_timeout(100)
        assert commands[-1]=='auto3 223344556677-0'
        page.locator('#save-auto-fourth').click();page.wait_for_timeout(100)
        assert commands[-1]=='auto4 334455667788-2'
        page.locator('#input-controller').select_option('334455667788-2')
        assert page.locator('#buttons').inner_text()=='Y'
        page.locator('#disconnect-334455667788-2').click();page.wait_for_timeout(100)
        assert commands[-1]=='disconnect 334455667788-2'
        state['bluetooth']=state['bluetooth'][:2];state['autos'][2:]=['','']
        page.wait_for_timeout(650)
        assert not page.locator('#search').is_disabled()
        assert page.locator('#disconnect-F46AD7BCB280-0').is_enabled()
        assert page.locator('#disconnect-112233445566-0').is_enabled()
        assert page.locator('#auto-second').input_value()=='112233445566-0'
        page.locator('#save-auto-second').click();page.wait_for_timeout(100)
        assert commands[-1]=='auto2 112233445566-0'
        page.locator('#input-controller').select_option('112233445566-0')
        assert page.locator('#buttons').inner_text()=='B'
        page.wait_for_timeout(650)
        assert page.locator('#input-controller').input_value()=='112233445566-0'
        page.locator('#disconnect-112233445566-0').click();page.wait_for_timeout(100)
        assert commands[-1]=='disconnect 112233445566-0'
        state['bluetooth']=state['bluetooth'][:1]
        page.wait_for_timeout(650)
        assert page.locator('#input-controller').input_value()=='F46AD7BCB280-0'
        assert 'Home' in page.locator('#buttons').inner_text()
        state['phase']='Ready';state['active']='';state['busy']=False;state['bluetooth']=[]
        page.wait_for_timeout(650)
        page.screenshot(path=str(ROOT/'testing_tools/build/manager-preview.png'),full_page=True)
        assert not errors,errors
        browser.close()
    print('Controller Manager browser tests passed: focus, safe names, confirmations, reconnect selection and input panel')

if __name__=='__main__':main()
