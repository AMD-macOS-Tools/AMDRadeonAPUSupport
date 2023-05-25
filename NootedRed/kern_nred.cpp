//  Copyright © 2023 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.0. See LICENSE for
//  details.

#include "kern_nred.hpp"
#include "kern_hwlibs.hpp"
#include "kern_model.hpp"
#include "kern_patches.hpp"
#include "kern_x5000.hpp"
#include "kern_x6000.hpp"
#include "kern_x6000fb.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <IOKit/IODeviceTreeSupport.h>

static const char *pathAGDP = "/System/Library/Extensions/AppleGraphicsControl.kext/Contents/PlugIns/"
                              "AppleGraphicsDevicePolicy.kext/Contents/MacOS/AppleGraphicsDevicePolicy";
static const char *pathBacklight = "/System/Library/Extensions/AppleBacklight.kext/Contents/MacOS/AppleBacklight";
static const char *pathMCCSControl = "/System/Library/Extensions/AppleMCCSControl.kext/Contents/MacOS/AppleMCCSControl";

static KernelPatcher::KextInfo kextAGDP {"com.apple.driver.AppleGraphicsDevicePolicy", &pathAGDP, 1, {true}, {},
    KernelPatcher::KextInfo::Unloaded};
static KernelPatcher::KextInfo kextBacklight {"com.apple.driver.AppleBacklight", &pathBacklight, 1, {true}, {},
    KernelPatcher::KextInfo::Unloaded};
static KernelPatcher::KextInfo kextMCCSControl {"com.apple.driver.AppleMCCSControl", &pathMCCSControl, 1, {true}, {},
    KernelPatcher::KextInfo::Unloaded};

NRed *NRed::callback = nullptr;

static X6000FB x6000fb;
static X5000HWLibs hwlibs;
static X5000 x5000;
static X6000 x6000;

void NRed::init() {
    SYSLOG("nred", "Copyright 2023 ChefKiss Inc. If you've paid for this, you've been scammed.");
    callback = this;
    SYSLOG_COND(checkKernelArgument("-wreddbg"), "nred", "You're using the legacy WRed debug flag. Update your EFI");

    lilu.onPatcherLoadForce(
        [](void *user, KernelPatcher &patcher) { static_cast<NRed *>(user)->processPatcher(patcher); }, this);
    lilu.onKextLoadForce(
        nullptr, 0,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            static_cast<NRed *>(user)->processKext(patcher, index, address, size);
        },
        this);
    lilu.onKextLoadForce(&kextAGDP);
    lilu.onKextLoadForce(&kextBacklight);
    lilu.onKextLoadForce(&kextMCCSControl);
    x6000fb.init();
    hwlibs.init();
    x6000.init();
    x5000.init();
}

void NRed::processPatcher(KernelPatcher &patcher) {
    auto *devInfo = DeviceInfo::create();
    if (devInfo) {
        devInfo->processSwitchOff();

        PANIC_COND(!devInfo->videoBuiltin, "nred", "videoBuiltin null");
        this->iGPU = OSDynamicCast(IOPCIDevice, devInfo->videoBuiltin);
        PANIC_COND(!this->iGPU, "nred", "videoBuiltin is not IOPCIDevice");
        PANIC_COND(WIOKit::readPCIConfigValue(this->iGPU, WIOKit::kIOPCIConfigVendorID) != WIOKit::VendorID::ATIAMD,
            "nred", "videoBuiltin is not AMD");

        WIOKit::renameDevice(this->iGPU, "IGPU");
        WIOKit::awaitPublishing(this->iGPU);
        char name[256] = {0};
        for (size_t i = 0, ii = 0; i < devInfo->videoExternal.size(); i++) {
            auto *device = OSDynamicCast(IOPCIDevice, devInfo->videoExternal[i].video);
            if (device) {
                snprintf(name, arrsize(name), "GFX%zu", ii++);
                WIOKit::renameDevice(device, name);
                WIOKit::awaitPublishing(device);
            }
        }

        static uint8_t builtin[] = {0x01};
        this->iGPU->setProperty("built-in", builtin, arrsize(builtin));
        this->deviceId = WIOKit::readPCIConfigValue(this->iGPU, WIOKit::kIOPCIConfigDeviceID);
        this->pciRevision = WIOKit::readPCIConfigValue(NRed::callback->iGPU, WIOKit::kIOPCIConfigRevisionID);
        auto *model = getBranding(this->deviceId, this->pciRevision);
        if (model) {
            auto len = static_cast<uint32_t>(strlen(model) + 1);
            this->iGPU->setProperty("model", const_cast<char *>(model), len);
            this->iGPU->setProperty("ATY,FamilyName", const_cast<char *>("Radeon"), 7);
            this->iGPU->setProperty("ATY,DeviceName", const_cast<char *>(model) + 11, len - 11);    // Vega ...
        }

        auto *prop = OSDynamicCast(OSData, this->iGPU->getProperty("ATY,bin_image"));
        if (UNLIKELY(prop)) {
            DBGLOG("nred", "VBIOS manually overridden");
            this->vbiosData = OSData::withBytes(prop->getBytesNoCopy(), prop->getLength());
            PANIC_COND(UNLIKELY(!this->vbiosData), "nred", "Failed to allocate VBIOS data");
        } else if (UNLIKELY(!this->getVBIOSFromVFCT(this->iGPU))) {
            SYSLOG("nred", "Failed to get VBIOS from VFCT.");
            PANIC_COND(UNLIKELY(!this->getVBIOSFromVRAM(this->iGPU)), "nred", "Failed to get VBIOS from VRAM");
        }
        auto len = this->vbiosData->getLength();
        if (len < 65536) {
            DBGLOG("nred", "Padding VBIOS to 65536 bytes (was %u)", len);
            this->vbiosData->appendByte(0, 65536 - len);
        }

        DeviceInfo::deleter(devInfo);
    } else {
        SYSLOG("nred", "Failed to create DeviceInfo");
    }

    KernelPatcher::RouteRequest requests[] = {
        {"__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass", wrapSafeMetaCast, this->orgSafeMetaCast},
        {"_cs_validate_page", csValidatePage, this->orgCsValidatePage},
    };

    size_t num = arrsize(requests);
    if (LIKELY(lilu.getRunMode() & LiluAPI::RunningNormal)) {
        auto *entry = IORegistryEntry::fromPath("/", gIODTPlane);
        if (entry) {
            DBGLOG("nred", "Setting hwgva-id to iMacPro1,1");
            entry->setProperty("hwgva-id", const_cast<char *>("Mac-7BA5B2D9E42DDD94"), sizeof("Mac-7BA5B2D9E42DDD94"));
            entry->release();
        }
    } else {
        num--;
    }
    PANIC_COND(!patcher.routeMultipleLong(KernelPatcher::KernelID, requests, num), "nred",
        "Failed to route kernel symbols");
}

OSMetaClassBase *NRed::wrapSafeMetaCast(const OSMetaClassBase *anObject, const OSMetaClass *toMeta) {
    auto ret = FunctionCast(wrapSafeMetaCast, callback->orgSafeMetaCast)(anObject, toMeta);
    if (UNLIKELY(!ret)) {
        for (const auto &ent : callback->metaClassMap) {
            if (LIKELY(ent[0] == toMeta)) {
                return FunctionCast(wrapSafeMetaCast, callback->orgSafeMetaCast)(anObject, ent[1]);
            } else if (UNLIKELY(ent[1] == toMeta)) {
                return FunctionCast(wrapSafeMetaCast, callback->orgSafeMetaCast)(anObject, ent[0]);
            }
        }
    }
    return ret;
}

void NRed::csValidatePage(vnode *vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data,
    int *validated_p, int *tainted_p, int *nx_p) {
    FunctionCast(csValidatePage, callback->orgCsValidatePage)(vp, pager, page_offset, data, validated_p, tainted_p,
        nx_p);

    char path[PATH_MAX];
    int pathlen = PATH_MAX;
    if (UNLIKELY(vn_getpath(vp, path, &pathlen))) { return; }

    if (UserPatcher::matchSharedCachePath(path)) {
        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kVideoToolboxDRMModelOriginal,
                arrsize(kVideoToolboxDRMModelOriginal), BaseDeviceInfo::get().modelIdentifier, 20)))
            DBGLOG("nred", "Relaxed VideoToolbox DRM model check");

        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kBoardIdOriginal,
                arrsize(kBoardIdOriginal), kBoardIdPatched, arrsize(kBoardIdPatched))))
            DBGLOG("nred", "Applied iMacPro1,1 spoof in AppleGVA");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kVAAcceleratorInfoIdentifyOriginal, kVAAcceleratorInfoIdentifyMask, kVAAcceleratorInfoIdentifyPatched,
                kVAAcceleratorInfoIdentifyMask, 0, 0)))
            DBGLOG("nred", "Patched VAAcceleratorInfo::identify");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kVAFactoryCreateGraphicsEngineOriginal, arrsize(kVAFactoryCreateGraphicsEngineOriginal),
                kVAFactoryCreateGraphicsEngineMask, arrsize(kVAFactoryCreateGraphicsEngineMask),
                kVAFactoryCreateGraphicsEnginePatched, arrsize(kVAFactoryCreateGraphicsEnginePatched), nullptr, 0, 0,
                0)))
            DBGLOG("nred", "Patched VAFactory::createGraphicsEngine");

        if (UNLIKELY(
                KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, kVAFactoryCreateVPOriginal,
                    arrsize(kVAFactoryCreateVPOriginal), kVAFactoryCreateVPMask, arrsize(kVAFactoryCreateVPMask),
                    kVAFactoryCreateVPPatched, arrsize(kVAFactoryCreateVPPatched), nullptr, 0, 0, 0)))
            DBGLOG("nred", "Patched VAFactory::create*VP");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kVAFactoryCreateImageBltOriginal, arrsize(kVAFactoryCreateImageBltOriginal),
                kVAFactoryCreateImageBltMask, arrsize(kVAFactoryCreateImageBltMask), kVAFactoryCreateImageBltPatched,
                arrsize(kVAFactoryCreateImageBltPatched), nullptr, 0, 0, 0)))
            DBGLOG("nred", "Patched VAFactory::createImageBlt");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kVAAddrLibInterfaceInitOriginal, kVAAddrLibInterfaceInitOriginalMask, kVAAddrLibInterfaceInitPatched,
                kVAAddrLibInterfaceInitPatchedMask, 0, 0)))
            DBGLOG("nred", "Patched VAAddrLibInterface::init");

        // ----------------------------------------------
        if (callback->chipType >= ChipType::Renoir) { return; }    // Everything after is for VCN 1
        // ----------------------------------------------

        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kWriteUvdNoOpOriginal,
                kWriteUvdNoOpPatched)))
            DBGLOG("nred", "Patched Vcn2DecCommand::writeUvdNoOp");

        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kWriteUvdEngineStartOriginal,
                kWriteUvdEngineStartPatched)))
            DBGLOG("nred", "Patched Vcn2DecCommand::writeUvdEngineStart");

        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kWriteUvdGpcomVcpuCmdOriginal,
                kWriteUvdGpcomVcpuCmdPatched)))
            DBGLOG("nred", "Patched Vcn2DecCommand::writeUvdGpcomVcpuCmdOriginal");

        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kWriteUvdGpcomVcpuData0Original,
                kWriteUvdGpcomVcpuData0Patched)))
            DBGLOG("nred", "Patched Vcn2DecCommand::writeUvdGpcomVcpuData0Original");

        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kWriteUvdGpcomVcpuData1Original,
                kWriteUvdGpcomVcpuData1Patched)))
            DBGLOG("nred", "Patched Vcn2DecCommand::writeUvdGpcomVcpuData1Original");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddEncodePacketOriginal, kAddEncodePacketMask, kAddEncodePacketPatched, kAddEncodePacketMask, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addEncodePacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddSliceHeaderPacketOriginal, kAddSliceHeaderPacketMask, kAddSliceHeaderPacketPatched,
                kAddSliceHeaderPacketMask, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addSliceHeaderPacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddIntraRefreshPacketOriginal, kAddIntraRefreshPacketMask, kAddIntraRefreshPacketPatched,
                kAddIntraRefreshPacketMask, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addIntraRefreshPacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddContextBufferPacketOriginal, kAddContextBufferPacketMask, kAddContextBufferPacketPatched,
                kAddContextBufferPacketMask, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addContextBufferPacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddBitstreamBufferPacketOriginal, kAddBitstreamBufferPacketMask, kAddBitstreamBufferPacketPatched,
                kAddBitstreamBufferPacketMask, 1, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addBitstreamBufferPacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddFeedbackBufferPacketOriginal, kAddFeedbackBufferPacketMask, kAddFeedbackBufferPacketPatched,
                kAddFeedbackBufferPacketMask, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addFeedbackBufferPacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddInputFormatPacketOriginal, arrsize(kAddInputFormatPacketOriginal), kAddFormatPacketMask,
                arrsize(kAddFormatPacketMask), kRetZero, arrsize(kRetZero), nullptr, 0, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addInputFormatPacket");

        if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE,
                kAddOutputFormatPacketOriginal, arrsize(kAddOutputFormatPacketOriginal), kAddFormatPacketMask,
                arrsize(kAddFormatPacketMask), kRetZero, arrsize(kRetZero), nullptr, 0, 0, 0)))
            DBGLOG("nred", "Patched Vcn2EncCommand::addOutputFormatPacket");
    } else if (UNLIKELY(!strncmp(path, kCoreLSKDMSEPath, arrsize(kCoreLSKDMSEPath))) ||
               UNLIKELY(!strncmp(path, kCoreLSKDPath, arrsize(kCoreLSKDPath)))) {
        if (UNLIKELY(KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kCoreLSKDOriginal,
                kCoreLSKDPatched)))
            DBGLOG("nred", "Patched streaming CPUID to Haswell");
    }
}

void NRed::setRMMIOIfNecessary() {
    if (UNLIKELY(!this->rmmio || !this->rmmio->getLength())) {
        this->rmmio = this->iGPU->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress5);
        PANIC_COND(UNLIKELY(!this->rmmio || !this->rmmio->getLength()), "nred", "Failed to map RMMIO");
        this->rmmioPtr = reinterpret_cast<uint32_t *>(this->rmmio->getVirtualAddress());

        this->fbOffset = static_cast<uint64_t>(this->readReg32(0x296B)) << 24;
        this->revision = (this->readReg32(0xD2F) & 0xF000000) >> 0x18;
        switch (this->deviceId) {
            case 0x15D8:
                if (LIKELY(this->revision >= 0x8)) {
                    this->chipType = ChipType::Raven2;
                    this->enumeratedRevision = 0x79;
                    break;
                }
                this->chipType = ChipType::Picasso;
                this->enumeratedRevision = 0x41;
                break;
            case 0x15DD:
                if (LIKELY(this->revision >= 0x8)) {
                    this->chipType = ChipType::Raven2;
                    this->enumeratedRevision = 0x79;
                    break;
                }
                this->chipType = ChipType::Raven;
                this->enumeratedRevision = 0x10;
                break;
            case 0x164C:
                [[fallthrough]];
            case 0x1636:
                this->chipType = ChipType::Renoir;
                this->enumeratedRevision = 0x91;
                break;
            case 0x15E7:
                [[fallthrough]];
            case 0x1638:
                this->chipType = ChipType::GreenSardine;
                this->enumeratedRevision = 0xA1;
                break;
            default:
                PANIC("nred", "Unknown device ID");
        }
    }
}

void NRed::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (kextAGDP.loadIndex == index) {
        KernelPatcher::LookupPatch patch = {&kextAGDP, reinterpret_cast<const uint8_t *>(kAGDPBoardIDKeyOriginal),
            reinterpret_cast<const uint8_t *>(kAGDPBoardIDKeyPatched), arrsize(kAGDPBoardIDKeyOriginal), 1};
        patcher.applyLookupPatch(&patch);
        SYSLOG_COND(patcher.getError() != KernelPatcher::Error::NoError, "nred",
            "Failed to apply AGDP board-id patch: %d", patcher.getError());
        patcher.clearError();
        if (getKernelVersion() >= KernelVersion::Ventura) {
            KernelPatcher::LookupPatch patch = {&kextAGDP, kAGDPFBCountCheckVenturaOriginal,
                kAGDPFBCountCheckVenturaPatched, arrsize(kAGDPFBCountCheckVenturaOriginal), 1};
            patcher.applyLookupPatch(&patch);
            SYSLOG_COND(patcher.getError() != KernelPatcher::Error::NoError, "nred",
                "Failed to apply AGDP FB count patch: %d", patcher.getError());
            patcher.clearError();
        } else {
            KernelPatcher::LookupPatch patch = {&kextAGDP, kAGDPFBCountCheckOriginal, kAGDPFBCountCheckPatched,
                arrsize(kAGDPFBCountCheckOriginal), 1};
            patcher.applyLookupPatch(&patch);
            SYSLOG_COND(patcher.getError() != KernelPatcher::Error::NoError, "nred",
                "Failed to apply AGDP FB count patch: %d", patcher.getError());
            patcher.clearError();
        }
    } else if (kextBacklight.loadIndex == index) {
        KernelPatcher::RouteRequest request {"__ZN15AppleIntelPanel10setDisplayEP9IODisplay", wrapApplePanelSetDisplay,
            orgApplePanelSetDisplay};
        if (patcher.routeMultiple(kextBacklight.loadIndex, &request, 1, address, size)) {
            const uint8_t find[] = {"F%uT%04x"};
            const uint8_t replace[] = {"F%uTxxxx"};
            KernelPatcher::LookupPatch patch = {&kextBacklight, find, replace, sizeof(find), 1};
            DBGLOG("nred", "applying backlight patch");
            patcher.applyLookupPatch(&patch);
        }
    } else if (kextMCCSControl.loadIndex == index) {
        KernelPatcher::RouteRequest request[] = {
            {"__ZN25AppleMCCSControlGibraltar5probeEP9IOServicePi", wrapFunctionReturnZero},
            {"__ZN21AppleMCCSControlCello5probeEP9IOServicePi", wrapFunctionReturnZero},
        };
        patcher.routeMultiple(index, request, address, size);
    } else if (x6000fb.processKext(patcher, index, address, size)) {
        DBGLOG("nred", "Processed AMDRadeonX6000Framebuffer");
    } else if (hwlibs.processKext(patcher, index, address, size)) {
        DBGLOG("nred", "Processed AMDRadeonX5000HWLibs");
    } else if (x6000.processKext(patcher, index, address, size)) {
        DBGLOG("nred", "Processed AMDRadeonX6000");
    } else if (x5000.processKext(patcher, index, address, size)) {
        DBGLOG("nred", "Processed AMDRadeonX5000");
    }
}

struct ApplePanelData {
    const char *deviceName;
    uint8_t deviceData[36];
};

static ApplePanelData appleBacklightData[] = {
    {"F14Txxxx", {0x00, 0x11, 0x00, 0x00, 0x00, 0x34, 0x00, 0x52, 0x00, 0x73, 0x00, 0x94, 0x00, 0xBE, 0x00, 0xFA, 0x01,
                     0x36, 0x01, 0x72, 0x01, 0xC5, 0x02, 0x2F, 0x02, 0xB9, 0x03, 0x60, 0x04, 0x1A, 0x05, 0x0A, 0x06,
                     0x0E, 0x07, 0x10}},
    {"F15Txxxx", {0x00, 0x11, 0x00, 0x00, 0x00, 0x36, 0x00, 0x54, 0x00, 0x7D, 0x00, 0xB2, 0x00, 0xF5, 0x01, 0x49, 0x01,
                     0xB1, 0x02, 0x2B, 0x02, 0xB8, 0x03, 0x59, 0x04, 0x13, 0x04, 0xEC, 0x05, 0xF3, 0x07, 0x34, 0x08,
                     0xAF, 0x0A, 0xD9}},
    {"F16Txxxx", {0x00, 0x11, 0x00, 0x00, 0x00, 0x18, 0x00, 0x27, 0x00, 0x3A, 0x00, 0x52, 0x00, 0x71, 0x00, 0x96, 0x00,
                     0xC4, 0x00, 0xFC, 0x01, 0x40, 0x01, 0x93, 0x01, 0xF6, 0x02, 0x6E, 0x02, 0xFE, 0x03, 0xAA, 0x04,
                     0x78, 0x05, 0x6C}},
    {"F17Txxxx", {0x00, 0x11, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x34, 0x00, 0x4F, 0x00, 0x71, 0x00, 0x9B, 0x00, 0xCF, 0x01,
                     0x0E, 0x01, 0x5D, 0x01, 0xBB, 0x02, 0x2F, 0x02, 0xB9, 0x03, 0x60, 0x04, 0x29, 0x05, 0x1E, 0x06,
                     0x44, 0x07, 0xA1}},
    {"F18Txxxx", {0x00, 0x11, 0x00, 0x00, 0x00, 0x53, 0x00, 0x8C, 0x00, 0xD5, 0x01, 0x31, 0x01, 0xA2, 0x02, 0x2E, 0x02,
                     0xD8, 0x03, 0xAE, 0x04, 0xAC, 0x05, 0xE5, 0x07, 0x59, 0x09, 0x1C, 0x0B, 0x3B, 0x0D, 0xD0, 0x10,
                     0xEA, 0x14, 0x99}},
    {"F19Txxxx", {0x00, 0x11, 0x00, 0x00, 0x02, 0x8F, 0x03, 0x53, 0x04, 0x5A, 0x05, 0xA1, 0x07, 0xAE, 0x0A, 0x3D, 0x0E,
                     0x14, 0x13, 0x74, 0x1A, 0x5E, 0x24, 0x18, 0x31, 0xA9, 0x44, 0x59, 0x5E, 0x76, 0x83, 0x11, 0xB6,
                     0xC7, 0xFF, 0x7B}},
    {"F24Txxxx", {0x00, 0x11, 0x00, 0x01, 0x00, 0x34, 0x00, 0x52, 0x00, 0x73, 0x00, 0x94, 0x00, 0xBE, 0x00, 0xFA, 0x01,
                     0x36, 0x01, 0x72, 0x01, 0xC5, 0x02, 0x2F, 0x02, 0xB9, 0x03, 0x60, 0x04, 0x1A, 0x05, 0x0A, 0x06,
                     0x0E, 0x07, 0x10}},
};

size_t NRed::wrapFunctionReturnZero() { return 0; }

bool NRed::wrapApplePanelSetDisplay(IOService *that, IODisplay *display) {
    static bool once = false;
    if (!once) {
        once = true;
        auto panels = OSDynamicCast(OSDictionary, that->getProperty("ApplePanels"));
        if (panels) {
            auto rawPanels = panels->copyCollection();
            panels = OSDynamicCast(OSDictionary, rawPanels);

            if (panels) {
                for (auto &entry : appleBacklightData) {
                    auto pd = OSData::withBytes(entry.deviceData, sizeof(entry.deviceData));
                    if (pd) {
                        panels->setObject(entry.deviceName, pd);
                        // No release required by current AppleBacklight implementation.
                    } else {
                        SYSLOG("nred", "Panel start cannot allocate %s data", entry.deviceName);
                    }
                }
                that->setProperty("ApplePanels", panels);
            }

            if (rawPanels) { rawPanels->release(); }
        } else {
            SYSLOG("nred", "Panel start has no panels");
        }
    }

    bool result = FunctionCast(wrapApplePanelSetDisplay, callback->orgApplePanelSetDisplay)(that, display);
    DBGLOG("nred", "Panel display set returned %d", result);

    return result;
}
