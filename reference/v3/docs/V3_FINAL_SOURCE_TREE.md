# Final Source Tree

```text
NoctiNIRSv3/
├── CMakeLists.txt
├── Kconfig.projbuild
├── sdkconfig.defaults
├── README.md
├── CHANGELOG_NoctiNIRSv3.md
├── NoctiNIRSv3_Validation_Checklist.md
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c
├── components/
│   ├── nocti_v3_common/
│   ├── nocti_v3_board/
│   ├── nocti_v3_timebase/
│   ├── nocti_v3_adc/
│   ├── nocti_v3_led/
│   ├── nocti_v3_optical/
│   ├── nocti_v3_stream/
│   ├── nocti_v3_wifi/
│   └── nocti_v3_health/
├── tools/
│   ├── host_static_check.sh
│   ├── run_host_logic_tests.sh
│   ├── test_stream_compat.c
│   ├── nocti_protocol.py
│   ├── nocti_control.py
│   ├── nocti_udp_capture.py
│   ├── nocti_acceptance_runner.py
│   └── host_stubs/
└── docs/
    ├── V3_ARCHITECTURE_CONTRACT.md
    ├── V3_FINAL_TASK_CORE_AUDIT.md
    ├── V3_FINAL_TIMING_CALCULATION.md
    ├── V3_PACKET_COMPATIBILITY.md
    ├── V3_COMMAND_COMPATIBILITY.md
    ├── V3_FAULT_RECOVERY_TABLE.md
    ├── V3_HEALTH_COUNTERS.md
    ├── V3_ACCEPTANCE_TEST_MATRIX.md
    ├── V3_PROGRESS.md
    ├── V3_BUILD_NOTES.md
    └── phase-specific audit/evidence files
```
