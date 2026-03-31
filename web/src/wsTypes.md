# Harbinger WebSocket Messages

Reference only — not used in code.

---

## Shared

```ts
type Motor = {
    angle: number // deg
    vel: number
    acc: number
}
```

```ts
type ShotData = {
    t: number[] // µs — coil interval
    v: number[] // m/s — stage velocity
    drain: number[] // V — Cap voltage drop
}
```

## Client → Server

```ts
// Heartbeat, sent every 2s
type Ping = {
    type: 'ping'
}
```

```ts
// Set turret aim
type Aim = {
    type: 'aim'
    heading: number // deg, 0–360
    elevation: number // deg
}
```

```ts
// Set arming state
// Disarming master should also send turret/gun false
type Arm = {
    type: 'arm'
    master?: boolean
    turret?: boolean
    gun?: boolean
}
```

```ts
// Set capacitor charge target
type SetVoltage = {
    type: 'set_voltage'
    voltage: number // V, 0–120
}
```

```ts
// Only valid when turret + gun armed
type Fire = {
    type: 'fire'
}
```

## Server → Client

```ts
// Heartbeat reply
type Pong = {
    type: 'pong'
}
```

```ts
// Periodic sensor update — all fields optional (partial/delta updates)
type Telemetry = {
    type: 'telemetry'
    heading?: number // deg
    elevation?: number // deg
    motor_a?: Motor
    motor_b?: Motor
    caps?: number[] // V
    coils?: number[] // coil state flags
    sensors?: boolean[]
}
```

```ts
// Authoritative state broadcast (after arm change, reconnect, etc.)
type State = {
    type: 'state'
    master_arm?: boolean
    turret_arm?: boolean
    gun_arm?: boolean
    target_v?: number // V
    stage_count?: number
}
```

```ts
// Sent after a shot completes
type Shot = {
    type: 'shot'
    count: number // total shots
    data?: ShotData
}
```