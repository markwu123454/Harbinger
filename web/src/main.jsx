import { h, render } from 'preact';
import { useState, useEffect, useRef, useCallback } from 'preact/hooks';

// ── Constants ──────────────────────────────────────────────────────────────
const MIN_V = 50, MAX_V = 400;

// ── Helpers ────────────────────────────────────────────────────────────────
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const sign  = v => (v > 0 ? '+' : '') + Math.round(v);
const pad3  = v => String(Math.round(v)).padStart(3, '0');

function capColor(pct) {
    if (pct > 80) return 'var(--red)';
    if (pct > 50) return 'var(--amber)';
    return 'var(--green)';
}

// ── Toggle ─────────────────────────────────────────────────────────────────
function Toggle({ on, onChange, disabled, variant = 'amber' }) {
    const cls = ['toggle',
        disabled              ? 'disabled' : '',
        on && variant === 'amber' ? 'on'     : '',
        on && variant === 'red'   ? 'gun-on' : '',
    ].filter(Boolean).join(' ');

    return h('label', { class: cls },
        h('input', { type: 'checkbox', checked: on, disabled,
            onChange: e => { if (!disabled) onChange(e.target.checked); } }),
        h('div', { class: 'toggle-track' }),
        h('div', { class: 'toggle-thumb' }),
    );
}

// ── VoltSlider ─────────────────────────────────────────────────────────────
function VoltSlider({ value, onChange, locked }) {
    return h('div', { class: 'section' },
        h('div', { class: 'section-label' }, 'Charge Target'),
        h('div', { class: 'volt-display' }, value, h('span', { class: 'unit' }, 'V')),
        h('input', {
            type: 'range', class: 'volt-slider',
            min: MIN_V, max: MAX_V, step: 10,
            value,
            disabled: locked,
            onInput: e => onChange(parseInt(e.target.value)),
        }),
        h('div', { style: { display:'flex', justifyContent:'space-between', fontSize:'9px', color:'var(--dim)', fontFamily:'var(--font-ui)', letterSpacing:'1px' } },
            h('span', null, MIN_V + 'V'),
            h('span', null, MAX_V + 'V'),
        ),
        locked && h('div', { class: 'volt-locked' }, 'LOCKED — DISARM GUN TO CHANGE'),
    );
}

// ── FireButton ─────────────────────────────────────────────────────────────
function FireButton({ ready, onFire }) {
    const ringRef  = useRef(null);
    const rafRef   = useRef(null);
    const timerRef = useRef(null);
    const startRef = useRef(null);
    const [firing, setFiring] = useState(false);

    const cancel = useCallback(() => {
        if (timerRef.current) { clearTimeout(timerRef.current); timerRef.current = null; }
        if (rafRef.current)   { cancelAnimationFrame(rafRef.current); rafRef.current = null; }
        startRef.current = null;
        if (ringRef.current) ringRef.current.style.setProperty('--p', '0%');
    }, []);

    const animRing = useCallback(() => {
        if (!startRef.current) return;
        const pct = clamp((Date.now() - startRef.current) / 600 * 100, 0, 100);
        if (ringRef.current) ringRef.current.style.setProperty('--p', pct + '%');
        if (pct < 100) rafRef.current = requestAnimationFrame(animRing);
    }, []);

    const onMouseDown = useCallback(() => {
        if (!ready) return;
        startRef.current = Date.now();
        rafRef.current = requestAnimationFrame(animRing);
        timerRef.current = setTimeout(() => {
            cancel();
            setFiring(true);
            onFire();
            setTimeout(() => setFiring(false), 260);
        }, 600);
    }, [ready, animRing, cancel, onFire]);

    useEffect(() => {
        document.addEventListener('mouseup', cancel);
        return () => document.removeEventListener('mouseup', cancel);
    }, [cancel]);

    const cls = ['fire-btn', ready ? 'ready' : '', firing ? 'firing' : ''].filter(Boolean).join(' ');

    return h('button', { class: cls, onMouseDown, onMouseLeave: cancel },
        h('div', { class: 'fire-ring', ref: ringRef }),
        firing ? '■ FIRING' : 'FIRE',
    );
}

// ── BiBar ──────────────────────────────────────────────────────────────────
function BiBar({ value, name }) {
    const clamped = clamp(value, -100, 100);
    const isPos   = clamped >= 0;
    const pct     = Math.abs(clamped);
    const h_fill  = (pct / 100) * 38;
    const color   = isPos ? 'var(--blue)' : 'var(--amber)';

    const fillStyle = isPos
        ? { height: h_fill + 'px', bottom: '50%', top: 'auto', background: color }
        : { height: h_fill + 'px', top: '50%',    bottom: 'auto', background: color };

    return h('div', { class: 'bibar-wrap' },
        h('div', { class: 'bibar-val', style: { color } }, sign(clamped)),
        h('div', { class: 'bibar-track' },
            h('div', { class: 'bibar-fill', style: { ...fillStyle, transition:'all 0.25s' } }),
            h('div', { class: 'bibar-mid' }),
        ),
        h('div', { class: 'bibar-name' }, name),
    );
}

// ── CapBar ─────────────────────────────────────────────────────────────────
function CapBar({ voltage, pct, targetV, name }) {
    const col     = capColor(pct);
    const tgtPct  = ((targetV - MIN_V) / (MAX_V - MIN_V)) * 100;

    return h('div', { class: 'cap-bar-wrap' },
        h('div', { class: 'cap-bar-val', style: { color: col } }, Math.round(voltage) + 'V'),
        h('div', { class: 'cap-bar-track' },
            h('div', { class: 'cap-bar-fill', style: { height: pct + '%', background: col } }),
            h('div', { class: 'cap-bar-target', style: { bottom: tgtPct + '%' } }),
        ),
        h('div', { class: 'cap-bar-name' }, name),
    );
}

// ── Compass ────────────────────────────────────────────────────────────────
function Compass({ heading }) {
    const deg = ((heading % 360) + 360) % 360;
    const rad = (deg - 90) * Math.PI / 180;
    const cx = 60, cy = 60, r = 52;

    const ticks = Array.from({ length: 36 }, (_, i) => {
        const a   = (i * 10 - 90) * Math.PI / 180;
        const maj = i % 9 === 0, med = i % 3 === 0;
        const r1  = r - (maj ? 11 : med ? 6 : 3);
        return h('line', { key: i,
            x1: cx + r * Math.cos(a), y1: cy + r * Math.sin(a),
            x2: cx + r1* Math.cos(a), y2: cy + r1* Math.sin(a),
            stroke: maj ? 'var(--mid)' : 'var(--border)',
            'stroke-width': maj ? 1.5 : 0.5,
        });
    });

    const labels = [['N',0,'var(--red)',15],['E',90,'var(--mid)',11],['S',180,'var(--mid)',11],['W',270,'var(--mid)',11]];

    return h('div', { style: { display:'flex', flexDirection:'column', alignItems:'center', gap:'4px' } },
        h('svg', { width: 120, height: 120 },
            h('circle', { cx, cy, r, fill:'var(--bg)', stroke:'var(--border)', 'stroke-width':1.5 }),
            ...ticks,
            ...labels.map(([d, an, col, fs]) => {
                const a = (an - 90) * Math.PI / 180;
                return h('text', { key: d,
                    x: cx + 38 * Math.cos(a), y: cy + 38 * Math.sin(a) + 4,
                    'text-anchor':'middle', 'font-size': fs,
                    'font-weight':'bold', fill: col,
                    'font-family':'var(--font-mono)',
                }, d);
            }),
            h('line', {
                x1: cx, y1: cy,
                x2: cx + r * 0.7  * Math.cos(rad), y2: cy + r * 0.7  * Math.sin(rad),
                stroke:'var(--red)', 'stroke-width':3, 'stroke-linecap':'round',
            }),
            h('line', {
                x1: cx, y1: cy,
                x2: cx - r * 0.33 * Math.cos(rad), y2: cy - r * 0.33 * Math.sin(rad),
                stroke:'var(--dim)', 'stroke-width':2, 'stroke-linecap':'round',
            }),
            h('circle', { cx, cy, r: 3.5, fill:'var(--mid)' }),
        ),
        h('div', { style: { fontSize:'20px', fontWeight:'700', fontFamily:'var(--font-mono)', letterSpacing:'3px' } },
            pad3(deg) + '°',
        ),
    );
}

// ── PitchView ──────────────────────────────────────────────────────────────
function PitchView({ pitch }) {
    const W = 128, H = 104, mid = H / 2;
    const scale = (mid * 0.78) / 45;
    const hy    = mid - pitch * scale;

    const graticules = [-30, -15, 0, 15, 30].map(v => {
        const y    = mid - v * scale;
        const isMid = v === 0;
        return h('g', { key: v },
            h('line', { x1: isMid ? 0 : 18, y1: y, x2: isMid ? W : W - 18, y2: y,
                stroke: isMid ? 'var(--mid)' : 'var(--border)',
                'stroke-width': isMid ? 1.5 : 0.5 }),
            h('text', { x: W - 2, y: y + 4, 'font-size': 9, 'text-anchor':'end',
                fill: isMid ? 'var(--mid)' : 'var(--dim)',
                'font-family':'var(--font-mono)' }, v),
        );
    });

    return h('div', { style: { display:'flex', flexDirection:'column', alignItems:'center', gap:'4px' } },
        h('svg', { width: W, height: H, style: { border:'1px solid var(--border)', display:'block' } },
            h('rect', { x:0, y:0, width:W, height:H, fill:'var(--bg)' }),
            h('rect', { x:0, y:hy, width:W, height: H - hy, fill:'var(--blue)', opacity:0.07 }),
            h('line', { x1:0, y1:hy, x2:W, y2:hy, stroke:'var(--blue)', 'stroke-width':2 }),
            ...graticules,
        ),
        h('div', { style: { fontSize:'18px', fontWeight:'700', fontFamily:'var(--font-mono)' } },
            sign(pitch) + '°',
        ),
    );
}

// ── AimPad ─────────────────────────────────────────────────────────────────
function AimPad() {
    const padRef    = useRef(null);
    const accumRef  = useRef({ x: 0, y: 0 });
    const [accum, setAccum]   = useState({ x: 0, y: 0 });
    const [locked, setLocked] = useState(false);
    const [size,   setSize]   = useState({ w: 0, h: 0 });

    // measure container
    useEffect(() => {
        const obs = new ResizeObserver(([e]) => {
            setSize({ w: e.contentRect.width, h: e.contentRect.height });
        });
        if (padRef.current) obs.observe(padRef.current);
        return () => obs.disconnect();
    }, []);

    useEffect(() => {
        const onMove = e => {
            if (document.pointerLockElement !== padRef.current) return;
            accumRef.current = {
                x: accumRef.current.x + e.movementX * 0.38,
                y: accumRef.current.y + e.movementY * 0.38,
            };
            setAccum({ ...accumRef.current });
        };
        const onLock = () => setLocked(document.pointerLockElement === padRef.current);
        const onKey  = e => {
            if (e.key === 'Escape' && document.pointerLockElement === padRef.current)
                document.exitPointerLock();
        };
        document.addEventListener('mousemove', onMove);
        document.addEventListener('pointerlockchange', onLock);
        document.addEventListener('keydown', onKey);
        return () => {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('pointerlockchange', onLock);
            document.removeEventListener('keydown', onKey);
        };
    }, []);

    const handleClick = useCallback(() => {
        if (!locked && padRef.current) padRef.current.requestPointerLock();
    }, [locked]);

    const reset = useCallback(e => {
        e.stopPropagation();
        accumRef.current = { x: 0, y: 0 };
        setAccum({ x: 0, y: 0 });
    }, []);

    const { w, h } = size;
    const vizX = w ? ((accum.x % w) + w) % w : 0;
    const vizY = h ? ((accum.y % h) + h) % h : 0;
    const CX = w / 2, CY = h / 2;
    const maxR = Math.min(w, h) * 0.44;
    const color = locked ? 'var(--red)' : 'var(--cyan)';

    const az = (accum.x * 0.28).toFixed(1);
    const el = (-accum.y * 0.18).toFixed(1);

    // Build SVG content
    const rings    = [0.25, 0.5, 0.75, 1].map(f =>
        h('circle', { key:f, cx:CX, cy:CY, r: maxR*f, fill:'none', stroke:'var(--border)', 'stroke-width':0.5 })
    );
    const degTicks = Array.from({ length: 36 }, (_, i) => {
        const a  = i * 10 * Math.PI / 180;
        const r0 = maxR * 0.92, r1 = maxR;
        return h('line', { key:i,
            x1: CX + r0*Math.cos(a), y1: CY + r0*Math.sin(a),
            x2: CX + r1*Math.cos(a), y2: CY + r1*Math.sin(a),
            stroke: i%9===0 ? 'var(--border2)' : 'var(--border)',
            'stroke-width': i%9===0 ? 1.5 : 0.5,
        });
    });

    // Corner brackets
    const bS = 16, bG = 6;
    const corners = [[0,0],[w,0],[w,h],[0,h]].map(([bx,by], i) => {
        const sx = bx === 0 ? 1 : -1, sy = by === 0 ? 1 : -1;
        const pts = `${bx+sx*bG},${by+sy*(bG+bS)} ${bx+sx*bG},${by+sy*bG} ${bx+sx*(bG+bS)},${by+sy*bG}`;
        return h('polyline', { key:i, points:pts, fill:'none', stroke:'var(--border2)', 'stroke-width':1.5 });
    });

    return h('div', { class: `aim-pad${locked ? ' locked' : ''}`, ref: padRef, onClick: handleClick },
        h('svg', { width: w, height: h, style:{ position:'absolute', inset:0 } },
            h('line', { x1:CX, y1:0,  x2:CX, y2:h, stroke:'var(--border)', 'stroke-width':0.5 }),
            h('line', { x1:0,  y1:CY, x2:w,  y2:CY, stroke:'var(--border)', 'stroke-width':0.5 }),
            ...rings, ...degTicks, ...corners,
            // crosshair
            h('line', { x1:vizX-18, y1:vizY, x2:vizX+18, y2:vizY, stroke:color, 'stroke-width':1.5 }),
            h('line', { x1:vizX, y1:vizY-18, x2:vizX, y2:vizY+18, stroke:color, 'stroke-width':1.5 }),
            h('circle', { cx:vizX, cy:vizY, r:14, fill:'none', stroke:color, 'stroke-width':1.5, opacity:0.75 }),
            h('circle', { cx:vizX, cy:vizY, r:2, fill:color }),
        ),
        !locked && h('div', { class:'aim-hint' }, 'CLICK TO AIM'),
        locked  && h('div', { class:'aim-locked-badge' }, 'LOCKED · ESC TO RELEASE'),
        h('div', { class:'aim-coords' },
            `AZ ${az >= 0 ? '+' : ''}${az}°   EL ${el >= 0 ? '+' : ''}${el}°`,
        ),
        h('button', {
            onClick: reset,
            style: {
                position:'absolute', bottom:'10px', right:'14px',
                background:'var(--surf2)', border:'1px solid var(--border2)',
                color:'var(--dim)', fontFamily:'var(--font-ui)', fontSize:'10px',
                fontWeight:'700', letterSpacing:'1.5px', textTransform:'uppercase',
                padding:'3px 10px', cursor:'pointer',
            },
        }, 'RESET'),
    );
}

// ── BarrelMonitor ──────────────────────────────────────────────────────────
// 2 coils, 1 photoresistor each (positioned just ahead of coil)
// coilState: 0=idle 1=charged 2=firing 3=discharged
function BarrelMonitor({ coilStates, sensorStates, lastShot }) {
    const VB_W = 700, VB_H = 110;
    const CY   = 55, TUBE_H = 18, TUBE_Y = CY - TUBE_H / 2;

    const COILS = [
        { cx: 195, w: 90, h: 52 },
        { cx: 455, w: 90, h: 52 },
    ];
    // sensors just ahead of each coil (cx + w/2 + gap)
    const SENSORS = [COILS[0].cx + COILS[0].w / 2 + 22, COILS[1].cx + COILS[1].w / 2 + 22];

    const COIL_COLS   = ['#111a14', 'var(--cyan)', 'var(--red)', '#2a1414'];
    const COIL_STROKE = ['var(--border)', 'var(--cyan)', 'var(--red)', '#4a2020'];
    const COIL_LABELS = ['IDLE', 'CHARGED', 'FIRING', 'DISCHGD'];
    const COIL_LC     = ['var(--dim)', 'var(--cyan)', 'var(--red)', '#6a3030'];

    const coilEls = COILS.map((c, i) => {
        const cs  = coilStates[i];
        const x   = c.cx - c.w / 2;
        const cy2 = CY - c.h / 2;
        const glow = cs === 2 ? `drop-shadow(0 0 7px var(--red))`
            : cs === 1 ? `drop-shadow(0 0 4px var(--cyan))` : 'none';
        // winding lines
        const nLines = 9;
        const windings = Array.from({ length: nLines }, (_, l) => {
            const ly = cy2 + 4 + l * ((c.h - 8) / (nLines - 1));
            return h('line', { key:l, x1:x+2, y1:ly, x2:x+c.w-2, y2:ly,
                stroke: COIL_STROKE[cs], 'stroke-width':0.8, opacity:0.45 });
        });
        return h('g', { key:i },
            h('rect', { x, y:cy2, width:c.w, height:c.h,
                fill: COIL_COLS[cs], stroke: COIL_STROKE[cs], 'stroke-width':1.5,
                style:{ filter: glow } }),
            ...windings,
            h('text', { x:c.cx, y:cy2-6, 'text-anchor':'middle', 'font-size':9,
                    fill: COIL_LC[cs], 'font-family':'var(--font-mono)', 'letter-spacing':1 },
                COIL_LABELS[cs]),
            h('text', { x:c.cx, y:CY+c.h/2+14, 'text-anchor':'middle', 'font-size':9,
                    fill:'var(--dim)', 'font-family':'var(--font-mono)', 'letter-spacing':1 },
                `COIL ${i+1}`),
            lastShot && h('text', { x:c.cx, y:cy2-16, 'text-anchor':'middle', 'font-size':9,
                    fill:'var(--amber)', 'font-family':'var(--font-mono)' },
                `−${i===0 ? lastShot.drain1 : lastShot.drain2}V`),
        );
    });

    const sensorEls = SENSORS.map((sx, i) => {
        const active = sensorStates[i];
        const col    = active ? 'var(--green)' : 'var(--dim)';
        const glow   = active ? 'drop-shadow(0 0 5px var(--green))' : 'none';
        const tipY   = TUBE_Y - 3, baseY = tipY - 17;
        return h('g', { key:i },
            h('polygon', {
                points:`${sx},${tipY} ${sx-7},${baseY} ${sx+7},${baseY}`,
                fill:col, opacity: active ? 1 : 0.35,
                style:{ filter: glow },
            }),
            active && h('circle', { cx:sx, cy:tipY-8, r:9, fill:'none', stroke:'var(--green)', 'stroke-width':1, opacity:0.35 }),
            h('text', { x:sx, y:baseY-4, 'text-anchor':'middle', 'font-size':9,
                fill:col, 'font-family':'var(--font-mono)', 'letter-spacing':1 }, `S${i+1}`),
            lastShot && h('g', null,
                h('text', { x:sx, y:CY+TUBE_H/2+24, 'text-anchor':'middle', 'font-size':9,
                        fill:'var(--cyan)', 'font-family':'var(--font-mono)' },
                    `${i===0 ? lastShot.t1 : lastShot.t2}µs`),
                h('text', { x:sx, y:CY+TUBE_H/2+37, 'text-anchor':'middle', 'font-size':9,
                        fill:'var(--green)', 'font-family':'var(--font-mono)' },
                    `${i===0 ? lastShot.v1 : lastShot.v2}m/s`),
            ),
        );
    });

    const BREECH_X = 30, MUZZLE_X = 668;

    return h('svg', { width:'100%', height:'100%', viewBox:`0 0 ${VB_W} ${VB_H}`, preserveAspectRatio:'xMidYMid meet' },
        // bore tube
        h('rect', { x:BREECH_X, y:TUBE_Y, width:MUZZLE_X-BREECH_X, height:TUBE_H,
            fill:'#080c0e', stroke:'var(--border2)', 'stroke-width':1 }),
        // bore axis
        h('line', { x1:BREECH_X+14, y1:CY, x2:MUZZLE_X, y2:CY,
            stroke:'var(--border)', 'stroke-width':0.5, 'stroke-dasharray':'4 6' }),
        // coils
        ...coilEls,
        // sensors
        ...sensorEls,
        // breech cap
        h('rect', { x:BREECH_X-12, y:TUBE_Y-8, width:14, height:TUBE_H+16,
            fill:'var(--surf2)', stroke:'var(--border2)', 'stroke-width':1.5 }),
        h('text', { x:BREECH_X-5, y:CY+5, 'text-anchor':'middle', 'font-size':8,
            fill:'var(--dim)', 'font-family':'var(--font-mono)',
            transform:`rotate(-90,${BREECH_X-5},${CY})` }, 'BREECH'),
        // muzzle
        h('rect', { x:MUZZLE_X, y:TUBE_Y-6, width:12, height:TUBE_H+12,
            fill:'var(--surf2)', stroke:'var(--border2)', 'stroke-width':1.5 }),
        h('text', { x:MUZZLE_X+6, y:CY+5, 'text-anchor':'middle', 'font-size':8,
            fill:'var(--dim)', 'font-family':'var(--font-mono)',
            transform:`rotate(90,${MUZZLE_X+6},${CY})` }, 'MUZZLE'),
    );
}

// ── LastShotPanel ──────────────────────────────────────────────────────────
function LastShotPanel({ shot }) {
    if (!shot) return h('div', { style:{ color:'var(--dim)', fontSize:'11px', fontFamily:'var(--font-ui)', letterSpacing:'1px' } }, '— no shots fired —');
    const rows = [
        ['C1 INTERVAL', shot.t1 + ' µs',  'var(--cyan)'],
        ['C2 INTERVAL', shot.t2 + ' µs',  'var(--cyan)'],
        ['V STAGE 1',   shot.v1 + ' m/s', 'var(--green)'],
        ['V STAGE 2',   shot.v2 + ' m/s', 'var(--green)'],
        ['C1 DRAIN',    '−' + shot.drain1 + 'V', 'var(--amber)'],
        ['C2 DRAIN',    '−' + shot.drain2 + 'V', 'var(--amber)'],
    ];
    return h('div', { class:'shot-grid' },
        ...rows.flatMap(([k, v, col]) => [
            h('div', { class:'shot-k' }, k),
            h('div', { class:'shot-v', style:{ color:col } }, v),
        ]),
    );
}

// ── ControlPanel (root) ────────────────────────────────────────────────────
function ControlPanel() {
    // ── arm state
    const [masterArm, setMasterArm] = useState(false);
    const [trigArm,   setTrigArm]   = useState(false);
    const [gunArm,    setGunArm]    = useState(false);

    // ── charge
    const [targetV, setTargetV] = useState(200);

    // ── telemetry (simulated)
    const [ping,    setPing]    = useState(12);
    const [heading, setHeading] = useState(38);
    const [pitch,   setPitch]   = useState(8);
    const [m1,      setM1]      = useState({ angle: 24,  vel: 55,  acc: -30 });
    const [m2,      setM2]      = useState({ angle: -12, vel: -38, acc: 18  });
    const [cap1,    setCap1]    = useState({ v: 248, pct: 62 });
    const [cap2,    setCap2]    = useState({ v: 180, pct: 45 });

    // ── shot state
    const [shots,     setShots]     = useState(0);
    const [lastShot,  setLastShot]  = useState(null);
    const [coilState, setCoilState] = useState([1, 1]);   // 0idle 1charged 2firing 3discharged
    const [sensorSt,  setSensorSt]  = useState([false, false]);

    // ── arm gating
    const handleMaster = useCallback(v => {
        setMasterArm(v);
        if (!v) { setTrigArm(false); setGunArm(false); }
    }, []);
    const handleTrig = useCallback(v => { if (masterArm) setTrigArm(v); }, [masterArm]);
    const handleGun  = useCallback(v => { if (masterArm) setGunArm(v);  }, [masterArm]);

    const canFire = trigArm && gunArm;

    // ── fire sequence
    const handleFire = useCallback(() => {
        setShots(s => s + 1);

        // coil / sensor animation sequence
        setCoilState([2, 1]); setSensorSt([false, false]);

        const t1 = 420 + Math.random() * 80;
        const t2 = 390 + Math.random() * 80;

        setTimeout(() => {
            setSensorSt([true, false]);
            setTimeout(() => {
                setCoilState([3, 2]);
                setTimeout(() => {
                    setSensorSt([true, true]);
                    setTimeout(() => {
                        setCoilState([3, 3]);
                        const v1    = (0.15 / (t1 / 1e6)).toFixed(1);
                        const v2    = (0.15 / (t2 / 1e6)).toFixed(1);
                        const d1    = Math.round(40 + Math.random() * 30);
                        const d2    = Math.round(35 + Math.random() * 25);
                        setLastShot({ t1: t1.toFixed(0), t2: t2.toFixed(0), v1, v2, drain1: d1, drain2: d2 });
                        // drain caps
                        setCap1(c => ({ v: Math.max(0, c.v - d1), pct: Math.max(0, c.pct - (d1 / targetV) * 100) }));
                        setCap2(c => ({ v: Math.max(0, c.v - d2), pct: Math.max(0, c.pct - (d2 / targetV) * 100) }));
                        setTimeout(() => { setCoilState([1, 1]); setSensorSt([false, false]); }, 1800);
                    }, 55);
                }, t2 / 1000);
            }, 40);
        }, t1 / 1000);
    }, [targetV]);

    // ── telemetry sim
    useEffect(() => {
        const id = setInterval(() => {
            setHeading(h => ((h + (Math.random() - 0.48) * 0.7) + 360) % 360);
            setPitch  (p => clamp(p + (Math.random() - 0.5) * 0.35, -45, 45));
            setM1(m  => ({
                angle: clamp(m.angle + m.vel * 0.008, -180, 180),
                vel:   clamp(m.vel   + (Math.random() - 0.5) * 3.5, -100, 100),
                acc:   clamp(m.acc   + (Math.random() - 0.5) * 5,   -100, 100),
            }));
            setM2(m  => ({
                angle: clamp(m.angle + m.vel * 0.008, -180, 180),
                vel:   clamp(m.vel   + (Math.random() - 0.5) * 3.5, -100, 100),
                acc:   clamp(m.acc   + (Math.random() - 0.5) * 5,   -100, 100),
            }));
            // slow recharge
            setCap1(c => {
                const nv = Math.min(targetV, c.v + 1.8);
                return { v: nv, pct: (nv / targetV) * 100 };
            });
            setCap2(c => {
                const nv = Math.min(targetV, c.v + 1.8);
                return { v: nv, pct: (nv / targetV) * 100 };
            });
        }, 80);
        return () => clearInterval(id);
    }, [targetV]);

    useEffect(() => {
        const id = setInterval(() => setPing(8 + Math.random() * 22), 1200);
        return () => clearInterval(id);
    }, []);

    // ── clock
    const [uptime, setUptime] = useState('00:00:00');
    const startRef = useRef(Date.now());
    useEffect(() => {
        const id = setInterval(() => {
            const s  = Math.floor((Date.now() - startRef.current) / 1000);
            const hh = String(Math.floor(s / 3600)).padStart(2, '0');
            const mm = String(Math.floor((s % 3600) / 60)).padStart(2, '0');
            const ss = String(s % 60).padStart(2, '0');
            setUptime(`${hh}:${mm}:${ss}`);
        }, 1000);
        return () => clearInterval(id);
    }, []);

    const pingColor = ping < 20 ? 'var(--green)' : ping < 50 ? 'var(--amber)' : 'var(--red)';

    return h('div', { class:'root-grid' },
        h('style', null, `@keyframes blink{0%,100%{opacity:1}50%{opacity:.35}}`),

        // ── Topbar ──────────────────────────────────────────────────────
        h('div', { class:'topbar' },
            h('div', { style:{ width:9, height:9, borderRadius:'50%', flexShrink:0, background:'var(--green)', boxShadow:'0 0 7px var(--green)' } }),
            h('span', { style:{ color:'var(--green)' } }, 'CONNECTED'),
            h('span', { style:{ color: pingColor, marginLeft:2 } }, ping.toFixed(0) + 'ms'),
            h('div',  { style:{ width:1, height:18, background:'var(--border)', margin:'0 4px' } }),
            trigArm && h('div', { class:'arm-tag' }, '⚠ TRIG ARMED'),
            gunArm  && h('div', { class:'arm-tag', style:{ borderColor:'var(--red)', color:'var(--red)', background:'#f03a3a12' } }, '⚠ GUN ARMED'),
            h('div', { style:{ marginLeft:'auto', fontSize:15, fontWeight:900, letterSpacing:5, color:'var(--text)', fontFamily:'var(--font-ui)' } }, 'HARBINGER'),
            h('div', { style:{ fontSize:12, color:'var(--dim)', letterSpacing:2 } }, uptime),
        ),

        // ── Controls column ─────────────────────────────────────────────
        h('div', { class:'controls-col' },

            // Master arm
            h('div', { class:'section' },
                h('div', { class:'section-label' }, 'Interlock'),
                h('div', { class:'toggle-row' },
                    h('span', { class:`toggle-name${masterArm ? ' amber' : ''}` }, 'MASTER ARM'),
                    h(Toggle, { on: masterArm, onChange: handleMaster }),
                ),
            ),

            // Turret + gun arm
            h('div', { class:'section' },
                h('div', { class:'section-label' }, 'Arm'),
                h('div', { class:'toggle-row' },
                    h('span', { class:`toggle-name${trigArm ? ' amber' : ''}` }, 'TURRET'),
                    h(Toggle, { on: trigArm, onChange: handleTrig, disabled: !masterArm }),
                ),
                h('div', { class:'toggle-row' },
                    h('span', { class:`toggle-name${gunArm ? ' red' : ''}` }, 'GUN'),
                    h(Toggle, { on: gunArm, onChange: handleGun, disabled: !masterArm, variant:'red' }),
                ),
            ),

            // Voltage
            h(VoltSlider, { value: targetV, onChange: setTargetV, locked: gunArm }),

            // Fire
            h('div', { class:'section' },
                h('div', { class:'section-label' }, 'Fire Control'),
                h(FireButton, { ready: canFire, onFire: handleFire }),
                h('div', { style:{ display:'flex', justifyContent:'space-between', alignItems:'center' } },
                    h('span', { style:{ fontSize:9, color:'var(--dim)', fontFamily:'var(--font-ui)', letterSpacing:'1.5px', textTransform:'uppercase' } }, 'Hold 600ms'),
                    h('span', { style:{ fontSize:11, color:'var(--dim)', fontFamily:'var(--font-mono)' } }, `shots: ${shots}`),
                ),
            ),

            h('div', { style:{ flex:1 } }),  // spacer
        ),

        // ── Main area ────────────────────────────────────────────────────
        h('div', { class:'main-area' },

            h('div', { class:'aim-panel' },
                h(AimPad),
            ),

            h('div', { class:'barrel-panel' },
                h('div', { class:'panel-label' }, 'BARREL MONITOR'),
                h('div', { style:{ flex:1, padding:'8px 20px', display:'flex', alignItems:'center' } },
                    h(BarrelMonitor, { coilStates: coilState, sensorStates: sensorSt, lastShot }),
                ),
            ),
        ),

        // ── Telemetry strip ──────────────────────────────────────────────
        h('div', { class:'telem-strip' },

            h('div', { class:'telem-cell', style:{ width:162 } },
                h('div', { class:'telem-cell-label' }, 'Heading'),
                h(Compass, { heading }),
            ),

            h('div', { class:'telem-cell', style:{ width:148 } },
                h('div', { class:'telem-cell-label' }, 'Pitch'),
                h(PitchView, { pitch }),
            ),

            h('div', { class:'telem-cell', style:{ width:124 } },
                h('div', { class:'telem-cell-label' }, 'Motor A'),
                h('div', { style:{ fontSize:26, fontWeight:700, fontFamily:'var(--font-mono)', lineHeight:1 } }, sign(m1.angle) + '°'),
                h('div', { style:{ display:'flex', gap:10, marginTop:4 } },
                    h(BiBar, { value: m1.vel, name:'vel' }),
                    h(BiBar, { value: m1.acc, name:'acc' }),
                ),
            ),

            h('div', { class:'telem-cell', style:{ width:124 } },
                h('div', { class:'telem-cell-label' }, 'Motor B'),
                h('div', { style:{ fontSize:26, fontWeight:700, fontFamily:'var(--font-mono)', lineHeight:1 } }, sign(m2.angle) + '°'),
                h('div', { style:{ display:'flex', gap:10, marginTop:4 } },
                    h(BiBar, { value: m2.vel, name:'vel' }),
                    h(BiBar, { value: m2.acc, name:'acc' }),
                ),
            ),

            h('div', { class:'telem-cell', style:{ width:130 } },
                h('div', { class:'telem-cell-label' }, 'Cap Bank'),
                h('div', { style:{ display:'flex', gap:10, height:'100%', paddingBottom:4 } },
                    h(CapBar, { voltage: cap1.v, pct: cap1.pct, targetV, name:'C1' }),
                    h(CapBar, { voltage: cap2.v, pct: cap2.pct, targetV, name:'C2' }),
                ),
            ),

            h('div', { class:'telem-cell' },
                h('div', { class:'telem-cell-label' }, 'Last Shot'),
                h(LastShotPanel, { shot: lastShot }),
            ),
        ),
    );
}

// ── Mount ──────────────────────────────────────────────────────────────────
render(h(ControlPanel, null), document.getElementById('app'));