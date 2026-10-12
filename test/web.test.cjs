const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const state={light:[100,100,100,100],waterTemp:20,ambientTemp:22,humidity:60,pump:0,pumpMode:'manual',remainingSeconds:0,red:100,blue:200,actualRed:100,actualBlue:200,autoLight:false,lightTarget:50,onMinutes:5,offMinutes:5,floors:1,ssid:'Garden',wifiMode:'ready',wifiError:'',wifiStatus:'Connected',ip:'192.168.4.1',uptime:1};
function harness() {
  const elements=new Map();
  const html=fs.readFileSync(path.join(__dirname,'../web/index.html'),'utf8');
  for(const match of html.matchAll(/id="([^"]+)"/g)) elements.set(match[1], {
    value:'0',checked:false,disabled:false,textContent:'',style:{},classList:{add(){},remove(){},toggle(){}},
  });
  const badge={textContent:''}; const calls=[]; let handler=async()=>({...state});
  const context={console,AbortController,setTimeout:()=>1,clearTimeout(){},setInterval:()=>1,clearInterval(){},
    localStorage:{getItem:()=>null,setItem(){}},
    document:{getElementById:id=>{assert.ok(id==='toast'||elements.has(id),'Missing HTML element '+id);return elements.get(id)||null;},
      querySelector:()=>badge,querySelectorAll:()=>[...elements.values()],addEventListener(){},
      createElement:()=>({classList:{add(){},remove(){}}}),body:{appendChild(el){elements.set(el.id,el);}}},
    fetch:async(url,options)=>{calls.push({url,options});const result=await handler(url,options);return {ok:!result.error,status:result.error?400:200,text:async()=>JSON.stringify(result)};}
  };
  vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/app.js'),'utf8'),context);
  return {context,elements,calls,setHandler(fn){handler=fn;},run:code=>vm.runInContext(code,context)};
}
test('initial snapshot restores settings without overwriting later edits',()=>{
  const h=harness();h.context.updateUI(state);
  assert.equal(h.elements.get('slider-red-0').value,100);
  h.elements.get('slider-red-0').value='42';h.context.updateUI({...state,red:200});
  assert.equal(h.elements.get('slider-red-0').value,'42');
});
test('pump POST succeeds immediately and returns confirmed state',async()=>{
  const h=harness();h.context.updateUI(state);
  h.setHandler(async()=>({...state,pump:1}));
  assert.equal(await h.context.command(null,'/api/pump',{state:1},'Pump ON'),true);
  assert.equal(h.elements.get('pump-toggle').checked,true);
  assert.deepEqual(JSON.parse(h.calls[0].options.body),{state:1});
});
test('API errors remain errors and release disabled controls',async()=>{
  const h=harness();h.context.updateUI(state);h.setHandler(async()=>({error:'Invalid duration'}));
  assert.equal(await h.context.command(null,'/api/pump/schedule',{},'Saved'),false);
  assert.equal(h.elements.get('toast').textContent,'Invalid duration');
  assert.equal(h.elements.get('pump-toggle').disabled,false);
});
test('timer rejects invalid and fractional durations before sending',async()=>{
  const h=harness();h.elements.get('sched-hours').value='0';h.elements.get('sched-mins').value='0';
  await h.context.confirmSchedule(null);assert.equal(h.calls.length,0);
  h.elements.get('sched-mins').value='1.5';await h.context.confirmSchedule(null);assert.equal(h.calls.length,0);
});
test('cancel schedule sends a server cancellation',async()=>{
  const h=harness();await h.context.cancelSchedule(null);
  assert.equal(h.calls[0].url,'/api/pump/schedule');assert.equal(JSON.parse(h.calls[0].options.body).mode,'manual');
});
test('stale poll cannot revert a later pump command',async()=>{
  const h=harness();h.context.updateUI(state);let resolve;
  h.setHandler(url=>url==='/api/sensors'?new Promise(r=>resolve=r):Promise.resolve({...state,pump:1}));
  const pending=h.context.fetchSensors();
  await h.context.command(null,'/api/pump',{state:1},'ON');
  resolve({...state,pump:0});await pending;
  assert.equal(h.elements.get('pump-toggle').checked,true);
});
test('parallel commands are suppressed while the first is pending',async()=>{
  const h=harness();let resolve;h.setHandler(()=>new Promise(r=>resolve=r));
  const first=h.context.command(null,'/api/pump',{state:1},'ON');
  assert.equal(await h.context.command(null,'/api/pump',{state:0},'OFF'),false);
  assert.equal(h.calls.length,1);resolve({...state,pump:1});await first;
});
test('connection failure does not report success',async()=>{
  const h=harness();h.setHandler(async()=>{throw new Error('Offline');});
  assert.equal(await h.context.command(null,'/api/pump',{state:1},'ON'),false);
  assert.equal(h.elements.get('toast').textContent,'Offline');
});

test('missing credentials show only setup and retain an unfinished form during polling',()=>{
  const h=harness();h.context.updateUI({...state,wifiMode:'setup',ssid:''});
  assert.equal(h.elements.get('garden-hub').hidden,true);
  assert.equal(h.elements.get('wifi-setup').hidden,false);
  assert.equal(h.elements.get('setup-form').hidden,false);
  h.elements.get('setup-ssid').value='Unsaved network';
  h.context.updateUI({...state,wifiMode:'setup',ssid:''});
  assert.equal(h.elements.get('setup-ssid').value,'Unsaved network');
});
test('connecting hides both credentials and dashboard; failure restarts a clean form',()=>{
  const h=harness();h.context.updateUI({...state,wifiMode:'setup',ssid:''});
  h.elements.get('setup-password').value='wrong-password';
  h.context.updateUI({...state,wifiMode:'connecting'});
  assert.equal(h.elements.get('garden-hub').hidden,true);
  assert.equal(h.elements.get('setup-form').hidden,true);
  assert.equal(h.elements.get('setup-progress').hidden,false);
  h.context.updateUI({...state,wifiMode:'setup',ssid:'',wifiError:'Could not connect'});
  assert.equal(h.elements.get('setup-password').value,'');
  assert.equal(h.elements.get('setup-ssid').value,'');
  assert.equal(h.elements.get('setup-error').textContent,'Could not connect');
  assert.equal(h.elements.get('setup-form').hidden,false);
});
test('successful connection reveals the garden and hides onboarding',()=>{
  const h=harness();h.context.updateUI({...state,wifiMode:'connecting'});
  h.context.updateUI(state);
  assert.equal(h.elements.get('garden-hub').hidden,false);
  assert.equal(h.elements.get('wifi-setup').hidden,true);
  assert.equal(h.elements.get('wifi-ssid').value,'Garden');
});
test('setup Save sends credentials and leaves the form after the device accepts them',async()=>{
  const h=harness();h.context.updateUI({...state,wifiMode:'setup',ssid:''});
  h.elements.get('setup-ssid').value='New network';h.elements.get('setup-password').value='new-password';
  h.setHandler(async()=>({...state,wifiMode:'connecting',ssid:'New network'}));
  await h.context.saveSetupWiFi({preventDefault(){}},{querySelector(){return h.elements.get('setup-save');}});
  assert.equal(h.calls[0].url,'/api/wifi');
  assert.deepEqual(JSON.parse(h.calls[0].options.body),{ssid:'New network',password:'new-password'});
  assert.equal(h.elements.get('setup-form').hidden,true);
  assert.equal(h.elements.get('setup-password').value,'');
});
test('a rejected Save keeps the credential form available for correction',async()=>{
  const h=harness();h.context.updateUI({...state,wifiMode:'setup',ssid:''});
  h.elements.get('setup-ssid').value='New network';h.elements.get('setup-password').value='short';
  h.setHandler(async()=>({error:'Password too short'}));
  await h.context.saveSetupWiFi({preventDefault(){}},{querySelector(){return h.elements.get('setup-save');}});
  assert.equal(h.elements.get('setup-form').hidden,false);
  assert.equal(h.elements.get('setup-ssid').value,'New network');
  assert.equal(h.elements.get('setup-save').disabled,false);
});

test('missing wifiMode shows a usable form and an update message',()=>{
 const h=harness(); const old={...state}; delete old.wifiMode;
 h.context.updateUI(old);
 assert.equal(h.elements.get('setup-form').hidden,false);
 assert.equal(h.elements.get('garden-hub').hidden,true);
 assert.match(h.elements.get('setup-error').textContent,/firmware and filesystem/);
});
test('failed first status request exposes setup and allows polling to recover',async()=>{
 const h=harness(); h.setHandler(async()=>{throw new Error('Offline');});
 await h.context.fetchSensors();
 assert.equal(h.elements.get('setup-form').hidden,false);
 assert.match(h.elements.get('setup-error').textContent,/Cannot read device status/);
 h.setHandler(async()=>state); await h.context.fetchSensors();
 assert.equal(h.elements.get('garden-hub').hidden,false);
});
