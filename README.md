# NootedRed ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/NootInc/NootedRed/main.yml?branch=master&logo=github&style=for-the-badge)

The AMD Vega iGPU support [Lilu](https://github.com/acidanthera/Lilu) (1.6.4+) plugin.

Conflicts with WhateverGreen.

The Source Code of this Original Work is licensed under the `Thou Shalt Not Profit License version 1.0`. See [`LICENSE`](https://github.com/NootInc/NootedRed/blob/master/LICENSE)

Thanks [Acidanthera](https://github.com/Acidanthera) for the AppleBacklight code, framebuffer init zero-fill fix, and UnfairGVA patches in [WhateverGreen](https://github.com/Acidanthera/WhateverGreen).

## Boot arguments

|  Boot Argument  |                              Description                              |
|:---------------:|:---------------------------------------------------------------------:|
| `-nredfbonly`   | Boot in FB-only macOS, without acceleration. May have visual glitches |
| `-nreddmlogger` | Enable Display Core debugging output                                  |
| `-nreddbg`      | Enable debugging output from kext and FB                              |
| `-nredoff`      | Disable NootedRed                                                     |

## Prerequisites

- Increase VRAM size, otherwise the device will fail to initialise. 512MiB VRAM minimum. 1GiB or more for proper functionality
- Disable Legacy Boot aka CSM, otherwise you will get a panic saying "Failed to get VBIOS from VRAM"

## Recommendations

- From this repository, add [`SSDT-PNLF.aml`](Assets/SSDT-PNLF.aml) and [`SSDT-ALS0.aml`](Assets/SSDT-ALS0.aml) if you have no Ambient Light Sensor, along with [`SMCLightSensor.kext`](https://github.com/Acidanthera/VirtualSMC) for backlight functionality. Usually only works on laptops. Add [`BrightnessKeys.kext`](https://github.com/Acidanthera/BrightnessKeys) for brightness control from the keyboard
- Use `MacBookPro16,3`, `MacBookPro16,4` or `MacPro7,1` SMBIOS
- Add our custom [`AGPMInjector.kext`](Assets/AGPMInjector.kext.zip) from this repository. Has definitions only for `MacBookPro16,3`, `MacBookPro16,4` and `MacPro7,1` SMBIOS
- Update to latest macOS 11 (Big Sur) version

## FAQ

### Can I have an AMD dGPU installed on the system?

We are mixing AMDRadeonX5000 for GCN 5, AMDRadeonX6000 for VCN, and AMDRadeonX6000Framebuffer for DCN, so your system must not have a GCN 5 or RDNA AMD dGPU, as this kext will conflict with them.

### How functional is the kext?

This project is under active research and development; You may face crashes here and there, and full support for Renoir-based iGPUs (Like Cezanne, Lucienne, etc.) is a work in progress.

Renoir (Ryzen 4XXX series and newer) doesn't have graphics acceleration working yet (see [`Issue #11`](https://github.com/NootInc/NootedRed/issues/11) for details) but you can achieve "partial" acceleration by adding `-nredfbonly` to your boot-args.

The kext is mostly fully functional on Picasso/Raven/Raven2-based iGPUs (Ryzen 3XXX series and older).

See repository issues for more information.

### On which macOS versions am I able to use this on?

Due to the complexity and secrecy of the Metal drivers, adding support for non-existent logic is basically impossible. In addition, the required logic for our iGPUs has been purged from the AMD kexts since Monterey. This cannot be resolved without breaking macOS' integrity, and potentially even stability.

Injecting the GPU kexts is not possible during the OpenCore injection stage; the prelink stage fails for kexts of this type since their dependencies aren't contained in the Boot Kext Collection, where OpenCore injects kexts to, they're in the System Kext Collection.

In conclusion, this kext is (currently) exclusive to macOS 11 (Big Sur) since there are too many incompatibilities with other major macOS versions.
