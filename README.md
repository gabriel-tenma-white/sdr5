# SDR based comms experimentation platform

**zynq_som_2** - a simple SoM with Zynq-7010, 32 bit DDR3, and 40 GPIOs. Used to verify layout design of Zynq and DDR3.

![zynq_som_2](zynq_som_2.png)
* 4 layer board
* 5cm x 5cm
* prototyped and verified


**sdr5_2** - a Zynq + AD9363 SDR platform, layout based on zynq_som_2.
* awaiting prototype

* Differences compared to ADALM-PLUTO:
  * Full 32 bits of the DDR3 are routed (for 2x memory bandwidth and 1GB memory)
  * Support for Zynq-7020 and Zynq-7010 in 400pin BGA package
  * Integrated RF frontends (LNA, PA, SAW filter) for two ISM bands (860/915MHz and 2450MHz)
  * Shielding
  * 4 layer board to reduce cost
  * SD slot for running real Linux distros

---
To open schematics, it is necessary to add all gEDA symbols here to your symbol library: https://github.com/gabriel-tenma-white/sym

To edit PCB layouts, make sure "packages" is a symlink to a cloned repository of: https://github.com/gabriel-tenma-white/packages2
