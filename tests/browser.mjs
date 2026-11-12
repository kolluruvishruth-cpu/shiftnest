import {chromium} from 'playwright';
import fs from 'node:fs';
import assert from 'node:assert/strict';
const base=process.env.TEST_URL||'http://localhost:8090';
const password=process.env.SEED_PASSWORD||fs.readFileSync('.env','utf8').match(/^SEED_PASSWORD=(.*)$/m)?.[1].trim();
const browser=await chromium.launch({headless:true,channel:process.env.PLAYWRIGHT_CHANNEL||'chromium'});
const context=await browser.newContext({viewport:{width:1440,height:1040}}),page=await context.newPage();
let errors=[];page.on('pageerror',e=>errors.push(e.message));
const screenshot=async name=>{fs.mkdirSync('artifacts',{recursive:true});await page.screenshot({path:'artifacts/'+name+'.png',fullPage:true});};
async function login(email){await page.locator('#login-form [name=email]').fill(email);await page.locator('#login-form [name=password]').fill(password);await page.locator('#login-form button').click();await page.locator('#app').waitFor({state:'visible'});}
async function state(){return page.evaluate(async()=>{const r=await fetch('/api/state');return r.json();});}
async function submit(){await page.locator('#dialog-form button[type=submit]').click();await page.locator('#dialog').waitFor({state:'hidden',timeout:20000});}
try{
 await page.goto(base);await page.locator('#login-form').waitFor();await screenshot('login-desktop');
 await login('manager@shiftnest.demo');await screenshot('dashboard-desktop');
 let s=await state(),week=s.settings[0].current_week;
 await page.locator('#nav [data-page=requirements]').click();await page.locator('[data-action=add-shift]').click();
 await page.locator('#dialog-form [name=starts]').fill(week+'T16:00');await page.locator('#dialog-form [name=ends]').fill(week+'T18:00');await page.locator('#dialog-form [name=count]').fill('1');await submit();
 await page.locator('#nav [data-page=planner]').click();await page.locator('[data-action=generate]').click();await page.locator('#dialog-form [name=time_limit_ms]').fill('1000');await submit();
 await page.locator('[data-action=publish]').click();await submit();
 await screenshot('planner-desktop');s=await state();assert.equal(s.versions[0].status,'published');assert.equal(s.versions[0].details.filled,s.versions[0].details.required);
 const ass=s.assignments.find(a=>a.version_id===s.versions[0].id),assigned=s.employees.find(e=>e.id===ass.employee_id);
 await page.setViewportSize({width:390,height:844});await screenshot('planner-mobile');assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.locator('#nav [data-page=wages]').click();await screenshot('wages-mobile');
 await page.setViewportSize({width:1440,height:1040});await page.locator('#logout').click();await login(assigned.email);await screenshot('employee-desktop');
 await page.locator('#nav [data-page=notifications]').click();const ack=page.locator('[data-action=acknowledge]').first();await ack.click();await page.locator('#toast').filter({hasText:'Schedule acknowledged'}).waitFor();
 await page.locator('#nav [data-page=availability]').click();await page.locator('[data-action=add-availability]').click();await page.locator('#dialog-form [name=starts]').fill('2032-01-05T08:00');await page.locator('#dialog-form [name=ends]').fill('2032-01-05T18:00');await submit();
 await page.setViewportSize({width:390,height:844});await page.locator('#nav [data-page=overview]').click();await screenshot('employee-mobile');assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 assert.deepEqual(errors,[]);console.log('PASS browser: create requirement → generate → inspect → publish → employee login → acknowledge → submit availability');console.log('PASS desktop 1440px and mobile 390px, no page errors or body overflow');
}catch(e){console.error('Browser check failed:',e.message);await screenshot('browser-failure');throw e;}finally{await Promise.race([browser.close(),new Promise(resolve=>setTimeout(resolve,5000))]);}
