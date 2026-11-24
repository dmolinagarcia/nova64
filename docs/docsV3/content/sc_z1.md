# Changelog

| Date     | Version | Changes                                                                               |
|----------|---------|---------------------------------------------------------------------------------------|
|11/24/2026|0.2.6    |Argon settled: optional NVM32 coprocessor; Argon 2 sole noVa128 CPU (D131, D132)       |
|          |         |Argon bus in noVa64: shared pins, independent BE, CPU on its own PHI2 (D133)           |
|          |         |RDY never stops the CPU: every stall holds PHI2                                        |
|          |         |Vision (A2.3, A2.5): backward compatibility within the noVa family                     |
|          |         |Sheet EM8. Source-level debugging. Pending human review                                |
|          |         |Kernel moved to upper memory (F0:FD)                                                   |
|          |         |Sheet CN1. Command line interface. Pending human                                       |
|          |         |Sheet LB1. C library (Calypsi clib + libnova). Pending human review                    |
|          |         |Sheet TP1. Training plan. Pending human review                                         |
|09/23/2026|0.2.5    |Sheet A3. How to read this document human-reviewed                                     |
|          |         |NVM32 added. (VM1-VM4). An emulated 32bit cpu on top of the CPU. Pending human review  |
|          |         |NV1. Future roadmap for noVa. Pending human review                                     |
|          |         |Y4. Filesystem implementation Series. Pending human review                             |
|          |         |Changes in printed version. Colour removed for clarity                                 |
|09/18/2026|0.2.4    |EC ↔ Helium single link: one chip select, header-routed endpoints, frame format        |
|          |         |Boot memory load consolidated into the Debug Agent; `MEM_WRITE` withdrawn              |
|          |         |Development environment deployment procedure published                                 |
|09/10/2026|0.2.3    |AI content for Palette Control and Panel specifications                                |
|          |         |AI content for Phase 0 Spartan-6 training rig added                                    |
|          |         |Prototipe sheet split into tracks                                                      |
|          |         |AI content for the host emulator added as a new Emulation area                         |
|09/07/2026|0.2.2    |Vision and Philosophy human-reviewed                                                   |
|09/05/2026|0.2.1    |AI content for Debug Port and EC boot flash details added                              |
|08/31/2026|0.2      |First public release of the documentation. 100% AI                                     |
