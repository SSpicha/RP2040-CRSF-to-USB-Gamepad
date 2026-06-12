# TODO

## Beta blockers
- [ ] Add wiring diagram and Betaflight example config.
- [ ] Add retry/resend logic in companion web for mid-operation disconnects.
- [ ] Reduce 1-minute web app freeze risk (serial read loop / frame timeout / decode robustness).
- [ ] Document how to reproduce freeze reports and expected logs.

## In progress
- [x] Core0/Core1 snapshot sync validation: fixed `snapshotSharedDataCopyFrom()` read sequencer; bounded retry against in-flight writes and preserved generator monotonicity. Build verified on `yd_rp2040`.
- [x] Core0/Core1 blocking path: replaced `mutex_enter_blocking` with `mutex_try_enter` + 2ms bounded wait on Core0; Core1 keeps blocking priority. Degraded mode produces safe zeros on timeout. Build verified.
- [x] CLI build safety fix in `handleCLI()`: replaced `String::contains()` with `String::indexOf() >= 0`.
- [x] CI workflow: added `.github/workflows/ci.yml` with tests + firmware jobs. Verified locally.
- [x] PR preparation: decided to continue in `feature/companion-app-v2`; `main` has unrelated history and will stay separate for now.

## Up next
- [x] Review Core0/Core1 blocking path and choose spin_lock vs optional double-buffer tradeoff. Decision: keep bounded mutex on Core0, skip double-buffer (low latency path ~2 µs CS, no over-engineering).
- [x] Add parser/transport unit tests with focus on serial snapshot concurrency cases.
- [x] Extend CI with companion-web build (Vite) in addition to firmware build.

## Backlog / Improvements
- [x] Batch command `app set map <json>`.
- [x] ArduinoJson integration in companion-web. Replaced with lightweight runtime validation in `serialService.ts`; no extra deps added.
- [x] Integration tests for Web Serial flow.
