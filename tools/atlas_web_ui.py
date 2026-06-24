from __future__ import annotations

import html
import json
from string import Template
from typing import Any


def _h(value: object) -> str:
    return html.escape(str(value or ""))


def _json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def render_devices_page(devices: list[dict[str, Any]]) -> str:
    cards: list[str] = []
    for device in devices:
        status = device.get("status") if isinstance(device.get("status"), dict) else {}
        online = bool(device.get("online"))
        state = "在线" if online else "未连接"
        tone = "ok" if online else "warn"
        cards.append(f"""
        <a class="device-card" href="{_h(device.get('app_path') or '/app')}">
          <div class="device-face"><span></span><span></span></div>
          <div>
            <p class="eyebrow">Atlas 设备</p>
            <h2>{_h(device.get('name') or 'Atlas DualEye')}</h2>
            <p class="muted">{_h(device.get('model'))}</p>
            <p><b class="{tone}">{state}</b> · {_h(status.get('scene') or '待机')}</p>
          </div>
        </a>""")
    return _DEVICES_TEMPLATE.safe_substitute(base_css=_BASE_CSS + _LIVE_CSS, cards="".join(cards) or "<p>还没有发现设备。</p>")


def render_device_app_page(device: dict[str, Any], *, dualeye_url: str, lan_url: str, rover_enabled: bool) -> str:
    status = device.get("status") if isinstance(device.get("status"), dict) else {}
    online = bool(device.get("online"))
    scene = str(device.get("scene") or status.get("scene") or "待连接")
    page = str(status.get("page") or "chat")
    theme = str(status.get("theme") or "pet")
    chat_mode = str(status.get("chat_mode") or device.get("chat_mode") or "pet_head")
    expression = str(status.get("expression") or "idle")
    state_label = "已连接" if online else "正在等待设备"
    state_class = "ok" if online else "warn"
    rover_label = "可用" if rover_enabled else "暂停"
    initial_snapshot = {
        "online": online,
        "device": {
            "name": device.get("name") or "Atlas DualEye",
            "url": dualeye_url,
            "online": online,
            "sta_ip": device.get("sta_ip", ""),
            "error": device.get("error", ""),
        },
        "scene": {
            "label": scene,
            "state": device.get("scene_state", ""),
            "title": device.get("scene_title", ""),
            "severity": device.get("scene_severity", ""),
        },
        "ui": {
            "page": page,
            "theme": theme,
            "chat_mode": chat_mode,
            "expression": expression,
        },
        "pet_visual": {
            "state": "idle",
            "animation": "",
            "view": "yaw_c",
            "asset_version": "0.3.0",
            "background": "transparent",
            "embedded_spiffs": True,
            "sdcard_required": False,
            "animations": ["blink", "speak", "sing", "laugh"],
        },
        "audio_service": {
            "mode": device.get("audio_mode", ""),
        },
        "voice_wake": {
            "enabled": bool(device.get("continuous_voice", False)),
        },
        "last_turn": {},
    }
    return _APP_TEMPLATE.safe_substitute(
        base_css=_BASE_CSS + _LIVE_CSS,
        app_js=_APP_JS,
        device_name=_h(device.get("name") or "Atlas DualEye"),
        dualeye_url=_h(dualeye_url),
        lan_url=_h(lan_url),
        scene=_h(scene),
        page=_h(page),
        theme=_h(theme),
        chat_mode=_h(chat_mode),
        expression=_h(expression),
        state_label=_h(state_label),
        state_class=state_class,
        online_attr="true" if online else "false",
        rover_label=_h(rover_label),
        initial_snapshot=_json(initial_snapshot),
    )


def render_admin_page(*,
                      dualeye_url: str,
                      llm_model: str,
                      asr_model: str,
                      tts_model: str,
                      provider_summary: dict[str, Any]) -> str:
    providers = provider_summary.get("providers", {}) if isinstance(provider_summary.get("providers"), dict) else {}
    cards = []
    for key, label in (("llm", "大语言"), ("asr", "语音识别"), ("tts", "语音合成"), ("weather", "天气")):
        item = providers.get(key, {}) if isinstance(providers.get(key), dict) else {}
        enabled = True if key == "weather" else bool(item.get("enabled") or item.get("configured"))
        cards.append(f"""
        <div class="metric">
          <b>{_h(label)}</b>
          <span class="{'ok' if enabled else 'warn'}">{'已配置' if enabled else '未配置'}</span>
          <small>{_h(item.get('model') or item.get('provider') or '')}</small>
        </div>""")
    return _ADMIN_TEMPLATE.safe_substitute(
        base_css=_BASE_CSS + _LIVE_CSS,
        dualeye_url=_h(dualeye_url),
        llm_model=_h(llm_model or "未配置"),
        asr_model=_h(asr_model or "未配置"),
        tts_model=_h(tts_model or "未配置"),
        cards="".join(cards),
    )


_BASE_CSS = """
:root{color-scheme:light;--ink:#16211d;--muted:#66736d;--line:#d8ddd5;--panel:#fffdf8;--soft:#f4efe4;--mint:#bfe8d4;--green:#167b5f;--coral:#ee6f57;--gold:#f2b84b;--blue:#4c7fd9;--shadow:0 18px 45px rgba(36,45,40,.12)}
*{box-sizing:border-box}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:linear-gradient(180deg,#fbf7ef 0%,#eef5ee 100%);color:var(--ink)}a{color:inherit}button,input,textarea,select{font:inherit}button{border:0;border-radius:8px;background:#19231f;color:white;padding:11px 14px;cursor:pointer}button.secondary{background:#ffffff;color:var(--ink);border:1px solid var(--line)}button.ghost{background:transparent;color:var(--ink);border:1px solid var(--line)}button.danger{background:#42201b;color:#ffe6dc}button:disabled{opacity:.5;cursor:not-allowed}textarea,input,select{border:1px solid var(--line);border-radius:8px;background:white;color:var(--ink);padding:11px 12px}textarea{width:100%;min-height:104px;resize:vertical}.shell{max-width:1180px;margin:0 auto;padding:22px}.topbar{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:18px}.brand{display:flex;align-items:center;gap:12px}.mark{width:40px;height:40px;border-radius:50%;background:#18231f;display:grid;grid-template-columns:1fr 1fr;gap:4px;padding:10px}.mark span{background:#a9e7ff;border-radius:50%}.nav{display:flex;gap:8px;flex-wrap:wrap}.nav a{text-decoration:none;border:1px solid var(--line);background:rgba(255,255,255,.72);border-radius:999px;padding:8px 12px;color:#34413b}.hero{display:grid;grid-template-columns:minmax(280px,440px) 1fr;gap:18px;align-items:stretch}.device-stage{background:#18231f;border-radius:8px;min-height:360px;color:white;position:relative;overflow:hidden;padding:24px;display:flex;flex-direction:column;justify-content:space-between;box-shadow:var(--shadow)}.device-stage:before{content:'';position:absolute;inset:auto -10% -30% -10%;height:220px;background:radial-gradient(circle at 30% 50%,rgba(191,232,212,.5),transparent 34%),radial-gradient(circle at 75% 40%,rgba(242,184,75,.35),transparent 32%)}.eye-pair{display:flex;gap:16px;justify-content:center;margin-top:46px;position:relative;z-index:1}.eye{width:132px;aspect-ratio:1;border-radius:50%;background:#050807;border:10px solid #26352f;display:grid;place-items:center;box-shadow:inset 0 0 0 8px #0e1512}.eye:after{content:'';width:58px;aspect-ratio:1;border-radius:50%;background:#d8f7ff;box-shadow:inset 0 0 0 18px #5bcbef}.status-strip{position:relative;z-index:1;display:flex;justify-content:space-between;gap:10px;align-items:end}.pill{display:inline-flex;align-items:center;gap:7px;border-radius:999px;padding:7px 10px;background:rgba(255,255,255,.12);font-size:13px}.dot{width:8px;height:8px;border-radius:50%;background:var(--gold)}.dot.ok{background:#62d894}.panel{background:rgba(255,253,248,.92);border:1px solid rgba(216,221,213,.9);border-radius:8px;padding:18px;box-shadow:var(--shadow)}.panel h1,.panel h2{margin:0}.panel h1{font-size:30px;letter-spacing:0}.subtitle{color:var(--muted);line-height:1.45}.quick-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:14px}.app-card{border:1px solid var(--line);background:white;border-radius:8px;padding:13px;text-align:left;min-height:86px;color:var(--ink)}.app-card b{display:block;margin-bottom:6px}.app-card small{color:var(--muted)}.chat-box{margin-top:14px}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.stack{display:grid;gap:12px}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:18px}.mini{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}.mini b{display:block;margin-bottom:6px}.muted{color:var(--muted);font-size:13px;line-height:1.45}.ok{color:var(--green)}.warn{color:#a66b00}.bad{color:#b43d2a}.voice-bar{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:end}.tone-select{display:grid;grid-template-columns:1fr 1fr;gap:8px}audio{width:100%;margin-top:10px}.drawer{margin-top:18px}.drawer summary{cursor:pointer;color:#41504a}.debug{white-space:pre-wrap;background:#101614;color:#d7e8df;border-radius:8px;padding:12px;max-height:300px;overflow:auto}.metric{background:white;border:1px solid var(--line);border-radius:8px;padding:14px}.metric b,.metric span,.metric small{display:block}.metric small{color:var(--muted);margin-top:5px}.device-card{display:grid;grid-template-columns:120px 1fr;gap:16px;text-decoration:none;background:white;border:1px solid var(--line);border-radius:8px;padding:16px;box-shadow:var(--shadow)}.device-face{background:#18231f;border-radius:8px;height:96px;display:flex;gap:10px;align-items:center;justify-content:center}.device-face span{width:34px;height:34px;border-radius:50%;background:#a9e7ff;border:8px solid #284038}.eyebrow{text-transform:uppercase;font-size:12px;color:var(--muted);letter-spacing:.08em;margin:0}@media(max-width:860px){.hero,.grid{grid-template-columns:1fr}.quick-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.voice-bar{grid-template-columns:1fr}.shell{padding:14px}.panel h1{font-size:25px}.eye{width:112px}.topbar{align-items:flex-start;flex-direction:column}}
"""

_LIVE_CSS = """
.device-stage{transition:background .28s ease,box-shadow .28s ease}.device-stage[data-online=false]{background:#20231f}.device-stage[data-online=false] .eye{opacity:.62;filter:saturate(.55)}.device-stage[data-theme=raptor]{background:#251611}.device-stage[data-theme=raptor] .eye{border-color:#4a2118;box-shadow:inset 0 0 0 8px #100704}.device-stage[data-theme=raptor] .eye:after{width:26px;height:76px;border-radius:999px;background:#ffe2a8;box-shadow:0 0 18px #ff855e,inset 0 0 0 8px #a9211e}.device-stage[data-theme=mecha]{background:#101c24}.device-stage[data-theme=mecha] .eye{border-color:#24475a;box-shadow:inset 0 0 0 8px #061117}.device-stage[data-theme=mecha] .eye:after{background:#d4fbff;box-shadow:0 0 22px #51d6ff,inset 0 0 0 16px #1b80a5}.device-stage[data-theme=goggle]{background:#251f11}.device-stage[data-theme=goggle] .eye{border-color:#775f18;box-shadow:inset 0 0 0 8px #171104}.device-stage[data-theme=goggle] .eye:after{background:#fff1b0;box-shadow:0 0 16px #f2b84b,inset 0 0 0 19px #2e4148}.device-stage[data-theme=pet]{background:#17231f}.device-stage[data-theme=pet] .eye:after{background:#fbf7ef;box-shadow:0 0 18px #bfe8d4,inset 0 0 0 18px #4c7fd9}.device-stage[data-expression=listen] .eye:after{transform:scale(1.08)}.device-stage[data-expression=blink] .eye:after{height:16px;border-radius:999px}.device-stage[data-expression=sleepy] .eye:after{height:12px;border-radius:999px}.pet-head-stage{--pet-x:0;--pet-y:0;--pet-roll:0;--pupil-x:0px;--pupil-y:0px;display:none;position:relative;z-index:1;justify-content:center;align-items:center;margin-top:10px;min-height:220px;perspective:680px;isolation:isolate}.pet-stage-hint{position:absolute;right:10px;bottom:0;color:rgba(255,255,255,.48);font-size:12px}.pet-shadow{position:absolute;left:50%;bottom:2px;width:154px;height:28px;border-radius:50%;background:radial-gradient(ellipse,rgba(0,0,0,.38),transparent 70%);transform:translateX(-50%);filter:blur(1px)}.pet-head-img{display:none;width:224px;height:224px;object-fit:contain;position:relative;z-index:2;filter:drop-shadow(0 20px 26px rgba(0,0,0,.34));transform:rotateY(calc(var(--pet-x)*1deg)) rotateX(calc(var(--pet-y)*-0.75deg)) rotateZ(calc(var(--pet-roll)*1deg));transform-style:preserve-3d;transition:transform .16s ease,filter .2s ease;animation:petFloat 4.8s ease-in-out infinite}.pet-head-stage.asset-ready .pet-head{display:none}.pet-head-stage.asset-ready .pet-head-img{display:block}.pet-head{width:196px;aspect-ratio:1;border-radius:47% 48% 45% 44%;background:radial-gradient(circle at 32% 25%,rgba(255,225,162,.55) 0 18%,transparent 19%),radial-gradient(circle at 68% 70%,rgba(91,50,24,.24) 0 4%,transparent 5%),linear-gradient(135deg,#c97835 0%,#e4a765 52%,#a56531 100%);border:8px solid #101614;box-shadow:inset -22px -22px 0 rgba(76,40,17,.25),inset 16px 14px 0 rgba(255,231,184,.34),0 20px 44px rgba(0,0,0,.28);position:relative;transform:rotateY(calc(var(--pet-x)*1deg)) rotateX(calc(var(--pet-y)*-0.75deg)) rotateZ(calc(var(--pet-roll)*1deg));transform-style:preserve-3d;transition:transform .22s ease,filter .22s ease;animation:petFloat 4.8s ease-in-out infinite}.pet-head:before,.pet-head:after{content:'';position:absolute;top:-12px;width:36px;height:36px;border-radius:12px;background:#d68b49;border:8px solid #101614;box-shadow:inset -5px -6px 0 rgba(70,37,16,.22)}.pet-head:before{left:26px}.pet-head:after{right:26px}.pet-eye{position:absolute;top:55px;width:45px;height:45px;border-radius:50%;background:#fff;border:7px solid #101614;overflow:hidden;transform:translateZ(20px)}.pet-eye.left{left:43px}.pet-eye.right{right:43px}.pet-eye i{display:block;width:11px;height:11px;border-radius:50%;background:#101614;margin:14px auto;transform:translate(var(--pupil-x),var(--pupil-y));transition:transform .12s ease}.pet-brow{position:absolute;top:43px;width:36px;height:7px;border-radius:999px;background:#101614;opacity:.86;transform:translateZ(24px)}.pet-brow.left{left:47px}.pet-brow.right{right:47px}.pet-nose{position:absolute;left:87px;top:106px;width:18px;height:13px;border-radius:50%;background:#57351f;transform:translateZ(25px)}.pet-mouth{position:absolute;left:94px;top:122px;width:4px;height:42px;background:#101614;border-radius:4px;transform:translateZ(28px);transition:all .16s ease}.pet-cheek{position:absolute;top:100px;width:28px;height:12px;border-radius:999px;background:rgba(236,117,92,.35);filter:blur(.2px);transform:translateZ(15px)}.pet-cheek.left{left:34px}.pet-cheek.right{right:34px}.device-stage[data-chat-mode=pet_head] .eye-pair{display:none}.device-stage[data-chat-mode=pet_head] .pet-head-stage{display:flex}.device-stage[data-pet-state=speak] .pet-mouth,.device-stage[data-expression=speaking] .pet-mouth{animation:petTalk .34s steps(2,end) infinite}.device-stage[data-pet-state=sing] .pet-mouth{animation:petSing .42s ease-in-out infinite}.device-stage[data-pet-state=happy] .pet-mouth,.device-stage[data-expression=happy] .pet-mouth{width:46px;height:24px;left:72px;background:transparent;border-bottom:7px solid #101614}.device-stage[data-pet-state=laugh] .pet-mouth{width:44px;height:34px;left:74px;border-radius:0 0 999px 999px;background:#2b1610}.device-stage[data-pet-state=cry] .pet-head,.device-stage[data-pet-state=cry] .pet-head-img{filter:saturate(.82) drop-shadow(0 20px 26px rgba(0,0,0,.34))}.device-stage[data-pet-state=cry] .pet-eye:after{content:'';position:absolute;left:22px;top:27px;width:7px;height:18px;border-radius:999px;background:#7bd5ff}.device-stage[data-pet-state=sleepy] .pet-eye,.device-stage[data-expression=sleepy] .pet-eye{height:13px;margin-top:15px;border-radius:999px}.device-stage[data-pet-state=think] .pet-head{animation:petThink 2.3s ease-in-out infinite}.device-stage[data-pet-state=listen] .pet-head{--pet-roll:-4;animation:petListen 2s ease-in-out infinite}.device-stage[data-pet-state=surprised] .pet-eye{transform:translateZ(20px) scale(1.16)}.device-stage[data-pet-state=surprised] .pet-mouth{width:24px;height:24px;left:84px;border-radius:50%;background:#2b1610}.device-stage[data-pet-state=blink] .pet-eye{height:12px;margin-top:16px;border-radius:999px}.device-stage[data-pet-local=blink] .pet-eye{height:12px;margin-top:16px;border-radius:999px}.device-stage[data-pet-local=listen] .pet-head{--pet-roll:-4}.device-stage[data-pet-local=think] .pet-head{filter:saturate(1.05) contrast(1.04)}.device-stage[data-pet-local=surprised] .pet-eye{transform:translateZ(20px) scale(1.12)}.device-stage[data-pet-local=happy] .pet-mouth{width:42px;height:24px;left:75px;background:transparent;border-bottom:7px solid #101614}.device-stage[data-scene-severity=error]{box-shadow:0 18px 45px rgba(180,61,42,.26)}.device-stage[data-scene-severity=warn]{box-shadow:0 18px 45px rgba(166,107,0,.22)}.sync-line{display:flex;gap:8px;flex-wrap:wrap}.mini-grid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:12px;margin-top:18px}.mini span{display:block;min-height:20px}.last-sync{font-variant-numeric:tabular-nums}.dot.warn{background:#f2b84b}.dot.bad{background:#ee6f57}@keyframes petFloat{0%,100%{margin-top:0}50%{margin-top:-5px}}@keyframes petTalk{0%{width:8px;height:18px;left:91px;border-radius:8px;background:#2b1610}100%{width:25px;height:23px;left:83px;border-radius:50%;background:#2b1610}}@keyframes petSing{0%,100%{width:24px;height:24px;left:84px;border-radius:50%;background:#2b1610}50%{width:38px;height:32px;left:76px;border-radius:0 0 999px 999px;background:#2b1610}}@keyframes petThink{0%,100%{transform:rotateY(-8deg) rotateZ(-2deg)}50%{transform:rotateY(9deg) rotateZ(2deg)}}@keyframes petListen{0%,100%{transform:rotateY(-14deg) rotateZ(-4deg)}50%{transform:rotateY(6deg) rotateZ(2deg)}}@media(max-width:1080px){.mini-grid{grid-template-columns:repeat(3,minmax(0,1fr))}}@media(max-width:680px){.mini-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
.control-block{margin-top:18px}.section-head{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin-bottom:12px}.section-head h2{font-size:20px;margin:0;letter-spacing:0}.section-head p{margin:4px 0 0;color:var(--muted);font-size:13px}.control-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.control-card{border:1px solid var(--line);background:white;border-radius:8px;padding:14px;text-align:left;min-height:88px;color:var(--ink);display:flex;flex-direction:column;justify-content:space-between}.control-card b{display:block;font-size:17px;margin-bottom:6px}.control-card small{color:var(--muted);line-height:1.35}.control-card.primary-soft{background:#f8fbf5;border-color:#cbd9cf}.control-card.eye-soft{background:#f8fbff;border-color:#cbd8e6}.control-card.chat-soft{background:#fffaf2;border-color:#e4d5ba}.control-card.app-soft{background:#fbfaf8;border-color:#ded8cb}@media(max-width:860px){.control-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.section-head{align-items:flex-start;flex-direction:column}.control-card b{font-size:16px}}@media(max-width:420px){.control-grid{grid-template-columns:1fr}}
"""


_APP_JS = """
let rec=null,lastReply='',lastLive={},continuousVoice=false,continuousRestartTimer=null;
const PET_ANIMS={blink:{frames:6,fps:12,loop:false,fallback:'idle'},speak:{frames:8,fps:10,loop:true,fallback:'speak'},sing:{frames:10,fps:10,loop:true,fallback:'sing'},laugh:{frames:8,fps:12,loop:true,fallback:'laugh'}};
const PET_VIEW_STATES=new Set(['idle','listen','think','speak']);
let petPreview={state:'idle',animation:'',view:'yaw_c',frame:0,lastFrameAt:0,lastSrc:''};
function q(id){return document.getElementById(id)}
function out(v){const el=q('out');if(el)el.textContent=typeof v==='string'?v:JSON.stringify(v,null,2)}
async function postJson(url,payload){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});const t=await r.text();try{return JSON.parse(t)}catch(e){return {ok:false,status:r.status,raw:t}}}
async function getJson(url){const r=await fetch(url);const t=await r.text();try{return JSON.parse(t)}catch(e){return {ok:false,raw:t}}}
function replyFrom(res){return (res&&res.llm&&res.llm.reply)||(res&&res.text_result&&res.text_result.llm&&res.text_result.llm.reply)||(res&&res.reply)||''}
function setText(id,text,cls){const el=q(id);if(!el)return;el.textContent=text||'';el.className=cls||''}
function ttsOptions(){const style=q('ttsStyle').value;return {tts_voice:q('ttsVoice').value,tts_style:style,tts_singing:q('ttsSinging').checked||style==='singing'}}
function setVoiceState(text,cls){
  setText('recState',text,cls||'muted');
  setText('voiceLoopState',text,cls||'muted');
}
function setButton(id,text,cls){const el=q(id);if(!el)return;el.textContent=text;if(cls!==undefined)el.className=cls}
function clearContinuousTimer(){if(continuousRestartTimer){clearTimeout(continuousRestartTimer);continuousRestartTimer=null}}
function updateContinuousButton(){setButton('contBtn',continuousVoice?'停止连续对话':'连续对话',continuousVoice?'danger':'secondary')}
function audioUrlForBrowser(src){
  try{
    const u=new URL(src,window.location.href);
    if(u.pathname.startsWith('/tts/'))return u.pathname+u.search;
  }catch(e){}
  return src;
}
function scheduleContinuousRestart(delay){
  if(!continuousVoice)return;
  clearContinuousTimer();
  continuousRestartTimer=setTimeout(()=>{if(continuousVoice&&!rec)startRec('full',{continuous:true}).catch(e=>setVoiceState('麦克风不可用','bad'))},delay||650);
}
async function playResultAudio(res,waitUntilEnd){
  const src=(res&&res.tts_url)||(res&&res.audio_url)||(res&&res.tts&&res.tts.audio_url);
  if(!src)return false;
  const a=q('player');
  if(!a)return false;
  a.src=audioUrlForBrowser(src);
  setVoiceState('播报中','ok');
  await a.play().catch(()=>false);
  if(waitUntilEnd){
    await new Promise(resolve=>{
      let done=false;
      const finish=()=>{if(done)return;done=true;a.onended=null;a.onerror=null;resolve()};
      a.onended=finish;
      a.onerror=finish;
      setTimeout(finish,20000);
    });
  }
  return true;
}
function zhPage(v){return ({clock:'时钟',chat:'对话',pomodoro:'番茄',calendar:'日历',status:'状态',voice:'语音',music:'音乐',story:'故事',photo:'照片'}[v]||v||'待机')}
function zhTheme(v){return ({raptor:'猛禽眼',mecha:'机械眼',goggle:'护目镜',pet:'电子宠物',blue_pupil:'蓝瞳',tomoe_spin:'红色旋纹',no_smoking:'禁烟'}[v]||v||'默认')}
function zhChatMode(v){return ({pet_head:'土拨鼠头',text:'双屏文字',eyes_only:'纯眼睛'}[v]||v||'土拨鼠头')}
function zhExpression(v){return ({idle:'待机',blink:'眨眼',listen:'聆听',happy:'开心',sleepy:'困了',speaking:'说话',error:'异常'}[v]||v||'待机')}
function zhPetState(v){return ({idle:'待机',blink:'眨眼',listen:'聆听',speak:'说话',sing:'唱歌',happy:'开心',laugh:'大笑',cry:'大哭',sleepy:'困了',think:'思考',surprised:'惊讶',offline:'Brain 离线'}[v]||v||'待机')}
function statusClass(ok,severity){if(!ok)return 'warn';if(severity==='error')return 'bad';if(severity==='warn')return 'warn';return 'ok'}
function compactLastTurn(last){if(!last||!last.updated_at)return '暂无会话';const t=new Date(last.updated_at*1000).toLocaleTimeString('zh-CN',{hour12:false});return t+' · '+(last.skill||last.source||last.intent||'会话')}
function applyDeviceLive(payload){
  if(!payload)payload={};
  lastLive=payload;
  const device=payload.device||{};
  const ui=payload.ui||{};
  const scene=payload.scene||{};
  const audio=payload.audio_service||{};
  const wake=payload.voice_wake||{};
  const pet=payload.pet_visual||device.pet_visual||{};
  const online=Boolean(payload.online??device.online);
  const theme=String(ui.theme||device.theme||'pet');
  const page=String(ui.page||device.page||'chat');
  const chatMode=String(ui.chat_mode||device.chat_mode||'pet_head');
  const expression=String(ui.expression||device.expression||'idle');
  const petState=String(pet.state||pet.animation||({speaking:'speak',listen:'listen',happy:'happy',sleepy:'sleepy',blink:'blink'}[expression])||'idle');
  const severity=String(scene.severity||device.scene_severity||'');
  const stateCls=statusClass(online,severity);
  const stage=q('deviceStage');
  if(stage){stage.dataset.online=online?'true':'false';stage.dataset.theme=theme||'pet';stage.dataset.chatMode=chatMode||'pet_head';stage.dataset.expression=expression||'idle';stage.dataset.petState=petState||'idle';stage.dataset.sceneSeverity=severity||''}
  const dot=q('deviceDot');if(dot)dot.className='dot '+stateCls;
  setText('deviceState',online?'已连接':'等待设备',stateCls);
  setText('scenePill',scene.label||scene.title||device.scene||device.scene_title||(online?'待机':'设备离线'),'');
  setText('themePill',zhTheme(theme),'');
  setText('themePill2',zhTheme(theme),'');
  setText('chatModePill',zhChatMode(chatMode),'');
  setText('chatModePill2',zhChatMode(chatMode),'');
  setText('pagePill',zhPage(page),'');
  setText('pagePill2',zhPage(page),'');
  setText('expressionPill',zhExpression(expression),'');
  setText('deviceConnState',online?'DualEye 在线':'DualEye 未连接',online?'ok':'warn');
  setText('deviceIp',device.sta_ip?('局域网 '+device.sta_ip):(device.url||''),'muted');
  setText('audioMode',audio.mode?('音频 '+audio.mode):'音频待机',audio.active?'ok':'muted');
  setText('voiceWake',wake.enabled?'连续语音已开':'连续语音未开',wake.enabled?'ok':'muted');
  setText('sceneTitle',scene.title||scene.subtitle||scene.hint||'同步设备真实页面、主题和表情','muted');
  setText('lastTurnState',compactLastTurn(payload.last_turn),'muted');
  setText('petVisualState',zhPetState(petState||''),petState==='offline'?'warn':'ok');
  setText('petVisualAsset',(pet.asset_version?('资源 '+pet.asset_version):'资源待同步')+' · '+(pet.background==='transparent'?'透明底':'背景未确认'),'muted');
  setText('petVisualMotion',(pet.view||'yaw_c')+' · '+((pet.animations||[]).join('/')||'动画待同步'),'muted');
  setPetPreview(petState, String(pet.animation||''), String(pet.view||petPreview.view||'yaw_c'));
  const now=new Date().toLocaleTimeString('zh-CN',{hour12:false});
  setText('lastSync','同步 '+now,online?'ok':'warn');
}
async function syncDevice(silent=true){
  try{
    const live=await getJson('/api/device/live');
    applyDeviceLive(live);
    if(!silent)out(live);
    return live;
  }catch(e){
    const live={ok:false,online:false,device:{error:String(e)},scene:{label:'同步失败',severity:'warn'},ui:lastLive.ui||{}};
    applyDeviceLive(live);
    if(!silent)out(live);
    return live;
  }
}
function afterAction(){setTimeout(()=>syncDevice(true),250);setTimeout(()=>syncDevice(true),1300)}
function updateSummary(res){const reply=replyFrom(res);const asr=(res&&res.asr&&res.asr.text)||(res&&res.asr_text)||'';if(reply){lastReply=reply;setText('replyText',reply,'')}if(asr)setText('asrText',asr,'');if(res&&res.dualeye_play)setText('playState',res.dualeye_play.ok?'已播报':'等待设备连接',res.dualeye_play.ok?'ok':'warn');out(res);afterAction()}
async function sendText(){const text=q('text').value.trim();if(!text)return out('先输入一句话');setVoiceState('思考中','warn');const res=await postJson('/turn/text',{text,speak:q('speak').checked,...ttsOptions()});updateSummary(res);if(q('speak').checked)await playResultAudio(res);setVoiceState(continuousVoice?'继续听':'待机',continuousVoice?'ok':'muted')}
async function sendTextOnly(){const was=q('speak').checked;q('speak').checked=false;try{await sendText()}finally{q('speak').checked=was}}
async function speakText(){const text=q('text').value.trim()||lastReply;if(!text)return out('没有可朗读内容');setVoiceState('准备播报','warn');const res=await postJson('/speak',{text,...ttsOptions()});updateSummary(res);await playResultAudio(res);setVoiceState(continuousVoice?'继续听':'待机',continuousVoice?'ok':'muted')}
async function skill(name,args,speak){const res=await postJson('/skill',{skill:name,args:args||{},speak:!!speak,...ttsOptions()});updateSummary(res);if(speak)await playResultAudio(res);setVoiceState(continuousVoice?'继续听':'待机',continuousVoice?'ok':'muted')}
async function setChatMode(mode){await skill('atlas.ui.set_chat_mode',{mode},false)}
async function petState(state,text){await skill('atlas.pet.set_state',{state,right_text:text||''},false)}
async function petAnim(animation,text){await skill('atlas.pet.play_animation',{animation,right_text:text||''},false)}
async function refreshStatus(silent=false){const h=await getJson('/health');setText('brainState',h.ok?'Brain 在线':'Brain 异常',h.ok?'ok':'bad');setText('providerState',(h.llm_enabled&&h.asr_enabled&&h.tts_enabled)?'模型已就绪':'模型待配置',(h.llm_enabled&&h.asr_enabled&&h.tts_enabled)?'ok':'warn');const live=await syncDevice(true);if(!silent)out({health:h,live})}
function clamp(n,min,max){return Math.max(min,Math.min(max,n))}
function viewFromPointer(x){if(x<-0.68)return 'yaw_l30';if(x<-0.24)return 'yaw_l15';if(x>0.68)return 'yaw_r30';if(x>0.24)return 'yaw_r15';return 'yaw_c'}
function petAssetPath(state,animation,view,frame){
  const safeState=state||'idle';
  if(animation&&PET_ANIMS[animation]){
    const f=String(frame%PET_ANIMS[animation].frames).padStart(2,'0');
    return '/assets/pet_head/animations/'+animation+'/frame_'+f+'.png';
  }
  if(PET_VIEW_STATES.has(safeState)){
    return '/assets/pet_head/views/'+safeState+'/'+(view||'yaw_c')+'.png';
  }
  return '/assets/pet_head/keyframes/'+safeState+'.png';
}
function renderPetAsset(){
  const img=q('petHeadAsset');
  const pet=q('petHeadStage');
  if(!img||!pet)return;
  const src=petAssetPath(petPreview.state,petPreview.animation,petPreview.view,petPreview.frame);
  if(src===petPreview.lastSrc)return;
  petPreview.lastSrc=src;
  img.src=src;
}
function setPetPreview(state,animation,view){
  const local=q('deviceStage')?.dataset.petLocal||'';
  let nextState=local||state||'idle';
  let nextAnimation=animation||'';
  if(local==='blink')nextAnimation='blink';
  if(!nextAnimation&&PET_ANIMS[nextState])nextAnimation=nextState;
  if(nextAnimation&&PET_ANIMS[nextAnimation])nextState=PET_ANIMS[nextAnimation].fallback||nextState;
  petPreview.state=nextState;
  petPreview.animation=nextAnimation;
  petPreview.view=view||petPreview.view||'yaw_c';
  petPreview.frame=0;
  petPreview.lastFrameAt=0;
  renderPetAsset();
}
function setupPetStage(){
  const stage=q('deviceStage');
  const pet=q('petHeadStage');
  if(!stage||!pet)return;
  const img=q('petHeadAsset');
  if(img){
    img.addEventListener('load',()=>pet.classList.add('asset-ready'));
    img.addEventListener('error',()=>pet.classList.remove('asset-ready'));
    if(img.complete&&img.naturalWidth>0)pet.classList.add('asset-ready');
  }
  const move=(clientX,clientY)=>{
    const rect=pet.getBoundingClientRect();
    const cx=rect.left+rect.width/2;
    const cy=rect.top+rect.height/2;
    const x=clamp((clientX-cx)/(rect.width/2),-1,1);
    const y=clamp((clientY-cy)/(rect.height/2),-1,1);
    pet.style.setProperty('--pet-x',(x*18).toFixed(1));
    pet.style.setProperty('--pet-y',(y*12).toFixed(1));
    pet.style.setProperty('--pet-roll',(x*2.2).toFixed(1));
    pet.style.setProperty('--pupil-x',(x*6).toFixed(1)+'px');
    pet.style.setProperty('--pupil-y',(y*5).toFixed(1)+'px');
    petPreview.view=viewFromPointer(x);
    renderPetAsset();
  };
  stage.addEventListener('pointermove',e=>move(e.clientX,e.clientY));
  stage.addEventListener('pointerleave',()=>{
    pet.style.setProperty('--pet-x','0');
    pet.style.setProperty('--pet-y','0');
    pet.style.setProperty('--pet-roll','0');
    pet.style.setProperty('--pupil-x','0px');
    pet.style.setProperty('--pupil-y','0px');
  });
  const ambient=['blink','listen','think','surprised','happy'];
  setInterval(()=>{
    if(stage.dataset.chatMode!=='pet_head')return;
    const busy=['speak','sing','laugh','cry','offline'].includes(stage.dataset.petState||'');
    if(busy)return;
    const next=ambient[Math.floor(Math.random()*ambient.length)];
    stage.dataset.petLocal=next;
    setPetPreview(stage.dataset.petState||'idle','',petPreview.view);
    setTimeout(()=>{if(stage.dataset.petLocal===next){stage.dataset.petLocal='';setPetPreview(stage.dataset.petState||'idle','',petPreview.view)}},next==='blink'?520:1400);
  },4200);
  setInterval(()=>{
    const anim=petPreview.animation;
    if(!anim||!PET_ANIMS[anim])return;
    const now=Date.now();
    const frameMs=1000/PET_ANIMS[anim].fps;
    if(petPreview.lastFrameAt&&now-petPreview.lastFrameAt<frameMs)return;
    petPreview.lastFrameAt=now;
    petPreview.frame=(petPreview.frame+1)%PET_ANIMS[anim].frames;
    renderPetAsset();
  },45);
  setPetPreview(stage.dataset.petState||'idle','',petPreview.view);
}
async function toggleContinuous(){
  continuousVoice=!continuousVoice;
  updateContinuousButton();
  clearContinuousTimer();
  if(continuousVoice){
    if(rec)await stopRec({send:false});
    try{
      await startRec('full',{continuous:true});
    }catch(e){
      continuousVoice=false;
      updateContinuousButton();
      setVoiceState('麦克风不可用','bad');
      out({ok:false,error:String(e),stage:'microphone'});
    }
  }else{
    if(rec)await stopRec({send:false});
    setVoiceState('待机','muted');
  }
}
async function toggleRec(mode){
  if(rec){await stopRec({manual:true});return}
  try{
    await startRec(mode||'full',{continuous:false});
  }catch(e){
    rec=null;
    setButton('recBtn','按一次说话','secondary');
    setVoiceState('麦克风不可用','bad');
    out({ok:false,error:String(e),stage:'microphone'});
  }
}
async function startRec(mode,opts){
  if(rec)return;
  opts=opts||{};
  const stream=await navigator.mediaDevices.getUserMedia({audio:true});
  const ctx=new (window.AudioContext||window.webkitAudioContext)();
  const source=ctx.createMediaStreamSource(stream);
  const proc=ctx.createScriptProcessor(4096,1,1);
  const mute=ctx.createGain();
  mute.gain.value=0;
  const chunks=[];
  rec={stream,ctx,source,proc,mute,chunks,sampleRate:ctx.sampleRate,mode,continuous:!!opts.continuous,startedAt:performance.now(),lastVoiceAt:0,speechStarted:false,processing:false,level:0};
  proc.onaudioprocess=e=>{
    const input=e.inputBuffer.getChannelData(0);
    const frame=new Float32Array(input);
    chunks.push(frame);
    let sum=0;
    for(let i=0;i<input.length;i++)sum+=input[i]*input[i];
    const rms=Math.sqrt(sum/Math.max(1,input.length));
    const current=rec;
    if(!current)return;
    current.level=Math.max(current.level*0.86,rms);
    if(rms>0.018){
      current.speechStarted=true;
      current.lastVoiceAt=performance.now();
      setVoiceState(mode==='asr'?'正在识别':'正在听你说','ok');
    }
  };
  source.connect(proc);
  proc.connect(mute);
  mute.connect(ctx.destination);
  setButton('recBtn',opts.continuous?'连续监听中':'停止并发送','danger');
  setVoiceState(mode==='asr'?'正在识别':'正在听','ok');
  rec.monitor=setInterval(()=>{
    if(!rec||!rec.continuous||rec.processing)return;
    const now=performance.now();
    if(rec.speechStarted&&now-rec.lastVoiceAt>900&&now-rec.startedAt>1200)stopRec({auto:true});
    if(!rec.speechStarted&&now-rec.startedAt>12000){stopRec({send:false});scheduleContinuousRestart(250)}
  },220);
}
async function stopRec(opts){
  opts=opts||{};
  const r=rec;
  if(!r||r.processing)return;
  r.processing=true;
  rec=null;
  if(r.monitor)clearInterval(r.monitor);
  try{r.proc.disconnect()}catch(e){}
  try{r.source.disconnect()}catch(e){}
  try{r.mute.disconnect()}catch(e){}
  r.stream.getTracks().forEach(t=>t.stop());
  await r.ctx.close().catch(()=>{});
  setButton('recBtn','按一次说话','secondary');
  if(opts.send===false){
    setVoiceState(continuousVoice?'继续听':'待机',continuousVoice?'ok':'muted');
    return;
  }
  if(!r.speechStarted&&r.continuous){
    setVoiceState('继续听','ok');
    scheduleContinuousRestart(300);
    return;
  }
  setVoiceState(r.mode==='asr'?'识别中':'思考中','warn');
  const audio_data=wavDataUrl(r.chunks,r.sampleRate);
  let res;
  if(r.mode==='asr'){
    res=await postJson('/asr',{audio_data,language:'auto'});
    if(res.asr&&res.asr.text){q('text').value=res.asr.text;setText('asrText',res.asr.text,'')}
  }else{
    res=await postJson('/turn/audio',{audio_data,language:'auto',speak:q('speak').checked,...ttsOptions()});
    updateSummary(res);
    if(q('speak').checked)await playResultAudio(res,r.continuous);
  }
  if(r.mode==='asr')updateSummary(res);
  if(r.continuous&&continuousVoice){
    setVoiceState('继续听','ok');
    scheduleContinuousRestart(550);
  }else{
    setVoiceState('待机','muted');
  }
}
function wavDataUrl(chunks,sampleRate){let len=0;chunks.forEach(c=>len+=c.length);const data=new Float32Array(len);let off=0;chunks.forEach(c=>{data.set(c,off);off+=c.length});const buf=new ArrayBuffer(44+data.length*2);const v=new DataView(buf);let p=0;function s(x){for(let i=0;i<x.length;i++)v.setUint8(p++,x.charCodeAt(i))}s('RIFF');v.setUint32(p,36+data.length*2,true);p+=4;s('WAVEfmt ');v.setUint32(p,16,true);p+=4;v.setUint16(p,1,true);p+=2;v.setUint16(p,1,true);p+=2;v.setUint32(p,sampleRate,true);p+=4;v.setUint32(p,sampleRate*2,true);p+=4;v.setUint16(p,2,true);p+=2;v.setUint16(p,16,true);p+=2;s('data');v.setUint32(p,data.length*2,true);p+=4;for(let i=0;i<data.length;i++,p+=2){let x=Math.max(-1,Math.min(1,data[i]));v.setInt16(p,x<0?x*0x8000:x*0x7fff,true)}let bin='';new Uint8Array(buf).forEach(b=>bin+=String.fromCharCode(b));return 'data:audio/wav;base64,'+btoa(bin)}
function boot(){applyDeviceLive(window.ATLAS_INITIAL_LIVE||{});setupPetStage();refreshStatus(true);setInterval(()=>syncDevice(true),3500)}
"""


_APP_TEMPLATE = Template("""<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atlas Robot</title>
<style>$base_css</style>
<body><main class="shell">
  <header class="topbar">
    <div class="brand"><div class="mark"><span></span><span></span></div><div><b>Atlas Robot</b><p class="muted" style="margin:2px 0 0">桌面伙伴控制台</p></div></div>
    <nav class="nav"><a href="/devices">设备</a><a href="/admin">高级设置</a><a href="/acceptance">烧录验收</a></nav>
  </header>
  <section class="hero">
    <div class="device-stage" id="deviceStage" data-online="$online_attr" data-theme="$theme" data-chat-mode="$chat_mode" data-expression="$expression">
      <div class="status-strip"><span class="pill"><i id="deviceDot" class="dot $state_class"></i><span id="deviceState">$state_label</span></span><span class="pill" id="scenePill">$scene</span></div>
      <div class="eye-pair"><div class="eye"></div><div class="eye"></div></div>
      <div class="pet-head-stage" id="petHeadStage"><span class="pet-shadow"></span><img class="pet-head-img" id="petHeadAsset" alt="" src="/assets/pet_head/keyframes/idle.png"><div class="pet-head"><span class="pet-brow left"></span><span class="pet-brow right"></span><span class="pet-eye left"><i></i></span><span class="pet-eye right"><i></i></span><span class="pet-cheek left"></span><span class="pet-cheek right"></span><span class="pet-nose"></span><span class="pet-mouth"></span></div><span class="pet-stage-hint">鼠标会被它盯上</span></div>
      <div class="status-strip sync-line"><span class="pill">主题 <span id="themePill">$theme</span></span><span class="pill">界面 <span id="chatModePill">$chat_mode</span></span><span class="pill">页面 <span id="pagePill">$page</span></span><span class="pill">表情 <span id="expressionPill">$expression</span></span></div>
    </div>
    <div class="panel stack">
      <div><p class="eyebrow">My robot</p><h1>$device_name</h1><p class="subtitle">听你说话，回应你，也把情绪和应用同步到双眼。</p></div>
      <div class="chat-box"><textarea id="text" placeholder="例如：给我讲个短故事 / 打开番茄专注 / 今天济南天气怎么样"></textarea></div>
      <div class="row"><button class="secondary" id="contBtn" onclick="toggleContinuous()">连续对话</button><button onclick="sendText()">发送</button><button class="secondary" id="recBtn" onclick="toggleRec('full')">按一次说话</button><button class="ghost" onclick="sendTextOnly()">只发文字</button><button class="ghost" onclick="toggleRec('asr')">语音转文字</button><button class="ghost" onclick="speakText()">朗读</button><label class="muted"><input id="speak" type="checkbox" checked> 自动播报</label><span id="recState" class="muted">待机</span></div>
      <div class="tone-select"><select id="ttsVoice"><option value="mimo_default">默认音色</option><option value="冰糖">冰糖</option><option value="茉莉">茉莉</option><option value="苏打">苏打</option><option value="白桦">白桦</option><option value="Mia">Mia</option><option value="Chloe">Chloe</option></select><select id="ttsStyle"><option value="playful">俏皮</option><option value="sweet">甜美</option><option value="jiazi">夹子音</option><option value="excited">兴奋</option><option value="singing">唱歌</option><option value="default">自然</option></select></div>
      <label class="muted"><input id="ttsSinging" type="checkbox"> 唱歌模式</label>
      <audio id="player" controls></audio>
    </div>
  </section>
  <section class="panel control-block">
    <div class="section-head"><div><p class="eyebrow">Eyes</p><h2>眼睛展示</h2><p>只切双眼视觉主题，不改变对话界面。</p></div><span class="pill">当前 <span id="themePill2">$theme</span></span></div>
    <div class="control-grid">
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'pet'},false)"><b>电子宠物眼</b><small>温和陪伴视觉</small></button>
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'raptor'},false)"><b>猛禽眼</b><small>野兽警戒视觉</small></button>
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'mecha'},false)"><b>机械眼</b><small>电子冷光视觉</small></button>
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'goggle'},false)"><b>护目镜</b><small>黄色护目镜视觉</small></button>
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'blue_pupil'},false)"><b>蓝瞳</b><small>圆形蓝色瞳孔</small></button>
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'tomoe_spin'},false)"><b>红色旋纹</b><small>旋纹视觉主题</small></button>
      <button class="control-card eye-soft" onclick="skill('atlas.set_theme',{theme:'no_smoking'},false)"><b>禁烟</b><small>禁烟禁电子烟主题</small></button>
    </div>
  </section>
  <section class="panel control-block">
    <div class="section-head"><div><p class="eyebrow">Conversation</p><h2>对话界面</h2><p>选择对话时双屏如何呈现。</p></div><span class="pill">当前 <span id="chatModePill2">$chat_mode</span></span></div>
    <div class="control-grid">
      <button class="control-card chat-soft" onclick="setChatMode('pet_head')"><b>土拨鼠头</b><small>左屏宠物头，右屏短文本</small></button>
      <button class="control-card chat-soft" onclick="setChatMode('text')"><b>双屏文字</b><small>两块屏幕展示对话字幕</small></button>
      <button class="control-card chat-soft" onclick="setChatMode('eyes_only')"><b>纯眼睛</b><small>不显示文字，只做眼神和表情</small></button>
    </div>
  </section>
  <section class="panel control-block">
    <div class="section-head"><div><p class="eyebrow">Apps</p><h2>桌面 App</h2><p>打开具体功能页，适合日常使用。</p></div><span class="pill">当前 <span id="pagePill2">$page</span></span></div>
    <div class="control-grid">
      <button class="control-card app-soft" onclick="skill('atlas.clock.show',{},false)"><b>时钟</b><small>桌面时间与圆形石英表</small></button>
      <button class="control-card app-soft" onclick="skill('atlas.pomodoro.start',{task_name:'当前任务',focus_minutes:25},true)"><b>番茄</b><small>25 分钟专注</small></button>
      <button class="control-card app-soft" onclick="skill('atlas.calendar.today',{},true)"><b>日历</b><small>今日卡片与提醒</small></button>
      <button class="control-card app-soft" onclick="skill('atlas.weather.query',{location:''},true)"><b>天气</b><small>默认城市天气</small></button>
      <button class="control-card app-soft" onclick="skill('atlas.music.play',{},true)"><b>音乐</b><small>播放入口与唱歌表情</small></button>
      <button class="control-card app-soft" onclick="skill('atlas.story.tell',{},true)"><b>故事</b><small>睡前/陪伴故事</small></button>
    </div>
  </section>
  <section class="panel control-block">
    <div class="section-head"><div><p class="eyebrow">Persona</p><h2>人格模式</h2><p>联动主题、页面、TTS 风格和系统提示词。</p></div></div>
    <div class="control-grid">
      <button class="control-card primary-soft" onclick="skill('atlas.role.switch',{role:'pet'},true)"><b>电子宠物</b><small>回到陪伴人格</small></button>
      <button class="control-card primary-soft" onclick="skill('atlas.role.switch',{role:'raptor'},true)"><b>猛禽</b><small>警戒感人格</small></button>
      <button class="control-card primary-soft" onclick="skill('atlas.role.switch',{role:'mecha'},true)"><b>机械电子</b><small>清晰克制人格</small></button>
      <button class="control-card primary-soft" onclick="skill('atlas.role.switch',{role:'goggle'},true)"><b>护目镜</b><small>明亮日常人格</small></button>
    </div>
  </section>
  <section class="panel stack control-block">
    <div><p class="eyebrow">Pet Head V0.3</p><h2>宠物表现</h2><p class="subtitle">透明底、无 SD 卡、只有头。真机负责拟 3D 转头和嘴型动画，Brain 负责下发状态。</p></div>
    <div class="row">
      <span class="pill">状态 <span id="petVisualState">待机</span></span>
      <span class="pill" id="petVisualAsset">资源待同步</span>
      <span class="pill" id="petVisualMotion">yaw_c</span>
    </div>
    <div class="quick-grid">
      <button class="app-card" onclick="petState('idle','我在')"><b>待机</b><small>慢转头</small></button>
      <button class="app-card" onclick="petState('listen','我在听')"><b>聆听</b><small>轻歪头</small></button>
      <button class="app-card" onclick="petState('think','我想想')"><b>思考</b><small>左右摆头</small></button>
      <button class="app-card" onclick="petAnim('speak','我开口了')"><b>说话</b><small>嘴型循环</small></button>
      <button class="app-card" onclick="petAnim('sing','准备开唱')"><b>唱歌</b><small>唱歌嘴型</small></button>
      <button class="app-card" onclick="petState('happy','开心')"><b>开心</b><small>软胶笑脸</small></button>
      <button class="app-card" onclick="petAnim('laugh','哈哈哈')"><b>大笑</b><small>夸张反馈</small></button>
      <button class="app-card" onclick="petState('cry','委屈了')"><b>大哭</b><small>失败软反馈</small></button>
      <button class="app-card" onclick="petState('sleepy','困了')"><b>困</b><small>低动效</small></button>
      <button class="app-card" onclick="petState('surprised','啊？')"><b>惊讶</b><small>突然反应</small></button>
      <button class="app-card" onclick="petState('offline','Brain 离线')"><b>离线</b><small>不进异常页</small></button>
      <button class="app-card" onclick="petAnim('blink','眨一下')"><b>眨眼</b><small>短动画</small></button>
    </div>
  </section>
  <section class="mini-grid">
    <div class="mini"><b>Brain</b><span id="brainState" class="warn">检查中</span><p class="muted">$lan_url</p></div>
    <div class="mini"><b>模型</b><span id="providerState" class="warn">检查中</span><p class="muted">MiMo LLM / ASR / TTS</p></div>
    <div class="mini"><b>DualEye</b><span id="deviceConnState" class="$state_class">$state_label</span><p id="deviceIp" class="muted">$dualeye_url</p></div>
    <div class="mini"><b>会话</b><span id="voiceLoopState" class="muted">待机</span><p id="audioMode" class="muted">音频待机</p></div>
    <div class="mini"><b>最近动作</b><span id="lastTurnState" class="muted">暂无会话</span><p id="lastSync" class="muted last-sync">等待同步</p></div>
    <div class="mini"><b>底盘</b><span class="warn">$rover_label</span><p id="sceneTitle" class="muted">这一版先做桌面机器人</p></div>
  </section>
  <section class="grid">
    <div class="mini"><b>识别</b><span id="asrText" class="muted">等待语音</span></div>
    <div class="mini"><b>回复</b><span id="replyText" class="muted">等待回复</span></div>
    <div class="mini"><b>播报</b><span id="playState" class="muted">待机</span></div>
  </section>
  <details class="drawer"><summary>高级诊断</summary><div class="row" style="margin:12px 0"><button class="secondary" onclick="refreshStatus()">刷新状态</button><button class="secondary" onclick="getJson('/api/providers').then(out)">Provider</button><button class="secondary" onclick="getJson('/api/tools/list').then(out)">工具表</button><button class="secondary" onclick="getJson('/api/runtime/score').then(out)">评分</button></div><pre id="out" class="debug">$initial_snapshot</pre></details>
</main><script>window.ATLAS_INITIAL_LIVE=$initial_snapshot;$app_js boot();</script></body></html>""")


_DEVICES_TEMPLATE = Template("""<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atlas 设备</title>
<style>$base_css</style><body><main class="shell"><header class="topbar"><div class="brand"><div class="mark"><span></span><span></span></div><div><b>Atlas 设备</b><p class="muted" style="margin:2px 0 0">选择要控制的机器人</p></div></div><nav class="nav"><a href="/">控制台</a><a href="/admin">高级设置</a></nav></header><section class="stack">$cards</section></main></body></html>""")


_ADMIN_TEMPLATE = Template("""<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Atlas 管理后台</title>
<style>$base_css</style><body><main class="shell"><header class="topbar"><div class="brand"><div class="mark"><span></span><span></span></div><div><b>Atlas 管理后台</b><p class="muted" style="margin:2px 0 0">设备、模型、协议、OTA</p></div></div><nav class="nav"><a href="/">控制台</a><a href="/devices">设备</a><a href="/acceptance">烧录验收</a></nav></header>
<section class="panel"><p class="eyebrow">System</p><h1>平台状态</h1><p class="subtitle">DualEye：$dualeye_url · LLM：$llm_model · ASR：$asr_model · TTS：$tts_model</p><div class="grid">$cards</div></section>
<section class="quick-grid">
  <button class="app-card" onclick="getJson('/api/acceptance/report')"><b>验收报告</b><small>烧录前后检查</small></button>
  <button class="app-card" onclick="getJson('/api/runtime/score')"><b>运行评分</b><small>80 分目标</small></button>
  <button class="app-card" onclick="getJson('/api/runtime/diagnostics')"><b>体验诊断</b><small>最近 Turn</small></button>
  <button class="app-card" onclick="getJson('/api/platform')"><b>平台快照</b><small>设备与应用</small></button>
  <button class="app-card" onclick="getJson('/api/providers')"><b>Provider</b><small>模型配置</small></button>
  <button class="app-card" onclick="getJson('/api/protocols')"><b>协议通道</b><small>音频与事件</small></button>
  <button class="app-card" onclick="getJson('/api/tools/list')"><b>工具表</b><small>技能 Schema</small></button>
  <button class="app-card" onclick="getJson('/api/audio/stream/status')"><b>音频流</b><small>OPUS 状态</small></button>
  <button class="app-card" onclick="getJson('/ota/manifest')"><b>OTA</b><small>包清单</small></button>
</section>
<section class="panel"><div class="row"><button onclick="runPost('/api/device/opus-stream/start',{duration_ms:1800})">启动 OPUS 真流</button><button class="secondary" onclick="runPost('/api/device/opus-stream/stop',{})">停止真流</button><button class="secondary" onclick="runPost('/api/diagnostics/simulate-turn',{speak:true})">模拟 Turn</button><button class="secondary" onclick="getJson('/api/brain/events')">最近事件</button><button class="secondary" onclick="getJson('/health')">Health</button></div><pre id="out" class="debug">等待诊断...</pre></section>
</main><script>
function out(v){document.getElementById('out').textContent=typeof v==='string'?v:JSON.stringify(v,null,2)}
async function getJson(url){const r=await fetch(url);const t=await r.text();try{out(JSON.parse(t))}catch(e){out(t)}}
async function runPost(url,payload){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload||{})});const t=await r.text();try{out(JSON.parse(t))}catch(e){out(t)}}
getJson('/health');
</script></body></html>""")
