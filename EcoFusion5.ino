#include <WiFiS3.h>
#include <math.h>

const char* WIFI_SSID = "Ayanka";
const char* WIFI_PASSWORD = "12345678";

// ---------------- hardware + calibration ----------------
const uint8_t PIN_PIEZO   = A0;
const uint8_t PIN_STORAGE = A1;
const uint8_t PIN_WATER   = A2;
const uint8_t PIN_DHT     = 2;
const bool HAS_PIEZO   = true;
const bool HAS_STORAGE = true;
const bool HAS_WATER   = true;
const bool DHT_ENABLED = true;
const uint8_t DHT_TYPE = 11;            // 22 = DHT22, 11 = DHT11
const int WATER_DRY_ADC = 0;            // raw reading with sensor dry
const int WATER_WET_ADC = 3000;         // raw reading with sensor wet (calibrate)

const float ADC_VREF = 5.0f;            // UNO R4 analog reference
const int   ADC_BITS = 12;
const float ADC_MAX  = 4095.0f;
const float PIEZO_DIVIDER   = 1.0f;     // real volts = pin volts * ratio
const float STORAGE_DIVIDER = 2.0f;
const float LOAD_OHMS = 1000.0f;        // load the harvested power is measured across (P = V^2 / R)
const float FORCE_FULL_SCALE_N = 1000.0f; // force shown when the pin reads full scale (calibrate!)

// Footstep detection (all voltages are measured ABOVE the idle level found at boot).
// A step needs the signal to stay above STEP_ON_V for STEP_CONFIRM_SAMPLES samples AND
// at least STEP_MIN_ABOVE_US microseconds, so a single noise spike cannot count.
// The next step is accepted only after the signal has stayed below STEP_OFF_V for
// STEP_REARM_MS and STEP_DEBOUNCE_MS has passed since the last step.
const float STEP_ON_V  = 0.40f;          // lower = more sensitive, raise if false steps return
const float STEP_OFF_V = 0.18f;
const float NOISE_GATE_V = 0.08f;        // drift tracking only; does not affect displayed voltage
const uint8_t PIEZO_OVERSAMPLE = 4;      // ADC reads averaged per sample
const uint8_t STEP_CONFIRM_SAMPLES = 2;
const unsigned long STEP_MIN_ABOVE_US = 500;
const unsigned long STEP_REARM_MS = 40;
const unsigned long STEP_DEBOUNCE_MS = 250;

const unsigned long HISTORY_INTERVAL_MS = 30000UL;  // one history sample every 30 s
const uint8_t HISTORY_N = 48;
const size_t SEND_CHUNK = 1024;         // bytes per Wi-Fi write

WiFiServer server(80);
bool apMode = false;

// ---------------- the website (served from flash) ----------------
// Edit the HTML below if you want to change the site. Each part is a raw string.
struct PagePart { const char* p; uint16_t n; };
static const char PG0[] PROGMEM = R"ECOSENSE(<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>EcoFusion by Ω(1) Thinkers | Kinetic Energy & Environmental Mesh</title>
<meta
name="description"
content="Ω (1) Thinkers monitors environmental conditions and footstep-generated electricity using smart Arduino and ESP32 systems."
/>
<meta name="theme-color" content="#070d18" />
<script src="https://cdn.tailwindcss.com"></script>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script type="importmap">
{
"imports": {
"ogl": "https://unpkg.com/ogl@1.0.11/dist/ogl.mjs"
}
}
</script>
<style>
@import url("https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,500;600;700;800&display=swap");
:root {
--bg: #edf3fa;
--text: #172033;
--muted: #667085;
--glass: rgba(255, 255, 255, 0.52);
--glass-strong: rgba(255, 255, 255, 0.82);
--border: rgba(255, 255, 255, 0.75);
--accent: #2563eb;
--cyan: #06b6d4;
--amber: #f59e0b;
--emerald: #10b981;
--rose: #f43f5e;
--shadow: 0 24px 80px rgba(38, 53, 91, 0.12);
--dock-text: #4a5568;
--dock-text-hover: #0e1116;
--dock-tint-a: rgba(255, 255, 255, 0.55);
--dock-tint-b: rgba(255, 255, 255, 0.18);
--dock-border: rgba(255, 255, 255, 0.85);
--dock-shadow: rgba(38, 53, 91, 0.28);
--dock-active-bg: linear-gradient(180deg, rgba(23, 32, 51, 0.88), rgba(23, 32, 51, 0.72));
--dock-active-text: #ffffff;
--dock-active-rim: rgba(255, 255, 255, 0.35);
}
[data-theme="dark"] {
--bg: #070d18;
--text: #edf3ff;
--muted: #8c9bb5;
--glass: rgba(18, 27, 46, 0.6);
--glass-strong: rgba(23, 34, 58, 0.88);
--border: rgba(255, 255, 255, 0.12);
--accent: #3b82f6;
--cyan: #38bdf8;
--amber: #fbbf24;
--emerald: #34d399;
--rose: #fb7185;
--shadow: 0 24px 80px rgba(0, 0, 0, 0.5);
--dock-text: rgba(237, 243, 255, 0.72);
--dock-text-hover: #ffffff;
--dock-tint-a: rgba(255, 255, 255, 0.2);
--dock-tint-b: rgba(255, 255, 255, 0.05);
--dock-border: rgba(255, 255, 255, 0.3);
--dock-shadow: rgba(0, 0, 0, 0.6);
--dock-active-bg: linear-gradient(180deg, rgba(255, 255, 255, 0.34), rgba(255, 255, 255, 0.14));
--dock-active-text: #ffffff;
--dock-active-rim: rgba(255, 255, 255, 0.45);
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
min-height: 100vh;
color: var(--text);
font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
background:
radial-gradient(circle at 20% 15%, rgba(37, 99, 235, 0.22), transparent 32rem),
radial-gradient(circle at 85% 25%, rgba(6, 182, 212, 0.2), transparent 30rem),
var(--bg);
background-attachment: fixed;
transition: background .35s ease, color .35s ease;
overflow-x: hidden;
scroll-behavior: smooth;
}
#strandsOverlay {
position: fixed;
inset: 0;
z-index: 99999;
pointer-events: none;
opacity: 0;
transition: opacity 0.4s cubic-bezier(0.16, 1, 0.3, 1);
background: rgba(3, 7, 18, 0.85);
backdrop-filter: blur(16px);
-webkit-backdrop-filter: blur(16px);
display: flex;
flex-direction: column;
align-items: center;
justify-content: center;
}
#strandsOverlay.active {
opacity: 1;
pointer-events: all;
}
.strands-container {
position: absolute;
inset: 0;
width: 100%;
height: 100%;
background: transparent;
pointer-events: none;
}
.strands-container canvas {
display: block;
width: 100%;
height: 100%;
}
.loader-holder {
position: relative;
z-index: 10;
font-size: 20px;
}
.pl {
width: 6em;
height: 6em;
}
.pl__ring {
animation: ringA 2s linear infinite;
}
.pl__ring--a {
stroke: #f42f25;
}
.pl__ring--b {
animation-name: ringB;
stroke: #f49725;
}
.pl__ring--c {
animation-name: ringC;
stroke: #255ff4;
}
.pl__ring--d {
animation-name: ringD;
stroke: #f42582;
}
@keyframes ringA {
from, 4% {
stroke-dasharray: 0 660;
stroke-width: 20;
stroke-dashoffset: -330;
}
12% {
stroke-dasharray: 60 600;
stroke-width: 30;
stroke-dashoffset: -335;
}
32% {
stroke-dasharray: 60 600;
stroke-width: 30;
stroke-dashoffset: -595;
}
40%, 54% {
stroke-dasharray: 0 660;
stroke-width: 20;
stroke-dashoffset: -660;
}
62% {
stroke-dasharray: 60 600;
stroke-width: 30;
stroke-dashoffset: -665;
}
82% {
stroke-dasharray: 60 600;
stroke-width: 30;
stroke-dashoffset: -925;
}
90%, to {
stroke-dasharray: 0 660;
stroke-width: 20;
stroke-dashoffset: -990;
}
}
@keyframes ringB {
from, 12% {
stroke-dasharray: 0 220;
stroke-width: 20;
stroke-dashoffset: -110;
}
20% {
stroke-dasharray: 20 200;
stroke-width: 30;
stroke-dashoffset: -115;
}
40% {
stroke-dasharray: 20 200;
stroke-width: 30;
stroke-dashoffset: -195;
}
48%, 62% {
stroke-dasharray: 0 220;
stroke-width: 20;
stroke-dashoffset: -220;
}
70% {
stroke-dasharray: 20 200;
stroke-width: 30;
stroke-dashoffset: -225;
}
90% {
stroke-dasharray: 20 200;
stroke-width: 30;
stroke-dashoffset: -305;
}
98%, to {
stroke-dasharray: 0 220;
stroke-width: 20;
stroke-dashoffset: -330;
}
}
@keyframes ringC {
from {
stroke-dasharray: 0 440;
stroke-width: 20;
stroke-dashoffset: 0;
}
8% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -5;
}
28% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -175;
}
36%, 58% {
stroke-dasharray: 0 440;
stroke-width: 20;
stroke-dashoffset: -220;
}
66% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -225;
}
86% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -395;
}
94%, to {
stroke-dasharray: 0 440;
stroke-width: 20;
stroke-dashoffset: -440;
}
}
@keyframes ringD {
from, 8% {
stroke-dasharray: 0 440;
stroke-width: 20;
stroke-dashoffset: 0;
}
16% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -5;
}
36% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -175;
}
44%, 50% {
stroke-dasharray: 0 440;
stroke-width: 20;
stroke-dashoffset: -220;
}
58% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -225;
}
78% {
stroke-dasharray: 40 400;
stroke-width: 30;
stroke-dashoffset: -395;
}
86%, to {
stroke-dasharray: 0 440;
stroke-width: 20;
stroke-dashoffset: -440;
}
}
.gradient-waves-container {
position: absolute;
inset: 0;
width: 100%;
height: 100%;
overflow: hidden;
z-index: 0;
pointer-events: auto;
border-radius: 1.5rem;
}
.gradient-waves-container canvas {
display: block;
width: 100%;
height: 100%;
}
.nav {
position: sticky;
top: 0;
z-index: 100;
display: grid;
grid-template-columns: 1fr auto 1fr;
align-items: center;
gap: 1rem;
padding: 0.85rem 5vw;
background: linear-gradient(to bottom, color-mix(in srgb, var(--bg) 85%, transparent), transparent);
pointer-events: none;
}
.nav > * { pointer-events: auto; }
.brand {
justify-self: start;
color: var(--text);
text-decoration: none;
font-weight: 800;
letter-spacing: -.04em;
white-space: nowrap;
font-size: 1.15rem;
}
.nav-end {
justify-self: end;
display: flex;
align-items: center;
}
.dock-wrap {
display: flex;
min-width: 0;
padding: 2px;
}
.cir-tabs {
position: relative;
isolation: isolate;
display: inline-flex;
)ECOSENSE";
static const char PG1[] PROGMEM = R"ECOSENSE(align-items: center;
gap: 4px;
padding: 6px;
margin-inline: auto;
background:
linear-gradient(135deg, var(--dock-tint-a), var(--dock-tint-b) 48%, var(--dock-tint-a));
border: 1px solid var(--dock-border);
border-radius: 999px;
-webkit-backdrop-filter: blur(18px) saturate(190%);
backdrop-filter: blur(18px) saturate(190%);
box-shadow:
inset 0 1px 1px rgba(255, 255, 255, 0.75),
inset 0 -1px 1px rgba(255, 255, 255, 0.18),
inset 0 0 22px rgba(255, 255, 255, 0.1),
0 1px 1px rgba(14, 17, 22, 0.06),
0 20px 40px -24px var(--dock-shadow);
font-family:
"Inter",
system-ui,
-apple-system,
sans-serif;
}
.cir-tabs::before {
content: "";
position: absolute;
inset: 0;
border-radius: inherit;
pointer-events: none;
z-index: 0;
background:
radial-gradient(120% 150% at 12% -30%, rgba(255, 255, 255, 0.5), transparent 46%),
radial-gradient(90% 130% at 92% 140%, rgba(255, 255, 255, 0.24), transparent 52%);
opacity: 0.75;
}
.cir-tabs__r {
position: absolute;
opacity: 0;
pointer-events: none;
}
.cir-tabs__t {
position: relative;
z-index: 1;
display: inline-flex;
align-items: center;
justify-content: center;
height: 36px;
padding: 0 18px;
border-radius: 999px;
font-size: 14px;
font-weight: 500;
color: var(--dock-text);
cursor: pointer;
white-space: nowrap;
transition:
background-color 220ms cubic-bezier(0.22, 1, 0.36, 1),
color 220ms cubic-bezier(0.22, 1, 0.36, 1),
box-shadow 220ms cubic-bezier(0.22, 1, 0.36, 1);
}
.cir-tabs__t:hover {
color: var(--dock-text-hover);
}
.cir-tabs__r:checked + .cir-tabs__t {
background: var(--dock-active-bg);
color: var(--dock-active-text);
box-shadow:
inset 0 1px 0 var(--dock-active-rim),
0 1px 1px rgba(14, 17, 22, 0.06),
0 8px 18px -10px rgba(14, 17, 22, 0.55);
}
.cir-tabs__r:focus-visible + .cir-tabs__t {
box-shadow: 0 0 0 3px rgba(46, 125, 239, 0.45);
}
.theme-toggle {
--glass-brd: var(--border);
--toggle-bg: linear-gradient(135deg, var(--dock-tint-a), var(--dock-tint-b) 48%, var(--dock-tint-a));
--toggle-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.65), 0 12px 30px -16px var(--dock-shadow);
--theme-fade: 0.3s;
--theme-ease: cubic-bezier(0.22, 1, 0.36, 1);
--glow: var(--accent);
position: fixed;
top: calc(14px + env(safe-area-inset-top, 0px));
right: calc(20px + env(safe-area-inset-right, 0px));
z-index: 110;
display: block;
width: 104px;
height: 52px;
padding: 0;
border: 1px solid var(--glass-brd);
border-radius: 999px;
background: var(--toggle-bg);
box-shadow: var(--toggle-shadow);
backdrop-filter: blur(14px) saturate(165%);
-webkit-backdrop-filter: blur(14px) saturate(165%);
cursor: pointer;
transform: translateZ(0);
will-change: transform;
-webkit-tap-highlight-color: transparent;
transition: background var(--theme-fade) var(--theme-ease),
border-color var(--theme-fade) var(--theme-ease),
box-shadow var(--theme-fade) var(--theme-ease),
transform 0.35s var(--theme-ease);
}
.theme-toggle:hover { transform: translateY(-1px) translateZ(0); }
.theme-toggle:active { transform: translateY(0) scale(0.985) translateZ(0); }
.theme-toggle__input {
position: absolute;
inset: 0;
z-index: 3;
width: 100%;
height: 100%;
margin: 0;
opacity: 0;
cursor: pointer;
}
.theme-toggle__input:focus-visible ~ .theme-toggle__sky {
outline: 2px solid var(--glow);
outline-offset: 4px;
}
.theme-toggle__sky {
position: absolute;
inset: 0;
z-index: 1;
overflow: hidden;
border-radius: inherit;
}
.theme-toggle__cloud {
position: absolute;
top: 26px;
left: 44px;
width: 34px;
height: 11px;
border-radius: 999px;
background: radial-gradient(ellipse at 40% 28%, #fff 0%, #f3f8ff 55%, #dbe8f8 100%);
box-shadow: 0 3px 10px rgba(110, 165, 255, 0.32), inset 0 -2px 3px rgba(190, 215, 255, 0.55), inset 0 2px 3px rgba(255, 255, 255, 0.95);
transition: opacity 1.2s var(--theme-ease), scale 1.2s var(--theme-ease);
animation: themeCloudFloat 10s ease-in-out infinite alternate;
will-change: transform;
}
.theme-toggle__cloud::before,
.theme-toggle__cloud::after {
position: absolute;
border-radius: 50%;
content: "";
background: radial-gradient(circle at 35% 32%, #fff 0%, #f0f7ff 70%, #dbe8f8 100%);
box-shadow: inset 0 -2px 3px rgba(190, 215, 255, 0.5);
}
.theme-toggle__cloud::before { top: -8px; left: 3px; width: 16px; height: 16px; }
.theme-toggle__cloud::after { top: -6px; left: 18px; width: 12px; height: 12px; }
@keyframes themeCloudFloat {
from { transform: translateX(-8px); }
to { transform: translateX(8px); }
}
.theme-toggle__stars {
position: absolute;
inset: 0;
opacity: 0;
transition: opacity 1.1s var(--theme-ease) 0.25s;
will-change: opacity;
}
.theme-toggle__stars i {
position: absolute;
width: 3px;
height: 3px;
border-radius: 50%;
background: #fff;
box-shadow: 0 0 6px rgba(185, 218, 255, 0.95);
animation: themeTwinkle 2.4s ease-in-out infinite;
}
.theme-toggle__stars i:nth-child(1) { top: 16px; left: 18px; animation-delay: 0s; }
.theme-toggle__stars i:nth-child(2) { top: 29px; left: 30px; animation-delay: 0.7s; }
.theme-toggle__stars i:nth-child(3) { top: 33px; left: 14px; animation-delay: 1.3s; }
.theme-toggle__stars i:nth-child(4) { top: 12px; left: 34px; animation-delay: 1.9s; }
@keyframes themeTwinkle {
0%, 100% { opacity: 0.4; transform: scale(0.75); }
50% { opacity: 1; transform: scale(1.25); }
}
.theme-toggle__orb {
position: absolute;
top: 5px;
left: 6px;
z-index: 2;
display: grid;
place-items: center;
width: 40px;
height: 40px;
transition: transform 1.4s cubic-bezier(0.4, 0, 0.2, 1);
will-change: transform;
}
.theme-toggle__sun,
.theme-toggle__moon {
position: absolute;
width: 26px;
height: 26px;
border-radius: 50%;
}
.theme-toggle__sun {
background: radial-gradient(circle at 38% 32%, #fff 0%, #fff7cf 14%, #ffe066 38%, #ffc23a 62%, #ffa321 82%, #ff8513 100%);
box-shadow: inset -3px -3px 6px rgba(255, 130, 30, 0.55), inset 2px 2px 5px rgba(255, 255, 225, 0.95), 0 0 10px rgba(255, 215, 120, 0.9), 0 0 22px rgba(255, 180, 70, 0.7), 0 0 38px rgba(255, 140, 40, 0.45), 0 0 64px rgba(255, 110, 40, 0.22);
transition: opacity 1.1s var(--theme-ease), transform 1.4s cubic-bezier(0.4, 0, 0.2, 1);
will-change: opacity, transform;
}
.theme-toggle__moon {
opacity: 0;
transform: scale(0.3) rotate(120deg);
box-shadow: inset -8px -6px 0 0 #e8f2ff;
filter: drop-shadow(0 0 7px rgba(172, 212, 255, 0.9));
transition: opacity 1.1s var(--theme-ease) 0.15s, transform 1.4s cubic-bezier(0.4, 0, 0.2, 1) 0.1s;
will-change: opacity, transform;
}
html[data-theme="dark"] .theme-toggle { --toggle-bg: linear-gradient(135deg, rgba(20, 42, 67, 0.9), rgba(7, 20, 37, 0.82)); }
html[data-theme="dark"] .theme-toggle__cloud { opacity: 0; scale: 0.65; }
html[data-theme="dark"] .theme-toggle__stars { opacity: 1; }
html[data-theme="dark"] .theme-toggle__orb { transform: translateX(52px); }
html[data-theme="dark"] .theme-toggle__sun { opacity: 0; transform: scale(0.3) rotate(-120deg); }
html[data-theme="dark"] .theme-toggle__moon { opacity: 1; transform: scale(1) rotate(0deg); }
@media (prefers-reduced-motion: reduce) {
.theme-toggle, .theme-toggle *, .theme-toggle::before, .theme-toggle::after { animation-duration: 0.01ms !important; transition-duration: 0.01ms !important; }
}
@media (max-width: 1100px) {
.nav { grid-template-columns: 1fr auto; row-gap: 0.6rem; }
.dock-wrap {
grid-column: 1 / -1;
grid-row: 2;
overflow-x: auto;
padding-bottom: 10px;
margin-bottom: -10px;
scrollbar-width: none;
}
.dock-wrap::-webkit-scrollbar { display: none; }
}
main {
width: min(1180px, 90%);
margin: auto;
}
.page {
display: none;
min-height: calc(100vh - 80px);
padding: 3.5rem 0 6rem;
animation: pageIn .45s cubic-bezier(0.16, 1, 0.3, 1) both;
}
.page.active {
display: block;
}
@keyframes pageIn {
from { opacity: 0; transform: translateY(16px); }
to { opacity: 1; transform: translateY(0); }
}
.weather {
min-height: 290px;
overflow: hidden;
position: relative;
isolation: isolate;
border-radius: 24px;
transition: background 0.6s ease, border-color 0.6s ease;
}
.weather::after {
content: "";
position: absolute;
inset: 0;
z-index: 2;
pointer-events: none;
border-radius: inherit;
background: linear-gradient(115deg, rgba(255,255,255,0.16) 0%, rgba(255,255,255,0) 30%, rgba(255,255,255,0) 68%, rgba(255,255,255,0.07) 100%);
box-shadow: inset 0 1px 0 rgba(255,255,255,0.35), inset 0 -1px 0 rgba(255,255,255,0.08);
}
.weather-content-wrap {
position: relative;
z-index: 3;
padding: 1.5rem;
}
.glass-rain-pane {
position: absolute;
inset: 0;
z-index: 1;
)ECOSENSE";
static const char PG2[] PROGMEM = R"ECOSENSE(pointer-events: none;
overflow: hidden;
opacity: 0;
transition: opacity 0.7s cubic-bezier(0.16, 1, 0.3, 1);
}
.glass-rain-canvas {
position: absolute;
inset: 0;
width: 100%;
height: 100%;
display: block;
}
.weather.rainy {
background: linear-gradient(135deg, rgba(16, 42, 86, 0.68), rgba(28, 88, 140, 0.42));
border: 1px solid rgba(135, 206, 250, 0.4);
box-shadow: 0 20px 60px rgba(8, 28, 62, 0.35);
}
.weather.rainy .glass-rain-pane { opacity: 1; }
.rain-frosted-backing {
position: absolute;
inset: 0;
-webkit-backdrop-filter: blur(5px) saturate(140%);
backdrop-filter: blur(5px) saturate(140%);
background:
radial-gradient(circle at 40% 30%, rgba(255, 255, 255, 0.12), transparent 70%),
linear-gradient(180deg, rgba(255, 255, 255, 0.05), rgba(255, 255, 255, 0));
}
.weather.normal {
background: linear-gradient(135deg, rgba(255, 255, 255, 0.45), rgba(71, 181, 148, 0.22));
border: 1px solid rgba(71, 181, 148, 0.35);
}
.weather.normal .glass-rain-pane { opacity: 0.7; }
.weather.normal .rain-frosted-backing { -webkit-backdrop-filter: blur(9px); backdrop-filter: blur(9px); }
.weather.dry {
background: linear-gradient(135deg, rgba(245, 158, 11, 0.18), var(--glass));
border: 1px solid rgba(245, 158, 11, 0.35);
}
.weather.dry .glass-rain-pane { opacity: 0; }
#mapContainer {
width: 100%;
height: 560px;
border-radius: 18px;
border: 1px solid rgba(0, 0, 0, 0.12);
box-shadow: 0 10px 30px rgba(0, 0, 0, 0.08);
z-index: 1;
}
.glass {
background: var(--glass);
border: 1px solid var(--border);
border-radius: 24px;
padding: 1.4rem;
box-shadow: var(--shadow);
backdrop-filter: blur(24px);
-webkit-backdrop-filter: blur(24px);
}
.metric {
min-height: 145px;
display: flex;
flex-direction: column;
justify-content: space-between;
}
.metric small { display: block; color: var(--muted); font-weight: 500; }
.metric strong { display: block; font-size: 2.25rem; letter-spacing: -.06em; }
.unit { color: var(--muted); font-size: .85rem; }
.progress-track {
width: 100%; height: 7px;
background: rgba(120, 140, 180, 0.18);
border-radius: 999px; margin-top: .6rem; overflow: hidden;
}
.progress-bar {
height: 100%;
background: linear-gradient(90deg, var(--accent), var(--cyan));
border-radius: 999px;
transition: width .4s cubic-bezier(0.16, 1, 0.3, 1);
}
.status {
display: inline-flex; align-items: center; gap: .4rem;
margin-top: .6rem; color: var(--emerald); font-size: .85rem; font-weight: 600;
}
.status::before {
content: ""; width: 8px; height: 8px; border-radius: 50%;
background: currentColor; box-shadow: 0 0 10px currentColor;
}
.flow-diagram {
display: flex; align-items: center; justify-content: space-between;
gap: 0.5rem; margin-top: 1.5rem; flex-wrap: wrap;
}
.flow-step {
flex: 1; min-width: 120px; text-align: center;
padding: 1rem 0.6rem; background: rgba(140, 160, 220, 0.08);
border: 1px solid var(--border); border-radius: 16px;
}
.primary, .secondary, .small-btn {
padding: .85rem 1.3rem; border-radius: 999px;
text-decoration: none; transition: transform .2s ease, box-shadow .2s ease;
display: inline-flex; align-items: center; gap: .5rem; font-weight: 600;
}
.primary {
color: white; background: linear-gradient(135deg, var(--accent), #0284c7);
box-shadow: 0 12px 30px rgba(37, 99, 235, 0.3);
}
.secondary { color: var(--text); background: var(--glass); border: 1px solid var(--border); }
.small-btn { padding: .55rem .9rem; color: var(--text); border: 1px solid var(--border); background: var(--glass); font-size: .85rem; }
@media (max-width: 900px) {
.grid-4 { grid-template-columns: repeat(2, 1fr) !important; }
.two-col { grid-template-columns: 1fr !important; }
}
@media (max-width: 600px) {
.grid-4 { grid-template-columns: 1fr !important; }
}
@media (prefers-reduced-motion: reduce) {
.pl__ring { animation-duration: 6s; }
}

.app{display:grid;grid-template-columns:304px minmax(0,1fr);min-height:100vh}
.col{min-width:0;display:flex;flex-direction:column}
.nav{position:sticky;top:0;height:100vh;z-index:120;display:flex;flex-direction:column;gap:1.3rem;grid-template-columns:none;padding:1.3rem 1rem;background:var(--glass);border-right:1px solid var(--border);-webkit-backdrop-filter:blur(22px) saturate(160%);backdrop-filter:blur(22px) saturate(160%);pointer-events:auto;overflow-y:auto}
html[data-theme="dark"] .nav{isolation:isolate;background:rgba(8,15,27,.66);border-right-color:rgba(255,255,255,.18);-webkit-backdrop-filter:blur(34px) saturate(185%);backdrop-filter:blur(34px) saturate(185%);box-shadow:inset -1px 0 rgba(255,255,255,.08),18px 0 48px rgba(0,0,0,.24)}
html[data-theme="dark"] .nav::before{content:"";position:absolute;inset:0;z-index:0;pointer-events:none;background:linear-gradient(118deg,rgba(255,255,255,.075),rgba(255,255,255,.012) 30%,transparent 58%),radial-gradient(ellipse at 12% 0%,rgba(186,230,253,.07),transparent 45%);}
html[data-theme="dark"] .nav>*{position:relative;z-index:1}
.brand{display:flex;align-items:center;gap:.7rem;padding:.2rem .5rem;justify-self:auto}
.brand-mark{font-size:2rem;line-height:1;background:linear-gradient(130deg,#67e8f9,#3b82f6);-webkit-background-clip:text;background-clip:text;color:transparent}
.brand b{display:block;font-size:1.25rem;font-weight:800;letter-spacing:-.04em;line-height:1.1}
.brand small{display:block;margin-top:3px;font-size:.72rem;font-weight:600;letter-spacing:.02em;color:var(--muted)}
.dock-wrap{display:block;width:100%;padding:0;margin:0;overflow:visible}
.cir-tabs{display:flex;flex-direction:column;align-items:stretch;gap:4px;border-radius:1.5rem;margin:0;width:100%}
.cir-tabs__t{box-sizing:border-box;align-self:stretch;justify-content:flex-start;gap:.7rem;width:100%;height:44px;padding:0 16px}
)ECOSENSE";
static const char PG3[] PROGMEM = R"ECOSENSE(.cir-tabs__t .ti{width:1.1rem;text-align:center;opacity:.85}
.side-foot{margin-top:auto;display:grid;gap:.7rem}
.side-card{padding:.8rem .9rem;border:1px solid var(--border);border-radius:1rem;background:var(--glass-strong);font-size:.75rem;color:var(--muted);line-height:1.6}
.side-card strong{display:flex;align-items:center;gap:.45rem;color:var(--text);font-size:.82rem}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--muted);flex:none}
.dot.live{background:var(--emerald);box-shadow:0 0 10px var(--emerald)}
.dot.demo{background:var(--amber);box-shadow:0 0 10px var(--amber)}
.dot.off{background:var(--rose);box-shadow:0 0 10px var(--rose)}
.tagline{font-size:.75rem;font-style:italic;color:var(--muted);padding:0 .5rem}
.side-scrim{display:none}
.topbar{position:sticky;top:.6rem;z-index:110;display:flex;align-items:center;gap:.8rem;width:min(1180px,90%);margin:.9rem auto 0;padding:.5rem .7rem;border:1px solid var(--border);border-radius:1.1rem;background:var(--glass);-webkit-backdrop-filter:blur(20px);backdrop-filter:blur(20px);box-shadow:var(--shadow)}
.icon-btn{display:none;place-items:center;width:38px;height:38px;border-radius:50%;border:1px solid var(--border);background:var(--glass-strong);color:var(--text);font-size:1.1rem;cursor:pointer;flex:none}
.top-spacer{flex:1}
.search{position:relative;flex:1 1 340px;max-width:480px;min-width:0}
.search input{width:100%;height:38px;border-radius:999px;border:1px solid var(--border);background:var(--glass-strong);color:var(--text);padding:0 52px 0 38px;font:500 13px Inter,system-ui,sans-serif;outline:none}
.search input::placeholder{color:var(--muted)}
.search input:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(59,130,246,.25)}
.search .si{position:absolute;left:13px;top:50%;transform:translateY(-50%);color:var(--muted);pointer-events:none;font-size:14px}
.search kbd{position:absolute;right:12px;top:50%;transform:translateY(-50%);font:600 11px Inter,system-ui,sans-serif;color:var(--muted);border:1px solid var(--border);border-radius:6px;padding:1px 6px;pointer-events:none}
.search-list{position:absolute;top:46px;left:0;width:max(100%,380px);max-width:92vw;max-height:62vh;overflow:auto;display:none;padding:6px;border:1px solid var(--border);border-radius:14px;background:var(--glass-strong);-webkit-backdrop-filter:blur(24px);backdrop-filter:blur(24px);box-shadow:var(--shadow);z-index:300}
.search-list.open{display:block}
.sg-head{padding:8px 10px 3px;font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);font-weight:700}
.sg{display:flex;align-items:center;gap:.7rem;width:100%;padding:8px 10px;border:0;border-radius:10px;background:transparent;color:var(--text);text-align:left;cursor:pointer;font:inherit}
.sg[aria-selected=true],.sg:hover{background:rgba(59,130,246,.2)}
.sg .ic{width:1.4rem;text-align:center;flex:none}
.sg .t{font-size:13px;font-weight:600;display:block}
.sg .s{font-size:11px;color:var(--muted);display:block}
.sg .v{margin-left:auto;font-size:12px;font-weight:700;color:var(--cyan);white-space:nowrap}
.sg mark{background:rgba(251,191,36,.38);color:inherit;border-radius:3px}
.sg-empty{padding:14px;font-size:12px;color:var(--muted)}
.seg{display:inline-flex;padding:3px;border:1px solid var(--border);border-radius:999px;background:var(--glass-strong);flex:none}
.seg button{border:0;background:transparent;color:var(--muted);font:600 12px Inter,system-ui,sans-serif;padding:6px 14px;border-radius:999px;cursor:pointer}
.seg button[aria-checked=true]{background:var(--accent);color:#fff}
.chip{display:inline-flex;align-items:center;gap:.45rem;font-size:12px;font-weight:600;white-space:nowrap;color:var(--text)}
.clock{font-size:11px;line-height:1.35;text-align:center;color:var(--muted);white-space:nowrap;border-inline:1px solid var(--border);padding:0 .9rem}
.clock b{color:var(--text);font-size:12px}
.banner{display:flex;align-items:center;justify-content:space-between;gap:1rem;flex-wrap:wrap;width:min(1180px,90%);margin:.7rem auto 0;padding:.7rem 1rem;border-radius:1rem;font-size:.85rem;border:1px solid rgba(244,63,94,.4);background:rgba(244,63,94,.12)}
.banner[hidden]{display:none}
.status.off{color:var(--rose)}
.page{padding-top:2rem;scroll-margin-top:100px}
.flash{animation:flash 1.8s ease 1}
@keyframes flash{0%,100%{outline:0 solid transparent}25%,70%{outline:3px solid var(--accent);outline-offset:4px}}
@media (max-width:1280px){.grid-4{grid-template-columns:repeat(2,1fr)!important}}
@media (max-width:1100px){.dock-wrap{overflow:visible;padding:0;margin:0}}
@media (max-width:900px){
.app{display:block}
.nav{position:fixed;left:0;top:0;bottom:0;width:min(320px,88vw);transform:translateX(-105%);transition:transform .3s cubic-bezier(.16,1,.3,1);box-shadow:var(--shadow);background:var(--glass-strong)}
.nav.open{transform:none}
.side-scrim.show{display:block;position:fixed;inset:0;z-index:115;background:rgba(0,0,0,.45)}
.icon-btn{display:grid}
.clock,.search kbd{display:none}
.topbar{gap:.5rem;width:94%}
.banner{width:94%}
main{width:94%}
}
@media (max-width:600px){.chip span{display:none}.seg button{padding:6px 10px}}
/* theme toggle: inline in the top bar */
.theme-toggle{position:relative;top:auto;right:auto;flex:none;width:84px;height:42px}
.theme-toggle__orb{top:4px;left:5px;width:32px;height:32px}
.theme-toggle__sun,.theme-toggle__moon{width:22px;height:22px}
.theme-toggle__cloud{top:21px;left:36px;width:28px;height:9px}
.theme-toggle__cloud::before{top:-6px;left:3px;width:13px;height:13px}
.theme-toggle__cloud::after{top:-5px;left:15px;width:10px;height:10px}
.theme-toggle__stars i:nth-child(1){top:12px;left:14px}
.theme-toggle__stars i:nth-child(2){top:23px;left:24px}
.theme-toggle__stars i:nth-child(3){top:27px;left:11px}
.theme-toggle__stars i:nth-child(4){top:9px;left:27px}
html[data-theme="dark"] .theme-toggle__orb{transform:translateX(42px)}

/* light theme: darker text on light backgrounds */
[data-theme="light"]{--muted:#475467;--emerald:#047857;--cyan:#0e7490;--dock-text:#1f2937;--dock-text-hover:#0b1220}
[data-theme="light"] .text-gray-200,[data-theme="light"] .text-gray-300{color:#1f2937!important}
[data-theme="light"] .text-gray-400,[data-theme="light"] .text-gray-500{color:#475467!important}
[data-theme="light"] .text-emerald-400{color:#047857!important}
[data-theme="light"] .text-blue-400,[data-theme="light"] .text-blue-500{color:#1d4ed8!important}
[data-theme="light"] .drop-shadow-md{filter:none}
[data-theme="light"] .weather.normal .weather-content-wrap *,
[data-theme="light"] .weather.dry .weather-content-wrap *{color:#0f172a!important;border-color:rgba(15,23,42,.3)!important}
/* ===== EcoSense UI v2 ===== */
:root{
--neo-hi:rgba(255,255,255,.95);--neo-lo:rgba(150,166,196,.5);
--i-home:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3 11.5 12 4l9 7.5'/%3E%3Cpath d='M5.5 10v10h13V10'/%3E%3C/svg%3E");
--i-pin:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 21s7-6.2 7-11a7 7 0 0 0-14 0c0 4.8 7 11 7 11z'/%3E%3Ccircle cx='12' cy='10' r='2.5'/%3E%3C/svg%3E");
--i-cloud:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M7 18a4.5 4.5 0 0 1-.6-8.96A6 6 0 0 1 18 10a4 4 0 0 1 0 8z'/%3E%3C/svg%3E");
--i-bolt:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M13 2 4 14h7l-1 8 9-12h-7z'/%3E%3C/svg%3E");
--i-hist:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3 12a9 9 0 1 0 3-6.7L3 8'/%3E%3Cpath d='M3 3v5h5'/%3E%3Cpath d='M12 7v5l3 2'/%3E%3C/svg%3E");
--i-chip:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Crect x='6' y='6' width='12' height='12' rx='2'/%3E%3Cpath d='M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4'/%3E%3C/svg%3E");
--i-act:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M3 12h4l3-8 4 16 3-8h4'/%3E%3C/svg%3E");
--i-temp:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M14 14.8V5a2 2 0 0 0-4 0v9.8a4 4 0 1 0 4 0z'/%3E%3C/svg%3E");
--i-drop:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 3s6 6.3 6 10.5a6 6 0 0 1-12 0C6 9.3 12 3 12 3z'/%3E%3C/svg%3E");
}

/* ---- night mode: deeper base, lifted glass, readable text ---- */
html[data-theme="dark"]{--bg:#050a14;--text:#eaf1ff;--muted:#9db0cf;--glass:rgba(22,33,58,.58);--glass-strong:rgba(28,41,72,.86);--border:rgba(255,255,255,.1);--accent:#4f8dff;--neo-hi:rgba(255,255,255,.05);--neo-lo:rgba(0,0,0,.65);--shadow:0 20px 60px rgba(0,0,0,.55)}
html[data-theme="dark"] body{background:radial-gradient(60rem 36rem at 10% -8%,rgba(59,130,246,.22),transparent 60%),radial-gradient(48rem 32rem at 100% 8%,rgba(124,58,237,.18),transparent 60%),radial-gradient(40rem 28rem at 60% 110%,rgba(6,182,212,.14),transparent 60%),var(--bg);background-attachment:fixed}
html[data-theme="dark"] .glass{background:linear-gradient(145deg,rgba(255,255,255,.07),rgba(255,255,255,.015) 45%),var(--glass);box-shadow:inset 0 1px 0 rgba(255,255,255,.08),var(--shadow)}
html[data-theme="dark"] .topbar{background:rgba(16,25,46,.72);border-color:rgba(255,255,255,.1)}
html[data-theme="dark"] .search input,html[data-theme="dark"] .seg,html[data-theme="dark"] .icon-btn,html[data-theme="dark"] .small-btn,html[data-theme="dark"] .secondary{background:rgba(255,255,255,.07)}
html[data-theme="dark"] .flow-step{background:rgba(255,255,255,.05)}
html[data-theme="dark"] .metric strong{color:#fff}
html[data-theme="dark"] .text-gray-400,html[data-theme="dark"] .text-gray-500{color:#9db0cf!important}
html[data-theme="dark"] .leaflet-tile-pane{filter:invert(1) hue-rotate(180deg) brightness(.85) contrast(.9) saturate(.8)}
html[data-theme="dark"] #mapContainer{border-color:rgba(255,255,255,.12)}
.eyebrow{display:inline-flex;padding:.3rem .8rem;border-radius:999px;font-size:.78rem;font-weight:600;color:var(--cyan);background:rgba(56,189,248,.12);border:1px solid rgba(56,189,248,.28)}

/* ---- fluid layout for phone / tablet / laptop ---- */
body{min-height:100dvh}
main,.topbar,.banner{width:min(1180px,calc(100% - clamp(1.5rem,4vw,3rem)))}
.grid-4{grid-template-columns:repeat(auto-fit,minmax(min(100%,220px),1fr))!important}
#mapContainer{height:clamp(320px,60vh,560px)}
.topbar{border-radius:20px;flex-wrap:wrap}
.search{flex:1 1 180px}

/* ---- sidebar: floating glass panel, icon rows, grouped ---- */
.app{grid-template-columns:288px minmax(0,1fr)}
.nav{align-self:start;position:sticky;top:12px;height:calc(100dvh - 24px);margin:12px 0 12px 12px;padding:1rem .8rem;gap:.9rem;border:1px solid var(--border);border-radius:28px;background:linear-gradient(160deg,rgba(255,255,255,.62),rgba(255,255,255,.26));-webkit-backdrop-filter:blur(26px) saturate(170%);backdrop-filter:blur(26px) saturate(170%);box-shadow:inset 0 1px 1px rgba(255,255,255,.75),0 24px 60px -24px rgba(38,53,91,.35)}
html[data-theme="dark"] .nav{background:linear-gradient(160deg,rgba(64,90,140,.44),rgba(14,24,46,.66));border-color:rgba(255,255,255,.14);box-shadow:inset 0 1px 1px rgba(255,255,255,.14),0 28px 60px -20px rgba(0,0,0,.7)}
.brand{padding:.3rem .6rem}
.cir-tabs{background:none;border:0;box-shadow:none;-webkit-backdrop-filter:none;backdrop-filter:none;padding:0;gap:2px;border-radius:0}
.cir-tabs::before{display:none}
.cir-tabs__t{height:46px;padding:0 14px;gap:.85rem;border-radius:14px;font-size:14px;font-weight:600}
.cir-tabs__t:hover{background:rgba(120,150,210,.14)}
.cir-tabs__r:checked+.cir-tabs__t{background:rgba(37,99,235,.14);color:#1d4ed8;box-shadow:inset 0 1px 0 rgba(255,255,255,.6)}
html[data-theme="dark"] .cir-tabs__r:checked+.cir-tabs__t{background:linear-gradient(180deg,rgba(255,255,255,.22),rgba(255,255,255,.08));color:#fff;box-shadow:inset 0 1px 0 rgba(255,255,255,.28),0 8px 20px -10px rgba(0,0,0,.6)}
.cir-tabs__t[for="tab-environment"],.cir-tabs__t[for="tab-history"]{margin-top:14px}
.cir-tabs__t[for="tab-environment"]::after,.cir-tabs__t[for="tab-history"]::after{content:"";position:absolute;left:8px;right:8px;top:-8px;height:1px;background:var(--border)}
.cir-tabs__t .ti{display:block;flex:none;width:20px;height:20px;font-size:0;opacity:1;background:currentColor;-webkit-mask:var(--ic) center/contain no-repeat;mask:var(--ic) center/contain no-repeat}
.cir-tabs__t[for="tab-home"]{--ic:var(--i-home)}
.cir-tabs__t[for="tab-map"]{--ic:var(--i-pin)}
.cir-tabs__t[for="tab-environment"]{--ic:var(--i-cloud)}
.cir-tabs__t[for="tab-energy"]{--ic:var(--i-bolt)}
.cir-tabs__t[for="tab-history"]{--ic:var(--i-hist)}
.cir-tabs__t[for="tab-connect"]{--ic:var(--i-chip)}

/* System online card: full menu width, taller */
.side-foot{gap:.8rem}
.side-card{width:100%;min-height:118px;display:flex;flex-direction:column;gap:.5rem;padding:1.1rem 1.2rem;border-radius:20px;font-size:.78rem;background:linear-gradient(145deg,rgba(255,255,255,.6),rgba(255,255,255,.22));box-shadow:inset 0 1px 0 rgba(255,255,255,.7),6px 6px 16px var(--neo-lo),-6px -6px 16px var(--neo-hi)}
html[data-theme="dark"] .side-card{background:linear-gradient(145deg,rgba(255,255,255,.1),rgba(255,255,255,.03))}
.side-card strong{font-size:.95rem;padding-bottom:.6rem;border-bottom:1px solid var(--border)}
.side-card br{display:none}

/* ---- home ---- */
.h-hero{position:relative;overflow:hidden;display:grid;grid-template-columns:1.15fr .85fr;gap:clamp(1.2rem,3vw,2.5rem);align-items:center;padding:clamp(1.4rem,4vw,3rem);border-radius:32px}
.h-hero::before{content:"";position:absolute;right:-90px;top:-110px;width:360px;height:360px;border-radius:50%;background:radial-gradient(circle,rgba(56,189,248,.3),transparent 65%);pointer-events:none}
.h-hero>*{position:relative}
.h-hero h1{font-size:clamp(2.2rem,5.5vw,4.2rem);font-weight:800;letter-spacing:-.05em;line-height:1.02;margin:.9rem 0 .8rem}
.h-hero p{color:var(--muted);max-width:34rem;line-height:1.7}
.h-cta{display:flex;gap:.7rem;flex-wrap:wrap;margin-top:1.5rem}
.h-dial-wrap{display:grid;place-items:center}
.h-dial{position:relative;width:min(100%,290px);aspect-ratio:1;border-radius:50%;display:grid;place-items:center;background:linear-gradient(145deg,var(--glass-strong),var(--glass));box-shadow:14px 14px 34px var(--neo-lo),-14px -14px 34px var(--neo-hi),inset 0 1px 1px rgba(255,255,255,.35)}
.h-dial::before{content:"";position:absolute;inset:13%;border-radius:50%;box-shadow:inset 8px 8px 18px var(--neo-lo),inset -8px -8px 18px var(--neo-hi)}
.h-dial svg{position:absolute;inset:4%;width:92%;height:92%;transform:rotate(-90deg)}
#hRing{transition:stroke-dashoffset .6s ease}
.h-dial-c{position:relative;text-align:center}
.h-dial-c small{display:block;color:var(--muted);font-size:.78rem;font-weight:600}
.h-dial-c strong{display:block;font-size:clamp(2rem,6vw,2.7rem);letter-spacing:-.05em;line-height:1.1}
.h-dial-c i{font-style:normal;font-size:.9rem;color:var(--muted);margin-left:.15rem}
.h-tiles,.h-feats{display:grid;gap:1rem;margin-top:1.1rem}
.h-tiles{grid-template-columns:repeat(auto-fit,minmax(min(100%,200px),1fr))}
.h-feats{grid-template-columns:repeat(auto-fit,minmax(min(100%,260px),1fr))}
.h-tile,.h-feat{border:1px solid var(--border);border-radius:22px;background:var(--glass);-webkit-backdrop-filter:blur(16px);backdrop-filter:blur(16px);box-shadow:8px 8px 20px var(--neo-lo),-6px -6px 16px var(--neo-hi)}
.h-tile{display:flex;align-items:center;gap:.9rem;padding:1.05rem 1.15rem}
.h-ic{flex:none;width:46px;height:46px;border-radius:15px;display:grid;place-items:center;color:var(--cyan);box-shadow:inset 4px 4px 9px var(--neo-lo),inset -4px -4px 9px var(--neo-hi)}
.h-ic::before{content:"";width:22px;height:22px;background:currentColor;-webkit-mask:var(--ic) center/contain no-repeat;mask:var(--ic) center/contain no-repeat}
.i-act{--ic:var(--i-act)}.i-bolt{--ic:var(--i-bolt)}.i-temp{--ic:var(--i-temp)}.i-drop{--ic:var(--i-drop)}.i-cloud{--ic:var(--i-cloud)}.i-pin{--ic:var(--i-pin)}
.h-tile small{display:block;color:var(--muted);font-size:.76rem;font-weight:600}
.h-tile strong{font-size:1.55rem;letter-spacing:-.04em;line-height:1.1}
.h-tile em{font-style:normal;color:var(--muted);font-size:.78rem;margin-left:.2rem}
.h-feat{display:block;padding:1.4rem;color:var(--text);text-decoration:none;transition:transform .25s ease}
.h-feat:hover{transform:translateY(-3px)}
.h-feat h3{font-size:1.05rem;font-weight:700;margin:.9rem 0 .35rem}
.h-feat p{font-size:.86rem;color:var(--muted);line-height:1.6}

.search-list{position:fixed;z-index:400;top:0;left:0;width:380px;max-width:calc(100vw - 16px);padding:8px;border-radius:18px;border:1px solid rgba(255,255,255,.55);background:linear-gradient(145deg,rgba(255,255,255,.78),rgba(255,255,255,.58));-webkit-backdrop-filter:blur(30px) saturate(180%);backdrop-filter:blur(30px) saturate(180%);box-shadow:inset 0 1px 1px rgba(255,255,255,.8),0 24px 60px -18px rgba(38,53,91,.45)}
html[data-theme="dark"] .search-list{border-color:rgba(255,255,255,.16);background:linear-gradient(145deg,rgba(40,58,96,.7),rgba(14,22,42,.7));box-shadow:inset 0 1px 1px rgba(255,255,255,.14),0 24px 60px -18px rgba(0,0,0,.75)}
@supports not ((backdrop-filter:blur(1px)) or (-webkit-backdrop-filter:blur(1px))){.search-list{background:var(--glass-strong)}}
/* ---- responsive: laptop >1100 | tablet rail 769-1100 | phone drawer <=768 ---- */
@media (min-width:769px) and (max-width:1100px){
.app{display:grid;grid-template-columns:84px minmax(0,1fr)}
.nav{position:sticky;left:auto;bottom:auto;transform:none;width:auto;padding:1rem .6rem;align-items:center}
.icon-btn{display:none}
.brand>span:last-child,.tagline,.side-card strong span,.side-card>span{display:none}
.cir-tabs{align-items:center}
.cir-tabs__t{width:52px;padding:0;gap:0;justify-content:center;font-size:0}
.cir-tabs__t .ti{width:22px;height:22px}
.side-card{min-height:0;padding:.7rem;align-items:center}
.side-card strong{padding:0;border:0}
}
@media (max-width:768px){
.app{display:block}
.nav{position:fixed;left:10px;top:10px;bottom:10px;height:auto;margin:0;width:min(320px,86vw);z-index:130;transform:translateX(-115%);transition:transform .35s cubic-bezier(.16,1,.3,1)}
.nav.open{transform:none}
.icon-btn{display:grid}
}
@media (max-width:900px){
.h-hero{grid-template-columns:1fr}
.h-dial{width:min(70%,260px)}
}
</style>
</head>
<body>
<div class="app">
<div id="strandsOverlay" role="status" aria-live="polite" aria-label="Loading">
<div id="strandsContainer" class="strands-container"></div>
<div class="loader-holder">
<svg class="pl" viewBox="0 0 240 240" aria-hidden="true">
<circle class="pl__ring pl__ring--a" cx="120" cy="120" r="105" fill="none" stroke="#000" stroke-width="20" stroke-dasharray="0 660" stroke-dashoffset="-330" stroke-linecap="round"></circle>
<circle class="pl__ring pl__ring--b" cx="120" cy="120" r="35" fill="none" stroke="#000" stroke-width="20" stroke-dasharray="0 220" stroke-dashoffset="-110" stroke-linecap="round"></circle>
<circle class="pl__ring pl__ring--c" cx="85" cy="120" r="70" fill="none" stroke="#000" stroke-width="20" stroke-dasharray="0 440" stroke-linecap="round"></circle>
<circle class="pl__ring pl__ring--d" cx="155" cy="120" r="70" fill="none" stroke="#000" stroke-width="20" stroke-dasharray="0 440" stroke-linecap="round"></circle>
</svg>
</div>
</div>
<nav class="nav" id="mainNav" aria-label="Primary">
<a class="brand" href="#home"><span class="brand-mark">◈</span><span><b>EcoFusion</b><small>by Ω(1) Thinkers</small></span></a>
<div class="dock-wrap"><div class="cir-tabs" role="radiogroup" aria-label="Sections">
<input class="cir-tabs__r" type="radio" name="dock" id="tab-home" value="home" />
<label class="cir-tabs__t" for="tab-home"><span class="ti">⌂</span>Home</label>
<input class="cir-tabs__r" type="radio" name="dock" id="tab-map" value="map" />
<label class="cir-tabs__t" for="tab-map"><span class="ti">◎</span>Map Nodes</label>
<input class="cir-tabs__r" type="radio" name="dock" id="tab-environment" value="environment" />
)ECOSENSE";
static const char PG4[] PROGMEM = R"ECOSENSE(<label class="cir-tabs__t" for="tab-environment"><span class="ti">☁</span>Environment</label>
<input class="cir-tabs__r" type="radio" name="dock" id="tab-energy" value="energy" />
<label class="cir-tabs__t" for="tab-energy"><span class="ti">⚡</span>Energy Flow</label>
<input class="cir-tabs__r" type="radio" name="dock" id="tab-history" value="history" />
<label class="cir-tabs__t" for="tab-history"><span class="ti">↺</span>History</label>
<input class="cir-tabs__r" type="radio" name="dock" id="tab-connect" value="connect" />
<label class="cir-tabs__t" for="tab-connect"><span class="ti">⚙</span>Hardware</label>
</div></div>
<div class="side-foot">
<div class="side-card"><strong><i class="dot" id="sideDot"></i><span id="sideState">Checking device</span></strong><span id="sideNet">Connecting…</span><br><span id="sideUp">Uptime —</span></div>
<div class="tagline">“Power every step.”</div>
</div>
</nav>
<div class="side-scrim" id="sideScrim"></div>
<div class="col">
<header class="topbar">
<button class="icon-btn" id="menuBtn" type="button" aria-label="Open menu">☰</button>
<div class="search" role="search">
<span class="si" aria-hidden="true">⌕</span>
<input id="siteSearch" type="search" placeholder="Search sensors, data or pages…" aria-label="Search" role="combobox" aria-expanded="false" aria-controls="searchList" aria-autocomplete="list" autocomplete="off" spellcheck="false" />
<kbd>/</kbd>
<div class="search-list" id="searchList" role="listbox" aria-label="Search suggestions"></div>
</div>
<div class="top-spacer"></div>
<div class="seg" id="modeSeg" role="radiogroup" aria-label="Data source"><button type="button" role="radio" data-mode="live" aria-checked="true">Live</button><button type="button" role="radio" data-mode="demo" aria-checked="false">Demo</button></div>
<span class="chip"><i class="dot" id="connDot"></i><span id="connText">Checking…</span></span>
<div class="clock" id="clock"></div>
<div class="nav-end">
<label class="theme-toggle" aria-label="Change color theme">
<input id="theme" class="theme-toggle__input" type="checkbox" role="switch" name="theme" value="dark" aria-label="Dark mode" checked />
<span class="theme-toggle__sky" aria-hidden="true">
<span class="theme-toggle__cloud"></span>
<span class="theme-toggle__stars"><i></i><i></i><i></i><i></i></span>
</span>
<span class="theme-toggle__orb" aria-hidden="true">
<span class="theme-toggle__sun"></span>
<span class="theme-toggle__moon"></span>
</span>
</label>
</div>
</header>
<div class="banner" id="offlineBanner" hidden><span><strong>Arduino not reachable.</strong> Live values show “--” until the board responds.</span><button type="button" class="small-btn" id="bannerDemo">Show demo data</button></div>
<main>
<section class="page active" id="home">
<div class="system-alert flex items-center justify-between gap-2 flex-wrap p-3 rounded-2xl mb-5 bg-emerald-500/10 border border-emerald-500/30 text-xs">
<div class="flex items-center gap-2 flex-wrap">
<span class="status m-0" id="nodeStatus">Node Active</span>
<span>Station: <strong>22.444034, 88.373864</strong> · <span id="coreState">Core 1 Online</span> · Signal: <strong id="sigVal">-58 dBm</strong></span>
</div>
<div class="text-gray-400" id="streamLabel">Live telemetry stream</div>
</div>
<div class="glass h-hero">
<div>
<span class="eyebrow">Kinetic harvesting and weather sensing</span>
<h1>Power every step.</h1>
<p>Footsteps on the tile become electricity, and the same node tracks temperature, humidity and rain around it. Everything updates live from the Arduino.</p>
<div class="h-cta"><a class="primary" href="#energy">See energy flow</a><a class="secondary" href="#environment">See environment</a></div>
</div>
<div class="h-dial-wrap">
<div class="h-dial">
<svg viewBox="0 0 100 100" aria-hidden="true"><defs><linearGradient id="hg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#38bdf8"/><stop offset="1" stop-color="#3b82f6"/></linearGradient></defs><circle cx="50" cy="50" r="46" fill="none" stroke="rgba(120,140,180,.2)" stroke-width="3.5"/><circle id="hRing" cx="50" cy="50" r="46" fill="none" stroke="url(#hg)" stroke-width="3.5" stroke-linecap="round" stroke-dasharray="289" stroke-dashoffset="289"/></svg>
<div class="h-dial-c"><small>Peak voltage</small><strong><span id="hVolt">--</span><i>V</i></strong></div>
</div>
</div>
</div>
<div class="h-tiles">
<div class="h-tile"><span class="h-ic i-act"></span><div><small>Footsteps</small><strong id="hSteps">--</strong></div></div>
<div class="h-tile"><span class="h-ic i-bolt"></span><div><small>Live voltage</small><strong><span id="hLive">--</span><em>V</em></strong></div></div>
<div class="h-tile"><span class="h-ic i-temp"></span><div><small>Temperature</small><strong><span id="hTemp">--</span><em>°C</em></strong></div></div>
<div class="h-tile"><span class="h-ic i-drop"></span><div><small>Humidity</small><strong><span id="hHum">--</span><em>%</em></strong></div></div>
</div>
<div class="h-feats">
<a class="h-feat" href="#energy"><span class="h-ic i-bolt"></span><h3>Energy flow</h3><p>Live and peak voltage, footsteps and impact force from the piezo tile.</p></a>
<a class="h-feat" href="#environment"><span class="h-ic i-cloud"></span><h3>Environment</h3><p>Actual and feels-like temperature, humidity and rain level from the DHT11 and water sensor.</p></a>
<a class="h-feat" href="#map"><span class="h-ic i-pin"></span><h3>Node location</h3><p>See where the installation sits on the map.</p></a>
</div>
</section>
<section class="page" id="map">
<div class="mb-6">
<span class="eyebrow">Location</span>
<h2 class="text-3xl font-bold mt-2">Installation Map</h2>
<p class="text-sm text-gray-400">Click on the highlighted marker to view details.</p>
</div>
<article class="glass">
<div id="mapContainer"></div>
</article>
</section>
)ECOSENSE";
static const char PG5[] PROGMEM = R"ECOSENSE(<section class="page" id="environment">
<div class="mb-6">
<span class="eyebrow">Microclimate Analysis</span>
<h2 class="text-3xl font-bold mt-2">Environmental Conditions</h2>
<p class="text-sm text-gray-400">Surrounding atmospheric status surrounding the kinetic tile installation.</p>
</div>
<div class="grid grid-cols-4 gap-4 grid-4">
<article class="glass metric"><small>Temperature (Actual)</small><strong id="envTemp">--</strong><span class="unit">Celsius · measured by the DHT11</span></article>
<article class="glass metric"><small>Temperature (Feels Like)</small><strong id="heatIndex">29.1°</strong><span class="unit">Celsius · with humidity included</span></article>
<article class="glass metric"><small>Humidity · DHT11</small><strong id="envHum">--</strong><span class="unit">Relative humidity</span></article>
<article class="glass metric"><small>Atmospheric State</small><strong id="atmState">Active Rain</strong><span class="unit">Rain level above 50</span></article>
</div>
<div class="grid grid-cols-3 gap-4 grid-4 mt-4">
<article class="glass metric"><small>Water Sensor Analog</small><strong id="waterLevel">72</strong><span class="unit">Scaled 0 – 100</span></article>
<article class="glass metric"><small>Water Sensor Raw</small><strong id="waterRaw">--</strong><span class="unit">ADC counts [0 - 4095]</span></article>
<article class="glass metric"><small>Sensor Status</small><strong id="sensorStatus" style="font-size:1.4rem">--</strong><span class="unit" id="sensorDetail">Waiting…</span></article>
</div>
<br />
<article class="glass weather rainy" id="environmentWeather">
<div class="glass-rain-pane">
<div class="rain-frosted-backing"></div>
</div>
<div class="weather-content-wrap">
<small class="text-white/80">Physical Sensor Mapping</small>
<h3 class="text-white text-2xl font-bold mt-1">Dynamic Moisture Plane</h3>
<p class="text-white/90 text-sm mt-2">
High-fidelity glass refraction mimics rain and atmospheric condensation beads in real-time.
</p>
</div>
</article>
</section>
<section class="page" id="energy">
<div class="mb-6">
<span class="eyebrow">Piezoelectric Harvester Circuit</span>
<h2 class="text-3xl font-bold mt-2">Energy Flow & Circuit Analysis</h2>
<p class="text-sm text-gray-400">Electrical telemetry across bridge rectification and voltage regulation stages.</p>
</div>
<div class="grid grid-cols-3 gap-4 grid-4">
<article class="glass metric">
<small>Live Voltage</small>
<strong id="liveMetric">--</strong>
<span class="unit">Volts · right now</span>
</article>
<article class="glass metric">
<small>Peak Voltage</small>
<strong id="voltMetric">18.6</strong>
<span class="unit">Volts · highest in the last 1 s</span>
</article>
<article class="glass metric">
<small>Highest Peak</small>
<strong id="maxMetric">--</strong>
<span class="unit">Volts · since the board started</span>
</article>
<article class="glass metric">
<small>Footsteps</small>
<strong id="stepsMetric">--</strong>
<span class="unit">Counted since the board started</span>
</article>
<article class="glass metric">
<small>Last Footstep Spike</small>
<strong id="spikeMetric">--</strong>
<span class="unit" id="spikeInfo">Volts · waiting for a step</span>
</article>
<article class="glass metric">
<small>Dynamic Impact Force</small>
<strong id="impactMetric">780</strong>
<span class="unit">Newtons (ADC peak)</span>
</article>
</div>
<br />
<article class="glass">
<h3 class="font-bold text-lg">Live Voltage</h3>
<p class="text-xs text-gray-400 mb-3">Last 60 readings from pin A0 · solid = live, dashed = peak</p>
<div style="position:relative;height:240px"><canvas id="liveChart"></canvas></div>
</article>
<br />
<article class="glass">
<small class="text-gray-400">Circuit Topology Pipeline</small>
<div class="flow-diagram">
<div class="flow-step"><strong>1. Piezo Element</strong><span class="unit">PZT Tile</span></div>
<div class="text-blue-500 font-bold">&rarr;</div>
<div class="flow-step"><strong>2. Bridge Rectifier</strong><span class="unit">Schottky 1N5819</span></div>
<div class="text-blue-500 font-bold">&rarr;</div>
<div class="flow-step"><strong>3. Buck Regulator</strong><span class="unit">3.3V System Bus</span></div>
</div>
</article>
<br />
<article class="glass">
<h3 class="font-bold text-lg">Harvest Dynamic Output</h3>
<canvas id="energyChart"></canvas>
</article>
</section>
<section class="page" id="history">
<div class="mb-6">
<span class="eyebrow">Audited Logs</span>
<h2 class="text-3xl font-bold mt-2">Telemetry History</h2>
<p class="text-sm text-gray-400">Export or review collected physical telemetry records.</p>
</div>
<article class="glass">
<div class="flex justify-between items-center mb-6">
<h3 class="font-bold text-lg">Recorded Sessions</h3>
<button class="small-btn" onclick="exportDataToCSV()">📥 Download Log (.CSV)</button>
</div>
<div class="space-y-3 text-sm" id="histRows">
<p><strong>Today</strong> · 1,284 footsteps · 18.6 V peak voltage</p>
<p><strong>Yesterday</strong> · 946 footsteps · 17.2 V peak voltage</p>
<p><strong>May 22</strong> · 1,103 footsteps · 16.4 V peak voltage</p>
</div>
</article>
</section>
<section class="page" id="connect">
<div class="mb-6">
<span class="eyebrow">Gateway Setup</span>
<h2 class="text-3xl font-bold mt-2">Hardware Pairing</h2>
<p class="text-sm text-gray-400">Provision the Arduino UNO R4 WiFi node over Wi-Fi.</p>
</div>
<article class="glass space-y-4">
<div class="flex justify-between items-center py-2 border-b border-white/10">
<div>
<h3 class="font-semibold">Wi-Fi REST Gateway</h3>
<p class="text-xs text-gray-400">The Arduino serves live JSON telemetry over HTTP/REST (/api/data).</p>
</div>
<button class="small-btn" onclick="inspectGateway()">Inspect Status</button>
</div>
)ECOSENSE";
static const char PG6[] PROGMEM = R"ECOSENSE(<p id="connectionMessage" class="text-xs text-blue-400 font-medium"></p>
</article>
</section>
</main>
<footer class="text-center py-8 text-xs text-gray-500">
© 2026 Ω (1) Thinkers · Smart energy and environmental monitoring platform
</footer>
</div>
</div>
<script type="module">
import { Renderer as OGLRenderer, Program as OGLProgram, Mesh as OGLMesh, Triangle as OGLTriangle, Color as OGLColor } from 'ogl';
const MAX_STRANDS = 12;
const MAX_COLORS = 8;
const STRANDS_VERT = `#version 300 es
in vec2 position;
void main() { gl_Position = vec4(position, 0.0, 1.0); }`;
const STRANDS_FRAG = `#version 300 es
)ECOSENSE";
static const char PG7[] PROGMEM = R"ECOSENSE(precision highp float;
uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uColors[${MAX_COLORS}];
uniform int uColorCount;
uniform int uStrandCount;
uniform float uSpeed;
uniform float uAmplitude;
uniform float uWaviness;
uniform float uThickness;
uniform float uGlow;
uniform float uTaper;
uniform float uSpread;
uniform float uHueShift;
uniform float uIntensity;
uniform float uOpacity;
uniform float uScale;
uniform float uSaturation;
out vec4 fragColor;
const float PI = 3.14159265;
vec3 samplePalette(float t) {
t = fract(t);
float scaled = t * float(uColorCount);
int idx = int(floor(scaled));
float blend = fract(scaled);
int nextIdx = idx + 1;
if (nextIdx >= uColorCount) nextIdx = 0;
return mix(uColors[idx], uColors[nextIdx], blend);
}
void main() {
vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
uv /= max(uScale, 0.0001);
float e = 0.06 + uIntensity * 0.94;
float env = pow(max(cos(uv.x * PI * 1.3), 0.0), uTaper);
vec3 col = vec3(0.0);
for (int i = 0; i < ${MAX_STRANDS}; i++) {
if (i >= uStrandCount) break;
float fi = float(i);
float ph = fi * 1.7 * uSpread;
float freq = (2.0 + fi * 0.35) * uWaviness;
float spd = 1.4 + fi * 1.2;
float tt = uTime * uSpeed;
float w = sin(uv.x * freq + tt * spd + ph) * 0.60 + sin(uv.x * freq * 1.1 - tt * spd * 0.7 + ph * 1.7) * 0.40;
float amp = (0.1 + 0.02 * e) * env * uAmplitude;
float y = w * amp;
float d = abs(uv.y - y);
float thick = (0.001 + 0.05 * e) * (0.35 + env) * uThickness;
float g = thick / (d + thick * 0.45);
g = g * g;
float h = fi / float(uStrandCount) + uv.x * 0.30 + uTime * 0.04 + uHueShift;
col += samplePalette(h) * g * env;
}
col *= 0.45 + 0.7 * e;
col = 1.0 - exp(-col * uGlow);
float gray = dot(col, vec3(0.2126, 0.7152, 0.0722));
col = max(mix(vec3(gray), col, uSaturation), 0.0);
float lum = max(max(col.r, col.g), col.b);
fragColor = vec4(col * uOpacity, clamp(lum, 0.0, 1.0) * uOpacity);
}`;
const buildPalette = colors => {
const filled = colors && colors.length ? colors : ['#ffffff'];
const padded = [];
for (let i = 0; i < MAX_COLORS; i++) {
const hex = filled[i] ?? filled[filled.length - 1];
const c = new OGLColor(hex);
padded.push([c.r, c.g, c.b]);
}
return padded;
};
const strandsTarget = document.getElementById('strandsContainer');
if (strandsTarget) {
const strandsRenderer = new OGLRenderer({ alpha: true, premultipliedAlpha: true, antialias: true });
const gl = strandsRenderer.gl;
gl.clearColor(0, 0, 0, 0);
gl.enable(gl.BLEND);
gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
strandsTarget.appendChild(gl.canvas);
const geometry = new OGLTriangle(gl);
if (geometry.attributes.uv) delete geometry.attributes.uv;
const strandsProgram = new OGLProgram(gl, {
vertex: STRANDS_VERT,
fragment: STRANDS_FRAG,
uniforms: {
uTime: { value: 0 },
uResolution: { value: [window.innerWidth, window.innerHeight] },
uColors: { value: buildPalette(["#06B6D4", "#3B82F6", "#7C3AED", "#38BDF8"]) },
uColorCount: { value: 4 },
uStrandCount: { value: 4 },
uSpeed: { value: 0.6 },
uAmplitude: { value: 1.0 },
uWaviness: { value: 1.0 },
uThickness: { value: 0.7 },
uGlow: { value: 2.6 },
uTaper: { value: 3.0 },
uSpread: { value: 1.0 },
uHueShift: { value: 0.0 },
uIntensity: { value: 0.7 },
uOpacity: { value: 1.0 },
uScale: { value: 1.5 },
uSaturation: { value: 1.5 }
}
});
const strandsMesh = new OGLMesh(gl, { geometry, program: strandsProgram });
function resizeStrands() {
strandsRenderer.setSize(window.innerWidth, window.innerHeight);
strandsProgram.uniforms.uResolution.value = [window.innerWidth, window.innerHeight];
}
window.addEventListener('resize', resizeStrands);
resizeStrands();
function updateStrands(t) {
requestAnimationFrame(updateStrands);
strandsProgram.uniforms.uTime.value = t * 0.001;
strandsRenderer.render({ scene: strandsMesh });
}
requestAnimationFrame(updateStrands);
}
</script>
<script>
const TAU = Math.PI * 2;
const rand = (a, b) => a + Math.random() * (b - a);
function drawDrop(ctx, x, y, r, stretch, alpha) {
const rx = r, ry = r * stretch;
ctx.save();
ctx.translate(x, y);
ctx.globalAlpha = alpha;
if (r <= 2.2) {
const g = ctx.createRadialGradient(-rx * 0.3, -ry * 0.3, 0, 0, 0, Math.max(rx, ry) * 1.1);
g.addColorStop(0, 'rgba(255,255,255,0.55)');
g.addColorStop(0.5, 'rgba(190,225,255,0.16)');
g.addColorStop(1, 'rgba(6,20,46,0.34)');
ctx.fillStyle = g;
ctx.beginPath();
ctx.ellipse(0, 0, rx, ry, 0, 0, TAU);
ctx.fill();
ctx.restore();
return;
}
ctx.save();
ctx.beginPath();
ctx.rect(-rx * 3, -ry * 3, rx * 6, ry * 6);
ctx.moveTo(rx, 0);
ctx.ellipse(0, 0, rx, ry, 0, 0, TAU);
ctx.clip('evenodd');
const sh = ctx.createRadialGradient(0, ry * 0.35, 0, 0, ry * 0.35, ry * 1.7);
sh.addColorStop(0, 'rgba(0,12,32,0.34)');
sh.addColorStop(0.55, 'rgba(0,12,32,0.12)');
sh.addColorStop(1, 'rgba(0,12,32,0)');
ctx.fillStyle = sh;
ctx.fillRect(-rx * 3, -ry * 3, rx * 6, ry * 6);
ctx.restore();
ctx.save();
ctx.beginPath();
ctx.ellipse(0, 0, rx, ry, 0, 0, TAU);
ctx.clip();
const body = ctx.createLinearGradient(0, -ry, 0, ry);
body.addColorStop(0, 'rgba(6,22,50,0.44)');
body.addColorStop(0.45, 'rgba(120,170,220,0.07)');
body.addColorStop(1, 'rgba(226,243,255,0.40)');
ctx.fillStyle = body;
ctx.fillRect(-rx, -ry, rx * 2, ry * 2);
const rim = ctx.createRadialGradient(0, 0, Math.min(rx, ry) * 0.55, 0, 0, Math.max(rx, ry));
rim.addColorStop(0, 'rgba(255,255,255,0)');
rim.addColorStop(1, 'rgba(255,255,255,0.24)');
ctx.fillStyle = rim;
ctx.fillRect(-rx, -ry, rx * 2, ry * 2);
const caustic = ctx.createRadialGradient(rx * 0.1, ry * 0.55, 0, rx * 0.1, ry * 0.55, rx * 0.8);
caustic.addColorStop(0, 'rgba(255,255,255,0.5)');
caustic.addColorStop(1, 'rgba(255,255,255,0)');
ctx.fillStyle = caustic;
ctx.fillRect(-rx, -ry, rx * 2, ry * 2);
ctx.restore();
ctx.beginPath();
ctx.ellipse(0, 0, rx, ry, 0, 0, TAU);
ctx.strokeStyle = 'rgba(255,255,255,0.3)';
ctx.lineWidth = 0.8;
ctx.stroke();
ctx.fillStyle = 'rgba(255,255,255,0.92)';
ctx.beginPath();
ctx.ellipse(-rx * 0.36, -ry * 0.42, rx * 0.26, ry * 0.15, -0.6, 0, TAU);
ctx.fill();
ctx.fillStyle = 'rgba(255,255,255,0.5)';
ctx.beginPath();
ctx.ellipse(rx * 0.34, ry * 0.5, rx * 0.11, ry * 0.07, -0.6, 0, TAU);
ctx.fill();
ctx.restore();
}
const RAIN_CFG = {
rainy:  { rate: 7,   max: 60, speed: 1 },
normal: { rate: 1.6, max: 20, speed: 0.55 },
dry:    { rate: 0,   max: 0,  speed: 0.8 }
};
const TRAIL_LIFE = 6500;
class GlassRain {
constructor(pane, card) {
this.pane = pane;
this.card = card;
this.mist = document.createElement('canvas');
this.main = document.createElement('canvas');
this.mist.className = this.main.className = 'glass-rain-canvas';
pane.append(this.mist, this.main);
this.mctx = this.mist.getContext('2d');
this.ctx = this.main.getContext('2d');
this.drops = [];
this.trails = [];
this.spawnAcc = 0;
this.w = 0;
this.h = 0;
this.dpr = 1;
this.last = performance.now();
this.idle = false;
this.resize();
if (typeof ResizeObserver !== 'undefined') {
new ResizeObserver(() => this.resize()).observe(pane);
}
if (this.w) for (let i = 0; i < 18; i++) this.spawn();
)ECOSENSE";
static const char PG8[] PROGMEM = R"ECOSENSE(requestAnimationFrame(t => this.frame(t));
}
resize() {
const w = this.pane.clientWidth;
const h = this.pane.clientHeight;
if (!w || !h) { this.w = 0; return; }
if (w === this.w && h === this.h) return;
const first = this.w === 0 && this.drops.length === 0;
this.w = w;
this.h = h;
this.dpr = Math.min(window.devicePixelRatio || 1, 2);
for (const c of [this.mist, this.main]) {
c.width = Math.round(w * this.dpr);
c.height = Math.round(h * this.dpr);
}
this.paintMist();
if (first) for (let i = 0; i < 18; i++) this.spawn();
}
paintMist() {
const c = this.mctx;
c.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
c.clearRect(0, 0, this.w, this.h);
const n = Math.round((this.w * this.h) / 520);
for (let i = 0; i < n; i++) {
const r = Math.pow(Math.random(), 2.2) * 2.2 + 0.55;
drawDrop(c, rand(0, this.w), rand(0, this.h), r, 1.08, rand(0.55, 1));
}
const m = Math.round((this.w * this.h) / 9000);
for (let i = 0; i < m; i++) {
drawDrop(c, rand(0, this.w), rand(0, this.h), rand(2.6, 4.6), rand(1, 1.15), rand(0.7, 1));
}
}
spawn() {
const big = Math.random() < 0.12;
const r = big ? rand(5, 7) : rand(2.2, 6.2);
this.drops.push({
x: rand(6, this.w - 6),
y: rand(-4, this.h * 0.85),
r,
crit: Math.min(9.5, r + rand(1.0, 4.5)),
g: rand(0.5, 1.2),
moving: false,
v: 0, t: 0, om: rand(1.2, 3), phase: rand(0, TAU),
trail: null
});
}
frame(now) {
requestAnimationFrame(t => this.frame(t));
const dt = Math.min(0.05, (now - this.last) / 1000);
this.last = now;
if (!this.w) return;
const cl = this.card.classList;
const state = cl.contains('rainy') ? 'rainy' : cl.contains('normal') ? 'normal' : 'dry';
const cfg = RAIN_CFG[state];
if (cfg.rate === 0 && !this.drops.length && !this.trails.length) {
if (!this.idle) { this.ctx.setTransform(1, 0, 0, 1, 0, 0); this.ctx.clearRect(0, 0, this.main.width, this.main.height); this.idle = true; }
return;
}
this.idle = false;
this.spawnAcc += dt * cfg.rate;
while (this.spawnAcc >= 1) {
this.spawnAcc -= 1;
if (this.drops.length < cfg.max) this.spawn();
}
for (const d of this.drops) {
if (!d.moving) {
d.r += d.g * dt * (0.6 + cfg.speed * 0.4);
if (d.r >= d.crit) {
d.moving = true;
d.trail = { pts: [[d.x, d.y, d.r]], beads: [], end: null };
this.trails.push(d.trail);
}
} else {
d.t += dt;
const vmax = (26 + d.r * 14) * cfg.speed;
const sf = Math.max(0, Math.sin(d.t * d.om + d.phase) * 1.25 - 0.15);
d.v += (vmax * sf - d.v) * Math.min(1, dt * 4);
const dy = d.v * dt;
d.y += dy;
d.x += Math.sin(d.t * 3 + d.phase) * dy * 0.05;
d.r -= dy * 0.0075;
const last = d.trail.pts[d.trail.pts.length - 1];
if (d.y - last[1] > 5) d.trail.pts.push([d.x, d.y, d.r]);
if (d.v > 10 && Math.random() < dt * 3) {
d.trail.beads.push({ x: d.x + rand(-1.5, 1.5), y: d.y - d.r * 1.2, r: rand(0.8, 1.7) });
}
if (d.r < 1.5 || d.y - d.r > this.h + 10) {
d.dead = true;
d.trail.end = now;
}
}
}
this.drops = this.drops.filter(d => !d.dead);
const ctx = this.ctx;
ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
ctx.clearRect(0, 0, this.w, this.h);
ctx.lineCap = 'butt';   // butt caps: overlapping round caps make low-alpha trails look beaded
ctx.lineJoin = 'round';
this.trails = this.trails.filter(tr => tr.end === null || now - tr.end < TRAIL_LIFE);
for (const tr of this.trails) {
const a = tr.end === null ? 1 : Math.pow(Math.max(0, 1 - (now - tr.end) / TRAIL_LIFE), 1.5);
const p = tr.pts;
if (p.length > 1) {
ctx.strokeStyle = `rgba(0,18,42,${(0.11 * a).toFixed(3)})`;
for (let i = 1; i < p.length; i++) {
ctx.lineWidth = p[i][2] * 0.55 + 1.4;
ctx.beginPath(); ctx.moveTo(p[i - 1][0], p[i - 1][1]); ctx.lineTo(p[i][0], p[i][1]); ctx.stroke();
}
ctx.strokeStyle = `rgba(215,238,255,${(0.18 * a).toFixed(3)})`;
for (let i = 1; i < p.length; i++) {
ctx.lineWidth = p[i][2] * 0.55;
ctx.beginPath(); ctx.moveTo(p[i - 1][0], p[i - 1][1]); ctx.lineTo(p[i][0], p[i][1]); ctx.stroke();
}
}
for (const b of tr.beads) drawDrop(ctx, b.x, b.y, b.r, 1.1, 0.9 * a);
}
for (const d of this.drops) {
const stretch = d.moving ? 1.18 + Math.min(0.35, d.v / 160) : 1.05;
drawDrop(ctx, d.x, d.y, d.r, stretch, 1);
}
}
}
document.querySelectorAll(".weather").forEach(card => {
const pane = card.querySelector(".glass-rain-pane");
if (pane) new GlassRain(pane, card);
});
</script>
<script>
let stepCount = 1284;
let powerWatts = 12.8;
let energyChart;
let mapInstance;
let transitioning = false;
const ACTIVE_NODE = { lat: 22.444034, lng: 88.373864, name: "Node #01" };
function triggerPageLoadingTransition(actionCallback) {
const overlay = document.getElementById("strandsOverlay");
transitioning = true;
overlay.classList.add("active");
setTimeout(() => {
if (actionCallback) actionCallback();
setTimeout(() => {
overlay.classList.remove("active");
transitioning = false;
}, 350);
}, 700);
}
function goTo(target) {
if (location.hash === target) return;
if (transitioning) { location.hash = target; return; }
triggerPageLoadingTransition(() => { location.hash = target; });
}
function showRoute() {
const requested = location.hash.replace("#", "") || "home";
const route = document.querySelector(`.page#${CSS.escape(requested)}`) ? requested : "home";
document.querySelectorAll(".page").forEach(page => {
page.classList.toggle("active", page.id === route);
});
document.querySelectorAll(".cir-tabs__r").forEach(radio => {
radio.checked = radio.value === route;
});
if (route === "energy") {
setTimeout(renderChart, 120);
} else if (route === "map") {
setTimeout(initMap, 150);
}
}
document.querySelectorAll(".cir-tabs__r").forEach(radio => {
radio.addEventListener("change", () => goTo("#" + radio.value));
});
document.addEventListener("click", e => {
const link = e.target.closest('a[href^="#"]');
if (!link) return;
const target = link.getAttribute("href");
if (target.length < 2) return;
e.preventDefault();
goTo(target);
});
window.addEventListener("hashchange", showRoute);
showRoute();
function initMap() {
if (mapInstance) {
mapInstance.invalidateSize();
return;
}
mapInstance = L.map('mapContainer').setView([ACTIVE_NODE.lat, ACTIVE_NODE.lng], 16);
L.tileLayer('https://mt1.google.com/vt/lyrs=m&x={x}&y={y}&z={z}', {
maxZoom: 20,
subdomains: ['mt0', 'mt1', 'mt2', 'mt3']
}).addTo(mapInstance);
const redGoogleIcon = L.icon({
iconUrl: 'https://maps.google.com/mapfiles/ms/icons/red-dot.png',
)ECOSENSE";
static const char PG9[] PROGMEM = R"ECOSENSE(iconSize: [32, 32],
iconAnchor: [16, 32],
popupAnchor: [0, -28]
});
const marker = L.marker([ACTIVE_NODE.lat, ACTIVE_NODE.lng], { icon: redGoogleIcon }).addTo(mapInstance);
marker.bindPopup(`
<div style="font-family: inherit; font-size: 0.9rem; text-align: center; padding: 4px;">
<strong style="color: #10b981;">Available</strong><br>
<span>Active Node</span>
</div>
`).openPopup();
mapInstance.on('click', e => {
const dist = mapInstance.distance(e.latlng, [ACTIVE_NODE.lat, ACTIVE_NODE.lng]);
if (dist > 35) {
L.popup()
.setLatLng(e.latlng)
.setContent(`<div style="font-family: inherit; font-size: 0.85rem; text-align: center; color: #f43f5e; font-weight: 600; padding: 2px;">Not Available</div>`)
.openOn(mapInstance);
}
});
}
function setWeather(state) {
const card = document.getElementById("environmentWeather");
if (!card) return;
card.classList.remove("rainy", "normal", "dry");
card.classList.add(state);
}
function exportDataToCSV() {
const records = [
["Timestamp", "Steps", "Power_Watts", "Temp_C", "Humidity_Pct", "Lat", "Lng"],
[new Date().toISOString(), stepCount, powerWatts, 28.4, 64, ACTIVE_NODE.lat, ACTIVE_NODE.lng]
];
const csvContent = "data:text/csv;charset=utf-8," + records.map(r => r.join(",")).join("\n");
const link = document.createElement("a");
link.setAttribute("href", encodeURI(csvContent));
link.setAttribute("download", `telemetry_${Date.now()}.csv`);
document.body.appendChild(link);
link.click();
document.body.removeChild(link);
}
const themeToggle = document.getElementById("theme");
themeToggle.checked = document.documentElement.getAttribute("data-theme") === "dark";
themeToggle.addEventListener("change", () => {
document.documentElement.setAttribute("data-theme", themeToggle.checked ? "dark" : "light");
if (energyChart) renderChart();
});
function renderChart() {
const canvas = document.getElementById("energyChart");
if (!canvas) return;
if (energyChart) energyChart.destroy();
const textColor = getComputedStyle(document.body).getPropertyValue("--text").trim();
energyChart = new Chart(canvas, {
type: "line",
data: {
labels: ["12:00", "13:00", "14:00", "15:00", "16:00", "17:00"],
datasets: [
{
label: "Harvested Power (W)",
data: [4.2, 6.8, 12.8, 8.4, 10.1, 12.8],
borderColor: "#3b82f6",
backgroundColor: "rgba(59,130,246,.15)",
fill: true,
tension: 0.35,
yAxisID: 'y'
},
{
label: "Step Count Accumulation",
data: [620, 800, 940, 1080, 1190, 1284],
borderColor: "#38bdf8",
backgroundColor: "transparent",
borderDash: [5, 5],
tension: 0.35,
yAxisID: 'steps'
}
]
},
options: {
responsive: true,
plugins: { legend: { labels: { color: textColor } } },
scales: {
x: { ticks: { color: "#8792a8" }, grid: { color: "rgba(130,145,180,.1)" } },
y: { type: 'linear', position: 'left', ticks: { color: "#8792a8" }, grid: { color: "rgba(130,145,180,.1)" } },
steps: { type: 'linear', position: 'right', grid: { drawOnChartArea: false }, ticks: { color: "#8792a8" } }
}
}
});
}
</script>
)ECOSENSE";
static const char PG16[] PROGMEM = R"ECOSENSE(<script>
(function () {
"use strict";
var $ = function (id) { return document.getElementById(id); };
var store = {
get: function (k, d) { try { return localStorage.getItem(k) || d; } catch (e) { return d; } },
set: function (k, v) { try { localStorage.setItem(k, v); } catch (e) {} }
};
var set = function (id, t) { var e = $(id); if (e) e.textContent = t; };
var width = function (id, p) { var e = $(id); if (e) e.style.width = Math.max(0, Math.min(100, p || 0)) + "%"; };
var num = function (v, d) { return (v == null || isNaN(v)) ? "--" : Number(v).toFixed(d); };
var esc = function (s) { return String(s).replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; }); };
var MODE = store.get("eco-mode", "live") === "demo" ? "demo" : "live";
var online = false, fails = 0, lastSteps = null, hist = null, lastWeather = null, current = null;
var demo = { tempC: 28.4, humidity: 64, powerW: 12.8, piezoV: 18.6, liveV: 9.4, maxV: 18.6, currentA: 0.69, forceN: 780, storageV: 4.82, water: 0.72, steps: 1284, energyWh: null, maxPowerW: 12.8, rssi: -58, uptimeSeconds: 0, ip: "demo", mode: "demo" };
var demoTimer = null, demoStart = Date.now();
function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }
function demoTick() {
if (MODE !== "demo") return;
demo.tempC = +clamp(demo.tempC + (Math.random() - .5) * .4, 26.5, 31).toFixed(1);
demo.humidity = Math.round(clamp(demo.humidity + (Math.random() - .5) * 2, 58, 72));
demo.water = +clamp(demo.water + (Math.random() - .5) * .04, .55, .9).toFixed(2);
demo.piezoV = +clamp(demo.piezoV + (Math.random() - .5) * 1.2, 15, 21).toFixed(1);
demo.liveV = +clamp(demo.piezoV * (0.3 + Math.random() * 0.7), 0, 21).toFixed(2);
demo.maxV = Math.max(demo.maxV, demo.piezoV);
demo.currentA = +(0.69 * demo.piezoV / 18.6).toFixed(2);
demo.forceN = Math.round(clamp(demo.forceN + (Math.random() - .5) * 60, 600, 900));
if (Math.random() < .35) { demo.steps++; demo.powerW = +(11 + Math.random() * 6).toFixed(1); demo.storageV = +Math.min(5.45, demo.storageV + .01).toFixed(2); }
demo.uptimeSeconds = Math.floor((Date.now() - demoStart) / 1000);
render(demo);
}
function heatIndex(c, rh) {
if (c == null || rh == null) return null;
var f = c * 9 / 5 + 32, hi = 0.5 * (f + 61 + (f - 68) * 1.2 + rh * 0.094);
if (hi >= 80) hi = -42.379 + 2.04901523 * f + 10.14333127 * rh - .22475541 * f * rh - .00683783 * f * f - .05481717 * rh * rh + .00122874 * f * f * rh + .00085282 * f * rh * rh - .00000199 * f * f * rh * rh;
return (hi - 32) * 5 / 9;
}
function render(d) {
current = d;
var hi = heatIndex(d.tempC, d.humidity);
set("heatIndex", hi == null ? "--" : num(hi, 1) + "°");
set("waterLevel", d.water == null ? "--" : Math.round(d.water * 100));
set("envTemp", d.tempC == null ? "--" : num(d.tempC, 1) + "°");
set("envHum", d.humidity == null ? "--" : Math.round(d.humidity) + "%");
var dOk = d.mode === "demo" ? true : d.dhtOk;
var wRaw = d.mode === "demo" ? Math.round((d.water || 0) * 3000) : d.waterRaw;
set("waterRaw", wRaw == null ? "--" : wRaw);
if (d.mode === "offline") { set("sensorStatus", "Offline"); set("sensorDetail", "Arduino not reachable"); }
else { set("sensorStatus", dOk ? "All good" : "Check DHT11"); set("sensorDetail", "DHT11: " + (dOk ? "OK" : "no signal") + " · Water: " + (wRaw == null ? "n/a" : "OK")); }
set("voltMetric", num(d.piezoV, 2));
)ECOSENSE";
static const char PG17[] PROGMEM = R"ECOSENSE(
set("impactMetric", d.forceN == null ? "--" : Math.round(d.forceN));
if (d.water != null) {
var st = d.water > .5 ? "rainy" : d.water > .2 ? "normal" : "dry";
set("atmState", st === "rainy" ? "Active Rain" : st === "normal" ? "Humid / Mist" : "Dry");
if (st !== lastWeather) { lastWeather = st; if (window.setWeather) window.setWeather(st); }
} else set("atmState", "--");
set("sigVal", d.mode === "access-point" ? "AP mode" : (d.rssi == null || d.rssi === 0) ? "—" : d.rssi + " dBm");
set("sideNet", d.mode === "demo" ? "Simulated readings" : d.mode === "access-point" ? "Access point · " + d.ip : d.ip ? "Wi-Fi · " + d.ip : "—");
set("sideUp", d.uptimeSeconds == null ? "Uptime —" : "Uptime " + Math.floor(d.uptimeSeconds / 60) + " min");
renderHistoryRows(d);
set("stepsMetric", d.steps == null ? "--" : Number(d.steps).toLocaleString());
set("spikeMetric", d.spikeV ? num(d.spikeV, 2) : "--");
set("spikeInfo", d.spikeV ? "Volts · counted " + d.spikeSteps + (d.spikeSteps === 1 ? " step" : " steps") : "Volts · waiting for a step");
set("liveMetric", num(d.liveV, 2));
set("maxMetric", num(d.maxV, 2));
pushLive(d);
set("hVolt", d.piezoV == null ? "--" : num(d.piezoV, 2));
set("hSteps", d.steps == null ? "--" : Number(d.steps).toLocaleString());
set("hLive", d.liveV == null ? "--" : num(d.liveV, 2));
set("hTemp", d.tempC == null ? "--" : num(d.tempC, 1));
set("hHum", d.humidity == null ? "--" : Math.round(d.humidity));
var rg = $("hRing");
if (rg) rg.style.strokeDashoffset = 289 * (1 - ((d.piezoV && d.maxV) ? Math.min(1, d.piezoV / d.maxV) : 0));
}
var liveBuf = [], peakBuf = [], liveLabels = [], liveChart = null;
function pushLive(d) {
if (d.liveV == null || d.piezoV == null) return;
liveBuf.push(+d.liveV); peakBuf.push(+d.piezoV);
liveLabels.push(new Date().toLocaleTimeString([], { minute: "2-digit", second: "2-digit" }));
if (liveBuf.length > 60) { liveBuf.shift(); peakBuf.shift(); liveLabels.shift(); }
if (liveChart) { liveChart.data.labels = liveLabels; liveChart.data.datasets[0].data = liveBuf; liveChart.data.datasets[1].data = peakBuf; liveChart.update("none"); }
}
function initLive() {
var c = $("liveChart"); if (!c || typeof Chart === "undefined") return;
if (liveChart) liveChart.destroy();
var light = document.documentElement.getAttribute("data-theme") === "light";
var tick = light ? "#475467" : "#8792a8", txt = getComputedStyle(document.body).getPropertyValue("--text").trim(), grid = { color: "rgba(130,145,180,.12)" };
liveChart = new Chart(c, {
type: "line",
data: { labels: liveLabels, datasets: [
{ label: "Live voltage (V)", data: liveBuf, borderColor: "#38bdf8", backgroundColor: "rgba(56,189,248,.14)", fill: true, tension: .3, pointRadius: 0, borderWidth: 2 },
{ label: "Peak voltage (V)", data: peakBuf, borderColor: "#f59e0b", borderDash: [5, 5], stepped: true, pointRadius: 0, borderWidth: 2 } ] },
options: { responsive: true, maintainAspectRatio: false, animation: false, plugins: { legend: { labels: { color: txt } } },
scales: { x: { ticks: { color: tick, maxTicksLimit: 6 }, grid: grid }, y: { min: 0, ticks: { color: tick }, grid: grid, title: { display: true, text: "Volts", color: tick } } } }
});
}
function renderHistoryRows(d) {
var box = $("histRows"); if (!box) return;
if (d.mode === "demo") {
box.innerHTML = "<p><strong>Today</strong> · 1,284 footsteps · 18.6 V peak voltage</p><p><strong>Yesterday</strong> · 946 footsteps · 17.2 V peak voltage</p><p><strong>May 22</strong> · 1,103 footsteps · 16.4 V peak voltage</p>";
} else if (d.steps == null && d.powerW == null) {
box.innerHTML = "<p>No records yet — the Arduino is not reachable.</p>";
} else {
box.innerHTML = "<p><strong>This session</strong> · " + (d.steps == null ? "--" : Number(d.steps).toLocaleString()) + " footsteps · " + num(d.maxV, 2) + " V peak voltage</p>" +
"<p><strong>Energy harvested</strong> · " + num(d.energyWh, 4) + " Wh since the board started</p>" +
"<p><strong>Node uptime</strong> · " + Math.floor((d.uptimeSeconds || 0) / 60) + " min · " + esc(d.ip || "") + "</p>";
}
}
function setConn(state) {
var text = state === "live" ? "Live · connected" : state === "demo" ? "Demo data" : "Arduino offline";
set("connText", text);
$("connDot").className = "dot " + (state === "live" ? "live" : state === "demo" ? "demo" : "off");
$("sideDot").className = "dot " + (state === "live" ? "live" : state === "demo" ? "demo" : "off");
set("sideState", state === "live" ? "System online" : state === "demo" ? "Demo mode" : "Device offline");
var ns = $("nodeStatus");
if (ns) { ns.textContent = state === "live" ? "Node Active" : state === "demo" ? "Demo Node" : "Node Offline"; ns.classList.toggle("off", state === "offline"); }
set("coreState", state === "live" ? "Arduino Online" : state === "demo" ? "Simulated" : "Arduino Offline");
set("streamLabel", state === "live" ? "Live telemetry stream" : state === "demo" ? "Demo telemetry (simulated)" : "No telemetry");
$("offlineBanner").hidden = state !== "offline";
}
var EMPTY = { tempC: null, humidity: null, powerW: null, piezoV: null, liveV: null, maxV: null, currentA: null, forceN: null, storageV: null, water: null, steps: null, energyWh: null, maxPowerW: null, rssi: null, uptimeSeconds: null, ip: "", mode: "offline" };
var polling = false;
async function poll() {
if (MODE !== "live" || polling) return;
polling = true;
try {
var ctl = new AbortController(), to = setTimeout(function () { ctl.abort(); }, 3000);
var r = await fetch("/api/data", { cache: "no-store", signal: ctl.signal });
clearTimeout(to);
if (!r.ok) throw new Error("http");
var j = await r.json();
if (MODE !== "live") return;
fails = 0; online = true;
lastSteps = j.steps;
setConn("live"); render(j);
} catch (e) {
if (MODE !== "live") return;
if (++fails >= 6) { online = false; lastSteps = null; setConn("offline"); render(EMPTY); }
} finally { polling = false; }
}
async function loadHistory() {
if (MODE !== "live") return;
try { var r = await fetch("/api/history", { cache: "no-store" }); hist = await r.json(); } catch (e) { hist = null; }
updateChart();
}
function setMode(m) {
MODE = m; store.set("eco-mode", m); lastWeather = null; fails = 0; lastSteps = null;
document.querySelectorAll("#modeSeg button").forEach(function (b) { b.setAttribute("aria-checked", b.dataset.mode === m ? "true" : "false"); });
if (m === "demo") { setConn("demo"); render(demo); updateChart(); }
else { hist = null; setConn("offline"); $("offlineBanner").hidden = true; set("connText", "Connecting…"); set("sideState", "Connecting…"); set("nodeStatus", "Connecting…"); $("nodeStatus").classList.remove("off"); set("coreState", "Waiting for Arduino"); set("streamLabel", "Waiting for Arduino…"); render(EMPTY); poll(); loadHistory(); }
}
function hhmm(ms) { return new Date(ms).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }); }
function chartData() {
if (MODE === "demo") return { labels: ["12:00", "13:00", "14:00", "15:00", "16:00", "17:00"], power: [4.2, 6.8, 12.8, 8.4, 10.1, 12.8], steps: [620, 800, 940, 1080, 1190, 1284] };
if (hist && hist.n > 0) {
var now = Date.now(), n = hist.power.length;
return { labels: hist.power.map(function (_, i) { return hhmm(now - (n - 1 - i) * hist.interval * 1000); }), power: hist.power, steps: hist.steps };
}
return { labels: [], power: [], steps: [] };
}
function updateChart() {
if (typeof energyChart === "undefined" || !energyChart) return;
var c = chartData();
energyChart.data.labels = c.labels; energyChart.data.datasets[0].data = c.power; energyChart.data.datasets[1].data = c.steps;
energyChart.update("none");
}
window.renderChart = function () {
var canvas = $("energyChart"); if (!canvas || typeof Chart === "undefined") return;
if (energyChart) energyChart.destroy();
var textColor = getComputedStyle(document.body).getPropertyValue("--text").trim();
var tick = document.documentElement.getAttribute("data-theme") === "light" ? "#475467" : "#8792a8", c = chartData();
energyChart = new Chart(canvas, {
type: "line",
data: { labels: c.labels, datasets: [
{ label: "Harvested Power (W)", data: c.power, borderColor: "#3b82f6", backgroundColor: "rgba(59,130,246,.15)", fill: true, tension: .35, yAxisID: "y" },
{ label: "Step Count Accumulation", data: c.steps, borderColor: "#38bdf8", backgroundColor: "transparent", borderDash: [5, 5], tension: .35, yAxisID: "steps" } ] },
options: { responsive: true, plugins: { legend: { labels: { color: textColor } } },
scales: { x: { ticks: { color: tick }, grid: { color: "rgba(130,145,180,.1)" } },
y: { type: "linear", position: "left", ticks: { color: tick }, grid: { color: "rgba(130,145,180,.1)" } },
steps: { type: "linear", position: "right", grid: { drawOnChartArea: false }, ticks: { color: tick } } } }
});
initLive();
};
)ECOSENSE";
static const char PG18[] PROGMEM = R"ECOSENSE(
window.exportDataToCSV = function () {
var rows = [["Timestamp", "Steps", "Power_Watts", "Temp_C", "Humidity_Pct", "Lat", "Lng"]], lat = 22.444034, lng = 88.373864, now = Date.now();
if (MODE === "live" && hist && hist.n > 0) {
var n = hist.power.length;
for (var i = 0; i < n; i++) rows.push([new Date(now - (n - 1 - i) * hist.interval * 1000).toISOString(), hist.steps[i], hist.power[i], hist.temp[i], hist.hum[i], lat, lng]);
}
var d = current || EMPTY;
rows.push([new Date(now).toISOString(), d.steps, d.powerW, d.tempC, d.humidity, lat, lng].map(function (v) { return v == null ? "" : v; }));
rows = rows.map(function (r) { return r.map(function (v) { return v == null ? "" : v; }).join(","); });
var blob = new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8" }), a = document.createElement("a");
a.href = URL.createObjectURL(blob); a.download = "telemetry_" + Date.now() + ".csv";
document.body.appendChild(a); a.click(); document.body.removeChild(a); setTimeout(function () { URL.revokeObjectURL(a.href); }, 1000);
};
window.inspectGateway = async function () {
var out = $("connectionMessage");
if (MODE === "demo") { out.textContent = "Demo mode: no device is being queried. Switch to Live to inspect the Arduino."; return; }
try {
var r = await fetch("/api/status", { cache: "no-store" }), s = await r.json();
out.textContent = "Gateway online · " + (s.mode === "station" ? "Wi-Fi client · " + s.rssi + " dBm" : "access point") + " · IP " + s.ip + " · uptime " + Math.floor(s.uptimeSeconds / 60) + " min · telemetry at /api/data";
} catch (e) { out.textContent = "The Arduino gateway is unreachable from this browser."; }
};
var th = $("theme"), saved = store.get("eco-theme", null);
if (saved === "dark" || saved === "light") { document.documentElement.setAttribute("data-theme", saved); th.checked = saved === "dark"; }
th.addEventListener("change", function () { store.set("eco-theme", th.checked ? "dark" : "light"); });
var navEl = $("mainNav"), scrim = $("sideScrim");
function drawer(open) { navEl.classList.toggle("open", open); scrim.classList.toggle("show", open); }
$("menuBtn").addEventListener("click", function () { drawer(!navEl.classList.contains("open")); });
scrim.addEventListener("click", function () { drawer(false); });
window.addEventListener("hashchange", function () { drawer(false); });
function tick() {
var d = new Date();
$("clock").innerHTML = d.toLocaleDateString(undefined, { weekday: "short", day: "numeric", month: "short", year: "numeric" }) + "<br><b>" + d.toLocaleTimeString() + "</b>";
}
tick(); setInterval(tick, 1000);
var PAGES = [["home", "Home", "⌂", "Landing page"], ["map", "Map Nodes", "◎", "Installation map"], ["environment", "Environment", "☁", "Microclimate analysis"], ["energy", "Energy Flow", "⚡", "Circuit analysis"], ["history", "History", "↺", "Telemetry logs"], ["connect", "Hardware", "⚙", "Gateway setup"]];
function E(t, p, r, sel, k, v, u) { return { t: t, p: p, r: r, sel: sel, k: k, v: v, u: u || "" }; }
var ENTRIES = PAGES.map(function (p) { return { t: p[1], p: "Page · " + p[3], r: p[0], sel: "#" + p[0], k: p[3] + " tab section", ic: p[2], page: true }; }).concat([
E("Temperature (Actual)", "Environment", "environment", "#envTemp", "temp celsius degrees thermometer measured", "envTemp"),
E("Relative Humidity", "Environment", "environment", "#envHum", "moisture dht percent", "envHum"),
E("Temperature (Feels Like)", "Environment", "environment", "#heatIndex", "heat index feels like perceived", "heatIndex"),
E("Water Sensor Analog", "Environment", "environment", "#waterLevel", "rain wet adc moisture level", "waterLevel"),
E("Atmospheric State", "Environment", "environment", "#atmState", "rain threshold condition weather", "atmState"),
E("Dynamic Moisture Plane", "Environment", "environment", "#environmentWeather", "glass refraction beads rain pane", null),
E("Footsteps", "Energy Flow", "energy", "#stepsMetric", "steps count walking tile footfall", "stepsMetric"),
E("Live Voltage", "Energy Flow", "energy", "#liveMetric", "piezo volts live now instant voltage a0", "liveMetric", " V"),
E("Peak Voltage", "Energy Flow", "energy", "#voltMetric", "piezo volts peak voltage", "voltMetric", " V"),
E("Highest Peak", "Energy Flow", "energy", "#maxMetric", "max maximum highest volts since start", "maxMetric", " V"),
E("Dynamic Impact Force", "Energy Flow", "energy", "#impactMetric", "newtons pressure force adc peak", "impactMetric", " N"),
E("Circuit Topology Pipeline", "Energy Flow", "energy", ".flow-diagram", "piezo bridge rectifier buck regulator diagram", null),
E("Harvest Dynamic Output", "Energy Flow", "energy", "#energyChart", "graph chart plot power steps trend", null),
E("Installation Map · Node #01", "Map Nodes", "map", "#mapContainer", "location gps coordinates latitude longitude kolkata marker", null),
E("Download Log (.CSV)", "History", "history", ".small-btn[onclick*=exportData]", "export csv records download spreadsheet", null),
E("Recorded Sessions", "History", "history", "#histRows", "log history sessions yesterday today", null),
E("Wi-Fi REST Gateway", "Hardware", "connect", "#connect article", "wifi rest json http endpoint api inspect status ip", null),
{ t: "Switch to Demo data", p: "Action · data source", k: "demo simulate offline sample fake mode", ic: "▶", act: function () { setMode("demo"); } },
{ t: "Switch to Live data", p: "Action · data source", k: "live arduino real readings mode connect", ic: "◉", act: function () { setMode("live"); } },
{ t: "Toggle light / dark mode", p: "Action · appearance", k: "theme dark light night day mode appearance", ic: "◐", act: function () { th.click(); } }
]);
var inp = $("siteSearch"), list = $("searchList"), shown = [], active = -1;
function valOf(e) {
if (!e.v) return "";
var el = $(e.v), t = el ? el.textContent.trim() : "";
return t && t !== "--" ? t + (e.u && !/[A-Za-z]$/.test(t) ? e.u : "") : "";
}
)ECOSENSE";
static const char PG19[] PROGMEM = R"ECOSENSE(function hl(text, toks) {
var s = esc(text);
toks.forEach(function (t) { if (t) s = s.replace(new RegExp("(" + t.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")", "ig"), "<mark>$1</mark>"); });
return s;
}

function search(q) {
var toks = q.toLowerCase().split(/\s+/).filter(Boolean);
if (!toks.length) return ENTRIES.filter(function (e) { return e.page; }).concat(ENTRIES.filter(function (e) { return e.act; }));
var out = [];
ENTRIES.forEach(function (e) {
var hay = (e.t + " " + e.p + " " + e.k).toLowerCase(), t = e.t.toLowerCase();
if (!toks.every(function (k) { return hay.indexOf(k) > -1; })) return;
out.push({ e: e, s: t.indexOf(toks[0]) === 0 ? 0 : t.indexOf(toks[0]) > -1 ? 1 : 2 });
});
out.sort(function (a, b) { return a.s - b.s; });
return out.slice(0, 9).map(function (o) { return o.e; });
}
function paint() {
var q = inp.value.trim(), toks = q.toLowerCase().split(/\s+/).filter(Boolean);
shown = search(q);
active = shown.length ? 0 : -1;
if (!shown.length) { list.innerHTML = '<div class="sg-empty">No matches for “' + esc(q) + '”. Try “temperature”, “steps”, “map” or “csv”.</div>'; }
else {
var html = q ? "" : '<div class="sg-head">Jump to</div>';
shown.forEach(function (e, i) {
var v = valOf(e);
html += '<button type="button" class="sg" role="option" id="sg' + i + '" data-i="' + i + '" aria-selected="' + (i === 0) + '"><span class="ic">' + (e.ic || "•") + '</span><span><span class="t">' + hl(e.t, toks) + '</span><span class="s">' + esc(e.p) + '</span></span>' + (v ? '<span class="v">' + esc(v) + "</span>" : "") + "</button>";
});
list.innerHTML = html;
}
openList(true);
}
document.body.appendChild(list);
function place() {
var r = inp.getBoundingClientRect(), w = Math.min(Math.max(r.width, 380), window.innerWidth - 16);
list.style.width = w + "px";
list.style.left = Math.max(8, Math.min(r.left, window.innerWidth - w - 8)) + "px";
list.style.top = (r.bottom + 8) + "px";
}
function openList(o) { if (o) place(); list.classList.toggle("open", o); inp.setAttribute("aria-expanded", o ? "true" : "false"); }
function keepPlaced() { if (list.classList.contains("open")) place(); }
window.addEventListener("resize", keepPlaced);
window.addEventListener("scroll", keepPlaced, true);
function mark(i) {
active = i;
list.querySelectorAll(".sg").forEach(function (b, k) { b.setAttribute("aria-selected", k === i ? "true" : "false"); });
var b = $("sg" + i); if (b) { b.scrollIntoView({ block: "nearest" }); inp.setAttribute("aria-activedescendant", "sg" + i); }
}
function reveal(e) {
var el = e.page ? null : document.querySelector(e.sel);
if (!el) { window.scrollTo({ top: 0, behavior: "smooth" }); return; }
var box = el.closest("article") || el;
box.scrollIntoView({ behavior: "smooth", block: "center" });
box.classList.remove("flash"); void box.offsetWidth; box.classList.add("flash");
setTimeout(function () { box.classList.remove("flash"); }, 2000);
}
function go(e) {
openList(false); inp.blur(); inp.value = ""; drawer(false);
if (e.act) { e.act(); return; }
var cur = location.hash.replace("#", "") || "home";
if (cur === e.r) reveal(e);
else { window.goTo("#" + e.r); setTimeout(function () { reveal(e); }, 1250); }
}
inp.addEventListener("focus", paint);
inp.addEventListener("input", paint);
inp.addEventListener("blur", function () { setTimeout(function () { openList(false); }, 150); });
inp.addEventListener("keydown", function (ev) {
if (ev.key === "ArrowDown") { ev.preventDefault(); if (!list.classList.contains("open")) paint(); else if (shown.length) mark((active + 1) % shown.length); }
else if (ev.key === "ArrowUp") { ev.preventDefault(); if (shown.length) mark((active - 1 + shown.length) % shown.length); }
else if (ev.key === "Enter") { ev.preventDefault(); if (shown[active]) go(shown[active]); }
else if (ev.key === "Escape") { openList(false); inp.blur(); }
});
list.addEventListener("mousedown", function (ev) {
var b = ev.target.closest(".sg"); if (!b) return;
ev.preventDefault(); go(shown[+b.dataset.i]);
});
document.addEventListener("keydown", function (ev) {
var typing = /^(INPUT|TEXTAREA|SELECT)$/.test((ev.target.tagName || ""));
if ((ev.key === "/" && !typing) || ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "k")) { ev.preventDefault(); inp.focus(); inp.select(); }
});
document.querySelectorAll("#modeSeg button").forEach(function (b) { b.addEventListener("click", function () { if (MODE !== b.dataset.mode) setMode(b.dataset.mode); }); });
$("bannerDemo").addEventListener("click", function () { setMode("demo"); });
setInterval(demoTick, 2000);
setInterval(poll, 300);
setInterval(function () { if (MODE === "live" && online) loadHistory(); }, 15000);
setMode(MODE);
window.addEventListener("hashchange", function () { if ((location.hash || "").indexOf("energy") > -1) { loadHistory(); } });
})();
</script>
</body>
</html>

)ECOSENSE";
static const PagePart PAGE[] = {
  {PG0, sizeof(PG0) - 1},
  {PG1, sizeof(PG1) - 1},
  {PG2, sizeof(PG2) - 1},
  {PG3, sizeof(PG3) - 1},
  {PG4, sizeof(PG4) - 1},
  {PG5, sizeof(PG5) - 1},
  {PG6, sizeof(PG6) - 1},
  {PG7, sizeof(PG7) - 1},
  {PG8, sizeof(PG8) - 1},
  {PG9, sizeof(PG9) - 1},
  {PG16, sizeof(PG16) - 1},
  {PG17, sizeof(PG17) - 1},
  {PG18, sizeof(PG18) - 1},
  {PG19, sizeof(PG19) - 1},
};
static const size_t PAGE_PARTS = sizeof(PAGE) / sizeof(PAGE[0]);

// ---------------- live readings ----------------
uint32_t stepCount = 0;
float piezoPeakV = 0, powerW = 0, forceN = 0, maxPowerW = 0;
float piezoLiveV = 0, piezoMaxV = 0;   // live (smoothed) volts and highest 1 s peak since power-on
float storageV = NAN, waterLevel = NAN, tempC = NAN, humidity = NAN;
int waterRaw = -1;
bool dhtOk = false;
const char* dhtErr = "not read yet";
double energyJ = 0;
float winPeakVa = 0, winEnergyJ = 0;
bool stepArmed = true;
float lastSpikeV = 0;
uint8_t lastSpikeSteps = 0;
// footstep noise filter state
float baselineV = 0, noiseV = 0, gateV = NOISE_GATE_V, onV = STEP_ON_V, offV = STEP_OFF_V;
uint8_t runCount = 0;
unsigned long runStartUs = 0, quietStartMs = 0;
bool quiet = false;
unsigned long lastUs = 0, winStartMs = 0, lastStepMs = 0, lastSlowMs = 0, lastDhtMs = 0, lastHistMs = 0;
uint8_t dhtFails = 0;

float histP[HISTORY_N], histV[HISTORY_N], histT[HISTORY_N], histH[HISTORY_N];
uint32_t histS[HISTORY_N];
uint8_t histHead = 0, histCount = 0;

// ---------------- DHT11 / DHT22 (no library) ----------------
static int pulseLen(int level, unsigned int timeoutUs) {
  unsigned long t = micros();
  while (digitalRead(PIN_DHT) != level) if (micros() - t > timeoutUs) return -1;
  unsigned long s = micros();
  while (digitalRead(PIN_DHT) == level) if (micros() - s > timeoutUs) return -1;
  return (int)(micros() - s);
}

bool readDHTOnce(float &t, float &h) {
  uint8_t d[5] = {0, 0, 0, 0, 0};
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delay(DHT_TYPE == 11 ? 20 : 2);
  digitalWrite(PIN_DHT, HIGH);
  delayMicroseconds(30);
  pinMode(PIN_DHT, INPUT_PULLUP);
  if (pulseLen(LOW, 200) < 0) { dhtErr = "no response - check wiring / 5V / GND / pin D2"; return false; }
  if (pulseLen(HIGH, 200) < 0) { dhtErr = "bad response pulse"; return false; }
  for (int i = 0; i < 40; i++) {
    int lo = pulseLen(LOW, 200);
    int hi = pulseLen(HIGH, 200);
    if (lo < 0 || hi < 0) { dhtErr = "timeout while reading bits"; return false; }
    d[i / 8] <<= 1;
    if (hi > lo) d[i / 8] |= 1;
  }
  if (((d[0] + d[1] + d[2] + d[3]) & 0xFF) != d[4]) { dhtErr = "checksum error"; return false; }
  if (DHT_TYPE == 11) {
    h = d[0];
    t = d[2] + d[3] * 0.1f;
  } else {
    h = ((d[0] << 8) | d[1]) * 0.1f;
    int raw = ((d[2] & 0x7F) << 8) | d[3];
    t = raw * 0.1f;
    if (d[2] & 0x80) t = -t;
  }
  if (h < 0 || h > 100) { dhtErr = "value out of range"; return false; }
  dhtErr = "ok";
  return true;
}

bool readDHT(float &t, float &h) {
  for (int i = 0; i < 3; i++) {
    if (readDHTOnce(t, h)) return true;
    delay(60);
  }
  return false;
}

// ---------------- piezo reading + calibration ----------------
float readPiezoPin() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < PIEZO_OVERSAMPLE; i++) sum += analogRead(PIN_PIEZO);
  return (sum / (float)PIEZO_OVERSAMPLE) * ADC_VREF / ADC_MAX;
}

// Learns the idle level and noise of A0. Run with nothing pressing on the tile.
void calibratePiezo() {
  const int N = 400;
  float sum = 0, mn = 99, mx = -1;
  for (int i = 0; i < N; i++) {
    float v = readPiezoPin();
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delay(2);
  }
  baselineV = sum / N;
  noiseV = max(mx - baselineV, baselineV - mn);
  gateV = max(NOISE_GATE_V, noiseV * 1.5f);
  onV   = max(STEP_ON_V,    noiseV * 2.5f);
  offV  = min(max(STEP_OFF_V, noiseV * 1.5f), onV * 0.6f);
  Serial.print(F("Piezo idle level: ")); Serial.print(baselineV, 3);
  Serial.print(F(" V, noise: ")); Serial.print(noiseV, 3);
  Serial.print(F(" V, step threshold: ")); Serial.print(onV, 2); Serial.println(F(" V"));
  if (noiseV > 0.3f) Serial.println(F("WARNING: A0 is very noisy. Add a 1 Mohm resistor from A0 to GND."));
  if (baselineV > 1.0f) Serial.println(F("WARNING: A0 idles high. Was the tile pressed during start-up, or is the wire floating?"));
}

void pushHistory() {
  histP[histHead] = powerW;
  histS[histHead] = stepCount;
  histV[histHead] = storageV;
  histT[histHead] = tempC;
  histH[histHead] = humidity;
  histHead = (histHead + 1) % HISTORY_N;
  if (histCount < HISTORY_N) histCount++;
}

void sampleSensors() {
  unsigned long nowUs = micros();
  float dt = (nowUs - lastUs) * 1e-6f;
  lastUs = nowUs;
  if (dt > 1.0f) dt = 1.0f;
  unsigned long ms = millis();

  if (HAS_PIEZO) {
    float raw = readPiezoPin();                                  // volts at the pin, exactly as measured
    if (raw < baselineV) baselineV += (raw - baselineV) * 0.05f; // idle level can never sit above the signal
    float sig = raw - baselineV;                                 // signal above idle: used for step detection only
    if (sig < 0) sig = 0;
    if (stepArmed && runCount == 0 && sig < gateV)
      baselineV += (raw - baselineV) * 0.0002f;                  // track slow drift while idle
    float va = raw;                                              // dashboard voltage = real pin voltage (unfiltered)
    float vp = va * PIEZO_DIVIDER;                               // real volts
    piezoLiveV = piezoLiveV * 0.7f + vp * 0.3f;
    if (va > winPeakVa) winPeakVa = va;
    if (vp > piezoPeakV) { piezoPeakV = vp; if (vp > piezoMaxV) piezoMaxV = vp; }
    float fNow = (va / ADC_VREF) * FORCE_FULL_SCALE_N;
    if (fNow > forceN) forceN = fNow;
    winEnergyJ += (vp * vp / LOAD_OHMS) * dt;

    if (!stepArmed) {
      if (va > lastSpikeV) lastSpikeV = va;
      if (sig < offV) {                                          // must stay quiet before re-arming
        if (!quiet) { quiet = true; quietStartMs = ms; }
        else if (ms - quietStartMs >= STEP_REARM_MS) { stepArmed = true; quiet = false; }
      } else quiet = false;
    } else if (sig >= onV) {
      if (runCount == 0) runStartUs = nowUs;
      if (runCount < 255) runCount++;
      if (runCount >= STEP_CONFIRM_SAMPLES && nowUs - runStartUs >= STEP_MIN_ABOVE_US &&
          ms - lastStepMs >= STEP_DEBOUNCE_MS) {
        stepCount++;
        lastSpikeV = va; lastSpikeSteps = 1;
        lastStepMs = ms;
        stepArmed = false; quiet = false; runCount = 0;
      }
    } else {
      runCount = 0;                                              // dropped out: it was only noise
    }

    if (ms - winStartMs >= 1000UL) {                             // 1 s measurement window
      float secs = (ms - winStartMs) / 1000.0f;
      piezoPeakV = winPeakVa * PIEZO_DIVIDER;
      if (piezoPeakV > piezoMaxV) piezoMaxV = piezoPeakV;
      powerW = winEnergyJ / secs;
      forceN = (winPeakVa / ADC_VREF) * FORCE_FULL_SCALE_N;
      if (powerW > maxPowerW) maxPowerW = powerW;
      energyJ += winEnergyJ;
      winPeakVa = 0; winEnergyJ = 0; winStartMs = ms;
    }
  }

  if (ms - lastSlowMs >= 100UL) {
    lastSlowMs = ms;
    if (HAS_STORAGE) {
      float v = analogRead(PIN_STORAGE) * ADC_VREF / ADC_MAX * STORAGE_DIVIDER;
      storageV = isnan(storageV) ? v : storageV * 0.8f + v * 0.2f;
    }
    if (HAS_WATER) {
      int raw = analogRead(PIN_WATER);
      waterRaw = (waterRaw < 0) ? raw : (int)(waterRaw * 0.8f + raw * 0.2f);
      float w = (float)(waterRaw - WATER_DRY_ADC) / (float)(WATER_WET_ADC - WATER_DRY_ADC);
      waterLevel = constrain(w, 0.0f, 1.0f);
    }
  }

  if (DHT_ENABLED && ms - lastDhtMs >= 2500UL) {
    lastDhtMs = ms;
    float t, h;
    if (readDHT(t, h)) { tempC = t; humidity = h; dhtFails = 0; dhtOk = true; }
    else if (++dhtFails >= 10) { tempC = NAN; humidity = NAN; dhtFails = 10; dhtOk = false; }
  }

  if (ms - lastHistMs >= HISTORY_INTERVAL_MS) { lastHistMs = ms; pushHistory(); }
}

// ---------------- JSON ----------------
String jf(float v, unsigned int d) { return isnan(v) ? String("null") : String(v, d); }

bool stationMode() { return !apMode && WiFi.status() == WL_CONNECTED; }

String statusJson() {
  const bool st = stationMode();
  String s;
  s.reserve(160);
  s += "{\"mode\":\""; s += st ? "station" : "access-point";
  s += "\",\"ip\":\""; s += WiFi.localIP().toString();
  s += "\",\"rssi\":"; s += st ? String((int)WiFi.RSSI()) : String(0);
  s += ",\"uptimeSeconds\":"; s += String(millis() / 1000UL);
  s += "}";
  return s;
}

String dataJson() {
  const bool st = stationMode();
  String s;
  s.reserve(780);
  s += "{\"mode\":\""; s += st ? "station" : "access-point";
  s += "\",\"ip\":\""; s += WiFi.localIP().toString();
  s += "\",\"rssi\":"; s += st ? String((int)WiFi.RSSI()) : String(0);
  s += ",\"uptimeSeconds\":"; s += String(millis() / 1000UL);
  s += ",\"steps\":"; s += HAS_PIEZO ? String(stepCount) : String("null");
  s += ",\"piezoV\":"; s += HAS_PIEZO ? jf(piezoPeakV, 2) : String("null");
  s += ",\"spikeV\":"; s += HAS_PIEZO ? jf(lastSpikeV, 2) : String("null");
  s += ",\"spikeSteps\":"; s += String(lastSpikeSteps);
  s += ",\"liveV\":"; s += HAS_PIEZO ? jf(piezoLiveV, 2) : String("null");
  s += ",\"maxV\":"; s += HAS_PIEZO ? jf(piezoMaxV, 2) : String("null");
  s += ",\"powerW\":"; s += HAS_PIEZO ? jf(powerW, 4) : String("null");
  s += ",\"currentA\":"; s += HAS_PIEZO ? jf(piezoPeakV / LOAD_OHMS, 4) : String("null");
  s += ",\"forceN\":"; s += HAS_PIEZO ? jf(forceN, 0) : String("null");
  s += ",\"maxPowerW\":"; s += HAS_PIEZO ? jf(maxPowerW, 4) : String("null");
  s += ",\"energyWh\":"; s += HAS_PIEZO ? jf((float)(energyJ / 3600.0), 6) : String("null");
  s += ",\"storageV\":"; s += jf(storageV, 2);
  s += ",\"water\":"; s += jf(waterLevel, 2);
  s += ",\"tempC\":"; s += jf(tempC, 1);
  s += ",\"humidity\":"; s += jf(humidity, 1);
  s += ",\"dhtOk\":"; s += (DHT_ENABLED && dhtOk) ? "true" : "false";
  s += ",\"waterRaw\":"; s += (HAS_WATER && waterRaw >= 0) ? String(waterRaw) : String("null");
  s += "}";
  return s;
}

void histArray(String &s, const char* name, const float* a, unsigned int d, bool last) {
  s += "\""; s += name; s += "\":[";
  for (uint8_t i = 0; i < histCount; i++) {
    uint8_t idx = (histHead + HISTORY_N - histCount + i) % HISTORY_N;
    if (i) s += ",";
    s += jf(a[idx], d);
  }
  s += last ? "]" : "],";
}

String historyJson() {
  String s;
  s.reserve(2400);
  s += "{\"interval\":"; s += String(HISTORY_INTERVAL_MS / 1000UL);
  s += ",\"n\":"; s += String(histCount);
  s += ",";
  histArray(s, "power", histP, 4, false);
  s += "\"steps\":[";
  for (uint8_t i = 0; i < histCount; i++) {
    uint8_t idx = (histHead + HISTORY_N - histCount + i) % HISTORY_N;
    if (i) s += ",";
    s += String(histS[idx]);
  }
  s += "],";
  histArray(s, "storage", histV, 2, false);
  histArray(s, "temp", histT, 1, false);
  histArray(s, "hum", histH, 1, true);
  s += "}";
  return s;
}

// ---------------- HTTP ----------------
void sendBytes(WiFiClient &c, const char* p, size_t n) {
  int stalls = 0;
  while (n > 0 && c.connected()) {
    size_t k = n > SEND_CHUNK ? SEND_CHUNK : n;
    size_t w = c.write((const uint8_t*)p, k);
    if (w == 0) { if (++stalls > 200) return; delay(2); continue; }
    stalls = 0;
    p += w; n -= w;
  }
}

void sendHeader(WiFiClient &c, const char* code, const char* type, size_t len) {
  String h = "HTTP/1.1 ";
  h += code;
  h += "\r\nContent-Type: "; h += type;
  h += "\r\nContent-Length: "; h += String((unsigned long)len);
  h += "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
  sendBytes(c, h.c_str(), h.length());
}

void sendText(WiFiClient &c, const char* code, const char* type, const String &body, bool head) {
  sendHeader(c, code, type, body.length());
  if (!head) sendBytes(c, body.c_str(), body.length());
}

void sendPage(WiFiClient &c, bool head) {
  size_t total = 0;
  for (size_t i = 0; i < PAGE_PARTS; i++) total += PAGE[i].n;
  sendHeader(c, "200 OK", "text/html; charset=utf-8", total);
  if (head) return;
  for (size_t i = 0; i < PAGE_PARTS; i++) sendBytes(c, PAGE[i].p, PAGE[i].n);
}

void serveClients() {
  WiFiClient client = server.available();
  if (!client) return;
  String first;
  uint32_t tail = 0;
  unsigned long t0 = millis();
  bool lineDone = false;
  while (client.connected() && millis() - t0 < 400UL) {
    if (!client.available()) { sampleSensors(); continue; }
    char ch = client.read();
    tail = (tail << 8) | (uint8_t)ch;
    if (!lineDone) {
      if (ch == '\n') lineDone = true;
      else if (ch != '\r' && first.length() < 160) first += ch;
    }
    if (tail == 0x0D0A0D0AUL) break;
  }
  int sp1 = first.indexOf(' ');
  int sp2 = first.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) { client.stop(); return; }
  String method = first.substring(0, sp1);
  String path = first.substring(sp1 + 1, sp2);
  int q = path.indexOf('?');
  if (q >= 0) path = path.substring(0, q);
  const bool head = method == "HEAD";

  if (method == "OPTIONS") sendText(client, "204 No Content", "text/plain", "", true);
  else if (method != "GET" && !head) sendText(client, "405 Method Not Allowed", "text/plain", "Method not allowed", head);
  else if (path == "/" || path == "/index.html") sendPage(client, head);
  else if (path == "/api/data") sendText(client, "200 OK", "application/json", dataJson(), head);
  else if (path == "/api/history") sendText(client, "200 OK", "application/json", historyJson(), head);
  else if (path == "/api/status") sendText(client, "200 OK", "application/json", statusJson(), head);
  else if (path == "/favicon.ico") sendText(client, "204 No Content", "image/x-icon", "", true);
  else sendText(client, "404 Not Found", "text/plain; charset=utf-8", "Page not found", head);

  client.flush();
  delay(2);
  client.stop();
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(9600);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { }
  delay(200);
  analogReadResolution(ADC_BITS);
  if (DHT_ENABLED) pinMode(PIN_DHT, INPUT_PULLUP);

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Wi-Fi module not found.");
    while (true) { }
  }

  bool connected = false;
  if (strlen(WIFI_SSID) > 0) {
    Serial.print("Connecting to Wi-Fi");
    for (int attempt = 0; attempt < 3 && !connected; attempt++) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      for (int i = 0; i < 25 && WiFi.status() != WL_CONNECTED; i++) {
        delay(400);
        Serial.print(".");
      }
      connected = WiFi.status() == WL_CONNECTED;
      if (connected) {
        // Status can read "connected" before the router hands out an IP address.
        unsigned long w0 = millis();
        while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - w0 < 15000) {
          delay(250);
          Serial.print("+");
        }
        connected = WiFi.localIP() != IPAddress(0, 0, 0, 0);
      }
    }
    Serial.println();
    if (!connected) Serial.println("Wi-Fi failed (check SSID/password, 2.4 GHz only). Starting access point.");
  }

  if (!connected) {
    if (WiFi.status() == WL_CONNECTED || strlen(WIFI_SSID) > 0) WiFi.disconnect();
    randomSeed(analogRead(A0) ^ micros());
    String ssid = "EcoFusion-" + String((uint16_t)random(0x1000, 0xFFFF), HEX);
    char password[17];
    snprintf(password, sizeof(password), "%08lX%08lX",
             (unsigned long)random(0x7FFFFFFF), (unsigned long)random(0x7FFFFFFF));
    int status = WiFi.beginAP(ssid.c_str(), password);
    if (status != WL_AP_LISTENING) {
      Serial.println("Failed to start access point.");
      while (true) { }
    }
    apMode = true;
    delay(3000);
    Serial.println("Access point: " + ssid);
    Serial.println("Access point password: " + String(password));
    Serial.println("Open: http://" + WiFi.localIP().toString());
  } else {
    Serial.println("Open: http://" + WiFi.localIP().toString());
  }

  server.begin();
  calibratePiezo();                      // keep hands off the tile for ~1 s while this runs
  lastUs = micros();
  winStartMs = lastHistMs = millis();
  Serial.println("EcoFusion sensor node ready.");
}

void loop() {
  sampleSensors();
  serveClients();
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 2000UL) {
    lastPrint = millis();
    Serial.println(F("------ EcoFusion sensors ------"));
    if (dhtOk) {
      Serial.print(F("Temperature : ")); Serial.print(tempC, 1); Serial.println(F(" C"));
      Serial.print(F("Humidity    : ")); Serial.print(humidity, 1); Serial.println(F(" %"));
    } else {
      Serial.print(F("DHT11       : NO DATA (")); Serial.print(dhtErr); Serial.println(F(")"));
    }
    Serial.print(F("Water raw   : ")); Serial.print(waterRaw);
    Serial.print(F("  level: ")); Serial.print(waterLevel, 2);
    Serial.print(F("  state: "));
    if (isnan(waterLevel)) Serial.println(F("--"));
    else if (waterLevel > 0.5f) Serial.println(F("RAIN"));
    else if (waterLevel > 0.2f) Serial.println(F("MIST"));
    else Serial.println(F("DRY"));
    Serial.print(F("Storage V   : ")); Serial.println(storageV, 2);
    Serial.print(F("Piezo idle  : ")); Serial.print(baselineV, 3);
    Serial.print(F(" V  step at > ")); Serial.print(onV, 2); Serial.println(F(" V"));
    Serial.print(F("Steps       : ")); Serial.println(stepCount);
  }
}
