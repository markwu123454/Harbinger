export function createSimClient({ aimRef, targetVRef }) {
    let interval = null;
    let listeners = {};

    // ── Internal server state ─────────────────────────────────────────────
    // This is what the "server" actually holds. The UI never touches this
    // directly — it only sees what gets emitted back.
    let state = {
        heading:    38,
        elevation:  8,
        mA: 23, mB: 15,
        vA: 0,  vB: 0,
        aA: 0,  aB: 0,
        cap1: 62,
        cap2: 45,
        shots: 0,
        master_arm: false,
        trig_arm:   false,
        gun_arm:    false,
        coils:      [1, 1],
        sensors:    [false, false],
        lastShot:   null,
        // Internal: target voltage as confirmed by server
        confirmedV: 80,
        // Lock out concurrent fire sequences
        firing: false,
    };

    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

    // PD gains
    const KP         = 12;   // proportional — stiffness
    const KD         = 4.5;  // derivative   — damping
    const MAX_TORQUE = 320;  // deg/s²

    // Simulated sensor separation (metres) — used for velocity calculation
    const SENSOR_SPACING = 0.05;

    // ── Listener bus ──────────────────────────────────────────────────────
    const on = (type, cb) => {
        listeners[type] = listeners[type] || [];
        listeners[type].push(cb);
    };

    const emit = (msg) => {
        (listeners[msg.type] || []).forEach(cb => cb(msg));
    };

    // Emit the authoritative arm/voltage state back to the UI
    const emitState = () => {
        emit({
            type:        'state',
            master_arm:  state.master_arm,
            trig_arm:    state.trig_arm,
            gun_arm:     state.gun_arm,
            target_v:    state.confirmedV,
            stage_count: 2,
        });
    };

    // Emit cap + coil + sensor telemetry (called from tick and fire sequence)
    const emitTelemetryAux = () => {
        emit({
            type:    'telemetry',
            coils:   state.coils,
            sensors: state.sensors,
            caps:    [state.cap1, state.cap2],
        });
    };

    // ── Simulated round-trip delay ────────────────────────────────────────
    // Randomised 80–200 ms — represents MCU processing + USB/serial latency.
    // An extra jitter spike (~5% chance) simulates occasional bus contention.
    const roundTrip = () => {
        const base  = 80 + Math.random() * 120;
        const spike = Math.random() < 0.05 ? 200 + Math.random() * 300 : 0;
        return base + spike;
    };

    // ── Shortest-arc wrap ─────────────────────────────────────────────────
    const wrapErr = d => d - 360 * Math.round(d / 360);

    // ── Telemetry tick ────────────────────────────────────────────────────
    const start = () => {
        const DT = 0.08; // seconds per tick (matches 80 ms setInterval below)

        interval = setInterval(() => {
            const aim = aimRef.current;

            // Map aim → motor targets
            let aimH = aim.heading;
            if (aimH > 180) aimH -= 360;
            const tgtA = (aimH + aim.elevation) / 2;
            const tgtB = (aimH - aim.elevation) / 2;

            // Nearest-arc targets
            const nearA = state.mA + wrapErr(tgtA - state.mA);
            const nearB = state.mB + wrapErr(tgtB - state.mB);

            // PD — motor A
            const errA  = nearA - state.mA;
            const accA  = clamp(KP * errA - KD * state.vA, -MAX_TORQUE, MAX_TORQUE);
            const vA    = state.vA + accA * DT;
            const mA    = state.mA + vA   * DT;

            // PD — motor B
            const errB  = nearB - state.mB;
            const accB  = clamp(KP * errB - KD * state.vB, -MAX_TORQUE, MAX_TORQUE);
            const vB    = state.vB + accB * DT;
            const mB    = state.mB + vB   * DT;

            // Derive heading / elevation
            let h = ((( mA + mB) % 360) + 360) % 360;
            let e = clamp(mA - mB, -60, 60);

            // Cap recharge — only toward server-confirmed target, not desired
            const tv    = state.confirmedV;
            const rate  = state.gun_arm ? 0.2 : 0.8; // slow recharge when gun armed
            state.cap1  = Math.min(tv, state.cap1 + rate);
            state.cap2  = Math.min(tv, state.cap2 + rate);

            state = { ...state, heading: h, elevation: e, mA, mB, vA, vB, aA: accA, aB: accB };

            emit({
                type:      'telemetry',
                heading:   h,
                elevation: e,
                motor_a:   { angle: mA, vel: vA, acc: accA },
                motor_b:   { angle: mB, vel: vB, acc: accB },
                caps:      [state.cap1, state.cap2],
                coils:     state.coils,
                sensors:   state.sensors,
            });

        }, 80);
    };

    const stop = () => clearInterval(interval);

    // ── Command handler (called by transport layer, acts as fake MCU) ─────
    const send = (type, payload = {}) => {

        // ── aim: instant, no ack needed (position streams back via telemetry)
        if (type === 'aim') {
            // aimRef is already updated by the caller; nothing to do here
            return;
        }

        // ── set_voltage: MCU validates, updates charge target, acks
        if (type === 'set_voltage') {
            const requested = payload.voltage;
            setTimeout(() => {
                // Server clamps to valid range
                state.confirmedV     = clamp(requested, 0, 120);
                targetVRef.current   = state.confirmedV;
                // Emit authoritative state — this is what clears the pending flag in the UI
                emitState();
            }, roundTrip());
            return;
        }

        // ── arm: MCU validates interlock logic, acks with resolved state
        if (type === 'arm') {
            setTimeout(() => {
                let master = payload.master ?? state.master_arm;
                let trig   = payload.trig   ?? state.trig_arm;
                let gun    = payload.gun    ?? state.gun_arm;

                // Interlock: disarming master cascades; trig/gun require master
                if (!master) { trig = false; gun = false; }

                // MCU also refuses gun arm if caps are too low (safety gate)
                if (gun && state.cap1 < 10 && state.cap2 < 10) {
                    gun = false; // silently denied — UI will revert to confirmed=false
                }

                state = { ...state, master_arm: master, trig_arm: trig, gun_arm: gun };
                // Single authoritative state emit — UI clears pending on receipt
                emitState();
            }, roundTrip());
            return;
        }

        // ── fire: immediate coil sequence, no pending state needed
        if (type === 'fire') {
            if (!state.trig_arm || !state.gun_arm || state.firing) return;
            state.firing = true;
            state.shots++;

            // Timing is proportional to cap voltage — higher voltage = shorter pulse needed
            const capV = (state.cap1 + state.cap2) / 2;
            const scaledBase = clamp(600 - capV * 2.5, 250, 700); // ~350µs at 100V, ~600µs at 0V
            const t1 = Math.round(scaledBase + Math.random() * 60);
            const t2 = Math.round(scaledBase * 0.93 + Math.random() * 60); // coil 2 slightly faster

            // ── Sequence: C1 fires ────────────────────────────────────────
            state.coils   = [2, 1];
            state.sensors = [false, false];
            emitTelemetryAux();

            // S1 trips (projectile passes sensor 1)
            setTimeout(() => {
                state.sensors = [true, false];
                emitTelemetryAux();

                // C1 done, C2 fires
                setTimeout(() => {
                    state.coils   = [3, 2];
                    emitTelemetryAux();

                    // S2 trips (projectile passes sensor 2)
                    setTimeout(() => {
                        state.sensors = [true, true];
                        emitTelemetryAux();

                        // Both coils done — compute results
                        setTimeout(() => {
                            state.coils = [3, 3];

                            // Velocity = sensor spacing / transit time
                            const v1 = parseFloat((SENSOR_SPACING / (t1 / 1e6)).toFixed(1));
                            const v2 = parseFloat((SENSOR_SPACING / (t2 / 1e6)).toFixed(1));

                            // Drain scales with cap voltage (higher charge = more drain)
                            const drainScale = clamp(capV / 80, 0.4, 1.2);
                            const d1 = Math.round((8  + Math.random() * 10) * drainScale);
                            const d2 = Math.round((6  + Math.random() * 8)  * drainScale);

                            state.cap1     = Math.max(0, state.cap1 - d1);
                            state.cap2     = Math.max(0, state.cap2 - d2);
                            state.lastShot = {
                                t:     [t1, t2],
                                v:     [v1, v2],
                                drain: [d1, d2],
                            };

                            emitTelemetryAux();
                            emit({ type: 'shot', count: state.shots, data: state.lastShot });

                            // Cool-down: coils back to idle
                            setTimeout(() => {
                                state.coils   = [1, 1];
                                state.sensors = [false, false];
                                state.firing  = false;
                                emitTelemetryAux();
                            }, 1800);

                        }, 55);
                    }, t2 / 1000);
                }, 40);
            }, t1 / 1000);
        }
    };

    return { start, stop, on, send };
}