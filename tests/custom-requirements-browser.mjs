import {chromium} from 'playwright';
import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8092';
const password=fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)[1].trim();
const browser=await chromium.launch({channel:process.env.PLAYWRIGHT_CHANNEL||'msedge',headless:true});
let page,original,changed=false,added;
try{
 page=await browser.newPage({viewport:{width:1440,height:1000}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(base);await page.locator('#login-form [name=email]').fill('manager@shiftnest.demo');await page.locator('#login-form [name=password]').fill(password);await page.locator('#login-form button').click();await page.locator('#app').waitFor({state:'visible'});
 const s=await page.evaluate(async()=> (await fetch('/api/state')).json());original=s.employees.find(e=>e.active);
 await page.locator('#nav [data-page=employees]').click();assert.equal(await page.locator('#employee-status-filter').inputValue(),'active');
 await page.locator(`[data-action=fire-employee][data-id='${original.id}']`).click();changed=true;await page.locator('#dialog-form button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden'});
 await page.locator(`[data-action=edit-employee][data-id='${original.id}']`).waitFor({state:'detached'});
 await page.locator('#employee-status-filter').selectOption('inactive');assert(await page.locator(`[data-action=edit-employee][data-id='${original.id}']`).isVisible());
 console.log('PASS fired employee disappears from default list and is available in inactive archive');
 await page.evaluate(async e=>{await api('employee',{...e,password:'',active:1});await refresh();},original);changed=false;
 // Select a separate test week; no current-week requirements are modified.
 await page.evaluate(async()=>{const prior=await (await fetch('/api/state?week=2031-06-02')).json();for(const sh of prior.shifts)if(sh.title==='Reception & handover')await api('shift',{id:sh.id,action:'delete'});});
 await page.evaluate(async()=>{week='2031-06-02';await refresh();});await page.locator('#nav [data-page=requirements]').click();await page.locator('[data-action=add-shift]').click();
 await page.locator('#dialog-form [name=title]').fill('Reception & handover');await page.locator('#dialog-form [name=new_skill]').fill(' inventory audit ');await page.locator('#dialog-form button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden'});
 await page.locator('#main td').filter({hasText:'Reception & handover'}).first().waitFor();
 const after=await page.evaluate(async()=> (await fetch('/api/state?week=2031-06-02')).json());added=after.shifts.find(x=>x.title==='Reception & handover');assert(added);assert.equal(after.skills.filter(x=>x.name.toLowerCase()==='inventory audit').length,1);
 await page.locator('#nav [data-page=planner]').click();assert((await page.locator('.calendar').innerText()).includes('Reception & handover'));
 await page.locator('#nav [data-page=employees]').click();await page.locator('#employee-status-filter').selectOption('active');await page.locator(`[data-action=edit-employee][data-id='${original.id}']`).click();assert(await page.locator(`#dialog-form input[name=skills][value='${added.skill_id}']`).isVisible());await page.locator('[data-action=cancel-dialog]').click();
 await page.setViewportSize({width:390,height:844});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));assert.deepEqual(errors,[]);
 console.log('PASS custom requirement form, planner title, skill reuse, employee checkbox and mobile overflow');
}finally{
 if(page&&original&&changed)await page.evaluate(async e=>api('employee',{...e,password:'',active:1}),original);
 if(page&&added)await page.evaluate(async id=>api('shift',{id,action:'delete'}),added.id);
 await browser.close();
}
