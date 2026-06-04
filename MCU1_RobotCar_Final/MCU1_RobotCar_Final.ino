/*
 * Robot Car v3 - MCU1 - ESP32-C3
 * CAN: GPIO7=CTX GPIO8=CRX @ 500Kbps
 * Motors: ENABLE_LEFT=10 DIR_LEFT=2 STEP_LEFT=3
 *         DIR_RIGHT=4 STEP_RIGHT=5 ENABLE_RIGHT=6
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "driver/twai.h"

const char* ssid     = "Miyamura";
const char* password = "123456789";

WebServer        httpServer(80);
WebSocketsServer webSocket(81);

#define ENABLE_LEFT   10
#define DIR_LEFT       2
#define STEP_LEFT      3
#define DIR_RIGHT      4
#define STEP_RIGHT     5
#define ENABLE_RIGHT   6
#define CAN_TX_PIN     7
#define CAN_RX_PIN     8
#define CAN_ID_SERVO   0x100
#define DELAY_US       1800
#define DELAY_US_TURN  2800
#define ACCEL_STEPS    300
#define DELAY_START    3000
#define TURN_TRANSITION_MS 500

struct RouteStep { char cmd; uint16_t pulses; };
#define MAX_ROUTE_STEPS 4000
RouteStep routeData[MAX_ROUTE_STEPS];
int routeLen = 0;

enum Mode { MANUAL, RECORDING, PLAYING };
Mode   mode       = MANUAL;
String currentCmd = "STOP";
long   stepCounter = 0;
char     recCmd    = 0;
uint16_t recPulses = 0;
int      playIndex   = 0;
uint16_t playDone    = 0;
long     playAccel   = 0;
bool     playPausing = false;
unsigned long pauseUntil = 0;
bool  servoAt180 = false;

// CAN
void canInit() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN,(gpio_num_t)CAN_RX_PIN,TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if(twai_driver_install(&g,&t,&f)!=ESP_OK){Serial.println("[CAN] install fail");return;}
  if(twai_start()!=ESP_OK){Serial.println("[CAN] start fail");return;}
  Serial.println("[CAN] OK 500Kbps TX=7 RX=8");
}
void canSendServo(uint8_t angle){
  twai_message_t m;
  m.identifier=CAN_ID_SERVO;m.extd=0;m.rtr=0;m.data_length_code=1;m.data[0]=angle;
  if(twai_transmit(&m,pdMS_TO_TICKS(10))!=ESP_OK) Serial.println("[CAN] TX fail");
  else Serial.printf("[CAN] servo=%d\n",angle);
}

// Motors
void setMotors(bool lEn,bool lDir,bool rEn,bool rDir){
  digitalWrite(ENABLE_LEFT, lEn?LOW:HIGH);
  digitalWrite(ENABLE_RIGHT,rEn?LOW:HIGH);
  digitalWrite(DIR_LEFT, lDir);
  digitalWrite(DIR_RIGHT,rDir);
}
void stepMotors(bool doL,bool doR,int us){
  if(doL) digitalWrite(STEP_LEFT, HIGH);
  if(doR) digitalWrite(STEP_RIGHT,HIGH);
  delayMicroseconds(us);
  if(doL) digitalWrite(STEP_LEFT, LOW);
  if(doR) digitalWrite(STEP_RIGHT,LOW);
  delayMicroseconds(us);
}
int getAccelDelay(long s){
  if(s<ACCEL_STEPS) return (int)map(s,0,ACCEL_STEPS,DELAY_START,DELAY_US);
  return DELAY_US;
}
bool execCmd(char cmd,int us){
  switch(cmd){
    case 'F': setMotors(true,LOW, true,HIGH);stepMotors(true,true,us);           return true;
    case 'B': setMotors(true,HIGH,true,LOW );stepMotors(true,true,us);           return true;
    case 'L': setMotors(true,HIGH,true,HIGH);stepMotors(true,true,DELAY_US_TURN);return true;
    case 'R': setMotors(true,LOW, true,LOW );stepMotors(true,true,DELAY_US_TURN);return true;
    default:  setMotors(false,LOW,false,LOW);                                    return false;
  }
}
char cmdChar(const String& s){
  if(s=="FORWARD")  return 'F';
  if(s=="BACKWARD") return 'B';
  if(s=="LEFT")     return 'L';
  if(s=="RIGHT")    return 'R';
  return 0;
}
bool needTransitionDelay(char from,char to){return from!=0&&to!=0&&from!=to;}
void recFlush(){
  if(recCmd&&recPulses>0&&routeLen<MAX_ROUTE_STEPS) routeData[routeLen++]={recCmd,recPulses};
  recCmd=0;recPulses=0;
}

const char htmlPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="vi"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Robot Car</title>
<style>
:root{--bg:#f4f1ec;--sf:#fffef9;--bd:#d9d4ca;--tx:#1a1714;--mu:#8a857d;--ac:#2563eb;--dg:#dc2626;--go:#16a34a;}
*{margin:0;padding:0;box-sizing:border-box;}
body{background:var(--bg);font-family:sans-serif;color:var(--tx);min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:20px;gap:16px;}
.hdr{display:flex;align-items:center;gap:10px;}
.hdr h1{font-size:1rem;font-weight:700;}
.dot{width:8px;height:8px;border-radius:50%;background:var(--dg);}
.dot.on{background:var(--go);}
.card{background:var(--sf);border:1px solid var(--bd);border-radius:16px;padding:20px;width:100%;max-width:340px;box-shadow:0 2px 8px rgba(0,0,0,.08);}
.dpad{display:grid;grid-template-columns:repeat(3,88px);grid-template-rows:repeat(3,88px);gap:8px;justify-content:center;}
.btn{background:var(--sf);border:1.5px solid var(--bd);border-radius:14px;display:flex;flex-direction:column;align-items:center;justify-content:center;cursor:pointer;gap:5px;user-select:none;}
.btn svg{width:28px;height:28px;}
.btn span{font-size:.6rem;font-family:monospace;color:var(--mu);letter-spacing:1px;}
.btn.active{background:#f0f7ff;border-color:var(--ac);}
#btn-forward{grid-column:2;grid-row:1;}
#btn-left{grid-column:1;grid-row:2;}
#btn-stop{grid-column:2;grid-row:2;border-color:var(--dg);}
#btn-stop.active{background:#fff0f0;border-color:var(--dg);}
#btn-right{grid-column:3;grid-row:2;}
#btn-backward{grid-column:2;grid-row:3;}
.sbar{display:flex;justify-content:space-between;font-family:monospace;font-size:.72rem;color:var(--mu);margin-top:14px;}
#cmd-label{color:var(--tx);font-weight:600;}
.stitle{font-size:.65rem;font-family:monospace;letter-spacing:2px;color:var(--mu);text-transform:uppercase;margin-bottom:12px;}
.ract{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;}
.rb{border:1.5px solid var(--bd);background:var(--sf);border-radius:12px;padding:10px 6px;font-family:monospace;font-size:.65rem;cursor:pointer;text-align:center;color:var(--tx);line-height:1.6;}
.rb:disabled{opacity:.35;cursor:not-allowed;}
.rb.rec{border-color:var(--dg);color:var(--dg);}
.rb.rec.on{background:var(--dg);color:#fff;}
.rb.play{border-color:var(--go);color:var(--go);}
.rb.play.on{background:var(--go);color:#fff;}
#ri{margin-top:10px;font-family:monospace;font-size:.65rem;color:var(--mu);text-align:center;}
.sw{display:flex;flex-direction:column;align-items:center;gap:12px;}
#sb{width:100%;padding:14px;border-radius:12px;cursor:pointer;font-family:monospace;font-size:.8rem;border:1.5px solid var(--bd);background:var(--sf);color:var(--tx);font-weight:500;}
#sb.on{background:var(--ac);border-color:var(--ac);color:#fff;}
#sa{font-family:monospace;font-size:.68rem;color:var(--mu);}
</style></head><body>
<div class="hdr"><div class="dot" id="cd"></div><h1>ROBOT CAR v3</h1></div>

<div class="card">
<div class="dpad">
  <div class="btn" id="btn-forward"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><polyline points="18 15 12 9 6 15"/></svg><span>FWD</span></div>
  <div class="btn" id="btn-left"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><polyline points="15 18 9 12 15 6"/></svg><span>LEFT</span></div>
  <div class="btn" id="btn-stop"><svg viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="6" width="12" height="12" rx="2"/></svg><span style="color:var(--dg)">STOP</span></div>
  <div class="btn" id="btn-right"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><polyline points="9 18 15 12 9 6"/></svg><span>RIGHT</span></div>
  <div class="btn" id="btn-backward"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><polyline points="6 9 12 15 18 9"/></svg><span>REV</span></div>
</div>
<div class="sbar"><span id="cmd-label">STOP</span><span id="mode-label">MANUAL</span></div>
</div>

<div class="card">
<div class="stitle">Tuyen duong</div>
<div class="ract">
  <button class="rb rec" id="rb-rec">&#9210; GHI</button>
  <button class="rb play" id="rb-play" disabled>&#9654; PHAT</button>
  <button class="rb" id="rb-clear" disabled>&#10005; XOA</button>
</div>
<div id="ri">Chua co tuyen duong</div>
</div>

<div class="card">
<div class="stitle">Servo</div>
<div class="sw">
  <svg width="80" height="44" viewBox="0 0 80 44" fill="none">
    <path d="M4 40 A36 36 0 0 1 76 40" stroke="#e5e2db" stroke-width="5" stroke-linecap="round"/>
    <path id="af" d="M4 40 A36 36 0 0 1 76 40" stroke="#2563eb" stroke-width="5" stroke-linecap="round" stroke-dasharray="113" stroke-dashoffset="113" style="transition:stroke-dashoffset .5s"/>
    <line id="nd" x1="40" y1="40" x2="40" y2="8" stroke="#1a1714" stroke-width="2.5" stroke-linecap="round" style="transform-origin:40px 40px;transition:transform .5s"/>
  </svg>
  <button id="sb" onclick="toggleServo()">XOAY 180</button>
  <div id="sa">Vi tri: 0 do</div>
</div>
</div>

<script>
var ws,recState=false,playState=false,hasRoute=false,servoAt180=false;
var cd=document.getElementById('cd');
var cmdL=document.getElementById('cmd-label');
var modeL=document.getElementById('mode-label');
var ri=document.getElementById('ri');
var rbRec=document.getElementById('rb-rec');
var rbPlay=document.getElementById('rb-play');
var rbClear=document.getElementById('rb-clear');
var sb=document.getElementById('sb');
var sa=document.getElementById('sa');
var af=document.getElementById('af');
var nd=document.getElementById('nd');

function updateServo(at180){
  servoAt180=at180;
  var a=at180?180:0;
  af.style.strokeDashoffset=at180?0:113;
  nd.style.transform='rotate('+a+'deg)';
  sb.className=at180?'on':'';
  sb.textContent=at180?'QUAY VE 0':'XOAY 180';
  sa.textContent='Vi tri: '+a+' do';
}

function toggleServo(){
  var t=servoAt180?0:180;
  send('SERVO_'+t);
  updateServo(!servoAt180);
}

function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=function(){cd.className='dot on';send('STATE');};
  ws.onclose=function(){cd.className='dot';setTimeout(connect,2000);};
  ws.onerror=function(){ws.close();};
  ws.onmessage=function(e){
    try{
      var d=JSON.parse(e.data);
      if(d.segs!==undefined){
        hasRoute=d.segs>0;
        ri.textContent=hasRoute?d.segs+' doan da ghi':'Chua co tuyen duong';
        rbPlay.disabled=!hasRoute;
        rbClear.disabled=!hasRoute;
      }
      if(d.mode!==undefined){
        recState=d.mode==='REC';
        playState=d.mode==='PLAY';
        rbRec.classList.toggle('on',recState);
        rbPlay.classList.toggle('on',playState);
        rbRec.textContent=recState?'DUNG GHI':'&#9210; GHI';
        rbPlay.textContent=playState?'DUNG':'&#9654; PHAT';
        modeL.textContent=d.mode;
        if(playState) ri.textContent='Dang phat lai...';
      }
      if(d.servo!==undefined) updateServo(d.servo===180);
    }catch(err){}
  };
}

function send(c){if(ws&&ws.readyState===1)ws.send(c);}

var activeBtn=null;
var pad={'btn-forward':'FORWARD','btn-backward':'BACKWARD','btn-left':'LEFT','btn-right':'RIGHT','btn-stop':'STOP'};
var ids=['btn-forward','btn-backward','btn-left','btn-right','btn-stop'];
for(var i=0;i<ids.length;i++){
  (function(id){
    var el=document.getElementById(id);
    function dn(){setA(id);send(pad[id]);cmdL.textContent=pad[id];}
    el.addEventListener('mousedown',dn);
    el.addEventListener('touchstart',function(e){e.preventDefault();dn();},{passive:false});
  })(ids[i]);
}
function setA(id){
  for(var i=0;i<ids.length;i++) document.getElementById(ids[i]).classList.remove('active');
  document.getElementById(id).classList.add('active');
  activeBtn=id;
}
function stopAll(){if(activeBtn&&activeBtn!=='btn-stop'){setA('btn-stop');send('STOP');cmdL.textContent='STOP';}}
document.addEventListener('mouseup',stopAll);
document.addEventListener('touchend',stopAll);
var km={'ArrowUp':'btn-forward','ArrowDown':'btn-backward','ArrowLeft':'btn-left','ArrowRight':'btn-right',' ':'btn-stop'};
document.addEventListener('keydown',function(e){if(!km[e.key]||e.repeat)return;var id=km[e.key];setA(id);send(pad[id]);cmdL.textContent=pad[id];});
document.addEventListener('keyup',function(e){if(!km[e.key]||e.key===' ')return;stopAll();});
rbRec.addEventListener('click',function(){send(recState?'REC_STOP':'REC_START');});
rbPlay.addEventListener('click',function(){send(playState?'PLAY_STOP':'PLAY_START');});
rbClear.addEventListener('click',function(){if(confirm('Xoa tuyen duong?'))send('ROUTE_CLEAR');});
connect();
setInterval(function(){send('STATE');},3000);
</script></body></html>
)HTML";

// WebSocket
void sendState(uint8_t num=255){
  String ms=(mode==RECORDING)?"REC":(mode==PLAYING)?"PLAY":"MANUAL";
  int ang=servoAt180?180:0;
  String j="{\"mode\":\""+ms+"\",\"segs\":"+String(routeLen)+",\"servo\":"+String(ang)+"}";
  if(num==255) webSocket.broadcastTXT(j);
  else         webSocket.sendTXT(num,j);
}

void onWsEvent(uint8_t num,WStype_t type,uint8_t* payload,size_t len){
  if(type!=WStype_TEXT) return;
  String msg=String((char*)payload);
  if(msg=="STATE"){sendState(num);return;}
  if(msg=="SERVO_180"){servoAt180=true; canSendServo(180);sendState();return;}
  if(msg=="SERVO_0")  {servoAt180=false;canSendServo(0);  sendState();return;}
  if(msg=="REC_START"&&mode==MANUAL){
    routeLen=0;recCmd=0;recPulses=0;mode=RECORDING;currentCmd="STOP";stepCounter=0;
    Serial.println("REC START");sendState();return;
  }
  if(msg=="REC_STOP"&&mode==RECORDING){
    recFlush();mode=MANUAL;currentCmd="STOP";setMotors(false,LOW,false,LOW);
    Serial.println("REC STOP segs="+String(routeLen));sendState();return;
  }
  if(msg=="PLAY_START"&&mode==MANUAL&&routeLen>0){
    playIndex=0;playDone=0;playAccel=0;playPausing=false;mode=PLAYING;
    Serial.println("PLAY START segs="+String(routeLen));sendState();return;
  }
  if(msg=="PLAY_STOP"&&mode==PLAYING){
    mode=MANUAL;currentCmd="STOP";setMotors(false,LOW,false,LOW);sendState();return;
  }
  if(msg=="ROUTE_CLEAR"){
    routeLen=0;mode=MANUAL;currentCmd="STOP";setMotors(false,LOW,false,LOW);sendState();return;
  }
  if(mode==PLAYING) return;
  char c=cmdChar(msg);
  if(c||msg=="STOP"){
    String nc=c?msg:"STOP";
    if(currentCmd!=nc){
      if(mode==RECORDING) recFlush();
      stepCounter=0;
      if(mode==RECORDING&&c){recCmd=c;recPulses=0;}
    }
    currentCmd=nc;
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(ENABLE_LEFT, OUTPUT);digitalWrite(ENABLE_LEFT, HIGH);
  pinMode(DIR_LEFT,    OUTPUT);
  pinMode(STEP_LEFT,   OUTPUT);digitalWrite(STEP_LEFT,   LOW);
  pinMode(ENABLE_RIGHT,OUTPUT);digitalWrite(ENABLE_RIGHT,HIGH);
  pinMode(DIR_RIGHT,   OUTPUT);
  pinMode(STEP_RIGHT,  OUTPUT);digitalWrite(STEP_RIGHT,  LOW);
  canInit();
  WiFi.begin(ssid,password);
  Serial.print("WiFi");
  while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print(".");}
  Serial.println("\nIP: "+WiFi.localIP().toString());
  httpServer.on("/",[](){ httpServer.send_P(200,"text/html",htmlPage); });
  httpServer.begin();
  webSocket.begin();
  webSocket.onEvent(onWsEvent);
  Serial.println("Ready");
}

void loop(){
  httpServer.handleClient();
  webSocket.loop();
  if(mode==MANUAL){
    char c=cmdChar(currentCmd);
    if(c){execCmd(c,getAccelDelay(stepCounter));stepCounter++;}
    else{setMotors(false,LOW,false,LOW);stepCounter=0;}
    return;
  }
  if(mode==RECORDING){
    char c=cmdChar(currentCmd);
    if(c){
      execCmd(c,getAccelDelay(stepCounter));stepCounter++;
      if(recCmd==c&&recPulses<65535) recPulses++;
    }else{setMotors(false,LOW,false,LOW);stepCounter=0;}
    return;
  }
  if(mode==PLAYING){
    if(playIndex>=routeLen){
      mode=MANUAL;currentCmd="STOP";setMotors(false,LOW,false,LOW);
      Serial.println("PLAY DONE");sendState();return;
    }
    if(playPausing){
      setMotors(false,LOW,false,LOW);
      if(millis()>=pauseUntil){playPausing=false;playAccel=0;}
      return;
    }
    const RouteStep& rs=routeData[playIndex];
    if(playDone==0) playAccel=0;
    execCmd(rs.cmd,getAccelDelay(playAccel));
    playDone++;playAccel++;
    if(playDone>=rs.pulses){
      playDone=0;playIndex++;
      if(playIndex<routeLen){
        char nc=routeData[playIndex].cmd;
        if(needTransitionDelay(rs.cmd,nc)){playPausing=true;pauseUntil=millis()+TURN_TRANSITION_MS;}
      }
    }
    return;
  }
}
