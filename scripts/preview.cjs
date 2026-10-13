const http=require('http'),fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../web');
let clockDelta = 0;
let s={ledScheduleEnabled:false,ledStartMinute:360,ledEndMinute:1080,utcOffsetMinutes:-180,clockValid:true,ledWindowActive:true,localMinute:0,light:[0,1024,2048,4095],waterTemp:21,ambientTemp:24,humidity:62,pump:0,pumpMode:'manual',remainingSeconds:0,red:128,blue:128,actualRed:128,actualBlue:128,autoLight:false,lightTarget:50,onMinutes:5,offMinutes:10,floors:1,ssid:'',wifiMode:'setup',wifiError:'',wifiStatus:'Sin configurar',ip:'192.168.4.1',uptime:3600};
http.createServer((req,res)=>{
 if(req.url.startsWith('/api/')) {
  let body='';req.on('data',c=>body+=c);req.on('end',()=>{
   if(req.method==='POST') {
    const b=JSON.parse(body||'{}');
    if(s.wifiMode!=='ready' && req.url!=='/api/wifi') {res.writeHead(409,{'Content-Type':'application/json'});res.end(JSON.stringify({error:'Primero completá la configuración Wi-Fi'}));return;}
    if(req.url==='/api/leds/schedule') {
      if(typeof b.enabled!=='boolean'||!Number.isInteger(b.startMinute)||b.startMinute<0||b.startMinute>1439||!Number.isInteger(b.endMinute)||b.endMinute<0||b.endMinute>1439||b.startMinute===b.endMinute||!Number.isInteger(b.utcOffsetMinutes)||b.utcOffsetMinutes<-720||b.utcOffsetMinutes>840){res.writeHead(400);res.end(JSON.stringify({error:'Horario LED no válido'}));return;}
      Object.assign(s,{ledScheduleEnabled:b.enabled,ledStartMinute:b.startMinute,ledEndMinute:b.endMinute,utcOffsetMinutes:b.utcOffsetMinutes});
    }
    else if(req.url==='/api/time'){clockDelta=b.epoch-Math.floor(Date.now()/1000);s.clockValid=true;}
    else if(req.url==='/api/pump'){s.pump=b.state;s.pumpMode='manual';}
    else if(req.url==='/api/pump/schedule'){s.pumpMode=b.mode;s.remainingSeconds=(b.minutes||b.offMinutes||0)*60;Object.assign(s,{onMinutes:b.onMinutes||s.onMinutes,offMinutes:b.offMinutes||s.offMinutes});}
    else if(req.url==='/api/wifi'){if(s.wifiMode==='connecting'){res.writeHead(409);res.end(JSON.stringify({error:'Ya se está conectando'}));return;}
      s.ssid=b.ssid;s.wifiMode='connecting';s.wifiStatus='Conectando';s.wifiError='';
      setTimeout(()=>{if(b.ssid==='fail'){s.ssid='';s.wifiMode='setup';s.wifiStatus='Sin configurar';s.wifiError='No se pudo conectar. Revisá el nombre de la red y la contraseña y volvé a intentarlo.';}else{s.wifiMode='ready';s.wifiStatus='Conectado';}},3000);}
    else Object.assign(s,b);
   }
   const epoch=Math.floor(Date.now()/1000)+clockDelta;
   s.localMinute=((Math.floor(epoch/60)+s.utcOffsetMinutes)%1440+1440)%1440;
   s.ledWindowActive=!s.ledScheduleEnabled||(s.clockValid&&(s.ledStartMinute<s.ledEndMinute
     ? s.localMinute>=s.ledStartMinute&&s.localMinute<s.ledEndMinute
     : s.localMinute>=s.ledStartMinute||s.localMinute<s.ledEndMinute));
   s.sectionOutput=s.light.map(value=>{
     const scale=Math.max(0,Math.min(1,1-(value/4095)/(s.lightTarget/100)));
     const factor=s.autoLight ? (Math.max(s.red,s.blue) ? 255*scale/Math.max(s.red,s.blue) : 0) : 1;
     return {red:s.ledWindowActive?Math.round(s.red*factor):0,blue:s.ledWindowActive?Math.round(s.blue*factor):0};
   });
   s.actualRed=Math.floor(s.sectionOutput.reduce((sum,row)=>sum+row.red,0)/4);
   s.actualBlue=Math.floor(s.sectionOutput.reduce((sum,row)=>sum+row.blue,0)/4);
   res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(s));
  });return;
 }
 const name=req.url==='/'?'index.html':req.url.slice(1);
 if(!['index.html','style.css','app.js'].includes(name)){res.writeHead(404);res.end();return;}
 res.writeHead(200,{'Content-Type':name.endsWith('css')?'text/css':name.endsWith('js')?'text/javascript':'text/html'});
 res.end(fs.readFileSync(path.join(root,name)));
}).listen(8765,'127.0.0.1',()=>console.log('UI preview with simulated device: http://127.0.0.1:8765'));
