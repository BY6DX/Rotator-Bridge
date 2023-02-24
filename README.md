# Rotator-Bridge

Bridge with gpredict (with rotctld protocol) and customized surveillance camera rotator

## Goal

- Talk to devices with different response speeds
- Handle device error gracefully

## Design decisions

- `Sink`: Where the rotation control commands goes into
  - `CamPTZ`: implemented control functionalities of **星烁照明智能 3025 云台**
- `Source`: Where the rotation control commands comes from
  - `rotctld`: act as a fake `rotctld` daemon, used by gpredict

The `Source` will forward each of the request it received to `Sink`, as it's written in `cliMain.cpp`, and forward the response to the request made by `Sink`.

### CamPTZ's `smartSink` feature

Without `smartSink`, `CamPTZ` will forward rotation commands or direction queries to the actual rotator each time it gets requested.

But for **星烁照明智能 3025 云台**, each rotation command will shut off the motor and then restart, thus preventing a smooth rotation experience.

With `smartSink`, `CamPTZ` will spawn a separate thread to do actual orientation estimation.

States:
- `TRACKING`: args (dst_azi, dst_ele)
  - Will goto idle if delta in angle exceeds threshold
  - Will refresh if inactivity for >5sec (TODO: inactivity?)
- `IDLE`: args (cur_azi, cur_ele)
  - Will goto `TRACKING` if delta in angle exceeds threshold (TODO: which threshold?)

<!-- 

But with `smartSink`, `CamPTZ` will suppress the rotation command if **all** of the following criteria are met:

1. Requesting for a movement that is same to previous request
2. Previous effective request is in 7 seconds
3. Motor is moving with >5deg/sec past 1 second

-->