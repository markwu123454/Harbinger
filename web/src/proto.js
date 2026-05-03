// Binary protocol codec for Harbinger Bluetooth Serial transport.
// Mirrors include/proto.h — keep both in sync.
//
// Frame format: [TYPE: u8] [PAYLOAD: N bytes determined by type]
// All multi-byte values are little-endian.

// ── Message type IDs ──────────────────────────────────────────
export const MSG = Object.freeze({
    // Host → Device
    PING:        0x01,
    AIM:         0x02,
    ARM:         0x03,
    SET_VOLTAGE: 0x04,
    FIRE:        0x05,
    // Device → Host
    PONG:        0x81,
    STATE:       0x82,
    TELEMETRY:   0x83,
    SHOT:        0x84,
});

// Fixed payload sizes (bytes, excluding type byte).
// MSG.SHOT is omitted — it is self-describing (variable length).
export const PAYLOAD_SIZE = Object.freeze({
    [MSG.PING]:        0,
    [MSG.AIM]:         8,
    [MSG.ARM]:         1,
    [MSG.SET_VOLTAGE]: 4,
    [MSG.FIRE]:        0,
    [MSG.PONG]:        0,
    [MSG.STATE]:       5,
    [MSG.TELEMETRY]:   24,
});

// ── ARM flags ─────────────────────────────────────────────────
// 2 bits per field: 0b00=no-change, 0b01=false, 0b10=true
const ARM_NO_CHANGE = 0x00;
const ARM_FALSE     = 0x01;
const ARM_TRUE      = 0x02;

function armEncode(v) {
    if (v === undefined || v === null) return ARM_NO_CHANGE;
    return v ? ARM_TRUE : ARM_FALSE;
}

// ── Encode: host → device ─────────────────────────────────────
// Returns an ArrayBuffer ready to write to the serial port.
export function encode(type, data = {}) {
    switch (type) {
    case 'ping': {
        return new Uint8Array([MSG.PING]).buffer;
    }
    case 'fire': {
        return new Uint8Array([MSG.FIRE]).buffer;
    }
    case 'aim': {
        const buf = new ArrayBuffer(9);
        const v = new DataView(buf);
        v.setUint8(0, MSG.AIM);
        v.setFloat32(1, data.heading,   true);
        v.setFloat32(5, data.elevation, true);
        return buf;
    }
    case 'arm': {
        const flags = armEncode(data.master)
                    | (armEncode(data.turret) << 2)
                    | (armEncode(data.gun)    << 4);
        return new Uint8Array([MSG.ARM, flags]).buffer;
    }
    case 'set_voltage': {
        const buf = new ArrayBuffer(5);
        const v = new DataView(buf);
        v.setUint8(0, MSG.SET_VOLTAGE);
        v.setFloat32(1, data.voltage, true);
        return buf;
    }
    default:
        return null;
    }
}

// ── Decode: device → host ─────────────────────────────────────
// payload is a Uint8Array of exactly PAYLOAD_SIZE[typeId] bytes,
// or for MSG.SHOT the full variable payload (header + stage data).
// Returns a plain object matching the existing message shape used by the UI,
// or null for unknown types.
export function decode(typeId, payload) {
    const v = payload ? new DataView(payload.buffer, payload.byteOffset, payload.byteLength) : null;

    switch (typeId) {
    case MSG.PONG:
        return { type: 'pong' };

    case MSG.STATE:
        return {
            type:       'state',
            master_arm: !!(payload[0] & 0x01),
            turret_arm: !!(payload[0] & 0x02),
            gun_arm:    !!(payload[0] & 0x04),
            target_v:   v.getFloat32(1, true),
        };

    case MSG.TELEMETRY:
        return {
            type:      'telemetry',
            heading:   v.getFloat32(0,  true),
            elevation: v.getFloat32(4,  true),
            motor_a: {
                vel: v.getFloat32(8,  true),
                acc: v.getFloat32(12, true),
            },
            motor_b: {
                vel: v.getFloat32(16, true),
                acc: v.getFloat32(20, true),
            },
        };

    case MSG.SHOT: {
        // [uint32_t total_shots][uint8_t stage_count][{uint32_t t_us, float v_mps, float drain_v}×count]
        const totalShots = v.getUint32(0, true);
        const stageCount = payload[4];
        const stages = [];
        for (let i = 0; i < stageCount; i++) {
            const off = 5 + i * 12;
            stages.push({
                t_us:    v.getUint32(off,     true),
                v_mps:   v.getFloat32(off + 4, true),
                drain_v: v.getFloat32(off + 8, true),
            });
        }
        return {
            type:  'shot',
            count: totalShots,
            data:  { stages },
        };
    }

    default:
        return null;
    }
}
