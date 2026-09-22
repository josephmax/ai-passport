**English** · [简体中文](0003-light-sleep-during-focus.zh_CN.md)

# 0003 — Focus timing stays exact via light sleep; deep sleep only when idle

The product requires "screen-off must not interrupt timing", stacked blocks
up to 125 minutes, and a punctual victory animation and sound at the finish.
Decision: while focus blocks run, allow the screen off and enter **light
sleep** — esp_timer stays exact under light sleep (no dependence on the slow
RTC clock), the expiry timer wakes the chip, and any button wakes it via
GPIO. Timing state (absolute end timestamp, unit count, granted-XP count) is
written to RTC fast memory and mirrored to NVS on every change; after a power
loss the remainder is recomputed from absolute time. Deep sleep is entered
only when no block is running and the screen-off timeout elapses. Rejected:
deep sleep during timing with an RTC-timer wake (the internal 136 kHz RC
drifts by whole percents; minutes of error over 125 minutes, plus a full
display-stack rebuild on wake); keeping the CPU awake with only the backlight
off (an order of magnitude more current, wearing the battery).

Cost: light-sleep current (~hundreds of µA) exceeds deep sleep, but a single
run lasts at most ~two hours, with negligible battery impact.
