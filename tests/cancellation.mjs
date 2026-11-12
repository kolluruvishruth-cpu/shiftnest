import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8092';
const password=fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)[1].trim();
class Client {
 cookie='';csrf='';
 async call(path,body,status=200){const r=await fetch(base+'/api/'+path,{method:body?'POST':'GET',headers:{Cookie:this.cookie,...(body?{'Content-Type':'application/json','X-CSRF-Token':this.csrf}:{})},body:body?JSON.stringify(body):undefined});if(r.headers.get('set-cookie'))this.cookie=r.headers.get('set-cookie').split(';')[0];const j=await r.json();assert.equal(r.status,status,`${path}: ${JSON.stringify(j)}`);return j;}
 async login(email){await this.call('login',{email,password});const s=await this.call('state');this.csrf=s.user.csrf;return s;}
}
const m=new Client(),e=new Client();let s=await m.login('manager@shiftnest.demo');await e.login('employee1@shiftnest.demo');const employee=s.employees.find(x=>x.email==='employee1@shiftnest.demo');
// A separate test week preserves the manager's current requirements.
const week='2031-05-05';
for(const day of ['2031-05-05','2031-05-06']){
 await m.call('availability',{employee_id:employee.id,starts:day+'T08:00',ends:day+'T18:00'});
 await m.call('shift',{starts:day+'T09:00',ends:day+'T12:00',skill_id:1,count:1,priority:1});
}
let run=await m.call('generate',{week});assert(run.complete);await m.call('publish',{id:run.version_id,week});const published=run.version_id;
run=await m.call('generate',{week});const draft=run.version_id;s=await m.call('state?week='+week);const victim=s.shifts.at(-1),before=s.wages.find(w=>w.employee_id===employee.id).total;
await e.call('shift',{id:victim.id,action:'delete'},403);
await m.call('shift',{id:victim.id,action:'delete'});
s=await m.call('state?week='+week);assert(!s.shifts.some(x=>x.id===victim.id));assert(!s.assignments.some(x=>x.shift_id===victim.id));assert(s.wages.find(w=>w.employee_id===employee.id).total<before);
assert.equal(s.versions.find(v=>v.id===draft).details.required,1);assert(s.versions.find(v=>v.id===draft).details.needs_regeneration);assert.notEqual(s.versions[0].details.outcome,'Optimal');
let es=await e.call('state?week='+week);assert(!es.shifts.some(x=>x.id===victim.id));assert(es.history.some(x=>x.shift_id===victim.id&&x.cancelled_at));assert(es.notifications.some(n=>n.event_key===`cancel-requirement:${victim.id}:${published}`));assert.equal(es.wages[0].total,s.wages.find(w=>w.employee_id===employee.id).total);
console.log('PASS assigned requirement cancellation: draft + published rosters, estimates, retained history and employee notification');
const revision=s.settings[0].revision,n=es.notifications.filter(n=>n.event_key===`cancel-requirement:${victim.id}:${published}`).length;
await m.call('shift',{id:victim.id,action:'delete'});s=await m.call('state?week='+week);es=await e.call('state?week='+week);assert.equal(s.settings[0].revision,revision);assert.equal(es.notifications.filter(n=>n.event_key===`cancel-requirement:${victim.id}:${published}`).length,n);
await m.call('publish',{id:draft,week},409);run=await m.call('generate',{week});assert(run.complete&&run.required===1);await m.call('publish',{id:run.version_id,week});es=await e.call('state?week='+week);assert(es.assignments.every(x=>x.shift_id!==victim.id));console.log('PASS idempotent retry, stale-publication rejection and regeneration without the removed shift');
await m.call('shift',{starts:week+'T19:00',ends:week+'T20:00',skill_id:2,count:1,priority:1});s=await m.call('state?week='+week);const unused=s.shifts.find(x=>x.starts===week+'T19:00');await m.call('shift',{id:unused.id,action:'delete'});s=await m.call('state?week='+week);assert(!s.shifts.some(x=>x.id===unused.id));console.log('PASS unused requirement removal and employee authorization');
