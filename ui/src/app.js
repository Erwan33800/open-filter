/* "use strict" et JUCE_MODE viennent d'open-core/ui/bridge-core.js, placé avant ce fichier. */
/* ---------- constantes ---------- */
const FS = 48000, FMIN = 10, FMAX = 30000;
const TYPES = {
  bell:{n:"Cloche",gain:true,q:true,dyn:true},
  lowshelf:{n:"Plateau grave",gain:true,q:true,dyn:true},
  highshelf:{n:"Plateau aigu",gain:true,q:true,dyn:true},
  lowcut:{n:"Coupe-bas",gain:false,q:true,slope:true},
  highcut:{n:"Coupe-haut",gain:false,q:true,slope:true},
  notch:{n:"Notch",gain:false,q:true},
  bandpass:{n:"Passe-bande",gain:false,q:true}
};
const PLACES = {st:{n:"Stéréo",s:"St",c:"--pl-st"},mid:{n:"Mid",s:"M",c:"--pl-mid"},side:{n:"Side",s:"S",c:"--pl-side"}};
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const clamp = (v,a,b) => Math.min(b,Math.max(a,v));
const lg = Math.log10;

/* ---------- état ---------- */
const emptyMacros = () => Array.from({length:8},(_,i)=>({name:"Macro "+(i+1),v:0,asg:[]}));
const S = { bands:[], macros:emptyMacros(), sel:-1, range:12, outGain:0, phase:false, bypass:false, spec:"prepost", fall:"moy", dbRange:90, tilt:4.5, nextId:1, undo:[], redo:[], presetName:"Init", dirty:false, view:"curve" };
let drag = null;

function snapshot(){ return JSON.stringify({bands:S.bands,macros:S.macros,nextId:S.nextId}); }
function pushUndo(){ S.undo.push(snapshot()); if(S.undo.length>100) S.undo.shift(); S.redo.length=0; S.dirty=true; syncUndo(); }
function restore(js){ const o=JSON.parse(js); S.bands=o.bands; S.macros=o.macros; S.nextId=o.nextId; if(!S.bands.find(b=>b.id===S.sel)) S.sel=-1; refresh(); if(S.view==="macros") renderMacros(); }
function doUndo(){ if(!S.undo.length) return; S.redo.push(snapshot()); restore(S.undo.pop()); syncUndo(); }
function doRedo(){ if(!S.redo.length) return; S.undo.push(snapshot()); restore(S.redo.pop()); syncUndo(); }
function syncUndo(){ $("undo").disabled=!S.undo.length; $("redo").disabled=!S.redo.length; }
const $ = id => document.getElementById(id);
const selBand = () => S.bands.find(b=>b.id===S.sel);

/* ---------- formats ---------- */
const fmtF = f => f>=1000 ? (f/1000).toFixed(f>=10000?1:2).replace(/\.?0+$/,"")+" kHz" : (f<100?f.toFixed(1):Math.round(f))+" Hz";
const fmtG = g => (g>0?"+":"")+g.toFixed(1)+" dB";
const fmtQ = q => q.toFixed(q<1?2:1);
const NOTES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
function noteName(f){ // convention Ableton : C3 = MIDI 60
  const m = 69 + 12*Math.log2(f/440), r = Math.round(m), cents = Math.round((m-r)*100);
  return NOTES[((r%12)+12)%12] + (Math.floor(r/12)-2) + (cents? (cents>0?"+":"")+cents : "");
}

/* ---------- réponse en fréquence (RBJ, sans correction Nyquist : maquette) ---------- */
function coefs(type,f0,gain,q){
  const w0=2*Math.PI*clamp(f0,FMIN,FS*0.49)/FS, cw=Math.cos(w0), sw=Math.sin(w0);
  const A=Math.pow(10,gain/40), al=sw/(2*q);
  let b0,b1,b2,a0,a1,a2;
  switch(type){
    case "bell": b0=1+al*A;b1=-2*cw;b2=1-al*A;a0=1+al/A;a1=-2*cw;a2=1-al/A;break;
    case "lowshelf":{const s=2*Math.sqrt(A)*al; b0=A*((A+1)-(A-1)*cw+s);b1=2*A*((A-1)-(A+1)*cw);b2=A*((A+1)-(A-1)*cw-s);a0=(A+1)+(A-1)*cw+s;a1=-2*((A-1)+(A+1)*cw);a2=(A+1)+(A-1)*cw-s;break;}
    case "highshelf":{const s=2*Math.sqrt(A)*al; b0=A*((A+1)+(A-1)*cw+s);b1=-2*A*((A-1)+(A+1)*cw);b2=A*((A+1)+(A-1)*cw-s);a0=(A+1)-(A-1)*cw+s;a1=2*((A-1)-(A+1)*cw);a2=(A+1)-(A-1)*cw-s;break;}
    case "lowcut": b0=(1+cw)/2;b1=-(1+cw);b2=(1+cw)/2;a0=1+al;a1=-2*cw;a2=1-al;break;
    case "highcut": b0=(1-cw)/2;b1=1-cw;b2=(1-cw)/2;a0=1+al;a1=-2*cw;a2=1-al;break;
    case "notch": b0=1;b1=-2*cw;b2=1;a0=1+al;a1=-2*cw;a2=1-al;break;
    default: b0=al;b1=0;b2=-al;a0=1+al;a1=-2*cw;a2=1-al; // passe-bande
  }
  return [b0/a0,b1/a0,b2/a0,a1/a0,a2/a0];
}
function magDb(c,f){
  const w=2*Math.PI*f/FS, c1=Math.cos(w), s1=Math.sin(w), c2=Math.cos(2*w), s2=Math.sin(2*w);
  const nr=c[0]+c[1]*c1+c[2]*c2, ni=-(c[1]*s1+c[2]*s2);
  const dr=1+c[3]*c1+c[4]*c2, di=-(c[3]*s1+c[4]*s2);
  return 10*Math.log10((nr*nr+ni*ni)/(dr*dr+di*di)+1e-30);
}
// réponse d'une bande en dB à la fréquence f. gainOverride sert à l'animation dynamique.
function bandDb(b,f,gainOverride){
  if(!b.on) return 0;
  const g = gainOverride!==undefined ? gainOverride : b.gain;
  if(TYPES[b.type].slope){
    const stages = Math.max(1,Math.round(b.slope/12)); // 12 dB/oct par biquad
    const qs = clamp(b.q,0.3,40);
    let tot=0;
    for(let i=0;i<stages;i++){ const q = i===stages-1 ? qs : 0.7071; tot += magDb(coefs(b.type,b.freq,0,q),f); }
    return tot;
  }
  return magDb(coefs(b.type,b.freq,g,b.q),f);
}
const bandGainNow = b => b.dyn ? b.gain + b.range*dynAmount(b) : b.gain;
function totalDb(f,live){ let t=0; for(const b of S.bands) t += bandDb(b,f, live?bandGainNow(b):undefined); return t; }

/* ---------- animation dynamique factice ---------- */
let T=0;
const dynAmount = b => b.dyn ? (0.5+0.5*Math.sin(T*1.7+b.id*2.1))*(0.5+0.5*Math.sin(T*0.43+b.id)) : 0;

/* ---------- canvas : géométrie ---------- */
const cv=$("cv"), ctx=cv.getContext("2d");
const PAD={l:44,r:14,t:12,b:24};
let CW=0,CH=0,DPR=1;
const gx = f => PAD.l + (lg(f)-lg(FMIN))/(lg(FMAX)-lg(FMIN))*(CW-PAD.l-PAD.r);
const fx = x => Math.pow(10, lg(FMIN)+(x-PAD.l)/(CW-PAD.l-PAD.r)*(lg(FMAX)-lg(FMIN)));
const gy = g => PAD.t + (1-(g+S.range)/(2*S.range))*(CH-PAD.t-PAD.b);
const yg = y => (1-(y-PAD.t)/(CH-PAD.t-PAD.b))*2*S.range - S.range;
function resize(){
  DPR=window.devicePixelRatio||1; const r=cv.getBoundingClientRect(); CW=r.width; CH=r.height;
  cv.width=Math.round(CW*DPR); cv.height=Math.round(CH*DPR);
  const m=$("meter"), mr=m.getBoundingClientRect(); m.width=Math.round(mr.width*DPR); m.height=Math.round(mr.height*DPR);
  placePanel();
}
new ResizeObserver(resize).observe($("stage"));

/* ---------- spectre factice ---------- */
const NP=220, specPre=new Float32Array(NP), specPost=new Float32Array(NP), tgt=new Float32Array(NP), freqs=[];
for(let i=0;i<NP;i++){ freqs.push(Math.pow(10, lg(20)+(i/(NP-1))*(lg(20000)-lg(20)))); tgt[i]=-80; specPre[i]=-80; }
function stepSpectrum(){
  const fall = {lente:0.012,moy:0.03,rapide:0.07}[S.fall];
  for(let i=0;i<NP;i++){
    const f=freqs[i], l=Math.log2(f/60);
    let base=-38+22*Math.exp(-l*l/6) - 3.2*Math.max(0,Math.log2(f/2500)); // bosse de basses, descente des aigus
    base+=7*Math.sin(T*1.3+i*0.31)*Math.sin(T*0.37+i*0.07);
    if(Math.random()<0.015) tgt[i]=base+10+Math.random()*10; // pics
    tgt[i]=Math.max(tgt[i]-0.5, base+Math.random()*3);
    specPre[i] = tgt[i]>specPre[i] ? specPre[i]+(tgt[i]-specPre[i])*0.5 : specPre[i]-(specPre[i]-tgt[i])*fall*6;
    specPost[i] = specPre[i] + (S.bypass?0:totalDb(f,true)) + (S.bypass?0:S.outGain);
  }
}

/* ---------- dessin ---------- */
function draw(){
  ctx.setTransform(DPR,0,0,DPR,0,0);
  ctx.clearRect(0,0,CW,CH);
  const W=CW-PAD.l-PAD.r, H=CH-PAD.t-PAD.b, accent=css("--accent"), line=css("--line"), faint=css("--faint"), fg=css("--fg");
  ctx.font='10px '+css("--mono");

  // grille
  ctx.lineWidth=1; ctx.fillStyle=faint; ctx.textBaseline="middle";
  const step = S.range<=3?1: S.range<=6?2: S.range<=12?3:10;
  for(let g=-S.range; g<=S.range+1e-6; g+=step){
    const y=Math.round(gy(g))+.5; ctx.strokeStyle = g===0 ? "#3a3a3f" : line;
    ctx.beginPath(); ctx.moveTo(PAD.l,y); ctx.lineTo(CW-PAD.r,y); ctx.stroke();
    ctx.textAlign="right"; ctx.fillText((g>0?"+":"")+g, PAD.l-8, y);
  }
  ctx.textAlign="center"; ctx.textBaseline="top";
  for(const f of [20,50,100,200,500,1000,2000,5000,10000,20000]){
    const x=Math.round(gx(f))+.5; ctx.strokeStyle=line; ctx.beginPath(); ctx.moveTo(x,PAD.t); ctx.lineTo(x,CH-PAD.b); ctx.stroke();
    ctx.fillText(f>=1000?(f/1000)+"k":f, x, CH-PAD.b+7);
  }

  // spectres (échelle d'affichage : -90 dB en bas, 0 dB en haut, comme la plage de l'analyseur)
  const sy = d => PAD.t + (1-clamp((d+S.dbRange)/S.dbRange,0,1))*H;
  const drawSpec = (arr,fill,stroke) => {
    ctx.beginPath(); ctx.moveTo(gx(freqs[0]),PAD.t+H);
    for(let i=0;i<NP;i++) ctx.lineTo(gx(freqs[i]), sy(arr[i]-18+(S.tilt-4.5)*Math.log2(freqs[i]/1000)));
    ctx.lineTo(gx(freqs[NP-1]),PAD.t+H); ctx.closePath();
    ctx.fillStyle=fill; ctx.fill(); if(stroke){ctx.strokeStyle=stroke; ctx.lineWidth=1; ctx.stroke();}
  };
  ctx.save(); ctx.beginPath(); ctx.rect(PAD.l,PAD.t,W,H); ctx.clip();
  if(S.spec==="pre"||S.spec==="prepost") drawSpec(specPre,"rgba(164,164,170,.13)",null);
  if(S.spec==="post"||S.spec==="prepost") drawSpec(specPost,"rgba(95,212,184,.16)","rgba(95,212,184,.5)");

  // zones dynamiques
  for(const b of S.bands){
    if(!b.on||!b.dyn||!TYPES[b.type].dyn) continue;
    const col=css(PLACES[b.place].c);
    ctx.beginPath();
    for(let i=0;i<=160;i++){ const f=Math.pow(10,lg(FMIN)+i/160*(lg(FMAX)-lg(FMIN))); const y=gy(bandDb(b,f,b.gain)); i?ctx.lineTo(gx(f),y):ctx.moveTo(gx(f),y); }
    for(let i=160;i>=0;i--){ const f=Math.pow(10,lg(FMIN)+i/160*(lg(FMAX)-lg(FMIN))); ctx.lineTo(gx(f), gy(bandDb(b,f,b.gain+b.range))); }
    ctx.closePath(); ctx.fillStyle=col; ctx.globalAlpha=b.id===S.sel?.18:.1; ctx.fill(); ctx.globalAlpha=1;
  }

  // courbe de la bande sélectionnée
  const sb=selBand();
  if(sb&&sb.on){
    ctx.beginPath();
    for(let i=0;i<=240;i++){ const f=Math.pow(10,lg(FMIN)+i/240*(lg(FMAX)-lg(FMIN))); const y=gy(bandDb(sb,f,sb.dyn?bandGainNow(sb):undefined)); i?ctx.lineTo(gx(f),y):ctx.moveTo(gx(f),y); }
    ctx.strokeStyle=css(PLACES[sb.place].c); ctx.globalAlpha=.7; ctx.lineWidth=1.5; ctx.stroke(); ctx.globalAlpha=1;
  }

  // courbe totale
  const y0=gy(0); ctx.beginPath();
  for(let i=0;i<=360;i++){ const f=Math.pow(10,lg(FMIN)+i/360*(lg(FMAX)-lg(FMIN))); const y=gy(S.bypass?0:totalDb(f,true)+0); i?ctx.lineTo(gx(f),y):ctx.moveTo(gx(f),y); }
  ctx.strokeStyle=S.bypass?faint:accent; ctx.lineWidth=2; ctx.lineJoin="round"; ctx.stroke();
  ctx.lineTo(CW-PAD.r,y0); ctx.lineTo(PAD.l,y0); ctx.closePath(); ctx.globalAlpha=.07; ctx.fillStyle=accent; ctx.fill(); ctx.globalAlpha=1;
  ctx.restore();

  // nœuds
  for(const b of S.bands){
    const col=css(PLACES[b.place].c), x=gx(b.freq), y=gy(TYPES[b.type].gain? b.gain : 0);
    const sel=b.id===S.sel, r=sel?8:6.5;
    if(b.dyn&&TYPES[b.type].dyn){ // tige de range
      const y2=gy(b.gain+b.range); ctx.strokeStyle=col; ctx.globalAlpha=.6; ctx.setLineDash([3,3]); ctx.beginPath(); ctx.moveTo(x,gy(b.gain)); ctx.lineTo(x,y2); ctx.stroke(); ctx.setLineDash([]); ctx.globalAlpha=1;
      ctx.fillStyle=col; ctx.fillRect(x-4,y2-1.5,8,3);
    }
    ctx.beginPath(); ctx.arc(x,y,r,0,7);
    if(b.on){ ctx.fillStyle=col; ctx.fill(); } else { ctx.fillStyle=css("--panel"); ctx.fill(); ctx.strokeStyle=col; ctx.lineWidth=1.5; ctx.stroke(); }
    if(sel){ ctx.beginPath(); ctx.arc(x,y,r+4,0,7); ctx.strokeStyle=fg; ctx.lineWidth=1; ctx.stroke(); }
    ctx.fillStyle=b.on?"#101012":col; ctx.font='700 9.5px '+css("--sans"); ctx.textAlign="center"; ctx.textBaseline="middle";
    ctx.fillText(String(b.id), x, y+.5);
  }

  // info de survol/glisser
  if(drag&&drag.kind==="node"){
    const b=selBand(); if(b){ const s=fmtF(b.freq)+" · "+noteName(b.freq)+(TYPES[b.type].gain?"  "+fmtG(b.gain):""); ctx.font='11px '+css("--mono");
      const x=gx(b.freq), y=gy(TYPES[b.type].gain?b.gain:0), w=ctx.measureText(s).width+16, bx=clamp(x-w/2,PAD.l,CW-PAD.r-w), by=y>60?y-38:y+16;
      ctx.fillStyle="#343438"; ctx.beginPath(); ctx.roundRect(bx,by,w,22,6); ctx.fill(); ctx.fillStyle=fg; ctx.textAlign="left"; ctx.textBaseline="middle"; ctx.fillText(s,bx+8,by+11); }
  }
}

/* vumètre factice */
function drawMeter(){
  const m=$("meter"), c=m.getContext("2d"), w=m.width, h=m.height; c.clearRect(0,0,w,h);
  const lv=-18+10*Math.sin(T*2.3)*Math.sin(T*0.9)+(S.bypass?0:S.outGain+Math.max(0,totalDb(100,true))*0.3);
  const pk=lv+4+2*Math.sin(T*5);
  const X=d=>clamp((d+60)/60,0,1)*w;
  c.fillStyle=css("--tile"); c.fillRect(0,h*.2,w,h*.25); c.fillRect(0,h*.55,w,h*.25);
  c.fillStyle=css("--accent"); c.fillRect(0,h*.2,X(lv),h*.25); c.fillRect(0,h*.55,X(lv-1.5),h*.25);
  c.fillStyle=css("--fg"); c.fillRect(X(pk)-1.5,h*.2,3,h*.6);
  $("clip").classList.toggle("on", pk>-0.5);
  const rd=Math.floor(T*5); if(rd!==drawMeter.last){ drawMeter.last=rd; $("peakRead").textContent=pk.toFixed(1)+" dB"; }
}

/* ---------- interactions sur la courbe ---------- */
function hit(x,y){
  let best=null,bd=14*14;
  for(const b of S.bands){ const bx=gx(b.freq), by=gy(TYPES[b.type].gain?b.gain:0), d=(bx-x)**2+(by-y)**2; if(d<bd){bd=d;best=b;} }
  return best;
}
function rectPos(e){ const r=cv.getBoundingClientRect(); return {x:e.clientX-r.left,y:e.clientY-r.top}; }

function defaultType(f){ return f<30?"lowcut": f<90?"lowshelf": f>16000?"highcut": f>8000?"highshelf":"bell"; }
// Une bande garde sa case (1 à 24) à vie : c'est elle qui désigne ses paramètres dans Live (« B3 Freq »).
function freeSlot(){ for(let n=1;n<=24;n++) if(!S.bands.some(b=>b.id===n)) return n; return 0; }
function addBand(f,g){
  const slot=freeSlot(); if(!slot) return;
  pushUndo();
  const type=defaultType(f), t=TYPES[type];
  const b={id:slot,type,freq:clamp(f,FMIN,FMAX),gain:t.gain?clamp(g,-30,30):0,q:type==="bell"?1:0.71,slope:12,place:"st",on:true,dyn:false,range:-6};
  S.bands.push(b); S.bands.sort((x,y)=>x.id-y.id); S.sel=b.id; refresh();
}
function removeBand(id){ pushUndo(); S.bands=S.bands.filter(b=>b.id!==id); S.macros.forEach(m=>m.asg=m.asg.filter(a=>a.id!==id)); if(S.sel===id) S.sel=-1; refresh(); }

cv.addEventListener("pointerdown",e=>{
  if(e.button!==0) return; hideMenu();
  const p=rectPos(e), b=hit(p.x,p.y);
  if(!b){ S.sel=-1; refresh(); return; }
  if(e.altKey){ pushUndo(); b.on=!b.on; S.sel=b.id; refresh(); return; }
  S.sel=b.id; pushUndo();
  const hasGain=TYPES[b.type].gain;
  drag={kind:"node",id:b.id,sx:p.x,sy:p.y,f0:b.freq,g0:b.gain,moved:false,hasGain};
  cv.setPointerCapture(e.pointerId); refresh();
});
cv.addEventListener("pointermove",e=>{
  const p=rectPos(e);
  if(!drag){ const b=hit(p.x,p.y); cv.style.cursor=b?"grab":"crosshair"; return; }
  const b=selBand(); if(!b) return;
  const fine=e.shiftKey?0.1:1; cv.style.cursor="grabbing";
  const dx=(p.x-drag.sx)*fine, dy=(p.y-drag.sy)*fine;
  drag.moved=true;
  b.freq=clamp(fx(gx(drag.f0)+dx),FMIN,FMAX);
  if(drag.hasGain) b.gain=clamp(+(drag.g0 - dy*(2*S.range)/(CH-PAD.t-PAD.b)).toFixed(2),-30,30);
  // la gamme d'affichage s'élargit seule quand on dépasse
  if(Math.abs(b.gain)>S.range*0.95){ const nr=[3,6,12,30].find(r=>r>Math.abs(b.gain)/0.95)||30; if(nr>S.range) setRange(nr); }
  refreshPanel();
});
cv.addEventListener("pointerup",e=>{ if(drag&&!drag.moved) S.undo.pop(),syncUndo(); drag=null; cv.style.cursor="crosshair"; refresh(); });
cv.addEventListener("dblclick",e=>{ const p=rectPos(e); if(hit(p.x,p.y)) return; addBand(fx(p.x),yg(p.y)); });
cv.addEventListener("wheel",e=>{
  e.preventDefault(); const p=rectPos(e), b=hit(p.x,p.y)||selBand(); if(!b) return;
  S.sel=b.id; const dir=e.deltaY<0?1:-1, fine=e.shiftKey?0.25:1;
  if(TYPES[b.type].slope && !e.ctrlKey){ b.slope=clamp(b.slope+dir*6,6,96); } else { b.q=clamp(b.q*Math.pow(1.12,dir*fine),0.025,40); }
  refresh();
},{passive:false});
cv.addEventListener("contextmenu",e=>{
  e.preventDefault(); const p=rectPos(e), b=hit(p.x,p.y); if(!b) return; S.sel=b.id; refresh(); showBandMenu(b,p.x,p.y);
});

/* ---------- menus ---------- */
function hideMenu(){ $("menu").hidden=true; }
function showMenu(items,x,y){
  const m=$("menu"); m.innerHTML="";
  for(const it of items){
    if(it==="-"){ const s=document.createElement("div"); s.className="msep"; m.appendChild(s); continue; }
    const bt=document.createElement("button"); bt.className="mi"+(it.danger?" danger":""); bt.setAttribute("role","menuitemradio"); bt.setAttribute("aria-checked",it.on?"true":"false");
    bt.innerHTML=`<span>${it.label}</span>${it.hint?`<small>${it.hint}</small>`:""}`;
    bt.onclick=()=>{ hideMenu(); it.run(); }; m.appendChild(bt);
  }
  m.hidden=false; const st=$("stage").getBoundingClientRect();
  m.style.left=clamp(x,4,st.width-m.offsetWidth-4)+"px"; m.style.top=clamp(y,4,st.height-m.offsetHeight-4)+"px";
}
function showBandMenu(b,x,y){
  const items=[];
  for(const k in TYPES) items.push({label:TYPES[k].n,on:b.type===k,run:()=>{pushUndo();b.type=k; if(!TYPES[k].gain)b.gain=0; if(!TYPES[k].dyn)b.dyn=false; refresh();}});
  items.push("-");
  for(const k in PLACES) items.push({label:PLACES[k].n,on:b.place===k,run:()=>{pushUndo();b.place=k;refresh();}});
  items.push("-");
  if(TYPES[b.type].dyn) items.push({label:b.dyn?"Rendre statique":"Rendre dynamique",hint:"Alt+molette",run:()=>{pushUndo();b.dyn=!b.dyn;refresh();}});
  items.push({label:b.on?"Désactiver":"Activer",hint:"Alt+clic",run:()=>{pushUndo();b.on=!b.on;refresh();}});
  items.push({label:"Dupliquer",run:()=>{ const slot=freeSlot(); if(slot){pushUndo();const c=Object.assign({},b,{id:slot,freq:clamp(b.freq*1.2,FMIN,FMAX)});S.bands.push(c);S.bands.sort((x,y)=>x.id-y.id);S.sel=c.id;refresh();}}});
  items.push({label:"Supprimer",hint:"Suppr",danger:true,run:()=>removeBand(b.id)});
  showMenu(items,x+10,y+10);
}
document.addEventListener("pointerdown",e=>{ if(!$("menu").contains(e.target)) hideMenu(); },true);

/* ---------- valeurs à glisser (même langage que .vb d'Open Groove) ---------- */
function valueBox(el,o){
  const show=()=>{ el.textContent=o.fmt(); };
  let d=null;
  el.onpointerdown=e=>{ if(el.disabled) return; pushUndo(); d={y:e.clientY,v:o.get()}; el.classList.add("drag"); el.setPointerCapture(e.pointerId); };
  el.onpointermove=e=>{ if(!d) return; o.set(o.drag(d.v,(d.y-e.clientY)*(e.shiftKey?0.1:1))); refresh(true); show(); };
  el.onpointerup=()=>{ d=null; el.classList.remove("drag"); };
  el.onwheel=e=>{ e.preventDefault(); pushUndo(); o.set(o.drag(o.get(),(e.deltaY<0?4:-4)*(e.shiftKey?0.1:1))); refresh(true); show(); };
  el.ondblclick=()=>{ o.set(o.def); refresh(true); show(); };
  show();
}

/* ---------- panneau de bande (ancré sous la courbe) ---------- */
function fld(label,node){ const d=document.createElement("div"); d.className="fld"; d.innerHTML=`<span class="lab">${label}</span>`; d.appendChild(node); return d; }
function refreshPanel(){ if(S.view==="curve") buildPanel(); }
function mkVB(o,dis){ const x=document.createElement("button"); x.className="vb"; if(dis) x.disabled=true; valueBox(x,o); return x; }
const fmtMs = v => v==null ? "Auto" : (v<10 ? v.toFixed(1) : Math.round(v))+" ms";
function buildPanel(){
  const bp=$("bp"), b=selBand(); bp.innerHTML="";
  if(!b){
    bp.className="bp empty"; bp.style.removeProperty("--bc");
    bp.innerHTML=`<span><b>Aucune bande sélectionnée.</b> Double-clic sur la courbe pour en créer une, clique sur un nœud pour la régler, ou choisis un preset en haut.</span>`;
    return;
  }
  bp.className="bp"; bp.style.setProperty("--bc",`var(${PLACES[b.place].c})`);
  if(b.ratio===undefined) Object.assign(b,{thresh:null,ratio:4,attack:null,release:null,src:"in",listen:false});
  const t=TYPES[b.type], row=document.createElement("div"); row.className="brow";
  const title=document.createElement("div"); title.className="btitle"; title.innerHTML=`<b>Bande ${b.id}</b><span>${PLACES[b.place].n}</span>`; row.appendChild(title);
  const typ=document.createElement("button"); typ.className="vb sel"; typ.innerHTML=`<span>${t.n}</span><svg viewBox="0 0 12 12"><path d="M2 4l4 4 4-4"/></svg>`;
  typ.onclick=e=>{ const r=typ.getBoundingClientRect(), st=$("stage").getBoundingClientRect(); showBandMenu(b,r.left-st.left,r.top-st.top); e.stopPropagation(); };
  row.appendChild(fld("Type",typ));
  row.appendChild(fld("Fréquence · "+noteName(b.freq),mkVB({get:()=>b.freq,set:v=>b.freq=v,fmt:()=>fmtF(b.freq),drag:(v,dy)=>clamp(v*Math.pow(2,dy/60),FMIN,FMAX),def:1000})));
  row.appendChild(fld("Gain",mkVB({get:()=>b.gain,set:v=>b.gain=v,fmt:()=>t.gain?fmtG(b.gain):"—",drag:(v,dy)=>clamp(v+dy/10,-30,30),def:0},!t.gain)));
  if(t.slope) row.appendChild(fld("Pente",mkVB({get:()=>b.slope,set:v=>b.slope=v,fmt:()=>b.slope+" dB/oct",drag:(v,dy)=>clamp(Math.round((v+dy/8)/6)*6,6,96),def:12})));
  row.appendChild(fld(t.slope?"Résonance":"Q",mkVB({get:()=>b.q,set:v=>b.q=v,fmt:()=>fmtQ(b.q),drag:(v,dy)=>clamp(v*Math.pow(2,dy/50),0.025,40),def:1})));
  const sp=document.createElement("div"); sp.className="sep"; row.appendChild(sp);
  const pl=document.createElement("div"); pl.className="segc"; pl.setAttribute("role","radiogroup");
  for(const k in PLACES){ const bt=document.createElement("button"); bt.textContent=PLACES[k].s; bt.title=PLACES[k].n; bt.style.setProperty("--c",`var(${PLACES[k].c})`); bt.setAttribute("aria-checked",b.place===k); bt.onclick=()=>{pushUndo();b.place=k;refresh();}; pl.appendChild(bt); }
  row.appendChild(fld("Placement",pl));
  if(t.dyn){
    const dy=document.createElement("button"); dy.className="tg"; dy.textContent="Dynamique"; dy.setAttribute("aria-pressed",b.dyn); dy.title="Le gain de la bande suit le niveau du signal";
    dy.onclick=()=>{pushUndo();b.dyn=!b.dyn; if(b.dyn&&b.range===0)b.range=-6; refresh();};
    row.appendChild(fld("Mode",dy));
  }
  const g=document.createElement("div"); g.className="grow"; row.appendChild(g);
  const on=document.createElement("button"); on.className="tg"; on.textContent=b.on?"Actif":"Bypass"; on.setAttribute("aria-pressed",b.on); on.onclick=()=>{pushUndo();b.on=!b.on;refresh();};
  const del=document.createElement("button"); del.className="tg del"; del.textContent="✕ Supprimer"; del.title="Supprimer la bande (Suppr)"; del.onclick=()=>removeBand(b.id);
  const grp=document.createElement("div"); grp.style.cssText="display:flex;gap:6px"; grp.append(on,del); row.appendChild(grp);
  bp.appendChild(row);

  if(t.dyn&&b.dyn){
    const r2=document.createElement("div"); r2.className="brow brow2";
    const t2=document.createElement("div"); t2.className="btitle"; t2.innerHTML=`<b style="color:var(--fg)">Dynamique</b><span>Auto : selon le signal</span>`; r2.appendChild(t2);
    r2.appendChild(fld("Range",mkVB({get:()=>b.range,set:v=>b.range=v,fmt:()=>fmtG(b.range),drag:(v,dy)=>clamp(v+dy/10,-30,30),def:-6})));
    r2.appendChild(fld("Seuil",mkVB({get:()=>b.thresh,set:v=>b.thresh=v,fmt:()=>b.thresh==null?"Auto":b.thresh.toFixed(0)+" dB",drag:(v,dy)=>clamp((v==null?-30:v)+dy/6,-60,0),def:null})));
    r2.appendChild(fld("Ratio",mkVB({get:()=>b.ratio,set:v=>b.ratio=v,fmt:()=>b.ratio>=20?"∞ : 1":b.ratio.toFixed(1)+" : 1",drag:(v,dy)=>clamp(v*Math.pow(2,dy/50),1,20),def:4})));
    r2.appendChild(fld("Attaque",mkVB({get:()=>b.attack,set:v=>b.attack=v,fmt:()=>fmtMs(b.attack),drag:(v,dy)=>clamp((v==null?10:v)*Math.pow(2,dy/50),0.1,200),def:null})));
    r2.appendChild(fld("Relâchement",mkVB({get:()=>b.release,set:v=>b.release=v,fmt:()=>fmtMs(b.release),drag:(v,dy)=>clamp((v==null?200:v)*Math.pow(2,dy/50),10,2000),def:null})));
    const sp2=document.createElement("div"); sp2.className="sep"; r2.appendChild(sp2);
    const src=document.createElement("div"); src.className="segc"; src.setAttribute("role","radiogroup");
    for(const [k,l] of [["in","Entrée"],["sc","Sidechain"]]){ const bt=document.createElement("button"); bt.textContent=l; bt.setAttribute("aria-checked",b.src===k); bt.onclick=()=>{pushUndo();b.src=k;refresh();}; src.appendChild(bt); }
    r2.appendChild(fld("Détection sur",src));
    const ls=document.createElement("button"); ls.className="tg"; ls.textContent="Écouter"; ls.setAttribute("aria-pressed",b.listen); ls.title="Écouter le signal de détection (au casque)"; ls.onclick=()=>{b.listen=!b.listen; ls.setAttribute("aria-pressed",b.listen);};
    r2.appendChild(fld("Contrôle",ls));
    bp.appendChild(r2);
  }
}
function placePanel(){} // le panneau est ancré : plus de positionnement à calculer

/* ---------- barre du bas ---------- */
function seg(id,opts,get,set){
  const el=$(id); el.innerHTML="";
  for(const [k,l] of opts){ const b=document.createElement("button"); b.textContent=l; b.setAttribute("aria-checked",get()===k); b.onclick=()=>{set(k); for(const x of el.children) x.setAttribute("aria-checked",x===b);}; el.appendChild(b); }
}
function setRange(r){ S.range=r; seg("scale",[[3,"±3"],[6,"±6"],[12,"±12"],[30,"±30"]],()=>S.range,setRange); }
function syncBottomControls(){
  seg("scale",[[3,"±3"],[6,"±6"],[12,"±12"],[30,"±30"]],()=>S.range,setRange);
  seg("spec",[["off","Off"],["pre","Pré"],["post","Post"],["prepost","Pré+Post"]],()=>S.spec,v=>S.spec=v);
  seg("fall",[["lente","Lente"],["moy","Moyenne"],["rapide","Rapide"]],()=>S.fall,v=>S.fall=v);
  seg("drng",[[60,"60 dB"],[90,"90 dB"],[120,"120 dB"]],()=>S.dbRange,v=>S.dbRange=v);
  valueBox($("tilt"),{get:()=>S.tilt,set:v=>S.tilt=v,fmt:()=>S.tilt.toFixed(1)+" dB/oct",drag:(v,dy)=>clamp(v+dy/20,0,6),def:4.5});
}
syncBottomControls();
// échelle du vumètre
(function(){ const t=$("mticks"); for(const d of [-60,-40,-20,-6,0]){ const s=document.createElement("span"); s.textContent=d; s.style.left=((d+60)/60*100)+"%"; t.appendChild(s); } })();
// popovers ancrés au-dessus de leur bouton
function togglePop(popId,btn){
  const p=$(popId), open=p.hidden;
  for(const x of document.querySelectorAll(".pop")){ x.hidden=true; }
  for(const b of [$("anBtn"),$("helpBtn")]) b.setAttribute("aria-expanded","false");
  if(!open) return;
  p.hidden=false; const r=btn.getBoundingClientRect(), w=p.offsetWidth;
  p.style.left=clamp(r.right-w,8,window.innerWidth-w-8)+"px"; p.style.top=(r.top-p.offsetHeight-10)+"px";
  btn.setAttribute("aria-expanded","true");
}
$("anBtn").onclick=e=>{ e.stopPropagation(); togglePop("pAn",$("anBtn")); };
$("helpBtn").onclick=e=>{ e.stopPropagation(); togglePop("pHelp",$("helpBtn")); };
document.addEventListener("pointerdown",e=>{
  if(e.target.closest(".pop")||e.target.closest("#anBtn")||e.target.closest("#helpBtn")) return;
  for(const x of document.querySelectorAll(".pop")) x.hidden=true;
  for(const b of [$("anBtn"),$("helpBtn")]) b.setAttribute("aria-expanded","false");
},true);
function syncGlobalsUI(){
  valueBox($("outg"),{get:()=>S.outGain,set:v=>S.outGain=v,fmt:()=>fmtG(S.outGain),drag:(v,dy)=>clamp(v+dy/8,-36,36),def:0});
  $("phase").setAttribute("aria-pressed",S.phase);
  $("bypass").setAttribute("aria-pressed",S.bypass);
}
syncGlobalsUI();
$("phase").onclick=function(){ S.phase=!S.phase; this.setAttribute("aria-pressed",S.phase); refresh(true); };
$("bypass").onclick=function(){ S.bypass=!S.bypass; this.setAttribute("aria-pressed",S.bypass); refresh(true); };
$("undo").onclick=doUndo; $("redo").onclick=doRedo;
document.addEventListener("keydown",e=>{
  if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==="z"){ e.preventDefault(); e.shiftKey?doRedo():doUndo(); }
  else if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==="y"){ e.preventDefault(); doRedo(); }
  else if((e.key==="Delete"||e.key==="Backspace")&&S.sel>=0&&S.view==="curve"){ removeBand(S.sel); }
  else if(e.key==="Escape"){ hideMenu(); S.sel=-1; refresh(); }
});

/* ---------- presets ----------
   Chaque preset : bandes (type, fréquence, gain, Q, options) et macros ([indice de bande, réglage, valeur à 0 %, valeur à 100 %]). */
const B = (type,freq,gain,q,o) => Object.assign({type,freq,gain,q,slope:12,place:"st",on:true,dyn:false,range:0},o||{});
const PRESETS = {
  Kick:[
    {n:"Vintage 70s",d:"Rond, chaud, grave arrondi",
     bands:[B("lowcut",30,0,.71),B("lowshelf",70,3.5,.7),B("bell",330,-3.5,1.2),B("bell",2500,1.5,.9),B("highcut",12000,0,.71)],
     macros:[{n:"Chaleur",a:[[1,"gain",3.5,6.5]]},{n:"Boîte",a:[[2,"gain",-3.5,-6]]}]},
    {n:"Moderne punch",d:"Sub contrôlé, creux net, clic précis",
     bands:[B("lowcut",25,0,.71,{slope:24}),B("bell",60,3,1.4),B("bell",320,-4,1.8),B("bell",4500,3,.8),B("highshelf",10000,1.5,.7)],
     macros:[{n:"Punch",a:[[1,"gain",3,6],[3,"gain",3,5]]},{n:"Clic",a:[[3,"gain",3,7],[3,"freq",4500,6500]]}]},
    {n:"Dub profond",d:"Sub énorme, aigus fermés, bas mono",
     bands:[B("lowcut",28,0,.71,{slope:24}),B("bell",52,4,1.6),B("bell",140,-2,1.5),B("highcut",7000,0,.71,{slope:24}),B("lowcut",120,0,.71,{slope:24,place:"side"})],
     macros:[{n:"Sub",a:[[1,"gain",4,8]]},{n:"Sombre",a:[[3,"freq",7000,2500]]}]}
  ],
  Bass:[
    {n:"Vintage Motown",d:"Rondeur, médiums chantants, aigus coupés",
     bands:[B("lowcut",35,0,.71),B("bell",90,3,1),B("bell",220,-2.5,1.5),B("bell",700,2,.8),B("highcut",4500,0,.71)],
     macros:[{n:"Rondeur",a:[[1,"gain",3,6]]},{n:"Mordant",a:[[3,"gain",2,5]]}]},
    {n:"Moderne clean",d:"Sub propre, creux à 250 Hz, grain à 1,4 kHz, mono",
     bands:[B("lowcut",30,0,.71,{slope:24}),B("bell",55,2.5,1.5),B("bell",250,-3,2),B("bell",1400,2.5,1.2),B("lowcut",150,0,.71,{slope:24,place:"side"})],
     macros:[{n:"Grain",a:[[3,"gain",2.5,7]]},{n:"Sub",a:[[1,"gain",2.5,6]]}]},
    {n:"Dub sub",d:"Sub dynamique, filtre dub sur le haut",
     bands:[B("lowcut",22,0,.71,{slope:24}),B("bell",50,4,1.5),B("bell",90,0,2,{dyn:true,range:-6}),B("bell",300,-3,1.5),B("highcut",3500,0,.71,{slope:24})],
     macros:[{n:"Filtre dub",a:[[4,"freq",3500,400],[4,"q",.71,2.5]]},{n:"Sub",a:[[1,"gain",4,8]]}]},
    {n:"House pompe",d:"Le grave se creuse quand le kick tape (sidechain)",
     bands:[B("lowcut",30,0,.71,{slope:12}),B("bell",70,2,1.2,{dyn:true,range:-5}),B("bell",200,-2,1.6),B("bell",2000,2,1)],
     macros:[{n:"Pompe",a:[[1,"range",-5,-14]]},{n:"Présence",a:[[3,"gain",2,6]]}]}
  ],
  Lead:[
    {n:"Vintage analog",d:"Synthé chaud, médiums gras, aigus doux",
     bands:[B("lowcut",160,0,.71),B("bell",900,2,.9),B("bell",3000,2.5,1),B("highshelf",9000,2,.7),B("highcut",15000,0,.71)],
     macros:[{n:"Brillance",a:[[3,"gain",2,6]]},{n:"Gras",a:[[1,"gain",2,5]]}]},
    {n:"Moderne présent",d:"Dégage le bas, présence et air",
     bands:[B("lowcut",220,0,.71,{slope:24}),B("bell",350,-3,1.5),B("bell",3500,2.5,1),B("highshelf",11000,3,.7)],
     macros:[{n:"Air",a:[[3,"gain",3,7]]},{n:"Présence",a:[[2,"gain",2.5,6]]}]},
    {n:"Large stéréo",d:"Aigus sur le Side, médiums bas gardés au centre",
     bands:[B("lowcut",180,0,.71,{slope:24}),B("bell",3000,2,1),B("highshelf",8000,3,.7,{place:"side"}),B("lowcut",450,0,.71,{slope:12,place:"side"})],
     macros:[{n:"Largeur",a:[[2,"gain",3,8]]}]},
    {n:"Dub tape",d:"Bande passante étroite, effet tape",
     bands:[B("lowcut",250,0,.71,{slope:12}),B("highcut",5500,0,.71,{slope:18}),B("bell",1800,3,1.3),B("bell",6500,0,3,{dyn:true,range:-5})],
     macros:[{n:"Filtre dub",a:[[1,"freq",5500,900],[1,"q",.71,2.2]]}]}
  ],
  Vocal:[
    {n:"Vintage radio",d:"Chaleur et grain d'époque, aigus soyeux",
     bands:[B("lowcut",100,0,.71),B("bell",200,1.5,1),B("bell",3000,3,.9),B("highshelf",12000,3,.7),B("bell",450,-1.5,1.5)],
     macros:[{n:"Présence",a:[[2,"gain",3,6]]},{n:"Soie",a:[[3,"gain",3,6]]}]},
    {n:"Moderne propre",d:"Creux à 250 Hz, présence, dé-esser dynamique",
     bands:[B("lowcut",80,0,.71,{slope:18}),B("bell",250,-2.5,1.6),B("bell",3500,2,1),B("bell",7000,0,4,{dyn:true,range:-6}),B("highshelf",12000,2.5,.7)],
     macros:[{n:"Air",a:[[4,"gain",2.5,6]]},{n:"Dé-esser",a:[[3,"range",-6,-12]]}]},
    {n:"Parlé / podcast",d:"Voix pleine et intelligible",
     bands:[B("lowcut",90,0,.71,{slope:24}),B("bell",140,2,.9),B("bell",400,-2,1.5),B("bell",4500,2.5,1.1),B("highcut",14000,0,.71)],
     macros:[{n:"Corps",a:[[1,"gain",2,5]]},{n:"Clarté",a:[[3,"gain",2.5,6]]}]},
    {n:"Dub téléphone",d:"Voix filtrée, médiums nasaux",
     bands:[B("lowcut",350,0,.71,{slope:24}),B("highcut",4500,0,.71,{slope:24}),B("bell",1200,4,1.4)],
     macros:[{n:"Filtre dub",a:[[0,"freq",350,1500],[1,"freq",4500,2200]]}]}
  ],
  Drums:[
    {n:"Vintage kit",d:"Bus de batterie rond, caisse claire qui claque doucement",
     bands:[B("lowcut",40,0,.71),B("bell",100,2,1),B("bell",400,-2.5,1.2),B("bell",5000,2,.9),B("highshelf",11000,2.5,.7)],
     macros:[{n:"Corps",a:[[1,"gain",2,5]]},{n:"Éclat",a:[[3,"gain",2,5],[4,"gain",2.5,5]]}]},
    {n:"Moderne claquant",d:"Bus serré, creux à 300 Hz, attaque nette",
     bands:[B("lowcut",30,0,.71,{slope:24}),B("bell",80,2,1.2),B("bell",300,-3,1.5),B("bell",3000,2,1),B("highshelf",12000,3,.7)],
     macros:[{n:"Attaque",a:[[3,"gain",2,6]]},{n:"Air",a:[[4,"gain",3,7]]}]},
    {n:"Dub room",d:"Son de salle sombre, aigus fermés",
     bands:[B("lowcut",40,0,.71),B("highcut",9000,0,.71),B("bell",200,2,1),B("bell",4000,-2,1)],
     macros:[{n:"Filtre dub",a:[[1,"freq",9000,1200],[1,"q",.71,2]]}]},
    {n:"Overheads / hats",d:"Dégage le bas, ajoute de l'air, calme les stridences",
     bands:[B("lowcut",300,0,.71,{slope:24}),B("bell",600,-2,1),B("highshelf",10000,3,.7),B("bell",3500,0,2.5,{dyn:true,range:-4})],
     macros:[{n:"Air",a:[[2,"gain",3,7]]},{n:"Dompter",a:[[3,"range",-4,-10]]}]}
  ],
  Pad:[
    {n:"Chaud vintage",d:"Nappe douce, médiums arrondis",
     bands:[B("lowcut",120,0,.71),B("bell",300,1.5,.8),B("bell",2500,-2,1),B("highcut",9000,0,.71)],
     macros:[{n:"Chaleur",a:[[1,"gain",1.5,4.5]]},{n:"Voile",a:[[3,"freq",9000,3000]]}]},
    {n:"Aérien moderne",d:"Nappe large et brillante, bas dégagé",
     bands:[B("lowcut",200,0,.71,{slope:24}),B("bell",400,-2.5,1),B("highshelf",10000,3.5,.7),B("highshelf",6000,2,.7,{place:"side"})],
     macros:[{n:"Air",a:[[2,"gain",3.5,7]]},{n:"Largeur",a:[[3,"gain",2,6]]}]},
    {n:"Sombre dub",d:"Nappe étouffée qui respire avec le filtre",
     bands:[B("lowcut",180,0,.71),B("highcut",4500,0,.71,{slope:18}),B("bell",900,-2,1)],
     macros:[{n:"Filtre dub",a:[[1,"freq",4500,600],[1,"q",.71,2.5]]}]}
  ],
  Master:[
    {n:"Transparent",d:"Retouche légère, bas mono sous 100 Hz",
     bands:[B("lowcut",20,0,.71),B("lowshelf",60,1,.7),B("bell",300,-1,.8),B("highshelf",12000,1.5,.5),B("lowcut",100,0,.71,{place:"side"})],
     macros:[{n:"Brillance",a:[[3,"gain",1.5,4]]},{n:"Assise",a:[[1,"gain",1,3]]}]},
    {n:"Chaud dub",d:"Grave plein, aigus adoucis",
     bands:[B("lowcut",22,0,.71),B("lowshelf",80,1.5,.7),B("bell",3000,-1,.7),B("highshelf",8000,-1,.6),B("lowcut",90,0,.71,{place:"side"})],
     macros:[{n:"Assise",a:[[1,"gain",1.5,4]]},{n:"Douceur",a:[[3,"gain",-1,-4]]}]},
    {n:"Brillant house",d:"Ouvert et énergique, médiums dégagés",
     bands:[B("lowcut",25,0,.71),B("lowshelf",100,1,.7),B("bell",250,-1.5,1),B("highshelf",10000,2,.6),B("lowcut",100,0,.71,{place:"side"})],
     macros:[{n:"Éclat",a:[[3,"gain",2,5]]}]}
  ],
  Guitare:[
    {n:"Acoustique",d:"Retire le flou, ajoute brillance et air",
     bands:[B("lowcut",80,0,.71,{slope:18}),B("bell",200,-2,1.2),B("bell",3000,2,.9),B("highshelf",10000,2.5,.7)],
     macros:[{n:"Brillance",a:[[2,"gain",2,5],[3,"gain",2.5,5]]}]},
    {n:"Électrique rythmique",d:"Place dans le mix, médiums mordants",
     bands:[B("lowcut",100,0,.71,{slope:24}),B("bell",400,-2,1.2),B("bell",2500,2.5,1),B("highcut",9000,0,.71,{slope:24})],
     macros:[{n:"Mordant",a:[[2,"gain",2.5,6]]}]},
    {n:"Skank reggae / dub",d:"Coups de contretemps secs et nets",
     bands:[B("lowcut",250,0,.71,{slope:24}),B("bell",1500,2.5,1.2),B("highcut",7000,0,.71)],
     macros:[{n:"Filtre dub",a:[[2,"freq",7000,1200],[2,"q",.71,2]]},{n:"Mordant",a:[[1,"gain",2.5,6]]}]}
  ]
};
function applyPreset(cat,i){
  const p=PRESETS[cat][i]; pushUndo();
  S.bands=p.bands.map((b,i)=>Object.assign({},b,{id:i+1})); // cases 1 à n, les suivantes sont libérées
  const ids=S.bands.map(b=>b.id);
  S.macros=emptyMacros();
  (p.macros||[]).forEach((m,k)=>{ S.macros[k]={name:m.n,v:0,asg:m.a.map(([bi,pp,a,b])=>({id:ids[bi],p:pp,a,b}))}; });
  S.sel=-1; S.presetName=cat+" · "+p.n; S.dirty=false; refresh(); renderMacros();
}
function resetInit(){ pushUndo(); S.bands=[]; S.macros=emptyMacros(); S.sel=-1; S.presetName="Init"; S.dirty=false; refresh(); renderMacros(); }
function togglePresets(){
  const pm=$("pm"); if(!pm.hidden){ pm.hidden=true; return; }
  pm.innerHTML="";
  for(const cat in PRESETS){
    const col=document.createElement("div"); col.className="pcol"; col.innerHTML=`<div class="ptitle">${cat}</div>`;
    PRESETS[cat].forEach((p,i)=>{
      const bt=document.createElement("button"); bt.className="pi"; bt.setAttribute("aria-checked",S.presetName===cat+" · "+p.n);
      bt.innerHTML=`<b>${p.n}</b><small>${p.d}</small>`; bt.title=p.d;
      bt.onclick=()=>{ pm.hidden=true; applyPreset(cat,i); }; col.appendChild(bt);
    });
    pm.appendChild(col);
  }
  const f=document.createElement("div"); f.className="pfoot"; f.innerHTML=`<span>Les presets d'usine chargent aussi leurs macros.</span>`;
  const r=document.createElement("button"); r.className="link"; r.textContent="Réinitialiser (Init)"; r.style.cssText="border:0;background:none;color:var(--dim);text-decoration:underline;text-underline-offset:3px;font-size:11px;padding:0";
  r.onclick=()=>{ pm.hidden=true; resetInit(); }; f.appendChild(r); pm.appendChild(f);
  const br=$("presetBtn").getBoundingClientRect(), w=Math.max(br.width,660);
  pm.style.width=w+"px"; pm.style.left=clamp(br.left,8,window.innerWidth-w-8)+"px"; pm.hidden=false;
}
$("presetBtn").onclick=e=>{ e.stopPropagation(); togglePresets(); };
document.addEventListener("pointerdown",e=>{ const pm=$("pm"); if(!pm.hidden && !pm.contains(e.target) && !$("presetBtn").contains(e.target)) pm.hidden=true; },true);

/* ---------- macros ---------- */
const PL = {freq:"Fréq",gain:"Gain",q:"Q",range:"Range"};
const fmtP = (p,v) => p==="freq"?fmtF(v): p==="q"?fmtQ(v): fmtG(v);
const dragP = p => p==="freq" ? (v,dy)=>clamp(v*Math.pow(2,dy/60),FMIN,FMAX) : p==="q" ? (v,dy)=>clamp(v*Math.pow(2,dy/50),0.025,40) : (v,dy)=>clamp(v+dy/10,-30,30);
function applyMacro(m){
  for(const s of m.asg){
    const b=S.bands.find(x=>x.id===s.id); if(!b) continue;
    const v = (s.p==="freq"||s.p==="q") ? s.a*Math.pow(s.b/s.a,m.v) : s.a+(s.b-s.a)*m.v;
    b[s.p] = s.p==="freq" ? clamp(v,FMIN,FMAX) : s.p==="q" ? clamp(v,0.025,40) : clamp(v,-30,30);
  }
}
function addAsg(mi,p){
  const b=selBand(); if(!b) return;
  if(S.macros.some(m=>m.asg.some(a=>a.id===b.id&&a.p===p))) return; // un réglage ne se pilote que par une macro
  pushUndo(); const m=S.macros[mi], cur=b[p]; let to;
  if(p==="freq") to=clamp(cur*(cur>1000?0.25:4),FMIN,FMAX); else if(p==="q") to=clamp(cur*3,0.025,40); else to=clamp(cur+(p==="range"?-6:6),-30,30);
  m.v=0; m.asg.push({id:b.id,p,a:cur,b:to}); if(m.name.startsWith("Macro ")) m.name="Macro "+(mi+1);
  renderMacros(); refresh(true);
}
function renderMacros(){
  const g=$("macgrid"), sb=selBand(); g.innerHTML="";
  $("macsel").innerHTML = sb ? `Bande sélectionnée : <b>B${sb.id} · ${TYPES[sb.type].n} · ${fmtF(sb.freq)}</b>. Choisis une macro et ajoute un de ses réglages.` : `Aucune bande sélectionnée. Sélectionne une bande dans l'onglet <b>Courbe</b>, puis reviens l'assigner à une macro.`;
  S.macros.forEach((m,mi)=>{
    const t=document.createElement("div"); t.className="mt"+(m.asg.length?"":" empty");
    const fd=document.createElement("div"); fd.className="fd"; fd.innerHTML=`<div class="fdf" style="height:${m.v*100}%"></div>`;
    const body=document.createElement("div"); body.className="mb";
    const head=document.createElement("div"); head.className="mh"; head.innerHTML=`<span class="mn">${m.name}</span><span class="mv">${Math.round(m.v*100)}%</span>`;
    const setV=y=>{ const r=fd.getBoundingClientRect(); m.v=clamp(1-(y-r.top)/r.height,0,1); applyMacro(m); fd.firstChild.style.height=m.v*100+"%"; head.lastChild.textContent=Math.round(m.v*100)+"%"; refresh(true); };
    let on=false;
    fd.onpointerdown=e=>{ pushUndo(); on=true; fd.setPointerCapture(e.pointerId); setV(e.clientY); };
    fd.onpointermove=e=>{ if(on) setV(e.clientY); };
    fd.onpointerup=()=>{ on=false; };
    fd.ondblclick=()=>{ m.v=0; applyMacro(m); renderMacros(); refresh(true); };
    fd.onwheel=e=>{ e.preventDefault(); m.v=clamp(m.v+(e.deltaY<0?0.04:-0.04),0,1); applyMacro(m); renderMacros(); refresh(true); };
    body.appendChild(head);
    if(!m.asg.length){ const h=document.createElement("div"); h.className="as"; h.textContent="Aucun réglage assigné"; body.appendChild(h); }
    m.asg.forEach((s,si)=>{
      const b=S.bands.find(x=>x.id===s.id); if(!b) return;
      const row=document.createElement("div"); row.className="as";
      const lab=document.createElement("span"); lab.className="al"; lab.textContent=`B${b.id} ${PL[s.p]}`; lab.title=`${TYPES[b.type].n}, ${PL[s.p]}`;
      const mk=k=>{ const x=document.createElement("button"); x.className="vb"; valueBox(x,{get:()=>s[k],set:v=>{s[k]=v; applyMacro(m);},fmt:()=>fmtP(s.p,s[k]),drag:dragP(s.p),def:s[k]}); return x; };
      const arrow=document.createElement("span"); arrow.textContent="→";
      const del=document.createElement("button"); del.className="x"; del.textContent="✕"; del.title="Retirer"; del.onclick=()=>{ pushUndo(); m.asg.splice(si,1); renderMacros(); };
      row.append(lab,mk("a"),arrow,mk("b"),del); body.appendChild(row);
    });
    const adds=document.createElement("div"); adds.className="adds";
    const al=document.createElement("span"); al.textContent="Ajouter :"; al.style.cssText="font-size:11px;color:var(--dim);align-self:center"; adds.appendChild(al);
    for(const p of ["freq","gain","q","range"]){
      const ok=sb && (p==="freq"||p==="q"|| (p==="gain"&&TYPES[sb.type].gain) || (p==="range"&&sb.dyn)) && !S.macros.some(x=>x.asg.some(a=>a.id===sb.id&&a.p===p));
      const bt=document.createElement("button"); bt.textContent="+ "+PL[p]; bt.disabled=!ok; bt.onclick=()=>addAsg(mi,p); adds.appendChild(bt);
    }
    body.appendChild(adds); t.append(fd,body); g.appendChild(t);
  });
}
function setView(v){
  S.view=v; $("stage").hidden=v!=="curve"; $("macv").hidden=v!=="macros"; $("bp").hidden=v!=="curve";
  $("tCurve").setAttribute("aria-selected",v==="curve"); $("tMac").setAttribute("aria-selected",v==="macros");
  hideMenu(); if(v==="macros") renderMacros(); else { buildPanel(); resize(); }
}
$("tCurve").onclick=()=>setView("curve"); $("tMac").onclick=()=>setView("macros");

/* ---------- boucle ---------- */
function refresh(light){
  $("presetName").textContent = S.presetName + (S.dirty?" · modifié":"");
  $("bandCount").textContent=S.bands.length+(S.bands.length>1?" bandes":" bande");
  $("hint").hidden=S.bands.length>0;
  if(!light) buildPanel(); else placePanel();
  syncUndo();
  if(typeof bridgePush==="function") bridgePush();
}
let last=performance.now();
function loop(now){ T+=(now-last)/1000; last=now; stepSpectrum(); draw(); drawMeter(); requestAnimationFrame(loop); }

