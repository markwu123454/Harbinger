export function createWsClient(url) {
    let ws;
    let listeners = {};
    let stateListeners = [];

    let ping = 0;
    let lastPingSent = 0;

    const on = (type, cb) => {
        listeners[type] = listeners[type] || [];
        listeners[type].push(cb);
    };

    const onState = (cb) => {
        stateListeners.push(cb);
    };

    const emit = (msg) => {
        (listeners[msg.type] || []).forEach(cb => cb(msg));
    };

    const setConnState = (s) => {
        stateListeners.forEach(cb => cb(s));
    };

    let pingInterval = null;

    const connect = () => {
        setConnState('connecting');

        ws = new WebSocket(url);

        ws.onopen = () => {
            setConnState('connected');

            // simple ping loop
            if (pingInterval) clearInterval(pingInterval);
            pingInterval = setInterval(() => {
                if (ws.readyState !== 1) return;
                lastPingSent = Date.now();
                ws.send(JSON.stringify({ type: 'ping' }));
            }, 2000);
        };

        ws.onmessage = (e) => {
            const msg = JSON.parse(e.data);

            if (msg.type === 'pong') {
                ping = Date.now() - lastPingSent;
                return;
            }

            emit(msg);
        };

        ws.onclose = () => {
            if (pingInterval) { clearInterval(pingInterval); pingInterval = null; }
            setConnState('disconnected');
        };

        ws.onerror = () => {
            setConnState('disconnected');
        };
    };

    const send = (type, payload = {}) => {
        if (ws?.readyState === 1) {
            ws.send(JSON.stringify({ type, ...payload }));
        }
    };

    return {
        connect,
        on,
        onState,
        send,
        getPing: () => ping
    };
}