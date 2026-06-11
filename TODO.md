# TODO

## Beta blockers
- [ ] Add wiring diagram and Betaflight example config.
- [ ] Add retry/resend logic in companion web for mid-operation disconnects.
- [ ] Reduce 1-minute web app freeze risk (serial read loop / frame timeout / decode robustness).
- [ ] Document how to reproduce freeze reports and expected logs.

## Follow-ups
- [ ] Batch command `app set map <json>`.
- [ ] Review blocking mutex path on Core 0/Core 1 and evaluate spin_lock/double-buffer tradeoffs.
- [ ] Add CI builds for firmware + companion-web.
- [ ] Add parser/transport unit tests.
