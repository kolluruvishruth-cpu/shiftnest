import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8090';
const password=fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)[1].trim();
const saved=JSON.parse(fs.readFileSync('artifacts/persistence-check.json','utf8'));
const r=await fetch(base+'/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({email:'employee1@shiftnest.demo',password})});assert.equal(r.status,200);const cookie=r.headers.get('set-cookie').split(';')[0];
const response=await fetch(base+'/api/state?week='+saved.week,{headers:{Cookie:cookie}});assert.equal(response.status,200);const state=await response.json();assert(state.versions.some(v=>v.id===saved.versionId));assert(state.notifications.some(n=>n.id===saved.notificationId&&n.acknowledged_at));assert(state.assignments.some(a=>a.version_id===saved.versionId));console.log('PASS persisted published schedule, assignment and acknowledgement after application restart');
