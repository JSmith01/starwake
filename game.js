(() => {
  'use strict';
  const canvas = document.getElementById('game');
  const ctx = canvas.getContext('2d');
  const W = 960, H = 540;
  const keys = new Set();
  let mode = 'title', last = 0, time = 0, score = 0, lives = 3, muted = false;
  let player, bullets, enemyShots, enemies, particles, stars, pickups, boss, shake = 0;
  let audio = null, fireClock = 0, spawnClock = 0, charge = 0, charging = false, levelClock = 0, warning = 0;
  const clamp = (n,a,b)=>Math.max(a,Math.min(b,n));
  const rand = (a,b)=>a+Math.random()*(b-a);
  const reset = () => {
    time=0;score=0;lives=3;bullets=[];enemyShots=[];enemies=[];particles=[];pickups=[];boss=null;
    levelClock=0;spawnClock=.6;fireClock=0;charge=0;charging=false;warning=0;shake=0;
    player={x:125,y:H/2,hp:1,invuln:1.8};
    stars=Array.from({length:115},()=>({x:Math.random()*W,y:Math.random()*H,z:rand(.3,1.7),r:rand(.4,1.6),a:rand(.2,.9)}));
  };
  reset();
  function sound(freq=440,duration=.08,type='square',volume=.045,slide=0){
    if(muted)return;
    try{
      if(!audio)audio=new (window.AudioContext||window.webkitAudioContext)();
      if(audio.state==='suspended')audio.resume();
      const o=audio.createOscillator(),g=audio.createGain();o.type=type;o.frequency.setValueAtTime(freq,audio.currentTime);
      if(slide)o.frequency.exponentialRampToValueAtTime(Math.max(30,freq+slide),audio.currentTime+duration);
      g.gain.setValueAtTime(volume,audio.currentTime);g.gain.exponentialRampToValueAtTime(.001,audio.currentTime+duration);
      o.connect(g);g.connect(audio.destination);o.start();o.stop(audio.currentTime+duration);
    }catch(_){ }
  }
  const burst=(x,y,color,n=10,power=100)=>{for(let i=0;i<n;i++){const a=Math.random()*Math.PI*2,s=rand(power*.25,power);particles.push({x,y,vx:Math.cos(a)*s,vy:Math.sin(a)*s,life:rand(.25,.7),max:.7,color,r:rand(1,3)})}};
  function addEnemy(kind,x,y){const data={scout:[24,18,1,115,110],weaver:[28,23,2,74,120],turret:[31,31,4,42,90],heavy:[42,35,7,29,170]}[kind];enemies.push({kind,x,y,baseY:y,hp:data[2],max:data[2],speed:data[3],fire:data[4]/60,phase:Math.random()*6,age:0});}
  function shoot(){bullets.push({x:player.x+23,y:player.y,vx:590,vy:0,r:4,damage:1,charge:false});sound(640,.055,'triangle',.025,260)}
  function chargeShot(){const d=charge;bullets.push({x:player.x+28,y:player.y,vx:440+120*d,vy:0,r:7+9*d,damage:2+Math.floor(d*5),charge:true,life:1.2});charge=0;charging=false;burst(player.x+25,player.y,'#77fff1',12,170);sound(280,.28,'sawtooth',.07,720)}
  function start(){reset();mode='playing';sound(330,.18,'triangle',.04,220)}
  function hitPlayer(){if(player.invuln>0||mode!=='playing')return;lives--;player.invuln=1.8;shake=9;burst(player.x,player.y,'#ff729a',28,190);sound(140,.38,'sawtooth',.075,-90);if(lives<=0){mode='gameover';sound(100,.7,'triangle',.07,-60)}}
  function fireEnemy(e){if(e.x<0)return;const dx=player.x-e.x,dy=player.y-e.y,len=Math.hypot(dx,dy)||1;enemyShots.push({x:e.x-15,y:e.y,vx:dx/len*190-35,vy:dy/len*190,r:5,color:'#ff8b76'});sound(190,.06,'sine',.012,-30)}
  function spawnWave(){
    const t=levelClock;
    if(t<35){addEnemy('scout',W+35,rand(65,H-65));}
    else if(t<82){const k=Math.random()<.65?'weaver':'scout';addEnemy(k,W+35,rand(55,H-55));if(Math.random()<.42)addEnemy('scout',W+105,rand(60,H-60));}
    else if(t<132){const k=Math.random()<.48?'turret':'weaver';addEnemy(k,W+40,rand(65,H-65));if(Math.random()<.52)addEnemy('scout',W+80,rand(60,H-60));}
    else if(t<172){addEnemy(Math.random()<.52?'heavy':'weaver',W+45,rand(70,H-70));addEnemy('turret',W+100,rand(65,H-65));}
  }
  function makeBoss(){boss={x:W+120,y:H/2,hp:180,max:180,age:0,phase:1,fire:1.1,weakOpen:true,flash:0};warning=0;}
  function update(dt){
    time+=dt;shake=Math.max(0,shake-dt*25);
    for(const s of stars){s.x-=s.z*52*dt;if(s.x<0){s.x=W;s.y=Math.random()*H}}
    for(const p of particles){p.x+=p.vx*dt;p.y+=p.vy*dt;p.vx*=Math.pow(.18,dt);p.vy*=Math.pow(.18,dt);p.life-=dt}
    particles=particles.filter(p=>p.life>0);
    if(mode!=='playing')return;
    levelClock+=dt;player.invuln=Math.max(0,player.invuln-dt);
    const dx=(keys.has('arrowright')||keys.has('d')?1:0)-(keys.has('arrowleft')||keys.has('a')?1:0);
    const dy=(keys.has('arrowdown')||keys.has('s')?1:0)-(keys.has('arrowup')||keys.has('w')?1:0);
    player.x=clamp(player.x+dx*270*dt,42,500);player.y=clamp(player.y+dy*270*dt,45,H-45);
    if(keys.has('shift')){charging=true;charge=Math.min(1,charge+dt*.42)}else if(charging&&charge>.08)chargeShot();else charging=false;
    fireClock-=dt;if(keys.has(' ')&&!charging&&fireClock<=0){shoot();fireClock=.16}
    if(!boss){
      if(levelClock<172){spawnClock-=dt;if(spawnClock<=0){spawnWave();spawnClock=rand(levelClock<35?.95:1.1,levelClock<35?1.5:1.75)}}
      if(levelClock>172&&levelClock<177){warning+=dt;if(warning>0.9&&Math.floor(warning*3)%2===0)sound(560,.08,'sine',.018,-170)}
      if(levelClock>=177){makeBoss();sound(80,.85,'sawtooth',.09,180)}
    }
    for(const e of enemies){e.age+=dt;e.x-=e.speed*dt;if(e.kind==='weaver')e.y=e.baseY+Math.sin(e.age*3+e.phase)*74;if(e.kind==='turret'||e.kind==='heavy'){e.y=e.baseY+Math.sin(e.age*1.3+e.phase)*18}e.fire-=dt;if(e.fire<=0&&e.x<W-50){fireEnemy(e);e.fire=rand(1.2,2.4)}}
    for(const b of bullets){b.x+=b.vx*dt;b.y+=b.vy*dt;if(b.life)b.life-=dt}
    for(const b of enemyShots){b.x+=b.vx*dt;b.y+=b.vy*dt}
    for(const e of enemies){for(const b of bullets){const ew=e.kind==='heavy'?42:e.kind==='turret'?31:e.kind==='weaver'?28:24;if(b.x>e.x-ew/2-10&&b.x<e.x+ew/2+10&&Math.abs(b.y-e.y)<(e.kind==='heavy'?23:20)&&!b.dead){e.hp-=b.damage;b.dead=true;burst(b.x,b.y,'#8cfff3',4,55);if(e.hp<=0){e.dead=true;score+=e.max*100;burst(e.x,e.y,e.kind==='heavy'?'#ff9c6c':'#73f3ed',16,130);sound(300,.12,'triangle',.035,280);if(Math.random()<.1)pickups.push({x:e.x,y:e.y,r:9})}}}}
    if(boss){
      boss.age+=dt;boss.x+=((boss.x>735)?-55:0)*dt;boss.y=H/2+Math.sin(boss.age*1.25)*78;boss.phase=boss.hp>120?1:boss.hp>55?2:3;boss.weakOpen=Math.sin(boss.age*(boss.phase===1?1.3:2.1))>.12;boss.fire-=dt;
      if(boss.fire<=0){const count=boss.phase===1?3:boss.phase===2?5:7;for(let i=0;i<count;i++){const spread=(i-(count-1)/2)*.18;enemyShots.push({x:boss.x-72,y:boss.y+spread*90,vx:-245,vy:spread*230+Math.sin(boss.age*2)*30,r:7,color:'#ff5c91'})}if(boss.phase>=2&&Math.random()<.55){for(let i=0;i<3;i++)enemyShots.push({x:boss.x-30,y:boss.y+(i-1)*50,vx:-210,vy:(i-1)*65,r:6,color:'#f5bc61'})}boss.fire= boss.phase===1?1.35:boss.phase===2?.96:.72;sound(120,.11,'sawtooth',.03,-40)}
      for(const b of bullets){const weakX=boss.x-64,weakY=boss.y;if(!b.dead&&Math.hypot(b.x-weakX,b.y-weakY)<(b.charge?42:27)){if(boss.weakOpen){boss.hp-=b.damage;boss.flash=.12;score+=10;burst(weakX,weakY,'#bafff8',5,80)}else{burst(b.x,b.y,'#8293a8',3,45)}b.dead=true}}
      boss.flash=Math.max(0,boss.flash-dt);if(boss.hp<=0){boss=null;mode='victory';score+=5000;burst(760,270,'#8fffee',100,320);sound(520,.9,'triangle',.09,800)}
    }
    for(const b of enemyShots){if(Math.hypot(b.x-player.x,b.y-player.y)<b.r+13){b.dead=true;hitPlayer()}}
    for(const e of enemies){if(Math.hypot(e.x-player.x,e.y-player.y)<24)hitPlayer()}
    for(const p of pickups){p.x-=130*dt;if(Math.hypot(p.x-player.x,p.y-player.y)<26){p.dead=true;lives=Math.min(5,lives+1);sound(700,.2,'sine',.05,400);burst(p.x,p.y,'#9afcaa',12,100)}}
    enemies=enemies.filter(e=>!e.dead&&e.x>-70);bullets=bullets.filter(b=>!b.dead&&b.x<W+50&&(!b.life||b.life>0));enemyShots=enemyShots.filter(b=>!b.dead&&b.x>-30&&b.x<W+30&&b.y>-30&&b.y<H+30);pickups=pickups.filter(p=>!p.dead&&p.x>-20);
  }
  function polygon(points,fill,stroke){ctx.beginPath();points.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));ctx.closePath();if(fill){ctx.fillStyle=fill;ctx.fill()}if(stroke){ctx.strokeStyle=stroke;ctx.lineWidth=1.5;ctx.stroke()}}
  function drawShip(x,y,scale=1,color='#8efff1'){
    ctx.save();ctx.translate(x,y);ctx.scale(scale,scale);
    ctx.fillStyle='#ff9d62';ctx.beginPath();ctx.moveTo(-18,-6);ctx.lineTo(-38,0);ctx.lineTo(-18,6);ctx.fill();ctx.fillStyle='#ffd18b';ctx.fillRect(-19,-3,10,6);
    polygon([[-23,0],[-10,-12],[8,-10],[29,0],[8,10],[-10,12]],'#192e43',color);polygon([[-5,-8],[9,-5],[21,0],[9,5],[-5,8],[1,0]],'#b9fff5','#e3ffff');
    polygon([[-12,-8],[-18,-19],[0,-12]],'#28475d','#6bbdc7');polygon([[-12,8],[-18,19],[0,12]],'#28475d','#6bbdc7');ctx.restore();
  }
  function drawEnemy(e){ctx.save();ctx.translate(e.x,e.y);const c=e.kind==='heavy'?'#fb956e':e.kind==='turret'?'#f8bd66':e.kind==='weaver'?'#c18cff':'#ff718a';
    if(e.kind==='scout'){polygon([[22,0],[0,-13],[-15,-10],[-8,0],[-15,10],[0,13]],'#421f3a',c);ctx.fillStyle=c;ctx.fillRect(3,-3,8,6)}
    else if(e.kind==='weaver'){polygon([[20,0],[1,-17],[-17,-9],[-5,0],[-17,9],[1,17]],'#32264e',c);polygon([[-5,-8],[-24,-18],[-13,-2]],'#53366c',c);polygon([[-5,8],[-24,18],[-13,2]],'#53366c',c);ctx.fillStyle='#fff3cf';ctx.fillRect(5,-3,7,6)}
    else {ctx.fillStyle=e.kind==='heavy'?'#502d32':'#3f352a';ctx.strokeStyle=c;ctx.lineWidth=2;ctx.beginPath();ctx.roundRect(-20,-18,40,36,8);ctx.fill();ctx.stroke();ctx.fillStyle=c;ctx.fillRect(-23,-5,12,10);ctx.fillStyle='#fff0ca';ctx.fillRect(-8,-5,9,10);}
    if(e.max>1){ctx.fillStyle='#1b2531';ctx.fillRect(-17,-25,34,3);ctx.fillStyle=c;ctx.fillRect(-17,-25,34*e.hp/e.max,3)}ctx.restore();}
  function drawBoss(){if(!boss)return;ctx.save();ctx.translate(boss.x,boss.y);const glow=boss.weakOpen?'#9cfff1':'#f36d91';ctx.shadowBlur=24;ctx.shadowColor=glow;
    polygon([[105,-5],[55,-70],[10,-57],[-10,-91],[-51,-73],[-69,-40],[-83,-16],[-117,-8],[-115,14],[-82,22],[-61,65],[-17,85],[1,57],[51,70],[104,7]],'#22253a','#ff7c9a');ctx.shadowBlur=0;
    polygon([[38,-47],[69,-58],[97,-9],[56,-18]],'#492d54','#d595ff');polygon([[38,47],[69,58],[97,9],[56,18]],'#492d54','#d595ff');
    ctx.fillStyle='#a24c77';ctx.beginPath();ctx.ellipse(-54,0,28,36,0,0,Math.PI*2);ctx.fill();ctx.fillStyle=boss.weakOpen?'#d5fff3':'#fa527a';ctx.beginPath();ctx.arc(-64,0,15,0,Math.PI*2);ctx.fill();ctx.fillStyle='#fff';ctx.beginPath();ctx.arc(-68,-4,4,0,Math.PI*2);ctx.fill();
    for(let i=0;i<3;i++){ctx.fillStyle='#f7aa71';ctx.fillRect(31,-23+i*23,19,7)}ctx.restore();
  }
  function text(str,x,y,size,color,align='center',font='Barlow Condensed'){ctx.fillStyle=color;ctx.textAlign=align;ctx.font=`700 ${size}px ${font}, sans-serif`;ctx.fillText(str,x,y)}
  function drawHud(){
    ctx.fillStyle='rgba(5,11,21,.72)';ctx.fillRect(22,20,216,55);ctx.strokeStyle='rgba(133,214,222,.25)';ctx.strokeRect(22,20,216,55);
    text('STARWAKE  //  FLIGHT 01',34,38,12,'#86dcd9','left');text(`SCORE ${String(score).padStart(7,'0')}`,34,60,16,'#e6f3ff','left');
    text('HULL',258,36,10,'#92a8bd','left','DM Mono');for(let i=0;i<lives;i++){ctx.fillStyle=i<3?'#ff8291':'#82efd9';ctx.fillRect(258+i*19,46,13,9)}
    text('CHARGE',258,70,9,'#92a8bd','left','DM Mono');ctx.fillStyle='#182737';ctx.fillRect(310,63,90,8);ctx.fillStyle=charge>.72?'#fff0a1':'#76f5e8';ctx.fillRect(310,63,90*charge,8);
    text(`${Math.floor(levelClock/60)}:${String(Math.floor(levelClock%60)).padStart(2,'0')}`,W-28,35,14,'#a6bbca','right','DM Mono');
    if(boss){ctx.fillStyle='rgba(8,12,22,.85)';ctx.fillRect(286,20,388,44);ctx.strokeStyle='#a25d76';ctx.strokeRect(286,20,388,44);text('RIFT WARDEN',480,39,15,'#ffabb5');ctx.fillStyle='#291926';ctx.fillRect(312,47,336,7);ctx.fillStyle=boss.phase===3?'#ff567e':'#ff8790';ctx.fillRect(312,47,336*boss.hp/boss.max,7);text(`PHASE ${boss.phase}`,480,79,9,'#bd96a9','center','DM Mono');}
    else if(levelClock>168){ctx.fillStyle='#ffa0a4';ctx.globalAlpha=.75+Math.sin(time*12)*.25;text('RIFT SIGNATURE DETECTED',W/2,102,17,'#ff8f9a');ctx.globalAlpha=1}
  }
  function overlay(title,subtitle,lines){ctx.fillStyle='rgba(4,9,18,.74)';ctx.fillRect(0,0,W,H);ctx.fillStyle='rgba(8,17,30,.94)';ctx.fillRect(210,113,540,314);ctx.strokeStyle='rgba(118,236,226,.5)';ctx.strokeRect(210,113,540,314);ctx.strokeStyle='rgba(118,236,226,.17)';ctx.strokeRect(219,122,522,296);text('✦   STARWAKE',480,162,14,'#7cf4e8');text(title,480,222,48,'#edf6ff');text(subtitle,480,256,13,'#a0b4c9','center','DM Mono');lines.forEach((s,i)=>text(s,480,306+i*24,i===0?14:11,i===0?'#f5d58f':'#8ba1b7','center',i===0?'Barlow Condensed':'DM Mono'));}
  function render(){ctx.save();if(shake>0)ctx.translate(rand(-shake,shake),rand(-shake,shake));ctx.fillStyle='#070d19';ctx.fillRect(0,0,W,H);
    const grad=ctx.createLinearGradient(0,0,W,0);grad.addColorStop(0,'#101d31');grad.addColorStop(.5,'#0a1626');grad.addColorStop(1,'#161426');ctx.fillStyle=grad;ctx.fillRect(0,0,W,H);
    ctx.fillStyle='#13243a';ctx.beginPath();ctx.ellipse(705,275,175,114,0,0,Math.PI*2);ctx.fill();ctx.strokeStyle='rgba(120,176,202,.12)';for(let r=0;r<7;r++){ctx.beginPath();ctx.ellipse(705,275,50+r*22,32+r*14,0,0,Math.PI*2);ctx.stroke()}
    for(const s of stars){ctx.globalAlpha=s.a;ctx.fillStyle=s.z>1.2?'#b7f8ed':'#8caecb';ctx.fillRect(s.x,s.y,s.r*s.z,s.r*s.z)}ctx.globalAlpha=1;
    ctx.strokeStyle='rgba(120,205,208,.12)';ctx.setLineDash([3,8]);ctx.beginPath();ctx.moveTo(0,48);ctx.lineTo(W,48);ctx.moveTo(0,H-48);ctx.lineTo(W,H-48);ctx.stroke();ctx.setLineDash([]);
    enemies.forEach(drawEnemy);drawBoss();
    for(const b of bullets){ctx.shadowBlur=b.charge?20:8;ctx.shadowColor=b.charge?'#a2fff2':'#5af4e8';ctx.fillStyle=b.charge?'#eaffbd':'#8ffff2';ctx.beginPath();ctx.ellipse(b.x,b.y,b.r*1.8,b.r,0,0,Math.PI*2);ctx.fill()}ctx.shadowBlur=0;
    for(const b of enemyShots){ctx.shadowBlur=12;ctx.shadowColor=b.color;ctx.fillStyle=b.color;ctx.beginPath();ctx.arc(b.x,b.y,b.r,0,Math.PI*2);ctx.fill()}ctx.shadowBlur=0;
    for(const p of pickups){ctx.save();ctx.translate(p.x,p.y);ctx.rotate(time*2);ctx.strokeStyle='#9effaa';ctx.lineWidth=2;ctx.strokeRect(-8,-8,16,16);ctx.fillStyle='#caffaa';ctx.fillRect(-2,-5,4,10);ctx.fillRect(-5,-2,10,4);ctx.restore()}
    for(const p of particles){ctx.globalAlpha=clamp(p.life/p.max,0,1);ctx.fillStyle=p.color;ctx.fillRect(p.x,p.y,p.r,p.r)}ctx.globalAlpha=1;
    if(player&&mode!=='title'&&mode!=='victory'){if(player.invuln===0||Math.floor(time*14)%2===0)drawShip(player.x,player.y,1);}
    drawHud();
    if(mode==='title')overlay('THE RIFT IS OPEN','ONE FLIGHT. ONE WARDEN. FIND YOUR WAY HOME.', ['PRESS ENTER OR SPACE TO LAUNCH','WASD / ARROWS  ·  MOVE     SPACE  ·  FIRE','HOLD SHIFT TO CHARGE  ·  RELEASE TO UNLEASH']);
    if(mode==='paused')overlay('FLIGHT PAUSED','SYSTEMS STANDING BY',['PRESS P TO RESUME','M TO TOGGLE SOUND']);
    if(mode==='gameover')overlay('SHIP LOST',`FINAL SCORE  ${String(score).padStart(7,'0')}`,['PRESS ENTER TO FLY AGAIN','TIP: CHARGE SHOTS HIT HARDER']);
    if(mode==='victory')overlay('RIFT SEALED',`FLIGHT COMPLETE  ·  SCORE ${String(score).padStart(7,'0')}`,['THE WARDEN HAS FALLEN','PRESS ENTER TO FLY AGAIN']);
    ctx.restore();
  }
  function frame(ts){const dt=Math.min(.033,(ts-last)/1000||0);last=ts;update(dt);render();requestAnimationFrame(frame)}requestAnimationFrame(frame);
  window.addEventListener('keydown',e=>{const k=e.key.toLowerCase();if([' ','arrowup','arrowdown','arrowleft','arrowright','shift'].includes(k))e.preventDefault();keys.add(k);if(k==='m'){muted=!muted;if(!muted)sound(500,.08,'sine',.03,150)}if(k==='p'&&mode==='playing')mode='paused';else if(k==='p'&&mode==='paused')mode='playing';if(k==='r')start();else if((k==='enter'||k===' ')&&['title','gameover','victory'].includes(mode))start()});
  window.addEventListener('keyup',e=>{keys.delete(e.key.toLowerCase())});window.addEventListener('blur',()=>{keys.clear();if(mode==='playing')mode='paused'});
})();
