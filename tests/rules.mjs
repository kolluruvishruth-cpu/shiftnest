import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8090';
const password=fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)[1].trim();
let cookie='',csrf='';
async function call(path,body,expected=200){const r=await fetch(base+'/api/'+path,{method:body?'POST':'GET',headers:{Cookie:cookie,...(body?{'Content-Type':'application/json','X-CSRF-Token':csrf}:{})},body:body?JSON.stringify(body):undefined});if(r.headers.get('set-cookie'))cookie=r.headers.get('set-cookie').split(';')[0];const j=await r.json();assert.equal(r.status,expected,JSON.stringify(j));return j;}
await call('login',{email:'manager@shiftnest.demo',password});let s=await call('state');csrf=s.user.csrf;
const week='2031-02-03';s=await call('state?week='+week);const e=s.employees.find(e=>e.email==='employee1@shiftnest.demo');
const run=await call('generate',{week}),vid=run.version_id;s=await call('state?week='+week);const wages=s.wages.find(w=>w.employee_id===e.id).total;
const save=extra=>call('employee',{...e,password:'',active:e.active?1:0,...extra});
try{await save({rate:e.rate+10000});s=await call('state?week='+week);assert.equal(s.wages.find(w=>w.employee_id===e.id).total,wages);await call('publish',{id:vid,week},409);console.log('PASS employee editing, historical wage-rate snapshot, stale rejection after rate change');}finally{await save({rate:e.rate});}
// Login throttling applies even to an unknown account, without revealing account existence.
const throttled='rate-check-'+Date.now()+'@shiftnest.demo';for(let i=0;i<8;i++)await call('login',{email:throttled,password:'invalid-password'},401);await call('login',{email:throttled,password:'invalid-password'},429);console.log('PASS persistent login rate limiting');
const r=await fetch(base+'/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:'a'.repeat(32769)});assert.equal(r.status,413);console.log('PASS request-size limit');
await call('generate',{week:s.settings[0].current_week,time_limit_ms:1000,node_limit:100000});
