# TODO

## Beta blockers
- [ ] Add wiring diagram and Betaflight example config.
- [ ] Add retry/resend logic in companion web for mid-operation disconnects.
- [ ] Reduce 1-minute web app freeze risk (serial read loop / frame timeout / decode robustness).
- [ ] Document how to reproduce freeze reports and expected logs.

## In progress
- [ ] Core0/Core1 snapshot sync validation: fixed `snapshotSharedDataCopyFrom()` read sequencer; bounded retry against in-flight writes and preserved generator monotonicity. Build verified on `yd_rp2040`.
- [x] Core0/Core1 blocking path: replaced `mutex_enter_blocking` with `mutex_try_enter` + 2ms bounded wait on Core0; Core1 keeps blocking priority. Degraded mode produces safe zeros on timeout. Build verified.
- [ ] CLI build safety fix in `handleCLI()`: replaced `String::contains()` with `String::indexOf() >= 0`.

## Up next
- [ ] Review Core0/Core1 blocking path and choose spin_lock vs optional double-buffer tradeoff.
- [ ] Add parser/transport unit tests with focus on serial snapshot concurrency cases.
- [ ] Add CI builds for firmware + companion-web.

## Backlog / Improvements
- [ ] Batch command `app set map <json>`.
- [ ] ArduinoJson integration in companion-web.
- [ ] Integration tests for Web Serial flow.
