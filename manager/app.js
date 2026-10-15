'use strict';
var state=null, token='', dialogAction=null, refreshing=false, sending=false;
var renderedCards='';
var $=function(id){return document.getElementById(id);};
function message(text){$('message').textContent=text;}
function command(text){
  if(sending)return Promise.resolve(false);
  sending=true;
  return fetch('/api/command',{method:'POST',headers:{'X-XInput-Token':token,'Content-Type':'text/plain'},body:text})
    .then(function(r){return r.text().then(function(text){message(text);return r.ok;});})
    .catch(function(){message('The service is unavailable. Launch Controller Manager again.');return false;})
    .then(function(ok){sending=false;refresh();return ok;});
}
function confirmAction(title,text,label,fn){
  $('dialog-title').textContent=title;$('dialog-text').textContent=text;$('dialog-confirm').textContent=label;
  dialogAction=fn;$('dialog').hidden=false;$('dialog-cancel').focus();
}
function closeDialog(){ $('dialog').hidden=true;dialogAction=null;$('search').focus(); }
$('dialog-cancel').onclick=closeDialog;
$('dialog-confirm').onclick=function(){var fn=dialogAction;closeDialog();if(fn)fn();};
$('search').onclick=function(){command('scan');};
$('search-switch').onclick=function(){command('scan-switch');};
$('disconnect').onclick=function(){command('disconnect');};
$('save-auto').onclick=function(){command('auto '+$('auto').value);};
$('save-auto-second').onclick=function(){command('auto2 '+$('auto-second').value);};
$('save-auto-third').onclick=function(){command('auto3 '+$('auto-third').value);};
$('save-auto-fourth').onclick=function(){command('auto4 '+$('auto-fourth').value);};
$('input-controller').onchange=function(){renderInput();};
function bluetoothLinks(){return state.bluetooth||[];}
function controllerLink(id){return bluetoothLinks().filter(function(c){return c.id===id;})[0];}
function button(text,key,action,disabled){
  var b=document.createElement('button');b.textContent=text;b.id=key;b.disabled=!!disabled;b.onclick=action;return b;
}
function renderDevices(){
  var cardsSignature=JSON.stringify([state.devices.map(function(d){return[d.id,d.name,d.paired,d.candidate];}),state.active,bluetoothLinks().map(function(c){return[c.id,c.connected,c.phase];}),state.phase==='Connected',state.busy,state.scanning]);
  if(renderedCards!==cardsSignature){
  renderedCards=cardsSignature;
  var focused=document.activeElement&&document.activeElement.id;
  var saved=$('saved'), nearby=$('nearby');saved.textContent='';nearby.textContent='';
  var savedCount=0,nearbyCount=0;
  state.devices.forEach(function(d){
    var link=controllerLink(d.id);
    var current=link?link.connected:!state.bluetooth&&state.active===d.id&&state.phase==='Connected';
    var connecting=link?!link.connected:!state.bluetooth&&state.active===d.id&&state.busy&&!current;
    var card=document.createElement('article');card.className='device'+(current?' connected':'');
    var info=document.createElement('div'),h=document.createElement('h3'),p=document.createElement('p');
    h.textContent=d.name;p.textContent=d.id.replace(/(.{2})(?=.{2})/g,'$1 ').replace(' -','-')+' · '+(current?'Connected':d.paired?'Paired':d.candidate?'Controller found':'Unsupported device');
    info.appendChild(h);info.appendChild(p);card.appendChild(info);
    var actions=document.createElement('div');actions.className='actions';
    if(d.paired){
      savedCount++;
      actions.appendChild(button(current?'Connected':connecting?'Connecting':'Connect','connect-'+d.id,function(){command('connect '+d.id);},current||connecting||bluetoothLinks().length>=(state.bluetoothCapacity||4)));
      if(current)actions.appendChild(button('Disconnect','disconnect-'+d.id,function(){command('disconnect '+d.id);}));
      var forget=button('Forget','forget-'+d.id,function(){confirmAction('Forget this controller?','This removes its saved pairing from the PS5. To use it again, hold its Pair button and pair again.','Forget',function(){command('forget '+d.id);});});
      forget.className='secondary';actions.appendChild(forget);
    }else{
      nearbyCount++;
      actions.appendChild(button('Pair','pair-'+d.id,function(){confirmAction('Pair '+d.name,'Keep the controller in pairing mode with its light flashing rapidly. Its input profile will be checked before the pairing is saved.','Pair controller',function(){command('pair '+d.id);});},!d.candidate||bluetoothLinks().length>=(state.bluetoothCapacity||4)));
    }
    card.appendChild(actions);(d.paired?saved:nearby).appendChild(card);
  });
  if(!savedCount){var p=document.createElement('p');p.textContent='No saved controllers yet.';saved.appendChild(p);}
  if(!nearbyCount){var p=document.createElement('p');p.textContent=state.scanning?'Searching… Hold Pair on your controller.':'Hold Pair on your controller, then choose Search nearby.';nearby.appendChild(p);}
  if(focused&&$(focused)&&$('dialog').hidden)$(focused).focus();
  }
  var preferences=state.autos||[state.auto||'','','',''];
  ['auto','auto-second','auto-third','auto-fourth'].forEach(function(key,index){
    var select=$(key),previous=select.value;
    var signature=state.devices.filter(function(d){return d.paired;}).map(function(d){return d.id;}).join(',');
    if(select.dataset.signature!==signature){
      select.textContent='';var off=document.createElement('option');off.value='off';off.textContent='Off';select.appendChild(off);
      state.devices.filter(function(d){return d.paired;}).forEach(function(d){var o=document.createElement('option');o.value=d.id;o.textContent=d.name+' / '+d.id;select.appendChild(o);});
      select.dataset.signature=signature;select.value=previous||preferences[index]||'off';
    }
    if(select.dataset.saved!==preferences[index]){select.value=preferences[index]||'off';select.dataset.saved=preferences[index];}
  });
}

function renderInput(){
  var select=$('input-controller'),links=bluetoothLinks().filter(function(c){return c.connected;});
  var signature=links.map(function(c){return c.id;}).join(',');
  if(select.dataset.signature!==signature){
    var previous=select.value;select.textContent='';
    links.forEach(function(c){var option=document.createElement('option');option.value=c.id;option.textContent='Bluetooth '+c.slot+' / '+c.id;select.appendChild(option);});
    if(!links.length){var option=document.createElement('option');option.value='';option.textContent='No Bluetooth controllers connected';select.appendChild(option);}
    select.dataset.signature=signature;
    if(links.some(function(c){return c.id===previous;}))select.value=previous;
  }
  var link=controllerLink(select.value);
  var v=link?link.input:state.input,connected=link?link.connected:!state.bluetooth&&state.phase==='Connected';
  $('input-status').textContent=connected?'Receiving controller inputs':'Connect a controller to test its inputs.';
  $('left-stick').style.transform='translate('+((v.lx-128)/128*37)+'px,'+((v.ly-128)/128*37)+'px)';
  $('right-stick').style.transform='translate('+((v.rx-128)/128*37)+'px,'+((v.ry-128)/128*37)+'px)';
  $('lt').value=v.lt;$('rt').value=v.rt;
  var bits=[[2,'LS'],[4,'RS'],[8,'Menu'],[16,'Up'],[32,'Right'],[64,'Down'],[128,'Left'],[256,'LT'],[512,'RT'],[1024,'LB'],[2048,'RB'],[4096,'Y'],[8192,'B'],[16384,'A'],[32768,'X'],[65536,'Home'],[1048576,'View / Share']];
  $('buttons').textContent=bits.filter(function(p){return(v.buttons&p[0])!==0;}).map(function(p){return p[1];}).join(' · ')||'No buttons pressed';
}
var usbSignature='';
function renderUSB(){
  var devices=state.usb||[];
  $('usb-hint').textContent=state.usbEnabled?
    'Plug in up to four supported USB controllers. Select a PS5 user for each; unplug to disconnect.':
    'USB support is unavailable. Close any older USB controller payload, then restart Controller Manager.';
  var signature=JSON.stringify(devices.map(function(d){return[d.slot,d.name,d.ready,d.delivered>0];}));
  if(signature===usbSignature)return;
  usbSignature=signature;
  var root=$('usb-devices');root.textContent='';
  devices.forEach(function(d){
    var card=document.createElement('article');card.className='device'+(d.ready?' connected':'');
    var h=document.createElement('h3');h.textContent='USB '+d.slot+' · '+d.name;
    var p=document.createElement('p');p.textContent=!d.ready?'Initializing…':d.delivered?'Connected — inputs accepted':'Ready — select your PS5 user';
    card.appendChild(h);card.appendChild(p);root.appendChild(card);
  });
  if(!devices.length){var p=document.createElement('p');p.textContent='No USB controllers connected.';root.appendChild(p);}
}
function refresh(){
  if(refreshing)return;refreshing=true;
  fetch('/api/status',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error();return r.json();}).then(function(next){
    state=next;token=state.token;$('phase').textContent=state.phase;
    var connected=bluetoothLinks().filter(function(c){return c.connected;}).length;
    $('connection').textContent=connected?connected+' Bluetooth controller'+(connected===1?'':'s')+' connected':state.phase==='Connected'?'Controller connected':'Service running';
    $('connection').className='badge'+(connected?' connected':'');
    $('hint').textContent=state.scanning?'Searching for '+state.scanSeconds+' seconds. Hold Pair on your controller.':state.phase==='Connected'?'Your controller keeps working when you close this screen.':'Use your DualSense to add or connect a controller.';
    $('search').disabled=state.scanning||bluetoothLinks().length>=(state.bluetoothCapacity||4);$('search-switch').disabled=$('search').disabled;$('disconnect').disabled=!state.busy;
    if(state.error)message(state.error);renderDevices();renderInput();renderUSB();
  }).catch(function(){$('connection').textContent='Service unavailable';message('Launch Controller Manager again to start the service.');}).then(function(){refreshing=false;});
}
function focusMove(direction){
  var selected=document.activeElement;
  if(selected&&selected.tagName==='SELECT'&&(direction==='left'||direction==='right')){
    selected.selectedIndex=Math.max(0,Math.min(selected.options.length-1,selected.selectedIndex+(direction==='left'?-1:1)));return;
  }
  var root=$('dialog').hidden?document:$('dialog');
  var items=Array.prototype.slice.call(root.querySelectorAll('button:not(:disabled),select')).filter(function(e){return e.offsetParent!==null;});
  if(!items.length)return;
  var current=document.activeElement, index=items.indexOf(current);
  if(index<0){items[0].focus();return;}
  var r=current.getBoundingClientRect(),cx=r.left+r.width/2,cy=r.top+r.height/2,best=null,score=Infinity;
  items.forEach(function(e){if(e===current)return;var q=e.getBoundingClientRect(),dx=q.left+q.width/2-cx,dy=q.top+q.height/2-cy;
    var main=direction==='left'?-dx:direction==='right'?dx:direction==='up'?-dy:dy;
    var side=direction==='left'||direction==='right'?Math.abs(dy):Math.abs(dx);
    var s=main+side*3;if(main>5&&s<score){score=s;best=e;}});
  if(best)best.focus();
}
document.addEventListener('keydown',function(e){
  if(document.activeElement.tagName==='SELECT')return;
  var directions={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right'};
  if(directions[e.key]){e.preventDefault();focusMove(directions[e.key]);}
  if(e.key==='Escape'&&!$('dialog').hidden){e.preventDefault();closeDialog();}
});
/* The PS5 browser already handles DualSense navigation and activation.
   Polling Gamepad alongside it causes two independent focus/activation paths.
   Use native browser events; keyboard navigation above remains available on PC. */
$('search').focus();refresh();setInterval(refresh,500);
