const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const PORT = Number(process.env.OSLAB_PORT || 8766);

function number(data, key, min, max, fallback) {
  const n = data[key] === undefined ? fallback : data[key];
  if (!Number.isInteger(n) || n < min || n > max) throw Error(`${key} must be between ${min} and ${max}`);
  return n;
}
function select(data, key, values, fallback) {
  const v = data[key] === undefined ? fallback : data[key];
  if (!values.includes(v)) throw Error(`${key} must be one of: ${values.join(', ')}`);
  return v;
}
function command(file, args, timeout = 90000) {
  return new Promise((resolve, reject) => {
    const child = spawn(file, args, { cwd: ROOT, windowsHide: true, shell: false });
    let stdout = '', stderr = '';
    const timer = setTimeout(() => { child.kill(); reject(Error('Command timed out')); }, timeout);
    child.stdout.on('data', b => stdout += b.toString());
    child.stderr.on('data', b => stderr += b.toString());
    child.on('error', e => { clearTimeout(timer); reject(Error(`${file} could not start: ${e.message}`)); });
    child.on('close', code => {
      clearTimeout(timer);
      if (code !== 0) return reject(Error((stderr || stdout || `${file} failed`).trim().slice(-1800)));
      const values = {};
      for (const line of stdout.split(/\r?\n/)) {
        const pos = line.indexOf(': ');
        if (pos > 0) values[line.slice(0, pos)] = line.slice(pos + 2);
      }
      resolve({ output: stdout, values });
    });
  });
}
function build(name) {
  const table = {
    synclab: ['tools/synclab.c', ['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Wpedantic','-pthread','-o','synclab.exe','tools/synclab.c']],
    memlab: ['tools/memlab.c', ['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Wpedantic','-o','memlab.exe','tools/memlab.c']],
    fslab: ['tools/fslab.c', ['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Wpedantic','-o','fslab.exe','tools/fslab.c']],
    pagesim: ['tools/pagesim.c', ['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Wpedantic','-o','pagesim.exe','tools/pagesim.c']],
    disksim: ['tools/disksim.c', ['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Wpedantic','-Iinclude','-o','disksim.exe','tools/disksim.c','src/disk_sched.c']],
    deadsim: ['tools/deadsim.c', ['-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Wpedantic','-o','deadsim.exe','tools/deadsim.c']],
    all: null
  };
  if (!(name in table)) throw Error('Unknown build target');
  if (table[name] === null) return command('make', ['all'], 120000);
  return command('gcc', table[name][1], 120000);
}
async function dispatch(d) {
  let mode, args;
  switch (d.action) {
    case 'counter':
      mode = select(d,'mode',['none','mutex','sem','peterson'],'mutex');
      { const t=number(d,'threads',1,16,2); if(mode==='peterson'&&t!==2)throw Error('Peterson requires exactly two threads'); await build('synclab'); return command('./synclab.exe',['counter','--mode',mode,'--threads',String(t),'--iters',String(number(d,'iters',1,50000000,200000))]); }
    case 'rw':
      await build('synclab'); return command('./synclab.exe',['rw','--policy',select(d,'policy',['none','reader','writer','fair'],'fair'),'--readers',String(number(d,'readers',1,16,4)),'--writers',String(number(d,'writers',1,16,2)),'--ops',String(number(d,'ops',1,100000,100)),'--hold-us',String(number(d,'hold',0,100000,100))]);
    case 'philosophers':
      mode=select(d,'mode',['naive','ordered','waiter'],'ordered'); args=['phil','--mode',mode,'--n',String(number(d,'count',2,32,5))];
      if(mode==='naive')args.push('--timeout-ms',String(number(d,'timeout',1,60000,150)));else args.push('--meals',String(number(d,'meals',1,100000,30)),'--eat-us',String(number(d,'eat_us',0,100000,100)));
      await build('synclab'); return command('./synclab.exe',args);
    case 'memory':
      mode=select(d,'mode',['first','best','worst','buddy','slab'],'first'); await build('memlab');
      if(['first','best','worst'].includes(mode)) return command('./memlab.exe',['contiguous','--policy',mode,'--holes','100 500 200 300 600','--requests','212 417 112 426']);
      if(mode==='buddy')return command('./memlab.exe',['buddy','--total','1024','--min-block','32','--requests','100 200 60 500']);
      return command('./memlab.exe',['slab','--page-size','4096','--classes','16 32 64 128 256','--objects','12 24 40 100 200 12']);
    case 'filesystem':
      await build('fslab'); args=['--blocks','10','--method',select(d,'method',['contiguous','linked','indexed'],'indexed'),'--files','alpha:3,beta:4'];
      if(d.free_alpha)args.push('--free','alpha'); return command('./fslab.exe',args);
    case 'paging':
      await build('pagesim'); return command('./pagesim.exe',['--algo',select(d,'algorithm',['FIFO','LRU','CLOCK','OPT'],'FIFO'),'--frames',String(number(d,'frames',1,256,3)),'--string','7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1']);
    case 'disk':
      await build('disksim'); return command('./disksim.exe',['--algo',select(d,'algorithm',['FIFO','LIFO','SSTF','SCAN','CSCAN'],'SSTF'),'--head','53','--cylinders','200','--string','98 183 37 122 14 124 65 67']);
    case 'deadlock':
      mode=select(d,'operation',['safe','request','detect','recover'],'safe'); await build('deadsim');
      args=[mode,'--file',mode==='safe'||mode==='request'?'scenarios/banker_textbook.txt':'scenarios/detect_textbook_deadlock.txt'];
      if(mode==='request')args.push('--pid','1','--req','1 0 2'); if(mode==='recover')args.push('--policy','min-alloc'); return command('./deadsim.exe',args);
    case 'simulator':
      await build('all');
      return command('./exam_os.exe',['--headless','--strict','--tick-ms','10','--workload','INTERACTIVE','--students',String(number(d,'students',1,200,12)),'--duration',String(number(d,'duration',1,2000,150)),'--frames',String(number(d,'frames',1,256,24)),'--algo',select(d,'algorithm',['FCFS','SJF','SRTF','RR','PRIORITY','PRIO','MLFQ'],'RR'),'--seed',String(number(d,'seed',1,2147483647,7))],90000);
    default: throw Error('Unknown action');
  }
}
function reply(res,status,data,type='application/json; charset=utf-8') { const b=Buffer.from(type==='text/html; charset=utf-8'?data:JSON.stringify(data));res.writeHead(status,{'Content-Type':type,'Content-Length':b.length,'Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});res.end(b); }
const server=http.createServer(async(req,res)=>{
  if(req.method==='GET'&&(req.url==='/'||req.url==='/index.html')) return reply(res,200,fs.readFileSync(path.join(__dirname,'index.html'),'utf8'),'text/html; charset=utf-8');
  if(req.method!=='POST'||req.url!=='/api/run') return reply(res,404,{ok:false,error:'Not found'});
  try { let body='';for await(const chunk of req){body+=chunk;if(body.length>8192)throw Error('Request too large');}const result=await dispatch(JSON.parse(body));reply(res,200,{ok:true,...result}); }
  catch(e){reply(res,400,{ok:false,error:e.message||String(e)});}
});
server.listen(PORT,'127.0.0.1',()=>console.log(`Kernel Studio: http://127.0.0.1:${PORT}`));
