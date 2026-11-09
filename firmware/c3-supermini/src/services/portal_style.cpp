#include "services/portal_style.h"

#include "version.h"

namespace services::portal {
namespace {

// Matches the firmware page's original look: navy background, one rounded
// card, full-width blue buttons. WiFiManager wraps each page in
// <div class='wrap'>; our own pages use the same class.
#define PORTAL_CSS                                                           \
  "<style>"                                                                  \
  "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;"          \
  "background:#07101f;color:#eef6ff;margin:0;padding:1.5rem 1rem;"           \
  "text-align:left}"                                                         \
  ".wrap{display:block;box-sizing:border-box;max-width:34rem;min-width:0;"   \
  "margin:0 auto;padding:1.5rem;background:#101d31;border:1px solid #274464;" \
  "border-radius:14px;text-align:left}"                                      \
  "h1{font-size:1.6rem;margin:.2rem 0 .4rem}"                                \
  "h3{color:#a9bad0;font-weight:600;margin:.2rem 0 1rem}"                    \
  "hr{border:0;border-top:1px solid #274464}"                                \
  "input,select{background:#07101f;color:#eef6ff;border:1px solid #274464;"  \
  "border-radius:8px;padding:.6rem}"                                         \
  "input:focus,select:focus{outline:2px solid #2b8aef;border-color:#2b8aef}" \
  "input[type=checkbox],input[type=radio]{accent-color:#2b8aef;"             \
  "margin-right:.4rem}"                                                      \
  "input[type=file]{border:1px dashed #274464}"                              \
  "button,input[type=button],input[type=submit]{background:#2b8aef;"         \
  "color:#fff;border:0;border-radius:8px;font-weight:700;font-size:1rem;"    \
  "line-height:2.6rem;margin:.35rem 0}"                                      \
  "button.D{background:#d9483b}"                                             \
  "a{color:#73b7ff;font-weight:600}a:hover{color:#a9d3ff}"                   \
  "small,dt{color:#a9bad0}dt{font-weight:600}"                              \
  "dd{color:#eef6ff;margin:0 0 .6rem;padding:0}"                             \
  "label{color:#d6e4f5}"                                                     \
  ".msg{background:#0b1728;border:1px solid #274464;border-left-width:5px;"  \
  "border-radius:8px;padding:.75rem 1rem;margin:.75rem 0 1rem;"             \
  "line-height:1.45}"                                                        \
  ".msg.S{border-left-color:#4cc27a}.msg.D{border-left-color:#d9483b}"       \
  ".msg.P{border-left-color:#2b8aef}"                                        \
  /* Status page "Update available": text left, button right (wraps). */ \
  ".upd{display:flex;flex-wrap:wrap;align-items:center;gap:.6rem 1rem}"     \
  ".upd>div{flex:1 1 12rem}.upd small{display:block}"                       \
  ".upd button{width:auto;margin:0;padding:0 1.4rem;line-height:2.4rem}"    \
  ".upd p{flex-basis:100%;margin:0;padding-top:.6rem;"                      \
  "border-top:1px solid #274464;font-size:.85rem;color:#a9bad0}"            \
  ".q[role=img]{filter:invert(1)}"                                           \
  "input,select,button{box-sizing:border-box;width:100%}"                   \
  "input[type=checkbox],input[type=radio]{width:auto}"                      \
  "progress{width:100%;accent-color:#2b8aef}"                                \
  ".hint{font-size:.85em;color:#a9bad0;margin:2px 0 10px}"                  \
  "code{font-size:.9em;color:#d6e4f5;word-break:break-all}"                 \
  ".tabs{display:flex;flex-wrap:wrap;gap:.35rem;margin:0 0 1rem}"            \
  ".tabs button{width:auto;flex:1 1 auto;margin:0;padding:0 .7rem;"         \
  "line-height:2.1rem;font-size:.9rem;background:#0b1728;color:#a9bad0;"    \
  "border:1px solid #274464}"                                                \
  ".tabs button.on{background:#2b8aef;border-color:#2b8aef;color:#fff}"      \
  "form.tabbed section.tab h3{display:none}"                                 \
  "nav{display:flex;flex-wrap:wrap;gap:.4rem;margin:0 0 1rem;"               \
  "padding-bottom:.9rem;border-bottom:1px solid #274464}"                    \
  "nav a{padding:.35rem .75rem;border-radius:999px;background:#0b1728;"      \
  "border:1px solid #274464;color:#a9bad0;font-size:.9rem;"                  \
  "text-decoration:none}"                                                    \
  "nav a:hover{color:#eef6ff;border-color:#2b8aef;text-decoration:none}"     \
  "nav a.on{background:#2b8aef;border-color:#2b8aef;color:#fff}"             \
  "</style>"                                                                 \
  /* Links to the other portal pages at the top of every page's card.    */ \
  /* A script, because WiFiManager has no hook for its page bodies.       */ \
  "<script>document.addEventListener('DOMContentLoaded',function(){"         \
  "var w=document.querySelector('.wrap');if(!w)return;"                      \
  "var p=location.pathname,h='';"                                            \
  "[['/','Home',['/']],"                                                    \
  "['/0wifi','Wi-Fi',['/wifi','/0wifi','/wifisave','/erase']],"              \
  "['/param','Setup',['/param','/paramsave','/receiver','/receiversave']],"  \
  "['/info','Info',['/info']],"                                              \
  "['/firmware','Firmware',['/firmware','/update','/firmware-upload','/u']]," \
  "['/about','About',['/about']]"                                            \
  "].forEach(function(l){h+='<a href=\"'+l[0]+'\"'+"                        \
  "(l[2].indexOf(p)>=0?' class=on':'')+'>'+l[1]+'</a>';});"                 \
  "w.insertAdjacentHTML('afterbegin','<nav>'+h+'</nav>');});</script>"

}  // namespace

const char kStyleTag[] = PORTAL_CSS;

const char kHeadElement[] =
    PORTAL_CSS
    // The Info page has no hook for extra rows: add the version from here.
    "<script>document.addEventListener('DOMContentLoaded',function(){"
    "if(location.pathname!='/info')return;"
    "var d=document.querySelector('dl');if(!d)return;"
    "d.insertAdjacentHTML('afterbegin',"
    "'<dt>Firmware version</dt><dd>" PLANE_RADAR_VERSION "</dd>');});"
    "</script>"
    // Wi-Fi page without the up-front scan: a button fetches the scanning
    // page (/wifi) in the background and copies its network list in. Each
    // entry keeps WiFiManager's onclick that fills in the SSID field.
    "<script>document.addEventListener('DOMContentLoaded',function(){"
    // Home menu: the library labels this button "(No scan)".
    "var m=document.querySelector(\"form[action='/0wifi'] button\");"
    "if(m)m.textContent='Configure WiFi';"
    "if(location.pathname!='/0wifi')return;"
    "var f=document.querySelector(\"form[action='wifisave']\");if(!f)return;"
    "var r=document.querySelector(\"form[action^='/wifi?refresh']\");"
    "if(r)r.style.display='none';"
    "f.insertAdjacentHTML('beforebegin','<button type=button id=scan>"
    "Show visible networks</button><div id=nets></div>');"
    "var b=document.getElementById('scan'),n=document.getElementById('nets'),"
    "u='/wifi';"
    "b.onclick=function(){b.disabled=true;b.textContent='Scanning...';"
    "fetch(u,{credentials:'same-origin'}).then(function(x){"
    "if(!x.ok)throw 0;return x.text();}).then(function(t){"
    "var d=new DOMParser().parseFromString(t,'text/html'),h='';"
    "d.querySelectorAll('a[data-ssid]').forEach(function(a){"
    "h+=a.parentNode.outerHTML;});"
    "n.innerHTML=h||'<p><small>No networks found.</small></p>';"
    "b.textContent='Scan again';u='/wifi?refresh=1';"
    "}).catch(function(){b.textContent='Scan failed - try again';"
    "}).then(function(){b.disabled=false;});};"
    "f.parentNode.insertAdjacentHTML('beforeend','<hr><p><small>Forget the "
    "saved network and restart into the setup hotspot:</small></p>"
    "<a href=\"/erase\"><button type=button class=D>Forget this Wi-Fi"
    "</button></a>');});"
    "</script>"
    // Setup: one tab per <section class=tab>; all fields stay in the one
    // form, so Save stores every tab. The open tab is remembered.
    "<script>document.addEventListener('DOMContentLoaded',function(){"
    "var s=document.querySelectorAll('section.tab');if(!s.length)return;"
    "var f=s[0].closest('form');f.classList.add('tabbed');"
    "var h='',k='setupTab',c=0;"
    "try{c=+sessionStorage.getItem(k)||0;}catch(e){}"
    "if(c>=s.length)c=0;"
    "s.forEach(function(x,i){h+='<button type=button data-i='+i+'>'+"
    "x.getAttribute('data-tab')+'</button>';});"
    "f.insertAdjacentHTML('afterbegin','<div class=tabs>'+h+'</div>');"
    "var b=f.querySelectorAll('.tabs button');"
    "function show(i){s.forEach(function(x,j){x.hidden=j!=i;});"
    "b.forEach(function(x,j){x.classList.toggle('on',j==i);});"
    "try{sessionStorage.setItem(k,i);}catch(e){}}"
    "b.forEach(function(x){x.onclick=function(){show(+x.dataset.i);};});"
    // A field that fails validation on a hidden tab: open its tab.
    "f.addEventListener('invalid',function(e){var t=e.target.closest("
    "'section.tab');s.forEach(function(x,i){if(x==t)show(i);});},true);"
    "show(c);});"
    "</script>"
    // Setup coordinates: round to 6 decimals; a pasted "lat, lon" pair
    // (as Google Maps copies it) fills both boxes.
    "<script>document.addEventListener('DOMContentLoaded',function(){"
    "var a=document.getElementById('radar_lat'),"
    "o=document.getElementById('radar_lon');if(!a||!o)return;"
    "function r(e){var v=parseFloat(e.value);"
    "if(isFinite(v))e.value=+v.toFixed(6);}"
    "[a,o].forEach(function(e){e.addEventListener('change',function(){r(e);});"
    "e.addEventListener('paste',function(ev){var t=(ev.clipboardData||"
    "window.clipboardData).getData('text'),"
    "m=t.match(/(-?\\d+(?:\\.\\d+)?)\\s*[,; ]\\s*(-?\\d+(?:\\.\\d+)?)/);"
    "if(m){ev.preventDefault();a.value=m[1];o.value=m[2];}"
    "else{var n=t.match(/-?\\d+(?:\\.\\d+)?/);"
    "if(n){ev.preventDefault();e.value=n[0];}}"
    "r(a);r(o);});});"
    "a.form.addEventListener('submit',function(){r(a);r(o);});});"
    "</script>";

String page(const char* title, const char* extra_head,
            const String& body_html) {
  String out;
  out.reserve(sizeof(kStyleTag) + body_html.length() + 256);
  out += F("<!doctype html><html lang='en'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,"
           "initial-scale=1'><title>");
  out += title;
  out += F("</title>");
  out += extra_head;
  out += kStyleTag;
  out += F("</head><body><div class='wrap'>");
  out += body_html;
  out += F("</div></body></html>");
  return out;
}

}  // namespace services::portal
