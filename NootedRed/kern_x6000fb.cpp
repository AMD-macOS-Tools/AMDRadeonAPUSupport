//  Copyright © 2022-2023 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.0. See LICENSE for
//  details.

#include "kern_x6000fb.hpp"
#include "kern_nred.hpp"
#include "kern_patcherplus.hpp"
#include "kern_patches.hpp"
#include "kern_patterns.hpp"
#include <Headers/kern_api.hpp>

static const char *pathRadeonX6000Framebuffer =
    "/System/Library/Extensions/AMDRadeonX6000Framebuffer.kext/Contents/MacOS/AMDRadeonX6000Framebuffer";

static KernelPatcher::KextInfo kextRadeonX6000Framebuffer {"com.apple.kext.AMDRadeonX6000Framebuffer",
    &pathRadeonX6000Framebuffer, 1, {}, {}, KernelPatcher::KextInfo::Unloaded};

X6000FB *X6000FB::callback = nullptr;

void X6000FB::init() {
    callback = this;
    lilu.onKextLoadForce(&kextRadeonX6000Framebuffer);
}

bool X6000FB::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (kextRadeonX6000Framebuffer.loadIndex == index) {
        NRed::callback->setRMMIOIfNecessary();

        CailAsicCapEntry *orgAsicCapsTable = nullptr;

        SolveRequestPlus solveRequests[] = {
            {"__ZL20CAIL_ASIC_CAPS_TABLE", orgAsicCapsTable, kCailAsicCapsTablePattern},
            {"_dce_driver_set_backlight", this->orgDceDriverSetBacklight, kDceDriverSetBacklight},
        };
        PANIC_COND(!SolveRequestPlus::solveAll(&patcher, index, solveRequests, address, size), "x6000fb",
            "Failed to resolve symbols");

        RouteRequestPlus requests[] = {
            {"__ZNK15AmdAtomVramInfo16populateVramInfoER16AtomFirmwareInfo", wrapPopulateVramInfo,
                kPopulateVramInfoPattern},
            {"__ZNK32AMDRadeonX6000_AmdAsicInfoNavi1027getEnumeratedRevisionNumberEv", wrapGetEnumeratedRevision},
            {"_dce_panel_cntl_hw_init", wrapDcePanelCntlHwInit, this->orgDcePanelCntlHwInit,
                kDcePanelCntlHwInitPattern},
            {"__ZN35AMDRadeonX6000_AmdRadeonFramebuffer25setAttributeForConnectionEijm", wrapFramebufferSetAttribute,
                this->orgFramebufferSetAttribute},
            {"__ZN35AMDRadeonX6000_AmdRadeonFramebuffer25getAttributeForConnectionEijPm", wrapFramebufferGetAttribute,
                this->orgFramebufferGetAttribute},
            {"__ZNK22AmdAtomObjectInfo_V1_421getNumberOfConnectorsEv", wrapGetNumberOfConnectors,
                this->orgGetNumberOfConnectors, kGetNumberOfConnectorsPattern, kGetNumberOfConnectorsMask},
            {"_IH_4_0_IVRing_InitHardware", wrapIH40IVRingInitHardware, this->orgIH40IVRingInitHardware,
                kIH40IVRingInitHardwarePattern, kIH40IVRingInitHardwareMask},
            {"_IRQMGR_WriteRegister", wrapIRQMGRWriteRegister, this->orgIRQMGRWriteRegister,
                kIRQMGRWriteRegisterPattern},
        };
        PANIC_COND(!RouteRequestPlus::routeAll(patcher, index, requests, address, size), "x6000fb",
            "Failed to route symbols");

        LookupPatchPlus const patches[] = {
            {&kextRadeonX6000Framebuffer, kPopulateDeviceInfoOriginal, kPopulateDeviceInfoPatched, 1},
            {&kextRadeonX6000Framebuffer, kAmdAtomVramInfoNullCheckOriginal, kAmdAtomVramInfoNullCheckPatched, 1},
            {&kextRadeonX6000Framebuffer, kAmdAtomPspDirectoryNullCheckOriginal, kAmdAtomPspDirectoryNullCheckPatched,
                1},
            {&kextRadeonX6000Framebuffer, kGetFirmwareInfoNullCheckOriginal, kGetFirmwareInfoNullCheckPatched, 1},
            {&kextRadeonX6000Framebuffer, kAgdcServicesGetVendorInfoOriginal, kAgdcServicesGetVendorInfoMask,
                kAgdcServicesGetVendorInfoPatched, kAgdcServicesGetVendorInfoMask, 1},
        };
        PANIC_COND(!LookupPatchPlus::applyAll(&patcher, patches, address, size), "x6000fb",
            "Failed to apply patches: %d", patcher.getError());

        PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "x5000",
            "Failed to enable kernel writing");
        *orgAsicCapsTable = {
            .familyId = AMDGPU_FAMILY_RAVEN,
            .caps = NRed::callback->chipType < ChipType::Renoir ? ddiCapsRaven : ddiCapsRenoir,
            .deviceId = NRed::callback->deviceId,
            .revision = NRed::callback->revision,
            .extRevision = NRed::callback->extRevision,
            .pciRevision = NRed::callback->pciRevision,
        };
        MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
        DBGLOG("x6000fb", "Applied DDI Caps patches");

        return true;
    }

    return false;
}

uint16_t X6000FB::wrapGetEnumeratedRevision() { return NRed::callback->extRevision - NRed::callback->revision; }

IOReturn X6000FB::wrapPopulateVramInfo(void *, void *fwInfo) {
    uint32_t channelCount = 1;
    auto *table = NRed::callback->getVBIOSDataTable<IgpSystemInfo>(0x1E);
    uint8_t memoryType = 0;
    if (table) {
        DBGLOG("x6000fb", "Fetching VRAM info from iGPU System Info");
        switch (table->header.formatRev) {
            case 1:
                switch (table->header.contentRev) {
                    case 11:
                        [[fallthrough]];
                    case 12:
                        if (table->infoV11.umaChannelCount) { channelCount = table->infoV11.umaChannelCount; }
                        memoryType = table->infoV11.memoryType;
                        break;
                    default:
                        DBGLOG("x6000fb", "Unsupported contentRev %d", table->header.contentRev);
                        break;
                }
                break;
            case 2:
                switch (table->header.contentRev) {
                    case 1:
                        [[fallthrough]];
                    case 2:
                        if (table->infoV2.umaChannelCount) { channelCount = table->infoV2.umaChannelCount; }
                        memoryType = table->infoV2.memoryType;
                        break;
                    default:
                        DBGLOG("x6000fb", "Unsupported contentRev %d", table->header.contentRev);
                        break;
                }
                break;
            default:
                DBGLOG("x6000fb", "Unsupported formatRev %d", table->header.formatRev);
                break;
        }
    } else {
        DBGLOG("x6000fb", "No iGPU System Info in Master Data Table");
    }
    auto &videoMemoryType = getMember<uint32_t>(fwInfo, 0x1C);
    switch (memoryType) {
        case kDDR2MemType:
            [[fallthrough]];
        case kDDR2FBDIMMMemType:
            [[fallthrough]];
        case kLPDDR2MemType:
            videoMemoryType = kVideoMemoryTypeDDR2;
            break;
        case kDDR3MemType:
            [[fallthrough]];
        case kLPDDR3MemType:
            videoMemoryType = kVideoMemoryTypeDDR3;
            break;
        case kDDR4MemType:
            [[fallthrough]];
        case kLPDDR4MemType:
            [[fallthrough]];
        case kDDR5MemType:    // AMD's Kexts don't know about DDR5
            [[fallthrough]];
        case kLPDDR5MemType:
            videoMemoryType = kVideoMemoryTypeDDR4;
            break;
        default:
            DBGLOG("x6000fb", "Unsupported memory type %d", memoryType);
            videoMemoryType = kVideoMemoryTypeUnknown;
            break;
    }
    getMember<uint32_t>(fwInfo, 0x20) = channelCount * 64;    // VRAM Width (64-bit channels)
    return kIOReturnSuccess;
}

bool X6000FB::OnAppleBacklightDisplayLoad(void *, void *, IOService *newService, IONotifier *) {
    OSDictionary *params = OSDynamicCast(OSDictionary, newService->getProperty("IODisplayParameters"));
    if (!params) {
        DBGLOG("x6000fb", "OnAppleBacklightDisplayLoad: No 'IODisplayParameters' property");
        return false;
    }

    OSDictionary *linearBrightness = OSDynamicCast(OSDictionary, params->getObject("linear-brightness"));
    if (!linearBrightness) {
        DBGLOG("x6000fb", "OnAppleBacklightDisplayLoad: No 'linear-brightness' property");
        return false;
    }

    OSNumber *maxBrightness = OSDynamicCast(OSNumber, linearBrightness->getObject("max"));
    if (!maxBrightness) {
        DBGLOG("x6000fb", "OnAppleBacklightDisplayLoad: No 'max' property");
        return false;
    }

    callback->maxPwmBacklightLvl = maxBrightness->unsigned32BitValue();
    DBGLOG("x6000fb", "OnAppleBacklightDisplayLoad: Max brightness: 0x%X", callback->maxPwmBacklightLvl);

    return true;
}

void X6000FB::registerDispMaxBrightnessNotif() {
    if (callback->dispNotif) { return; }

    auto *matching = IOService::serviceMatching("AppleBacklightDisplay");
    if (!matching) {
        SYSLOG("x6000fb", "registerDispMaxBrightnessNotif: Failed to create match dictionary");
        return;
    }

    callback->dispNotif =
        IOService::addMatchingNotification(gIOFirstMatchNotification, matching, OnAppleBacklightDisplayLoad, nullptr);
    SYSLOG_COND(!callback->dispNotif, "x6000fb", "registerDispMaxBrightnessNotif: Failed to register notification");
    matching->release();
}

uint32_t X6000FB::wrapDcePanelCntlHwInit(void *panelCntl) {
    callback->panelCntlPtr = panelCntl;
    return FunctionCast(wrapDcePanelCntlHwInit, callback->orgDcePanelCntlHwInit)(panelCntl);
}

IOReturn X6000FB::wrapFramebufferSetAttribute(IOService *framebuffer, IOIndex connectIndex, IOSelect attribute,
    uintptr_t value) {
    auto ret = FunctionCast(wrapFramebufferSetAttribute, callback->orgFramebufferSetAttribute)(framebuffer,
        connectIndex, attribute, value);
    if (attribute != static_cast<UInt32>('bklt')) { return ret; }

    if (!callback->maxPwmBacklightLvl) {
        DBGLOG("x6000fb", "wrapFramebufferSetAttribute: maxPwmBacklightLvl is 0");
        return kIOReturnSuccess;
    }

    if (!callback->panelCntlPtr) {
        DBGLOG("x6000fb", "wrapFramebufferSetAttribute: panelCntl is null");
        return kIOReturnSuccess;
    }

    if (!callback->orgDceDriverSetBacklight) {
        DBGLOG("x6000fb", "wrapFramebufferSetAttribute: orgDceDriverSetBacklight is null");
        return kIOReturnSuccess;
    }

    // Set the backlight
    callback->curPwmBacklightLvl = static_cast<uint32_t>(value);
    uint32_t percentage = callback->curPwmBacklightLvl * 100 / callback->maxPwmBacklightLvl;
    uint32_t pwmValue = 0;
    if (percentage >= 100) {
        // This is from the dmcu_set_backlight_level function of Linux source
        // ...
        // if (backlight_pwm_u16_16 & 0x10000)
        // 	   backlight_8_bit = 0xFF;
        // else
        // 	   backlight_8_bit = (backlight_pwm_u16_16 >> 8) & 0xFF;
        // ...
        // The max brightness should have 0x10000 bit set
        pwmValue = 0x1FF00;
    } else {
        pwmValue = ((percentage * 0xFF) / 100) << 8U;
    }

    callback->orgDceDriverSetBacklight(callback->panelCntlPtr, pwmValue);
    return kIOReturnSuccess;
}

IOReturn X6000FB::wrapFramebufferGetAttribute(IOService *framebuffer, IOIndex connectIndex, IOSelect attribute,
    uintptr_t *value) {
    auto ret = FunctionCast(wrapFramebufferGetAttribute, callback->orgFramebufferGetAttribute)(framebuffer,
        connectIndex, attribute, value);
    if (attribute == static_cast<UInt32>('bklt')) {
        // Enable the backlight feature of AMD navi10 driver
        *value = callback->curPwmBacklightLvl;
        return kIOReturnSuccess;
    }
    return ret;
}

uint32_t X6000FB::wrapGetNumberOfConnectors(void *that) {
    static bool once = false;
    if (!once) {
        once = true;
        struct DispObjInfoTableV1_4 *objInfo = getMember<DispObjInfoTableV1_4 *>(that, 0x28);
        if (objInfo->formatRev == 1 && (objInfo->contentRev == 4 || objInfo->contentRev == 5)) {
            DBGLOG("nred", "Fixing VBIOS connectors");
            auto n = objInfo->pathCount;
            for (size_t i = 0, j = 0; i < n; i++) {
                // Skip invalid device tags
                if (objInfo->dispPaths[i].devTag) {
                    objInfo->dispPaths[j++] = objInfo->dispPaths[i];
                } else {
                    objInfo->pathCount--;
                }
            }
        }
    }
    return FunctionCast(wrapGetNumberOfConnectors, callback->orgGetNumberOfConnectors)(that);
}

bool X6000FB::wrapIH40IVRingInitHardware(void *ctx, void *param2) {
    auto ret = FunctionCast(wrapIH40IVRingInitHardware, callback->orgIH40IVRingInitHardware)(ctx, param2);
    if (NRed::callback->chipType >= ChipType::Renoir) {
        NRed::callback->writeReg32(mmIH_CHICKEN, NRed::callback->readReg32(mmIH_CHICKEN) | mmIH_MC_SPACE_GPA_ENABLE);
    }
    return ret;
}

void X6000FB::wrapIRQMGRWriteRegister(void *ctx, uint64_t index, uint32_t value) {
    if (index == mmIH_CLK_CTRL && NRed::callback->chipType >= ChipType::Renoir) {
        index |= (index & (1U << mmIH_DBUS_MUX_CLK_SOFT_OVERRIDE_SHIFT)) >>
                 (mmIH_DBUS_MUX_CLK_SOFT_OVERRIDE_SHIFT - mmIH_IH_BUFFER_MEM_CLK_SOFT_OVERRIDE_SHIFT);
        DBGLOG("x6000fb", "_IRQMGR_WriteRegister: Set IH_BUFFER_MEM_CLK_SOFT_OVERRIDE");
    }
    FunctionCast(wrapIRQMGRWriteRegister, callback->orgIRQMGRWriteRegister)(ctx, index, value);
}
