# Real ESP-IDF size fixture

`size.json` is unedited `python -m esp_idf_size --format json2` output from the
actual local ESP32-S3 scaffold build of main
`4a21171353b72ceff62774ba114705e0c8be2c31`. SDK v6.1
`fff9895c82d744c7237be8847347bdd1b07c6643`; GCC esp-15.2.0_20251204 / 15.2.0;
esp-idf-size 2.3.2; Windows x86_64/Python 3.14.6. Both map and project ELF were
available to the size tool. Report SHA-256:
`a5641a6ce559b08deb6eded739b089dad677e31e247c6a054413c11743898a33`.

The unit tests deliberately synthesize provenance/hypothetical deltas and damage
copies of this fixture. Their hashes/timestamps do not claim to describe real
firmware builds. Full measured provenance belongs in the baseline record and
retained CI artifact, linked from `docs/M1_ISSUE12_MEMORY_DIAGNOSIS.md`.
