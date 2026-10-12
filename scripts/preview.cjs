const http=require('http'),fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../web');
let s={light:[800,800,800,800],waterTemp:21,ambientTemp:24,humidity:62,pump:0,pumpMode:'manual',remainingSeconds:0,red:128,blue:128,actualRed:128,actualBlue:128,autoLight:false,lightTarget:50,onMinutes:5,offMinutes:10,floors:1,ssid:'',wifiMode:'setup',wifiError:'',wifiStatus:'Not configured',ip:'192.168.4.1',uptime:3600};
http.createServer((req,res)=>{
 if(req.url.startsWith('/api/')) {
  let body='';req.on('data',c=>body+=c);req.on('end',()=>{
   if(req.method==='POST') {
    const b=JSON.parse(body||'{}');
    if(s.wifiMode!=='ready' && req.url!=='/api/wifi') {res.writeHead(409,{'Content-Type':'application/json'});res.end(JSON.stringify({error:'Complete Wi-Fi setup first'}));return;}
    if(req.url==='/api/pump'){s.pump=b.state;s.pumpMode='manual';}
    else if(req.url==='/api/pump/schedule'){s.pumpMode=b.mode;s.remainingSeconds=(b.minutes||b.offMinutes||0)*60;Object.assign(s,{onMinutes:b.onMinutes||s.onMinutes,offMinutes:b.offMinutes||s.offMinutes});}
    else if(req.url==='/api/wifi'){if(s.wifiMode==='connecting'){res.writeHead(409);res.end(JSON.stringify({error:'Already connecting'}));return;}
      s.ssid=b.ssid;s.wifiMode='connecting';s.wifiStatus='Connecting';s.wifiError='';
      setTimeout(()=>{if(b.ssid==='fail'){s.ssid='';s.wifiMode='setup';s.wifiStatus='Not configured';s.wifiError='Could not connect. Check the network name and password, then try again.';}else{s.wifiMode='ready';s.wifiStatus='Connected';}},3000);}
    else Object.assign(s,b);
   }
   res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(s));
  });return;
 }
 const name=req.url==='/'?'index.html':req.url.slice(1);
 if(!['index.html','style.css','app.js'].includes(name)){res.writeHead(404);res.end();return;}
 res.writeHead(200,{'Content-Type':name.endsWith('css')?'text/css':name.endsWith('js')?'text/javascript':'text/html'});
 res.end(fs.readFileSync(path.join(root,name)));
}).listen(8765,'127.0.0.1',()=>console.log('UI preview with simulated device: http://127.0.0.1:8765'));
