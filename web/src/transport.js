import { createSimClient }      from './simClient.js';
import { createWsClient }       from './wsClient.js';
import { createBtSerialClient } from './btSerialClient.js';
import { useCallback, useEffect, useState } from "preact/hooks";

export function useTransport({ aimRef, targetVRef, mode }) {
    const [client, setClient] = useState(null);

    const [connState, setConnState] = useState('connecting');
    const [ping, setPing] = useState(0);

    useEffect(() => {
        let c;
        let pingInterval;

        if (mode === 'sim') {
            c = createSimClient({ aimRef, targetVRef });
            c.start();
            setConnState('connected');
            setPing(0);
        } else if (mode === 'btserial') {
            c = createBtSerialClient();
            c.onState?.(setConnState);
            // connect() shows the browser port-picker — requires a prior user gesture.
            c.connect().catch(() => setConnState('disconnected'));
            pingInterval = setInterval(() => setPing(c.getPing()), 500);
        } else {
            // default: WebSocket
            c = createWsClient(`ws://${location.host}/ws`);
            c.connect();
            c.onState?.(setConnState);
            pingInterval = setInterval(() => setPing(c.getPing()), 500);
        }

        setClient(c);

        return () => {
            c?.stop?.();
            if (pingInterval) clearInterval(pingInterval);
        };
    }, [mode]);

    const on = useCallback((type, cb) => {
        client?.on(type, cb);
    }, [client]);

    const send = useCallback((type, payload) => {
        client?.send(type, payload);
    }, [client]);

    return { on, send, connState, ping };
}
