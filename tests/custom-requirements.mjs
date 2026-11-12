import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8092';
const password=fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)[1].trim();let cookie='',csrf='';
async function call(path,body,status=200){const r=await fetch(base+'/api/'+path,{method:body?'POST':'GET',headers:{Cookie:cookie,...(body?{'Content-Type':'application/json','X-CSRF-Token':csrf}:{})},body:body?JSON.stringify(body):undefined});if(r.headers.get('set-cookie'))cookie=r.headers.get('set-cookie').split(';')[0];const j=await r.json();assert.equal(r.status,status,JSON.stringify(j));return j;}
await call('login',{email:'manager@shiftnest.demo',password});let s=await call('state');csrf=s.user.csrf;const e=s.employees.find(x=>x.active),week='2031-06-02',title='Stockroom audit';let shift;
try{
 await call('shift',{title,new_skill:'Inventory audit',skill_id:1,starts:week+'T10:00',ends:week+'T12:00',count:1,priority:1});
 s=await call('state?week='+week);shift=s.shifts.find(x=>x.title===title);const skill=s.skills.find(x=>x.name==='Inventory audit');assert(shift&&skill&&shift.skill_id===skill.id&&skill.id>8);
 let run=await call('generate',{week});assert(!run.complete);console.log('PASS custom title and skill persistence; unskilled employees cannot cover it');
 await call('employee',{...e,password:'',active:1,skills:e.skills|skill.id});await call('availability',{employee_id:e.id,starts:week+'T09:00',ends:week+'T18:00'});
 run=await call('generate',{week});assert(run.complete);s=await call('state?week='+week);assert(s.assignments.some(a=>a.version_id===run.version_id&&a.employee_id===e.id&&a.shift_id===shift.id));console.log('PASS new skill on employee and C optimizer eligibility');
 await call('shift',{title:'Invalid shift',new_skill:'Rollback skill',starts:week+'T15:00',ends:week+'T14:00',count:1,priority:1},400);s=await call('state?week='+week);assert(!s.skills.some(x=>x.name==='Rollback skill'));console.log('PASS invalid requirement rolls back newly created skill');
 await call('employee-status',{id:e.id,active:0});s=await call('state');assert(!s.employees.find(x=>x.id===e.id).active);console.log('PASS deactivation persists for active-list filtering');
}finally{
 await call('employee',{...e,password:'',active:1});if(shift)await call('shift',{id:shift.id,action:'delete'});console.log('Restored employee and removed test requirement.');
}
