// Harbinger transport over Bluetooth Classic SPP via Web Serial API.
//
// Classic BT SPP devices pair with the OS and appear as a virtual COM port.
// The Web Serial API provides access to that port from the browser.
// connect() triggers the browser's port-picker — must be called from a user gesture.

import { MSG, PAYLOAD_SIZE, encode, decode } from './proto.js';

export function createBtSerialClient() {
    let port   = null;
    let writer = null;
    let reader = null;
    let running = false;

    const listeners     = {};
    const stateListeners = [];
    let ping         = 0;
    let lastPingSent = 0;
    let pingInterval = null;

    // ── Event bus ──────────────────────────────────────────────
    const on       = (type, cb) => { listeners[type] = listeners[type] || []; listeners[type].push(cb); };
    const onState  = (cb) => stateListeners.push(cb);
    const emit     = (msg) => (listeners[msg.type] || []).forEach(cb => cb(msg));
    const setState = (s)   => stateListeners.forEach(cb => cb(s));

    // ── Connect ────────────────────────────────────────────────
    const connect = async () => {
        setState('connecting');
        try {
            port = await navigator.serial.requestPort();
            await port.open({ baudRate: 115200 });
            writer  = port.writable.getWriter();
            reader  = port.readable.getReader();
            running = true;
            setState('connected');

            pingInterval = setInterval(() => {
                if (!running) return;
                lastPingSent = Date.now();
                sendRaw(encode('ping'));
            }, 2000);

            readLoop();
        } catch {
            setState('disconnected');
        }
    };

    // ── RX byte-stream parser ──────────────────────────────────
    // All host→device messages have fixed payload sizes; MSG.SHOT (device→host)
    // is self-describing via a stage_count byte after the fixed base header.
    const readLoop = async () => {
        let rxType     = null;
        let rxExpected = 0;
        let rxPayload  = new Uint8Array(256);
        let rxGot      = 0;
        let shotPending = false; // true while reading stage data after SHOT base header

        try {
            while (running) {
                const { value, done } = await reader.read();
                if (done) break;

                for (const b of value) {
                    if (rxType === null) {
                        // Waiting for type byte
                        const fixedSize = PAYLOAD_SIZE[b];

                        if (b === MSG.SHOT) {
                            rxType      = b;
                            rxExpected  = 5; // uint32_t total_shots + uint8_t stage_count
                            rxGot       = 0;
                            shotPending = true;
                        } else if (fixedSize === undefined) {
                            // unknown type — skip and resync
                        } else if (fixedSize === 0) {
                            // zero-payload message: consume immediately
                            if (b === MSG.PONG) {
                                ping = Date.now() - lastPingSent;
                            } else {
                                const msg = decode(b, null);
                                if (msg) emit(msg);
                            }
                        } else {
                            rxType     = b;
                            rxExpected = fixedSize;
                            rxGot      = 0;
                            shotPending = false;
                        }
                    } else {
                        rxPayload[rxGot++] = b;

                        if (shotPending && rxGot === 5) {
                            // Read SHOT base header — determine how many stage bytes follow
                            const stageCount = rxPayload[4];
                            if (stageCount === 0) {
                                emit(decode(MSG.SHOT, rxPayload.slice(0, 5)));
                                rxType = null;
                                continue;
                            }
                            rxExpected  = 5 + stageCount * 12;
                            shotPending = false;
                        }

                        if (rxGot >= rxExpected) {
                            const msg = decode(rxType, rxPayload.slice(0, rxExpected));
                            if (msg) emit(msg);
                            rxType = null;
                        }
                    }
                }
            }
        } catch { /* port closed or stream error */ }

        setState('disconnected');
        if (pingInterval) { clearInterval(pingInterval); pingInterval = null; }
    };

    // ── TX ─────────────────────────────────────────────────────
    // Pack type + payload into one write to avoid framing gaps.
    const sendRaw = async (buf) => {
        if (!writer || !buf) return;
        try { await writer.write(new Uint8Array(buf)); } catch { /* ignore if port closed */ }
    };

    // Public send — mirrors wsClient.js interface (type string + plain object).
    const send = (type, data = {}) => {
        const buf = encode(type, data);
        if (buf) sendRaw(buf);
    };

    // ── Disconnect ─────────────────────────────────────────────
    const stop = async () => {
        running = false;
        if (pingInterval) { clearInterval(pingInterval); pingInterval = null; }
        try { await reader?.cancel(); }   catch {}
        try { writer?.releaseLock(); }    catch {}
        try { reader?.releaseLock(); }    catch {}
        try { await port?.close(); }      catch {}
    };

    return { connect, on, onState, send, getPing: () => ping, stop };
}
