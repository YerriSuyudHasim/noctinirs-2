# ESP-IDF Monitor STATUS input compatibility (#35)

The USB Serial/JTAG console selection in the generated acquisition configuration
already matches V3/V4. The active USB line parser, however, previously dispatched
only on LF. ESP-IDF Monitor's default CR Enter left STATUS pending until timeout.
V4's `reference/v4/main/app_main.c:console_run` accepts both terminators and swallows
the LF after CR. This change adapts that scoped technique to the bounded active
poll parser without introducing another RX owner or changing sdkconfig.

CR and LF now dispatch immediately, and CRLF dispatches once even across polls.
No echo is added. STATUS and SAFE_OFF retain their existing exact commands and
callbacks. Expired/oversized lines are rejected and drained through either ending.
USB diagnostics remain available through the existing application poll path;
this does not diagnose or fix optical fault267 itself.

Accountable: Firmware Developer. Scope: USB parser and host regression only.
Required review: integration/independent Verification. Acceptance: ordinary Enter
can dispatch STATUS, CRLF does not duplicate SAFE_OFF, timeout/overflow recovery
works, and the acquisition firmware links. Host verification: production parser
compiled under MSVC C11 with fake USB input, covering CR, LF, CRLF, split input,
consecutive commands, one SAFE_OFF callback, timeout and overflow recovery; PASS.
Existing Python USB status tests8/8 PASS. Tests open no COM port. Historical319
files verified before/after; references are unchanged. No flash/board operations.

Owner procedure after flashing the rebuilt acquisition image: open VS Code
ESP-IDF Monitor Device on COM11; type STATUS and press Enter within10seconds.
Text is intentionally not echoed. Keep Monitor open, run the existing acquisition
command from another terminal, and issue STATUS again immediately after failure.
Compare boot identity before/after. If no reply, retain monitor startup/error output
and investigate the RX/poll path rather than assuming CRC failure.

Rollback: revert this parser fix. Hardware behavior of USB/driver reset transitions
and normal-Enter response remain untested on target in this task.
