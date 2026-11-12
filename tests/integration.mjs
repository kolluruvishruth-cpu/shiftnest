// Uses the running C application. No application backend is implemented in JS.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8090';
const password=process.env.SEED_PASSWORD||fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)?.[1].trim();
assert(password,'Set SEED_PASSWORD in .env');
class Client{
  cookie='';csrf='';
  async call(path,body,expected=200){const r=await fetch(base+'/api/'+path,{method:body?'POST':'GET',headers:{...(body?{'Content-Type':'application/json','X-CSRF-Token':this.csrf}:{}),Cookie:this.cookie},body:body?JSON.stringify(body):undefined});const cookie=r.headers.get('set-cookie');if(cookie)this.cookie=cookie.split(';')[0];const json=await r.json();assert.equal(r.status,expected,`${path}: ${JSON.stringify(json)}`);return json;}
  async login(email){await this.call('login',{email,password});const s=await this.call('state');this.csrf=s.user.csrf;return s;}
}
const manager=new Client(),emp=new Client(),anon=new Client();
await anon.call('state',undefined,401);await anon.call('login',{email:'manager@shiftnest.demo',password:'incorrect'},401);
let s=await manager.login('manager@shiftnest.demo'),week=s.settings[0].current_week;
assert.equal(s.employees.length,12);assert(s.shifts.length>0);console.log('PASS manager login and real database state');
const target=s.employees.find(e=>e.email==='employee1@shiftnest.demo');
await emp.login(target.email);let es=await emp.call('state');assert.equal(es.employees.length,1);assert.equal(es.employees[0].id,target.id);assert(es.notifications.every(n=>n.user_id===es.user.id));await emp.call('generate',{week},403);console.log('PASS employee privacy and manager authorization');
const saved=manager.csrf;manager.csrf='';await manager.call('settings',{rest_minutes:0},403);manager.csrf=saved;console.log('PASS CSRF rejection');
const futureWeek='2031-01-06';
await emp.call('availability',{starts:futureWeek+'T08:00',ends:futureWeek+'T20:00'});es=await emp.call('state?week='+futureWeek);const av=es.availability.find(a=>a.starts===futureWeek+'T08:00');assert(av);await emp.call('availability',{id:av.id,action:'delete'});console.log('PASS availability submission and removal persisted');
// A deterministic one-shift planning week. This also checks overnight support.
const planning='2031-02-03';s=await manager.call('state?week='+planning);
if(!s.shifts.length)await manager.call('shift',{starts:planning+'T22:00',ends:'2031-02-04T02:00',skill_id:1,count:1,priority:1});
await manager.call('availability',{employee_id:target.id,starts:planning+'T20:00',ends:'2031-02-04T04:00'});
await manager.call('shift',{starts:'2031-02-09T22:00',ends:'2031-02-10T02:00',skill_id:1,count:1,priority:1},400);
let run=await manager.call('generate',{week:planning,node_limit:10000,time_limit_ms:2000});assert(run.complete);assert.equal(run.outcome,'Optimal');s=await manager.call('state?week='+planning);let v=s.versions[0];assert.equal(s.wages.reduce((a,w)=>a+w.total,0),run.cost);console.log('PASS exact generation, overnight shifts, cross-week rejection and shared wage rules');
await manager.call('assignment',{id:v.id,week:planning,shift_id:s.shifts[0].id,old_employee_id:target.id,employee_id:target.id});
await manager.call('publish',{id:v.id,week:planning});
let after=await manager.call('state?week='+planning);const n1=after.notifications.filter(n=>n.event_key==='publish:'+v.id).length;assert(n1>0);await manager.call('publish',{id:v.id,week:planning});after=await manager.call('state?week='+planning);assert.equal(after.notifications.filter(n=>n.event_key==='publish:'+v.id).length,n1);console.log('PASS transactional publication and retry-safe notifications');
es=await emp.call('state?week='+planning);assert(es.assignments.some(a=>a.version_id===v.id));assert(!('details' in es.versions[0]));const notification=es.notifications.find(n=>n.event_key==='publish:'+v.id);assert(notification);await emp.call('notification',{id:notification.id,action:'acknowledge'});es=await emp.call('state?week='+planning);assert(es.notifications.find(n=>n.id===notification.id).acknowledged_at);console.log('PASS employee reads published schedule and acknowledges notification');
await manager.call('assignment',{id:v.id,week:planning,shift_id:s.shifts[0].id,old_employee_id:target.id,employee_id:0});es=await emp.call('state?week='+planning);assert(es.notifications.some(n=>n.message.includes('cancelled')));await manager.call('assignment',{id:v.id,week:planning,shift_id:s.shifts[0].id,old_employee_id:0,employee_id:target.id});es=await emp.call('state?week='+planning);assert(es.notifications.some(n=>n.message.includes('changed')));console.log('PASS published assignment cancellation and change notifications');
run=await manager.call('generate',{week:planning});await manager.call('availability',{employee_id:target.id,starts:planning+'T08:00',ends:planning+'T10:00'});await manager.call('publish',{id:run.version_id,week:planning},409);console.log('PASS stale draft publication rejection');
// Generate a current-week proposal for a useful demo landing page.
await manager.call('generate',{week,time_limit_ms:1000,node_limit:100000});
fs.mkdirSync('artifacts',{recursive:true});fs.writeFileSync('artifacts/persistence-check.json',JSON.stringify({versionId:v.id,notificationId:notification.id,week:planning}));
console.log('All ShiftNest API integration checks passed.');
