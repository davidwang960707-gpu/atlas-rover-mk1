"""Web page route helpers for Atlas Brain."""

from __future__ import annotations

import html
from typing import Any

from atlas_brain_tool_routes import handle_legacy_tools
from atlas_web_ui import render_admin_page, render_device_app_page, render_devices_page


def handle_devices_page(handler: Any, bridge: Any) -> None:
    handler.send_html(render_devices_page(bridge.devices()))


def handle_device_app_page(handler: Any, bridge: Any, *, lan_url: str, rover_skills_enabled: bool) -> None:
    handler.send_html(render_device_app_page(
        bridge.device_summary(),
        dualeye_url=bridge.dualeye_url,
        lan_url=lan_url,
        rover_enabled=rover_skills_enabled,
    ))


def handle_admin_page(handler: Any, bridge: Any, *, provider_summary: dict[str, Any]) -> None:
    handler.send_html(render_admin_page(
        dualeye_url=bridge.dualeye_url,
        llm_model=bridge.llm_model if bridge.llm_enabled() else "",
        asr_model=bridge.asr_model if bridge.asr_enabled() else "",
        tts_model=bridge.tts_model if bridge.tts_enabled() else "",
        provider_summary=provider_summary,
    ))


def handle_tools_page(handler: Any, bridge: Any, *, rover_skills_enabled: bool) -> None:
    handle_legacy_tools(handler, bridge, rover_skills_enabled=rover_skills_enabled)


def render_acceptance_page(bridge: Any, *, brain_port: int) -> str:
    return f"""<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atlas 烧录验收</title>
<style>body{{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;background:#0a1017;color:#eef3f8}}main{{max-width:1040px;margin:0 auto;padding:16px}}section{{border:1px solid #26384a;background:#101a24;border-radius:8px;padding:13px;margin:10px 0}}button{{font:inherit;border:1px solid #3f6078;background:#162333;color:#eef3f8;border-radius:8px;padding:9px 11px;cursor:pointer;margin:4px 6px 4px 0}}button.primary{{border-color:#3fc9ff}}pre{{white-space:pre-wrap;background:#070b10;border:1px solid #26384a;border-radius:8px;padding:10px;max-height:280px;overflow:auto}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:10px}}.card{{border:1px solid #26384a;background:#0b121b;border-radius:8px;padding:10px;min-height:96px}}.muted{{color:#a9b7c6;font-size:13px;line-height:1.45}}.kv{{display:block;margin-top:6px;color:#a9b7c6;font-size:13px;line-height:1.45}}.pass{{color:#74e0a3}}.warn{{color:#ffd166}}.fail{{color:#ff8a75}}</style>
<main><h1>Atlas 烧录验收页</h1><p class="muted">DualEye：{html.escape(bridge.dualeye_url)} · Brain：本机 {html.escape(str(brain_port))} · <a style="color:#9ae3ff" href="/admin">管理端</a> · <a style="color:#9ae3ff" href="/devices">设备列表</a></p>
	<section><button class="primary" onclick="runReport()">生成验收报告</button><button onclick="runAll()">运行逐项验收</button><button onclick="runOne('/api/runtime/diagnostics')">体验诊断</button><button onclick="runPost('/api/diagnostics/simulate-turn',{{speak:true}})">模拟 Turn</button><button onclick="runOne('/api/runtime/score')">80 分评分</button><button onclick="runOne('/api/runtime')">运行时</button><button onclick="runOne('/api/device/selftest')">DualEye 自检</button><button onclick="runPost('/api/device/opus-probe',{{duration_ms:1200}})">OPUS 真机探针</button><button onclick="runPost('/api/device/opus-stream/start',{{duration_ms:1800}})">OPUS 真流 1.8s</button><button onclick="runPost('/api/device/opus-turn/start',{{duration_ms:2200,speak:true,language:'zh'}})">OPUS 语音 Turn</button><button onclick="runPost('/api/device/opus-stream/stop',{{}})">停止真流</button><button onclick="runOne('/api/audio/stream/status')">流状态</button><button onclick="runOne('/api/device/system-info')">系统信息</button><button onclick="runOne('/api/tools/list')">工具表</button><button onclick="runOne('/ota/manifest')">OTA Manifest</button><button onclick="runOne('/health')">Brain Health</button></section>
<section><h2>实机状态</h2><div id="liveStatus" class="grid"><div class="card"><b class="warn">加载中</b><span class="kv">正在读取设备、Provider、OPUS 和诊断状态</span></div></div></section>
<section><h2>验收摘要</h2><div id="cards" class="grid"></div></section>
<section><h2>原始结果</h2><pre id="out">等待验收...</pre></section></main>
<script>
const checks=[
  ['DualEye 自检','/api/device/selftest',true],
  ['DualEye OPUS 真机探针','/api/device/opus-probe',true,'POST'],
  ['DualEye OPUS WebSocket 真流','/api/device/opus-stream/start',true,'POST',{{duration_ms:1800}}],
  ['DualEye OPUS 语音 Turn','/api/device/opus-turn/start',true,'POST',{{duration_ms:2200,speak:false,language:'zh'}}],
  ['运行时评分','/api/runtime/score',true],
  ['体验诊断','/api/runtime/diagnostics',true],
  ['设备 Live','/api/device/live',false],
  ['OPUS 流状态','/api/audio/stream/status',true],
  ['DualEye 系统信息','/api/device/system-info',true],
  ['Brain Health','/health',true],
  ['工具表','/api/tools/list',true],
  ['MCP 工具表','/mcp/tools/list',false],
  ['OTA Manifest','/ota/manifest',true],
  ['OTA 包清单','/api/ota/packages',true],
  ['协议通道','/api/protocols',true]
];
function h(s){{return String(s??'').replace(/[&<>"']/g,m=>({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}}[m]))}}
async function getJson(url,method='GET',payload=null){{const opt={{method,cache:'no-store'}};if(method==='POST'){{opt.headers={{'Content-Type':'application/json'}};opt.body=JSON.stringify(payload||{{duration_ms:1200}})}}const r=await fetch(url,opt);const text=await r.text();let data;try{{data=JSON.parse(text)}}catch(e){{data={{ok:false,raw:text,error:e.message||String(e)}}}}return {{status:r.status,ok:r.ok&&data.ok!==false,data,url}}}}
function compact(name,res){{const d=res.data||{{}};if(name.includes('自检'))return 'ready='+d.ready_to_flash+' pass='+(d.summary?.pass)+' warn='+(d.summary?.warn)+' fail='+(d.summary?.fail);if(name.includes('体验诊断')){{const x=d.diagnostics||{{}};return 'stage='+(x.stage||'')+' tool='+(x.latest_tool?.name||'')+' next='+(x.next_step||'')}}if(name.includes('设备 Live'))return 'online='+d.online+' scene='+(d.scene?.state||'')+' title='+(d.scene?.title||'');if(name.includes('真机探针')){{const p=d.probe||{{}};return 'frames='+(p.frames_encoded||0)+' bytes='+(p.encoded_bytes||0)+' avg='+(p.avg_packet_bytes||0)+' err='+(p.last_error||'')}}if(name.includes('真流')||name.includes('流状态')){{const s=(d.stream||d.last_stream||{{}});const ds=(d.dualeye_stream&&d.dualeye_stream.stream)||{{}};return 'rx_frames='+(s.frames||0)+' rx_bytes='+(s.bytes||0)+' last_seq='+(s.last_seq||0)+' gaps='+(s.sequence_gaps||0)+' device='+((ds.stage)||'')}}if(name.includes('系统'))return (d.fingerprint?.firmware_version||d.firmware||'')+' '+(d.storage?.assets_version||'');if(name.includes('Health'))return 'skills='+d.skill_count+' llm='+d.llm_enabled+' asr='+d.asr_enabled+' tts='+d.tts_enabled;if(name.includes('工具'))return 'protocol='+(d.protocol||'')+' count='+(d.tool_count||((d.tools||[]).length));if(name.includes('OTA'))return 'status='+(d.status||'')+' packages='+(d.packages||[]).length+' missing='+(d.missing||[]).length;return d.error||'ok'}}
function card(name,res,required){{const cls=res.ok?'pass':(required?'fail':'warn');const label=res.ok?'PASS':(required?'FAIL':'WARN');return '<div class="card"><b class="'+cls+'">'+label+' · '+h(name)+'</b><p class="muted">'+h(compact(name,res))+'</p><p class="muted">'+h(res.url)+' · HTTP '+res.status+'</p></div>'}}
function liveCard(title,cls,lines){{return '<div class="card"><b class="'+cls+'">'+h(title)+'</b>'+lines.map(x=>'<span class="kv">'+h(x)+'</span>').join('')+'</div>'}}
async function loadLiveStatus(){{const box=document.getElementById('liveStatus');try{{const [live,diag,stream,providers,report]=await Promise.all([getJson('/api/device/live'),getJson('/api/runtime/diagnostics'),getJson('/api/audio/stream/status'),getJson('/api/providers'),getJson('/api/acceptance/report')]);const d=diag.data?.diagnostics||{{}};const scene=live.data?.scene||{{}};const latestTool=d.latest_tool||{{}};const latestTurn=d.latest_turn||{{}};const failure=d.latest_failure||{{}};const s=stream.data?.stream||stream.data?.last_stream||{{}};const provSummary=providers.data?.summary||{{}};const exp=report.data?.experience_status?.summary||{{}};box.innerHTML=[liveCard('DualEye '+(live.data?.online?'在线':'离线'),live.data?.online?'pass':'warn',['scene='+(scene.state||''),'title='+(scene.title||''),'hint='+(scene.hint||scene.subtitle||'')]),liveCard('最近 Turn',latestTurn.ok?'pass':(latestTurn.failed_stage?'fail':'warn'),['source='+(latestTurn.source||''),'failed_stage='+(latestTurn.failed_stage||'ready'),'reply='+(latestTurn.reply||'')]),liveCard('最近工具',latestTool.ok?'pass':(latestTool.name?'fail':'warn'),['name='+(latestTool.name||''),'risk='+(latestTool.risk||''),'error='+(latestTool.error||latestTool.error_code||'')]),liveCard('诊断',d.ok?'pass':'fail',['stage='+(d.stage||'ready'),'next='+(d.next_step||''),'failure='+(failure.detail||failure.stage||'')]),liveCard('Provider',Number(provSummary.configured_providers||0)>0?'warn':'warn',['configured='+(provSummary.configured_providers||0)+'/'+(provSummary.provider_count||0),'LLM/ASR/TTS 详见 /api/providers']),liveCard('OPUS Stream',s.frames?'pass':'warn',['frames='+(s.frames||s.atlas_frames||0),'bytes='+(s.bytes||0),'gaps='+(s.sequence_gaps||0)]),liveCard('体验线评分','warn',['voice='+(exp.voice_continuous_conversation??''),'dualeye='+(exp.dualeye_pet_apps??''),'tools='+(exp.brain_tools_diagnostics??'')])].join('')}}catch(e){{box.innerHTML=liveCard('实机状态读取失败','fail',[e.message||String(e)])}}}}
async function runAll(){{const results=[];document.getElementById('cards').innerHTML='运行中...';for(const c of checks){{try{{const res=await getJson(c[1],c[3]||'GET',c[4]||null);res.name=c[0];res.required=c[2];results.push(res)}}catch(e){{results.push({{name:c[0],required:c[2],url:c[1],status:0,ok:false,data:{{error:e.message||String(e)}}}})}}}}document.getElementById('cards').innerHTML=results.map(r=>card(r.name,r,r.required)).join('');document.getElementById('out').textContent=JSON.stringify(results,null,2)}}
async function runReport(){{const res=await getJson('/api/acceptance/report');const checks=(res.data&&res.data.checks)||[];const exp=(res.data&&res.data.experience_status&&res.data.experience_status.lines)||{{}};const expCards=Object.keys(exp).map(k=>'<div class="card"><b class="'+(exp[k].ready?'pass':(exp[k].needs_device?'warn':'fail'))+'">'+h(k)+' · '+h(exp[k].state)+' · '+h(exp[k].score)+'</b><p class="muted">'+h(exp[k].detail||'')+'</p><p class="muted">'+h(exp[k].next_step||'')+'</p></div>').join('');document.getElementById('cards').innerHTML=expCards+checks.map(c=>'<div class="card"><b class="'+c.status+'">'+String(c.status).toUpperCase()+' · '+h(c.name)+'</b><p class="muted">'+h(c.detail||'')+'</p><p class="muted">'+h(c.next_step||'')+'</p></div>').join('');document.getElementById('out').textContent=JSON.stringify(res.data||res,null,2)}}
async function runOne(url){{const res=await getJson(url);document.getElementById('out').textContent=JSON.stringify(res,null,2)}}
async function runPost(url,payload){{const res=await getJson(url,'POST',payload);document.getElementById('out').textContent=JSON.stringify(res,null,2)}}
loadLiveStatus();runAll();
</script>"""


def handle_acceptance_page(handler: Any, bridge: Any, *, brain_port: int) -> None:
    handler.send_html(render_acceptance_page(bridge, brain_port=brain_port))
