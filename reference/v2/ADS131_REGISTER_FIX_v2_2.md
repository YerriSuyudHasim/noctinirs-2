NoctiNIRS ADS131M04 register-command fix v2.2
================================================
Source base: NOCTINIRSv2(8).zip

Overwrite these TWO files in your current project:

1) components/nocti_fnirs/nocti_fnirs.c
2) main/app_main_fnirs_test.c

Critical fix
------------
ADS131M04 RREG/WREG use a 6-bit register address in command bits [12:7]
and a 7-bit count-minus-one field in bits [6:0]. The old source shifted the
address by 8, so register addresses were doubled. CLOCK address 0x03 was
therefore encoded as CFG address 0x06.

Additional safety checks
------------------------
- Compile-time assertions verify command encodings:
    RREG MODE  = 0xA100
    RREG CLOCK = 0xA180
    WREG MODE  = 0x6100
    WREG CLOCK = 0x6180
    WREG GAIN  = 0x6200
- app_main explicitly writes CFG=0x0600 to restore continuous-conversion mode
  and disable global chop (GC_EN=0).
- Register validation now reads MODE, CLOCK, GAIN, and CFG from their true
  addresses before DRDY-rate validation.
- REGS command now reports CFG as well.

Build procedure
---------------
From the project root:

    idf.py fullclean
    idf.py build
    idf.py flash monitor

Do not reuse the previous build directory or old firmware binary.

Expected startup lines
----------------------
The exact MODE value can include other valid control bits, but these values
must be present:

    ADS131 readback MODE=0x.... CLOCK=0x0F0E GAIN=0x0000 CFG=0x0600
    ADS131 active-read validation: ... rate approximately 4000 SPS ...
    period approximately 250 us ... seq_gap=0 seq_dup=0 crc_bad=0

If CFG still reads 0x0F0E, an old source file or old binary is still being
built/flashed. If CLOCK=0x0F0E and CFG=0x0600 but rate is not near 4000 SPS,
then investigate the physical DRDY/CLKIN path separately.
