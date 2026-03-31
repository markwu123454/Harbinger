export function createSimClient({ aimRef, targetVRef }) {
    let interval = null;
    let listeners = {};

    let state = {
        heading: 38,
        elevation: 8,
        mA: 23,
        mB: 15,
        vA: 0,
        vB: 0,
        aA: 0,
        aB: 0,
        cap1: 62,
        cap2: 45,
        shots: 0,
        master_arm: false,
        trig_arm: false,
        gun_arm: false,
        coils: [1, 1],
        sensors: [false, false],
        lastShot: null,
    };

    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

    // PD gains
    const KP = 12;      // proportional — stiffness
    const KD = 4.5;     // derivative  — damping
    const MAX_TORQUE = 320;  // deg/s² clamp (motor saturation)

    const on = (type, cb) => {
        listeners[type] = listeners[type] || [];
        listeners[type].push(cb);
    };

    const emit = (msg) => {
        (listeners[msg.type] || []).forEach(cb => cb(msg));
    };

    const emitFullState = () => {
        emit({
            type: 'state',
            master_arm: state.master_arm,
            trig_arm: state.trig_arm,
            gun_arm: state.gun_arm,
            target_v: targetVRef.current,
            stage_count: 2,
        });
        // Also push coils/sensors as telemetry delta
        emit({
            type: 'telemetry',
            coils: state.coils,
            sensors: state.sensors,
            caps: [state.cap1, state.cap2],
        });
    };

    // shortest-arc delta: wraps diff into -180..180
    const wrapErr = d => d - 360 * Math.round(d / 360);

    const start = () => {
        const DT = 0.08;

        interval = setInterval(() => {
            const aim = aimRef.current;
            const tv  = targetVRef.current;

            // target motor angles from aim command
            //   heading = mA + mB,  elevation = mA - mB
            //   so mA = (heading + elevation) / 2,  mB = (heading - elevation) / 2
            let aimH = aim.heading;
            if (aimH > 180) aimH -= 360;
            const tgtA = (aimH + aim.elevation) / 2;
            const tgtB = (aimH - aim.elevation) / 2;

            // Shortest-arc targets: offset each target so the error
            // relative to current position is in -180..180, then PD
            // drives the short way around.
            const nearA = state.mA + wrapErr(tgtA - state.mA);
            const nearB = state.mB + wrapErr(tgtB - state.mB);

            // ── PD controller for motor A
            const errA = nearA - state.mA;
            const accA = clamp(KP * errA - KD * state.vA, -MAX_TORQUE, MAX_TORQUE);
            let vA = state.vA + accA * DT;
            let mA = state.mA + vA * DT;

            // ── PD controller for motor B
            const errB = nearB - state.mB;
            const accB = clamp(KP * errB - KD * state.vB, -MAX_TORQUE, MAX_TORQUE);
            let vB = state.vB + accB * DT;
            let mB = state.mB + vB * DT;

            // derive turret heading / elevation from motor angles
            let h = mA + mB;
            let e = mA - mB;

            // wrap heading into 0..360 for display only
            h = ((h % 360) + 360) % 360;
            // clamp elevation
            e = clamp(e, -60, 60);

            // cap recharge toward target
            state.cap1 = Math.min(tv, state.cap1 + 0.6);
            state.cap2 = Math.min(tv, state.cap2 + 0.6);

            state = {
                ...state,
                heading: h, elevation: e,
                mA, mB,
                vA, vB,
                aA: accA, aB: accB,
            };

            emit({
                type: 'telemetry',
                heading: h,
                elevation: e,
                motor_a: { angle: mA, vel: vA, acc: accA },
                motor_b: { angle: mB, vel: vB, acc: accB },
                caps: [state.cap1, state.cap2],
                coils: state.coils,
                sensors: state.sensors,
            });

        }, 50);
    };

    const stop = () => clearInterval(interval);

    const send = (type, payload = {}) => {
        if (type === 'aim') {
            // handled via aimRef
        }

        if (type === 'set_voltage') {
            targetVRef.current = payload.voltage;
            emitFullState();
        }

        if (type === 'arm') {
            let master = payload.master ?? state.master_arm;
            let trig   = payload.trig   ?? state.trig_arm;
            let gun    = payload.gun    ?? state.gun_arm;

            // enforce gating: disarming master cascades
            if (!master) { trig = false; gun = false; }
            // trig/gun require master
            if (!master) { trig = false; gun = false; }

            state = { ...state, master_arm: master, trig_arm: trig, gun_arm: gun };
            emitFullState();
        }

        if (type === 'fire') {
            if (!state.trig_arm || !state.gun_arm) return;

            state.shots++;

            const t1 = 420 + Math.random() * 80;
            const t2 = 390 + Math.random() * 80;

            // firing animation sequence
            state.coils = [2, 1];
            state.sensors = [false, false];
            emitFullState();

            setTimeout(() => {
                state.sensors = [true, false];
                emitFullState();
                setTimeout(() => {
                    state.coils = [3, 2];
                    emitFullState();
                    setTimeout(() => {
                        state.sensors = [true, true];
                        emitFullState();
                        setTimeout(() => {
                            state.coils = [3, 3];
                            const v1 = (0.15 / (t1 / 1e6)).toFixed(1);
                            const v2 = (0.15 / (t2 / 1e6)).toFixed(1);
                            const d1 = Math.round(8 + Math.random() * 12);
                            const d2 = Math.round(6 + Math.random() * 10);

                            state.cap1 = Math.max(0, state.cap1 - d1);
                            state.cap2 = Math.max(0, state.cap2 - d2);
                            state.lastShot = {
                                t: [Math.round(t1), Math.round(t2)],
                                v: [parseFloat(v1), parseFloat(v2)],
                                drain: [d1, d2],
                            };

                            emitFullState();
                            emit({
                                type: 'shot',
                                count: state.shots,
                                data: state.lastShot,
                            });

                            setTimeout(() => {
                                state.coils = [1, 1];
                                state.sensors = [false, false];
                                emitFullState();
                            }, 1800);
                        }, 55);
                    }, t2 / 1000);
                }, 40);
            }, t1 / 1000);
        }
    };

    return { start, stop, on, send };
}