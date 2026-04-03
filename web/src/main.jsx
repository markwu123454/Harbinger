import { h, render } from 'preact';
import { useState, useEffect, useRef, useCallback } from 'preact/hooks';
import {useTransport} from "./transport.js";

// ── Constants ──────────────────────────────────────────────────────────────
const MIN_V = 0, MAX_V = 120, DISPLAY_CAP_V = 160;
const CAP_DANGER_V = 50;
const PENDING_TIMEOUT_MS = 2000;

// ── Mode: ?mode=sim | ?mode=ws | auto (try WS, fall back to sim) ──────────
const mode = new URLSearchParams(location.search).get('mode');

// ── Helpers ────────────────────────────────────────────────────────────────
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const sign  = v => (v > 0 ? '+' : '') + Math.round(v);
const pad3  = v => String(Math.round(v)).padStart(3, '0');

function capColor(voltage, targetV) {
    if (voltage > targetV && targetV > 0) return 'var(--red)';
    if (voltage >= CAP_DANGER_V)          return 'var(--amber)';
    return 'var(--green)';
}

// ── usePendingBool ─────────────────────────────────────────────────────────
// Manages a boolean control that can be: confirmed-false | pending | confirmed-true
// Returns [confirmedVal, pendingVal, setPending, clearPending]
// setPending(desired) — starts the timeout clock
// clearPending()      — call when server confirms; set the confirmed value externally
function usePendingBool(confirmedVal) {
    const [pendingVal, setPendingVal] = useState(null); // null = not pending
    const timerRef = useRef(null);

    const clearPending = useCallback(() => {
        if (timerRef.current) { clearTimeout(timerRef.current); timerRef.current = null; }
        setPendingVal(null);
    }, []);

    const setPending = useCallback((desired) => {
        if (timerRef.current) clearTimeout(timerRef.current);
        setPendingVal(desired);
        timerRef.current = setTimeout(() => {
            setPendingVal(null); // revert — confirmed state is already the truth
        }, PENDING_TIMEOUT_MS);
    }, []);

    useEffect(() => () => { if (timerRef.current) clearTimeout(timerRef.current); }, []);

    const isPending = pendingVal !== null;
    // Visual display value: if pending, show the desired value (used for non-toggle display);
    // the toggle itself uses a separate "indeterminate" render path
    const displayVal = isPending ? pendingVal : confirmedVal;

    return { confirmedVal, isPending, pendingDesired: pendingVal, displayVal, setPending, clearPending };
}

// ── usePendingVoltage ──────────────────────────────────────────────────────
// Manages the voltage slider — desired floats freely; confirmed trails behind
function usePendingVoltage(confirmedVal) {
    const [desiredV, setDesiredV] = useState(confirmedVal);
    const [isPending, setIsPending] = useState(false);
    const timerRef = useRef(null);

    // When confirmed changes (server update), sync desired only if not actively dragging
    const draggingRef = useRef(false);
    const prevConfirmed = useRef(confirmedVal);

    const onDragStart = useCallback(() => { draggingRef.current = true; }, []);

    const onDragEnd = useCallback(() => { draggingRef.current = false; }, []);

    const setDesired = useCallback((v) => {
        setDesiredV(v);
        setIsPending(true);
        if (timerRef.current) clearTimeout(timerRef.current);
        timerRef.current = setTimeout(() => {
            setIsPending(false);
            setDesiredV(prevConfirmed.current); // revert to last confirmed
        }, PENDING_TIMEOUT_MS);
    }, []);

    const clearPending = useCallback((newConfirmed) => {
        if (timerRef.current) { clearTimeout(timerRef.current); timerRef.current = null; }
        prevConfirmed.current = newConfirmed;
        setIsPending(false);
        if (!draggingRef.current) setDesiredV(newConfirmed);
    }, []);

    useEffect(() => {
        // If confirmed changes while not pending, keep desired in sync
        if (!isPending && confirmedVal !== prevConfirmed.current) {
            prevConfirmed.current = confirmedVal;
            if (!draggingRef.current) setDesiredV(confirmedVal);
        }
    }, [confirmedVal, isPending]);

    useEffect(() => () => { if (timerRef.current) clearTimeout(timerRef.current); }, []);

    return { desiredV, confirmedV: confirmedVal, isPending, setDesired, clearPending, onDragStart, onDragEnd };
}


// ══════════════════════════════════════════════════════════════════════════
//  UI COMPONENTS
// ══════════════════════════════════════════════════════════════════════════

// ── Toggle ─────────────────────────────────────────────────────────────
// state: 'off' | 'pending' | 'on'
// variant: 'amber' | 'red'
function Toggle({ confirmedOn, isPending, onChange, disabled, variant = 'amber' }) {
    const cls = [
        'toggle',
        disabled  ? 'disabled' : '',
        isPending ? 'pending'  : '',
        !isPending && confirmedOn && variant === 'amber' ? 'on'     : '',
        !isPending && confirmedOn && variant === 'red'   ? 'gun-on' : '',
    ].filter(Boolean).join(' ');

    // While pending: clicking again should cancel / re-send the opposite — up to the caller
    const handleChange = useCallback((e) => {
        if (!disabled && !isPending) onChange(e.target.checked);
    }, [disabled, isPending, onChange]);

    // Visual checked state: confirmed when not pending; desired (opposite of confirmed) when pending
    const visualChecked = isPending ? !confirmedOn : confirmedOn;

    return h('label', { class: cls },
        h('input', {
            type: 'checkbox',
            checked: visualChecked,
            disabled: disabled || isPending,
            onChange: handleChange,
        }),
        h('div', { class: 'toggle-track' }),
        h('div', { class: 'toggle-thumb' }),
        isPending && h('div', { class: 'toggle-pending-pip' }),
    );
}

// ── VoltSlider ─────────────────────────────────────────────────────────
function VoltSlider({ desiredV, confirmedV, onChange, locked, isPending, onDragStart, onDragEnd }) {
    const confirmedPct = ((confirmedV - MIN_V) / (MAX_V - MIN_V)) * 100;

    return h('div', { class: 'section' },
        h('div', { class: 'section-label' }, 'Charge Target'),
        h('div', { class: 'volt-display' },
            h('span', { style: { color: isPending ? 'var(--amber)' : 'var(--text)' } }, desiredV),
            h('span', { class: 'unit' }, 'V'),
            isPending && h('span', {
                style: {
                    fontSize: '11px',
                    color: 'var(--amber)',
                    marginLeft: '8px',
                    fontFamily: 'var(--font-ui)',
                    letterSpacing: '1px',
                    opacity: 0.9,
                }
            }, `(${confirmedV}V ✓)`),
        ),
        h('div', { style: { position: 'relative' } },
            // Ghost tick for confirmed value
            isPending && h('div', {
                style: {
                    position: 'absolute',
                    left: `calc(${confirmedPct}% - 1px)`,
                    top: 0, bottom: 0,
                    width: '2px',
                    background: 'var(--green)',
                    opacity: 0.7,
                    pointerEvents: 'none',
                    zIndex: 2,
                    borderRadius: '1px',
                },
            }),
            h('input', {
                type: 'range',
                class: `volt-slider${isPending ? ' volt-pending' : ''}`,
                min: MIN_V, max: MAX_V, step: 1,
                value: desiredV,
                disabled: locked,
                onMouseDown: onDragStart,
                onTouchStart: onDragStart,
                onMouseUp: onDragEnd,
                onTouchEnd: onDragEnd,
                onInput: e => onChange(parseInt(e.target.value)),
            }),
        ),
        h('div', { style: { display:'flex', justifyContent:'space-between', fontSize:'10px', color:'var(--dim)', fontFamily:'var(--font-ui)', letterSpacing:'1px' } },
            h('span', null, MIN_V + 'V'),
            h('span', null, MAX_V + 'V'),
        ),
        locked && h('div', { class: 'volt-locked' }, 'LOCKED — DISARM GUN TO CHANGE'),
    );
}

// ── FireButton ─────────────────────────────────────────────────────────
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

// ── BiBar ──────────────────────────────────────────────────────────────
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

// ── CapBar ─────────────────────────────────────────────────────────────
function CapBar({ voltage, targetV, name }) {
    const displayV = Math.min(voltage, DISPLAY_CAP_V);
    const fillPct  = (displayV / DISPLAY_CAP_V) * 100;
    const col      = capColor(displayV, targetV);
    const tgtPct   = (targetV / DISPLAY_CAP_V) * 100;

    return h('div', { class: 'cap-bar-wrap' },
        h('div', { class: 'cap-bar-val', style: { color: col } }, Math.round(displayV) + 'V'),
        h('div', { class: 'cap-bar-track' },
            h('div', { class: 'cap-bar-fill', style: { height: fillPct + '%', background: col } }),
            h('div', { class: 'cap-bar-target', style: { bottom: tgtPct + '%' } }),
        ),
        h('div', { class: 'cap-bar-name' }, name),
    );
}

// ── Compass ────────────────────────────────────────────────────────────
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

// ── ElevationView ──────────────────────────────────────────────────────────
function ElevationView({ elevation }) {
    const W = 128, H = 104, mid = H / 2;
    const scale = (mid * 0.78) / 45;
    const hy    = clamp(mid - elevation * scale, 0, H);

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
            sign(elevation) + '°',
        ),
    );
}

// ── AimPad ─────────────────────────────────────────────────────────────
function AimPad({ heading, elevation, trigArm, onAim }) {
    const padRef    = useRef(null);
    const rawRef    = useRef(null);
    const initRef   = useRef(false);
    const [pos, setPos]       = useState({ x: 0, y: 0 });
    const [locked, setLocked] = useState(false);
    const [size,   setSize]   = useState({ w: 0, h: 0 });

    const HDG_MAX = 180, ELV_MAX = 60;

    useEffect(() => {
        const obs = new ResizeObserver(([e]) => {
            setSize({ w: e.contentRect.width, h: e.contentRect.height });
        });
        if (padRef.current) obs.observe(padRef.current);
        return () => obs.disconnect();
    }, []);

    useEffect(() => {
        if (initRef.current || size.w === 0 || size.h === 0) return;
        initRef.current = true;
        const hdgNorm = ((heading % 360) + 360) % 360;
        const hdgCentered = hdgNorm > 180 ? hdgNorm - 360 : hdgNorm;
        const pxPerDegX = (size.w / 2) / HDG_MAX;
        const pxPerDegY = (size.h / 2) / ELV_MAX;
        const initX = hdgCentered * pxPerDegX;
        const initY = -elevation * pxPerDegY;
        rawRef.current = { x: initX, y: initY };
        setPos({ x: initX, y: initY });
    }, [size, heading, elevation]);

    useEffect(() => {
        if (!trigArm && document.pointerLockElement === padRef.current)
            document.exitPointerLock();
    }, [trigArm]);

    useEffect(() => {
        const onMove = e => {
            if (document.pointerLockElement !== padRef.current) return;
            if (!rawRef.current) return;
            const raw = rawRef.current;
            raw.x += e.movementX * 0.38;
            raw.y += e.movementY * 0.38;
            const { h: ch } = size;
            if (ch > 0) {
                const pxPerDeg = (ch / 2) / ELV_MAX;
                const maxPxY = ELV_MAX * pxPerDeg;
                raw.y = clamp(raw.y, -maxPxY, maxPxY);
            }
            setPos({ x: raw.x, y: raw.y });
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
    }, [size]);

    const handleClick = useCallback(() => {
        if (!trigArm) return;
        if (!locked && padRef.current) padRef.current.requestPointerLock();
    }, [locked, trigArm]);

    const { w, h: ht } = size;
    const CX = w / 2, CY = ht / 2;
    const maxR = Math.min(w, ht) * 0.44;
    const color = locked ? 'var(--red)' : 'var(--cyan)';

    let hdgDeg = 0, elvDeg = 0;
    if (w > 0) {
        const pxPerDeg = (w / 2) / HDG_MAX;
        hdgDeg = pos.x / pxPerDeg;
    }
    if (ht > 0) {
        const pxPerDeg = (ht / 2) / ELV_MAX;
        elvDeg = clamp(-(pos.y / pxPerDeg), -ELV_MAX, ELV_MAX);
    }
    const displayHdgDeg = ((hdgDeg % 360 + 540) % 360) - 180;

    useEffect(() => {
        if (onAim) onAim(hdgDeg, elvDeg);
    }, [hdgDeg, elvDeg]);

    const vizX = w  ? ((CX + pos.x) % w  + w)  % w  : CX;
    const vizY = ht ? clamp(CY + pos.y, 0, ht) : CY;

    const hdgStr = (displayHdgDeg >= 0 ? '+' : '') + displayHdgDeg.toFixed(1);
    const elvStr = (elvDeg >= 0 ? '+' : '') + elvDeg.toFixed(1);

    const rings = [0.25, 0.5, 0.75, 1].map(f =>
        h('circle', { key:'r'+f, cx:CX, cy:CY, r: maxR*f, fill:'none', stroke:'var(--border)', 'stroke-width':0.5 })
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

    const bS = 16, bG = 6;
    const corners = [[0,0],[w,0],[w,ht],[0,ht]].map(([bx,by], i) => {
        const sx = bx === 0 ? 1 : -1, sy = by === 0 ? 1 : -1;
        const pts = `${bx+sx*bG},${by+sy*(bG+bS)} ${bx+sx*bG},${by+sy*bG} ${bx+sx*(bG+bS)},${by+sy*bG}`;
        return h('polyline', { key:'c'+i, points:pts, fill:'none', stroke:'var(--border2)', 'stroke-width':1.5 });
    });

    const crosshairAt = (cx, cy, key) => h('g', { key },
        h('line', { x1:cx-18, y1:cy, x2:cx+18, y2:cy, stroke:color, 'stroke-width':1.5 }),
        h('line', { x1:cx, y1:cy-18, x2:cx, y2:cy+18, stroke:color, 'stroke-width':1.5 }),
        h('circle', { cx, cy, r:14, fill:'none', stroke:color, 'stroke-width':1.5, opacity:0.75 }),
        h('circle', { cx, cy, r:2, fill:color }),
    );

    const wrapX = vizX < CX ? vizX + w : vizX - w;

    let actualX = CX, actualY = CY;
    if (w > 0) {
        const hdgNorm = ((heading % 360) + 360) % 360;
        const hdgCentered = hdgNorm > 180 ? hdgNorm - 360 : hdgNorm;
        const pxPerDeg = (w / 2) / HDG_MAX;
        actualX = ((CX + hdgCentered * pxPerDeg) % w + w) % w;
    }
    if (ht > 0) {
        const pxPerDeg = (ht / 2) / ELV_MAX;
        actualY = clamp(CY - elevation * pxPerDeg, 0, ht);
    }
    const actualWrapX = actualX < CX ? actualX + w : actualX - w;

    const blueReticle = (bx, by, key) => {
        const s = 12;
        return h('g', { key, opacity: 0.9 },
            h('polygon', {
                points: `${bx},${by-s} ${bx+s},${by} ${bx},${by+s} ${bx-s},${by}`,
                fill: 'none', stroke: 'var(--blue)', 'stroke-width': 1.8,
            }),
            h('circle', { cx: bx, cy: by, r: 2.5, fill: 'var(--blue)' }),
            h('line', { x1:bx, y1:by-s-5, x2:bx, y2:by-s, stroke:'var(--blue)', 'stroke-width':1.2 }),
            h('line', { x1:bx, y1:by+s, x2:bx, y2:by+s+5, stroke:'var(--blue)', 'stroke-width':1.2 }),
            h('line', { x1:bx-s-5, y1:by, x2:bx-s, y2:by, stroke:'var(--blue)', 'stroke-width':1.2 }),
            h('line', { x1:bx+s, y1:by, x2:bx+s+5, y2:by, stroke:'var(--blue)', 'stroke-width':1.2 }),
        );
    };

    const hintText = !trigArm ? 'TURRET DISARMED' : 'CLICK TO AIM';

    return h('div', { class: `aim-pad${locked ? ' locked' : ''}`, ref: padRef, onClick: handleClick },
        h('svg', { width: w, height: ht, style:{ position:'absolute', inset:0, overflow:'hidden' } },
            h('line', { x1:CX, y1:0,  x2:CX, y2:ht, stroke:'var(--border)', 'stroke-width':0.5 }),
            h('line', { x1:0,  y1:CY, x2:w,  y2:CY, stroke:'var(--border)', 'stroke-width':0.5 }),
            ...rings, ...degTicks, ...corners,
            blueReticle(actualX, actualY, 'bp-main'),
            blueReticle(actualWrapX, actualY, 'bp-wrap'),
            crosshairAt(vizX, vizY, 'ch-main'),
            crosshairAt(wrapX, vizY, 'ch-wrap'),
        ),
        !locked && h('div', { class:'aim-hint' }, hintText),
        locked  && h('div', { class:'aim-locked-badge' }, 'LOCKED · ESC TO RELEASE'),
        h('div', { class:'aim-coords' }, `HDG ${hdgStr}°   ELV ${elvStr}°`),
    );
}

// ── BarrelMonitor ──────────────────────────────────────────────────────
function BarrelMonitor({ coilStates, sensorStates, lastShot }) {
    const VB_W = 700, VB_H = 110;
    const CY   = 55, TUBE_H = 18, TUBE_Y = CY - TUBE_H / 2;

    const COILS = [
        { cx: 195, w: 90, h: 52 },
        { cx: 455, w: 90, h: 52 },
    ];
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
        const nLines = 9;
        const windings = Array.from({ length: nLines }, (_, l) => {
            const ly = cy2 + 4 + l * ((c.h - 8) / (nLines - 1));
            return h('line', { key:l, x1:x+2, y1:ly, x2:x+c.w-2, y2:ly,
                stroke: COIL_STROKE[cs], 'stroke-width':0.8, opacity:0.45 });
        });
        return h('g', { key:'coil-'+i },
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
                `−${lastShot.drain[i]}V`),
        );
    });

    const sensorEls = SENSORS.map((sx, i) => {
        const active = sensorStates[i];
        const col    = active ? 'var(--green)' : 'var(--dim)';
        const glow   = active ? 'drop-shadow(0 0 5px var(--green))' : 'none';
        const tipY   = TUBE_Y - 3, baseY = tipY - 17;
        return h('g', { key:'sen-'+i },
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
                    `${lastShot.t[i]}µs`),
                h('text', { x:sx, y:CY+TUBE_H/2+37, 'text-anchor':'middle', 'font-size':9,
                        fill:'var(--green)', 'font-family':'var(--font-mono)' },
                    `${lastShot.v[i]}m/s`),
            ),
        );
    });

    const BREECH_X = 30, MUZZLE_X = 668;

    return h('svg', { width:'100%', height:'100%', viewBox:`0 0 ${VB_W} ${VB_H}`, preserveAspectRatio:'xMidYMid meet' },
        h('rect', { x:BREECH_X, y:TUBE_Y, width:MUZZLE_X-BREECH_X, height:TUBE_H,
            fill:'#080c0e', stroke:'var(--border2)', 'stroke-width':1 }),
        h('line', { x1:BREECH_X+14, y1:CY, x2:MUZZLE_X, y2:CY,
            stroke:'var(--border)', 'stroke-width':0.5, 'stroke-dasharray':'4 6' }),
        ...coilEls,
        ...sensorEls,
        h('rect', { x:BREECH_X-12, y:TUBE_Y-8, width:14, height:TUBE_H+16,
            fill:'var(--surf2)', stroke:'var(--border2)', 'stroke-width':1.5 }),
        h('text', { x:BREECH_X-5, y:CY+5, 'text-anchor':'middle', 'font-size':8,
            fill:'var(--dim)', 'font-family':'var(--font-mono)',
            transform:`rotate(-90,${BREECH_X-5},${CY})` }, 'BREECH'),
        h('rect', { x:MUZZLE_X, y:TUBE_Y-6, width:12, height:TUBE_H+12,
            fill:'var(--surf2)', stroke:'var(--border2)', 'stroke-width':1.5 }),
        h('text', { x:MUZZLE_X+6, y:CY+5, 'text-anchor':'middle', 'font-size':8,
            fill:'var(--dim)', 'font-family':'var(--font-mono)',
            transform:`rotate(90,${MUZZLE_X+6},${CY})` }, 'MUZZLE'),
    );
}

// ── LastShotPanel ──────────────────────────────────────────────────────
function LastShotPanel({ shot }) {
    if (!shot) return h('div', { style:{ color:'var(--dim)', fontSize:'11px', fontFamily:'var(--font-ui)', letterSpacing:'1px' } }, '— no shots fired —');
    const rows = [
        ['C1 INTERVAL', shot.t[0] + ' µs',  'var(--cyan)'],
        ['C2 INTERVAL', shot.t[1] + ' µs',  'var(--cyan)'],
        ['V STAGE 1',   shot.v[0] + ' m/s', 'var(--green)'],
        ['V STAGE 2',   shot.v[1] + ' m/s', 'var(--green)'],
        ['C1 DRAIN',    '−' + shot.drain[0] + 'V', 'var(--amber)'],
        ['C2 DRAIN',    '−' + shot.drain[1] + 'V', 'var(--amber)'],
    ];
    return h('div', { class:'shot-grid' },
        ...rows.flatMap(([k, v, col]) => [
            h('div', { class:'shot-k' }, k),
            h('div', { class:'shot-v', style:{ color:col } }, v),
        ]),
    );
}


// ══════════════════════════════════════════════════════════════════════════
//  CONTROL PANEL (root)
// ══════════════════════════════════════════════════════════════════════════

function ControlPanel() {
    // ── Confirmed state — written ONLY by server events ───────────────────
    const [masterArm,  setMasterArm]  = useState(false);
    const [trigArm,    setTrigArm]    = useState(false);
    const [gunArm,     setGunArm]     = useState(false);
    const [confirmedV, setConfirmedV] = useState(80);

    // ── Pending state hooks ───────────────────────────────────────────────
    const masterPending = usePendingBool(masterArm);
    const trigPending   = usePendingBool(trigArm);
    const gunPending    = usePendingBool(gunArm);
    const voltPending   = usePendingVoltage(confirmedV);

    // ── Telemetry state ───────────────────────────────────────────────────
    const [heading,   setHeading]   = useState(38);
    const [elevation, setElevation] = useState(8);
    const [m1,        setM1]        = useState({ angle: 23, vel: 0, acc: 0 });
    const [m2,        setM2]        = useState({ angle: 15, vel: 0, acc: 0 });
    const [cap1,      setCap1]      = useState(62);
    const [cap2,      setCap2]      = useState(45);
    const [shots,     setShots]     = useState(0);
    const [lastShot,  setLastShot]  = useState(null);
    const [coilState, setCoilState] = useState([1, 1]);
    const [sensorSt,  setSensorSt]  = useState([false, false]);

    // Refs forwarded to transport
    const aimRef     = useRef({ heading: 38, elevation: 8 });
    const targetVRef = useRef(80);

    // ── Transport ─────────────────────────────────────────────────────────
    const { on, send, connState, ping } = useTransport({ aimRef, targetVRef, mode });

    // ── Server event listeners ────────────────────────────────────────────
    useEffect(() => {
        on('telemetry', msg => {
            if (msg.heading   !== undefined) setHeading(msg.heading);
            if (msg.elevation !== undefined) setElevation(msg.elevation);
            if (msg.caps)    { setCap1(msg.caps[0]); setCap2(msg.caps[1]); }
            if (msg.motor_a)   setM1(msg.motor_a);
            if (msg.motor_b)   setM2(msg.motor_b);
            if (msg.coils)     setCoilState(msg.coils);
            if (msg.sensors)   setSensorSt(msg.sensors);
        });

        on('state', msg => {
            // Server is the only authority — update confirmed values and clear pending
            if (msg.master_arm !== undefined) {
                setMasterArm(msg.master_arm);
                masterPending.clearPending();
            }
            if (msg.trig_arm !== undefined) {
                setTrigArm(msg.trig_arm);
                trigPending.clearPending();
            }
            if (msg.gun_arm !== undefined) {
                setGunArm(msg.gun_arm);
                gunPending.clearPending();
            }
            if (msg.target_v !== undefined) {
                setConfirmedV(msg.target_v);
                voltPending.clearPending(msg.target_v);
                targetVRef.current = msg.target_v;
            }
        });

        on('shot', msg => {
            if (msg.count !== undefined) setShots(msg.count);
            if (msg.data)  setLastShot(msg.data);
        });
    }, [on]);

    // ── Disarm on disconnect — clear pending too ──────────────────────────
    useEffect(() => {
        if (connState !== 'connected') {
            setMasterArm(false); setTrigArm(false); setGunArm(false);
            masterPending.clearPending();
            trigPending.clearPending();
            gunPending.clearPending();
        }
    }, [connState]);

    // ── Command dispatchers — send only, never setState ──────────────────
    const handleAim = useCallback((heading, elevation) => {
        aimRef.current = { heading, elevation };
        send('aim', { heading, elevation });
    }, [send]);

    const handleMaster = useCallback(v => {
        masterPending.setPending(v);
        send('arm', { master: v, ...(v ? {} : { trig: false, gun: false }) });
    }, [send, masterPending]);

    const handleTrig = useCallback(v => {
        trigPending.setPending(v);
        send('arm', { trig: v });
    }, [send, trigPending]);

    const handleGun = useCallback(v => {
        gunPending.setPending(v);
        send('arm', { gun: v });
    }, [send, gunPending]);

    const handleTargetV = useCallback(v => {
        voltPending.setDesired(v);
        targetVRef.current = v;
        send('set_voltage', { voltage: v });
    }, [send, voltPending]);

    const handleFire = useCallback(() => {
        send('fire', {});
    }, [send]);

    // ── Derived display values ────────────────────────────────────────────
    // For arm badges and fire-ready: use confirmed state only — never show
    // armed based solely on a pending request
    const canFire = trigArm && gunArm;

    // For toggle disabled logic: locked if pending or upstream not confirmed
    const masterDisabled = connState !== 'connected' || masterPending.isPending;
    const trigDisabled   = !masterArm || trigPending.isPending;
    const gunDisabled    = !masterArm || gunPending.isPending;

    // ── Clock ─────────────────────────────────────────────────────────────
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

    const connDot = connState === 'connected'
        ? { bg: 'var(--green)', shadow: '0 0 7px var(--green)', label: mode === 'sim' ? 'SIM' : 'CONNECTED' }
        : connState === 'connecting'
            ? { bg: 'var(--amber)', shadow: '0 0 7px var(--amber)', label: 'CONNECTING' }
            : { bg: 'var(--red)',   shadow: '0 0 7px var(--red)',   label: 'DISCONNECTED' };

    return h('div', { class:'root-grid' },

        // ── Topbar ──────────────────────────────────────────────────────
        h('div', { class:'topbar' },
            h('div', { style:{ width:9, height:9, borderRadius:'50%', flexShrink:0, background:connDot.bg, boxShadow:connDot.shadow } }),
            h('span', { style:{ color:connDot.bg } }, connDot.label),
            connState === 'connected' && h('span', { style:{ color: pingColor, marginLeft:2 } }, ping.toFixed(0) + 'ms'),
            h('div',  { style:{ width:1, height:18, background:'var(--border)', margin:'0 4px' } }),
            // Arm badges only reflect confirmed server state
            trigArm && !trigPending.isPending && h('div', { class:'arm-tag' }, '⚠ TRIG ARMED'),
            gunArm  && !gunPending.isPending  && h('div', { class:'arm-tag', style:{ borderColor:'var(--red)', color:'var(--red)', background:'#f03a3a12' } }, '⚠ GUN ARMED'),
            // Pending badges
            (masterPending.isPending || trigPending.isPending || gunPending.isPending) &&
            h('div', { class:'arm-tag', style:{ borderColor:'var(--amber)', color:'var(--amber)', background:'var(--amber-bg)', animation:'pulse-amber 1s ease-in-out infinite' } }, '⟳ AWAITING SERVER'),
            h('div', { style:{ marginLeft:'auto', fontSize:15, fontWeight:900, letterSpacing:5, color:'var(--text)', fontFamily:'var(--font-ui)' } }, 'HARBINGER'),
            h('div', { style:{ fontSize:12, color:'var(--dim)', letterSpacing:2 } }, uptime),
        ),

        // ── Controls column ─────────────────────────────────────────────
        h('div', { class:'controls-col' },

            h('div', { class:'section' },
                h('div', { class:'section-label' }, 'Interlock'),
                h('div', { class:'toggle-row' },
                    h('span', { class:`toggle-name${masterArm ? ' amber' : ''}${masterPending.isPending ? ' pending-text' : ''}` }, 'MASTER ARM'),
                    h(Toggle, {
                        confirmedOn: masterArm,
                        isPending:   masterPending.isPending,
                        onChange:    handleMaster,
                        disabled:    masterDisabled,
                    }),
                ),
            ),

            h('div', { class:'section' },
                h('div', { class:'section-label' }, 'Arm'),
                h('div', { class:'toggle-row' },
                    h('span', { class:`toggle-name${trigArm ? ' amber' : ''}${trigPending.isPending ? ' pending-text' : ''}` }, 'TURRET'),
                    h(Toggle, {
                        confirmedOn: trigArm,
                        isPending:   trigPending.isPending,
                        onChange:    handleTrig,
                        disabled:    trigDisabled,
                    }),
                ),
                h('div', { class:'toggle-row' },
                    h('span', { class:`toggle-name${gunArm ? ' red' : ''}${gunPending.isPending ? ' pending-text' : ''}` }, 'GUN'),
                    h(Toggle, {
                        confirmedOn: gunArm,
                        isPending:   gunPending.isPending,
                        onChange:    handleGun,
                        disabled:    gunDisabled,
                        variant:     'red',
                    }),
                ),
            ),

            h(VoltSlider, {
                desiredV:    voltPending.desiredV,
                confirmedV:  confirmedV,
                onChange:    handleTargetV,
                locked:      gunArm,
                isPending:   voltPending.isPending,
                onDragStart: voltPending.onDragStart,
                onDragEnd:   voltPending.onDragEnd,
            }),

            h('div', { class:'section' },
                h('div', { class:'section-label' }, 'Fire Control'),
                h(FireButton, { ready: canFire, onFire: handleFire }),
                h('div', { style:{ display:'flex', justifyContent:'space-between', alignItems:'center' } },
                    h('span', { style:{ fontSize:10, color:'var(--dim)', fontFamily:'var(--font-ui)', letterSpacing:'1.5px', textTransform:'uppercase' } }, 'Hold 600ms'),
                    h('span', { style:{ fontSize:11, color:'var(--dim)', fontFamily:'var(--font-mono)' } }, `shots: ${shots}`),
                ),
            ),

            h('div', { style:{ flex:1 } }),
        ),

        // ── Main area ────────────────────────────────────────────────────
        h('div', { class:'main-area' },
            h('div', { class:'aim-panel' },
                h(AimPad, { heading, elevation, trigArm, onAim: handleAim }),
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
                h('div', { class:'telem-cell-label' }, 'Elevation'),
                h(ElevationView, { elevation }),
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
                    // Cap bar target tick uses confirmed voltage — not desired
                    h(CapBar, { voltage: cap1, targetV: confirmedV, name:'C1' }),
                    h(CapBar, { voltage: cap2, targetV: confirmedV, name:'C2' }),
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