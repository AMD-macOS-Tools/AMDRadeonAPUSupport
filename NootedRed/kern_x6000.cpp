//  Copyright © 2022-2023 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.0. See LICENSE for
//  details.

#include "kern_x6000.hpp"
#include "kern_nred.hpp"
#include "kern_patcherplus.hpp"
#include "kern_patches.hpp"
#include "kern_x5000.hpp"
#include <Headers/kern_api.hpp>

static const char *pathRadeonX6000 = "/System/Library/Extensions/AMDRadeonX6000.kext/Contents/MacOS/AMDRadeonX6000";

static KernelPatcher::KextInfo kextRadeonX6000 = {"com.apple.kext.AMDRadeonX6000", &pathRadeonX6000, 1, {}, {},
    KernelPatcher::KextInfo::Unloaded};

X6000 *X6000::callback = nullptr;

void X6000::init() {
    callback = this;
    lilu.onKextLoadForce(&kextRadeonX6000);
}

bool X6000::processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
    if (kextRadeonX6000.loadIndex == id) {
        NRed::callback->setRMMIOIfNecessary();

        auto catalina = getKernelVersion() == KernelVersion::Catalina;
        SolveRequestPlus solveRequests[] = {
            {"__ZN30AMDRadeonX6000_AMDVCN2HWEngineC1Ev", this->orgVCN2EngineConstructor},
            {"__ZN31AMDRadeonX6000_AMDGFX10Hardware20allocateAMDHWDisplayEv", this->orgAllocateAMDHWDisplay},
            {"__ZN42AMDRadeonX6000_AMDGFX10GraphicsAccelerator15newVideoContextEv", this->orgNewVideoContext},
            {"__ZN31AMDRadeonX6000_IAMDSMLInterface18createSMLInterfaceEj", this->orgCreateSMLInterface},
            {"__ZN37AMDRadeonX6000_AMDGraphicsAccelerator9newSharedEv", this->orgNewShared, !catalina},
            {"__ZN37AMDRadeonX6000_AMDGraphicsAccelerator19newSharedUserClientEv", this->orgNewSharedUserClient,
                !catalina},
            {"__ZN35AMDRadeonX6000_AMDAccelVideoContext10gMetaClassE", NRed::callback->metaClassMap[0][1]},
            {"__ZN37AMDRadeonX6000_AMDAccelDisplayMachine10gMetaClassE", NRed::callback->metaClassMap[1][1]},
            {"__ZN34AMDRadeonX6000_AMDAccelDisplayPipe10gMetaClassE", NRed::callback->metaClassMap[2][1]},
            {"__ZN30AMDRadeonX6000_AMDAccelChannel10gMetaClassE", NRed::callback->metaClassMap[3][0]},
            {"__ZN28AMDRadeonX6000_IAMDHWChannel10gMetaClassE", NRed::callback->metaClassMap[4][1]},
            {"__ZN33AMDRadeonX6000_AMDHWAlignManager224getPreferredSwizzleMode2EP33_ADDR2_COMPUTE_SURFACE_INFO_INPUT",
                this->orgGetPreferredSwizzleMode2},
        };
        PANIC_COND(!SolveRequestPlus::solveAll(patcher, id, solveRequests, slide, size), "x6000",
            "Failed to resolve symbols");

        auto ventura = getKernelVersion() >= KernelVersion::Ventura;
        RouteRequestPlus requests[] = {
            {"__ZN37AMDRadeonX6000_AMDGraphicsAccelerator5startEP9IOService", wrapAccelStartX6000},
            {"__ZN39AMDRadeonX6000_AMDAccelSharedUserClient5startEP9IOService", wrapAccelSharedUCStartX6000, !catalina},
            {"__ZN39AMDRadeonX6000_AMDAccelSharedUserClient4stopEP9IOService", wrapAccelSharedUCStopX6000, !catalina},
            {"__ZN30AMDRadeonX6000_AMDGFX10Display23initDCNRegistersOffsetsEv", wrapInitDCNRegistersOffsets,
                this->orgInitDCNRegistersOffsets, NRed::callback->chipType < ChipType::Renoir},
            {"__ZN29AMDRadeonX6000_AMDAccelShared11SurfaceCopyEPjyP12IOAccelEvent", wrapAccelSharedSurfaceCopy,
                this->orgAccelSharedSurfaceCopy, !catalina},
            {"__ZN27AMDRadeonX6000_AMDHWDisplay17allocateScanoutFBEjP16IOAccelResource2S1_Py", wrapAllocateScanoutFB,
                this->orgAllocateScanoutFB, !ventura},
            {"__ZN27AMDRadeonX6000_AMDHWDisplay14fillUBMSurfaceEjP17_FRAMEBUFFER_INFOP13_UBM_SURFINFO",
                wrapFillUBMSurface, this->orgFillUBMSurface},
            {"__ZN27AMDRadeonX6000_AMDHWDisplay16configureDisplayEjjP17_FRAMEBUFFER_INFOP16IOAccelResource2",
                wrapConfigureDisplay, this->orgConfigureDisplay},
            {"__ZN27AMDRadeonX6000_AMDHWDisplay14getDisplayInfoEjbbPvP17_FRAMEBUFFER_INFO", wrapGetDisplayInfo,
                this->orgGetDisplayInfo},
        };
        PANIC_COND(!RouteRequestPlus::routeAll(patcher, id, requests, slide, size), "x6000", "Failed to route symbols");

        auto monterey = getKernelVersion() == KernelVersion::Monterey;
        LookupPatchPlus const patches[] = {
            {&kextRadeonX6000, kHWChannelSubmitCommandBufferCatalinaOriginal,
                kHWChannelSubmitCommandBufferCatalinaPatched, 1, catalina},
            {&kextRadeonX6000, kHWChannelSubmitCommandBufferOriginal, kHWChannelSubmitCommandBufferPatched, 1,
                !catalina},
            {&kextRadeonX6000, kDummyWPTRUpdateDiagCallOriginal, kDummyWPTRUpdateDiagCallPatched, 1, catalina},
            {&kextRadeonX6000, kIsDeviceValidCallOriginal, kIsDeviceValidCallPatched,
                catalina ? 20U :
                ventura  ? 23 :
                monterey ? 26 :
                           24},
            {&kextRadeonX6000, kIsDevicePCITunnelledCallOriginal, kIsDevicePCITunnelledCallPatched,
                catalina ? 9U :
                ventura  ? 3 :
                           1},
            {&kextRadeonX6000, kWriteWaitForRenderingPipeCallOriginal, kWriteWaitForRenderingPipeCallPatched, 1,
                catalina},
            {&kextRadeonX6000, kGetTtlInterfaceCallOriginal, kGetTtlInterfaceCallOriginalMask,
                kGetTtlInterfaceCallPatched, kGetTtlInterfaceCallPatchedMask, 38, catalina},
            {&kextRadeonX6000, kGetAMDHWHandlerCallOriginal, kGetAMDHWHandlerCallPatched, 19, catalina},
            {&kextRadeonX6000, kGetAMDHWHandlerCallOriginal, kGetAMDHWHandlerCallPatched, 64, catalina, 1},
            {&kextRadeonX6000, kGetHWRegistersCallOriginal, kGetHWRegistersCallOriginalMask, kGetHWRegistersCallPatched,
                kGetHWRegistersCallPatchedMask, 13, catalina},
            {&kextRadeonX6000, kGetHWMemoryCallOriginal, kGetHWMemoryCallPatched, 11, catalina},
            {&kextRadeonX6000, kGetHWGartCallOriginal, kGetHWGartCallPatched, 9, catalina},
            {&kextRadeonX6000, kGetHWAlignManagerCall1Original, kGetHWAlignManagerCall1OriginalMask,
                kGetHWAlignManagerCall1Patched, kGetHWAlignManagerCall1PatchedMask, 33, catalina},
            {&kextRadeonX6000, kGetHWAlignManagerCall2Original, kGetHWAlignManagerCall2Patched, 1, catalina},
            {&kextRadeonX6000, kGetHWEngineCallOriginal, kGetHWEngineCallPatched, 31, catalina},
            {&kextRadeonX6000, kGetHWChannelCall1Original, kGetHWChannelCall1Patched, 2, catalina},
            {&kextRadeonX6000, kGetHWChannelCall2Original, kGetHWChannelCall2Patched, 53, catalina},
            {&kextRadeonX6000, kGetHWChannelCall3Original, kGetHWChannelCall3Patched, 20, catalina},
            {&kextRadeonX6000, kRegisterChannelCallOriginal, kRegisterChannelCallPatched, 1, catalina},
            {&kextRadeonX6000, kGetChannelCountCallOriginal, kGetChannelCountCallPatched, 7, catalina},
            {&kextRadeonX6000, kGetChannelWriteBackFrameOffsetCall1Original,
                kGetChannelWriteBackFrameOffsetCall1Patched, 4, catalina},
            {&kextRadeonX6000, kGetChannelWriteBackFrameOffsetCall2Original,
                kGetChannelWriteBackFrameOffsetCall2Patched, 1, catalina},
            {&kextRadeonX6000, kGetChannelWriteBackFrameAddrCallOriginal, kGetChannelWriteBackFrameAddrCallOriginalMask,
                kGetChannelWriteBackFrameAddrCallPatched, kGetChannelWriteBackFrameAddrCallPatchedMask, 10, catalina},
            {&kextRadeonX6000, kGetDoorbellMemoryBaseAddressCallOriginal, kGetDoorbellMemoryBaseAddressCallPatched, 1,
                catalina},
            {&kextRadeonX6000, kGetChannelDoorbellOffsetCallOriginal, kGetChannelDoorbellOffsetCallPatched, 1,
                catalina},
            {&kextRadeonX6000, kGetIOPCIDeviceCallOriginal, kGetIOPCIDeviceCallPatched, 5, catalina},
            {&kextRadeonX6000, kGetSMLCallOriginal, kGetSMLCallPatched, 10, catalina},
            {&kextRadeonX6000, kGetPM4CommandUtilityCallOriginal, kGetPM4CommandUtilityCallPatched, 2, catalina},
            {&kextRadeonX6000, kDumpASICHangStateCallOriginal, kDumpASICHangStateCallPatched, 2, catalina},
            {&kextRadeonX6000, kGetSchedulerCallVenturaOriginal, kGetSchedulerCallVenturaPatched, 24, ventura},
            {&kextRadeonX6000, kGetSchedulerCallOriginal, kGetSchedulerCallPatched, monterey ? 21U : 22,
                !catalina && !ventura},
            {&kextRadeonX6000, kGetSchedulerCallCatalinaOriginal, kGetSchedulerCallCatalinaPatched, 22, catalina},
            {&kextRadeonX6000, kGetGpuDebugPolicyCallOriginal, kGetGpuDebugPolicyCallPatched,
                (getKernelVersion() == KernelVersion::Ventura && getKernelMinorVersion() >= 5) ? 38U :
                ventura                                                                        ? 37 :
                                                                                                 28,
                !catalina},
            {&kextRadeonX6000, kGetGpuDebugPolicyCallCatalinaOriginal, kGetGpuDebugPolicyCallCatalinaPatched, 27,
                catalina},
            {&kextRadeonX6000, kUpdateUtilizationStatisticsCounterCallOriginal,
                kUpdateUtilizationStatisticsCounterCallPatched, 2, catalina},
            {&kextRadeonX6000, kDisableGfxOffCallOriginal, kDisableGfxOffCallPatched, 17, catalina},
            {&kextRadeonX6000, kEnableGfxOffCallOriginal, kEnableGfxOffCallPatched, 16, catalina},
            {&kextRadeonX6000, kFlushSystemCachesCallOriginal, kFlushSystemCachesCallPatched, 4, catalina},
            {&kextRadeonX6000, kGetUbmSwizzleModeCallOriginal, kGetUbmSwizzleModeCallPatched, 1, catalina},
            {&kextRadeonX6000, kGetUbmTileModeCallOriginal, kGetUbmTileModeCallPatched, 1, catalina},
        };
        SYSLOG_COND(!LookupPatchPlus::applyAll(patcher, patches, slide, size), "x6000", "Failed to apply patches: %d",
            patcher.getError());
        patcher.clearError();

        return true;
    }

    return false;
}

/**
 * We don't want the `AMDRadeonX6000` personality defined in the `Info.plist` to do anything.
 * We only use it to force-load `AMDRadeonX6000` and snatch the VCN/DCN symbols.
 */
bool X6000::wrapAccelStartX6000() { return false; }

bool X6000::wrapAccelSharedUCStartX6000(void *that, void *provider) {
    return FunctionCast(wrapAccelSharedUCStartX6000, X5000::callback->orgAccelSharedUCStart)(that, provider);
}

bool X6000::wrapAccelSharedUCStopX6000(void *that, void *provider) {
    return FunctionCast(wrapAccelSharedUCStopX6000, X5000::callback->orgAccelSharedUCStop)(that, provider);
}

void X6000::wrapInitDCNRegistersOffsets(void *that) {
    FunctionCast(wrapInitDCNRegistersOffsets, callback->orgInitDCNRegistersOffsets)(that);
    auto fieldBase = getKernelVersion() == KernelVersion::Catalina ? 0x4838 :
                     getKernelVersion() > KernelVersion::Monterey  ? 0x590 :
                                                                     0x4830;
    auto base = getMember<uint32_t>(that, fieldBase);
    getMember<uint32_t>(that, fieldBase + 0x10) = base + mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS;
    getMember<uint32_t>(that, fieldBase + 0x48) = base + mmHUBPREQ1_DCSURF_PRIMARY_SURFACE_ADDRESS;
    getMember<uint32_t>(that, fieldBase + 0x80) = base + mmHUBPREQ2_DCSURF_PRIMARY_SURFACE_ADDRESS;
    getMember<uint32_t>(that, fieldBase + 0xB8) = base + mmHUBPREQ3_DCSURF_PRIMARY_SURFACE_ADDRESS;
    getMember<uint32_t>(that, fieldBase + 0x14) = base + mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
    getMember<uint32_t>(that, fieldBase + 0x4C) = base + mmHUBPREQ1_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
    getMember<uint32_t>(that, fieldBase + 0x84) = base + mmHUBPREQ2_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
    getMember<uint32_t>(that, fieldBase + 0xBC) = base + mmHUBPREQ3_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
    getMember<uint32_t>(that, fieldBase + 0x18) = base + mmHUBP0_DCSURF_SURFACE_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x50) = base + mmHUBP1_DCSURF_SURFACE_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x88) = base + mmHUBP2_DCSURF_SURFACE_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0xC0) = base + mmHUBP3_DCSURF_SURFACE_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x1C) = base + mmHUBPREQ0_DCSURF_SURFACE_PITCH;
    getMember<uint32_t>(that, fieldBase + 0x54) = base + mmHUBPREQ1_DCSURF_SURFACE_PITCH;
    getMember<uint32_t>(that, fieldBase + 0x8C) = base + mmHUBPREQ2_DCSURF_SURFACE_PITCH;
    getMember<uint32_t>(that, fieldBase + 0xC4) = base + mmHUBPREQ3_DCSURF_SURFACE_PITCH;
    getMember<uint32_t>(that, fieldBase + 0x20) = base + mmHUBP0_DCSURF_ADDR_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x58) = base + mmHUBP1_DCSURF_ADDR_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x90) = base + mmHUBP2_DCSURF_ADDR_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0xC8) = base + mmHUBP3_DCSURF_ADDR_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x24) = base + mmHUBP0_DCSURF_TILING_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x5C) = base + mmHUBP1_DCSURF_TILING_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x94) = base + mmHUBP2_DCSURF_TILING_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0xCC) = base + mmHUBP3_DCSURF_TILING_CONFIG;
    getMember<uint32_t>(that, fieldBase + 0x28) = base + mmHUBP0_DCSURF_PRI_VIEWPORT_START;
    getMember<uint32_t>(that, fieldBase + 0x60) = base + mmHUBP1_DCSURF_PRI_VIEWPORT_START;
    getMember<uint32_t>(that, fieldBase + 0x98) = base + mmHUBP2_DCSURF_PRI_VIEWPORT_START;
    getMember<uint32_t>(that, fieldBase + 0xD0) = base + mmHUBP3_DCSURF_PRI_VIEWPORT_START;
    getMember<uint32_t>(that, fieldBase + 0x2C) = base + mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION;
    getMember<uint32_t>(that, fieldBase + 0x64) = base + mmHUBP1_DCSURF_PRI_VIEWPORT_DIMENSION;
    getMember<uint32_t>(that, fieldBase + 0x9C) = base + mmHUBP2_DCSURF_PRI_VIEWPORT_DIMENSION;
    getMember<uint32_t>(that, fieldBase + 0xD4) = base + mmHUBP3_DCSURF_PRI_VIEWPORT_DIMENSION;
    getMember<uint32_t>(that, fieldBase + 0x30) = base + mmOTG0_OTG_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x68) = base + mmOTG1_OTG_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xA0) = base + mmOTG2_OTG_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xD8) = base + mmOTG3_OTG_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x110) = base + mmOTG4_OTG_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x148) = base + mmOTG5_OTG_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x34) = base + mmOTG0_OTG_INTERLACE_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x6C) = base + mmOTG1_OTG_INTERLACE_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xA4) = base + mmOTG2_OTG_INTERLACE_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xDC) = base + mmOTG3_OTG_INTERLACE_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x114) = base + mmOTG4_OTG_INTERLACE_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x14C) = base + mmOTG5_OTG_INTERLACE_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x38) = base + mmHUBPREQ0_DCSURF_FLIP_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x70) = base + mmHUBPREQ1_DCSURF_FLIP_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xA8) = base + mmHUBPREQ2_DCSURF_FLIP_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xE0) = base + mmHUBPREQ3_DCSURF_FLIP_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x3C) = base + mmHUBPRET0_HUBPRET_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x74) = base + mmHUBPRET1_HUBPRET_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xAC) = base + mmHUBPRET2_HUBPRET_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0xE4) = base + mmHUBPRET3_HUBPRET_CONTROL;
    getMember<uint32_t>(that, fieldBase + 0x40) = base + mmHUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE;
    getMember<uint32_t>(that, fieldBase + 0x78) = base + mmHUBPREQ1_DCSURF_SURFACE_EARLIEST_INUSE;
    getMember<uint32_t>(that, fieldBase + 0xB0) = base + mmHUBPREQ2_DCSURF_SURFACE_EARLIEST_INUSE;
    getMember<uint32_t>(that, fieldBase + 0xE8) = base + mmHUBPREQ3_DCSURF_SURFACE_EARLIEST_INUSE;
    getMember<uint32_t>(that, fieldBase + 0x44) = base + mmHUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE_HIGH;
    getMember<uint32_t>(that, fieldBase + 0x7C) = base + mmHUBPREQ1_DCSURF_SURFACE_EARLIEST_INUSE_HIGH;
    getMember<uint32_t>(that, fieldBase + 0xB4) = base + mmHUBPREQ2_DCSURF_SURFACE_EARLIEST_INUSE_HIGH;
    getMember<uint32_t>(that, fieldBase + 0xEC) = base + mmHUBPREQ3_DCSURF_SURFACE_EARLIEST_INUSE_HIGH;
}

#define HWALIGNMGR_ADJUST getMember<void *>(X5000::callback->hwAlignMgr, 0) = X5000::callback->hwAlignMgrVtX6000;
#define HWALIGNMGR_REVERT getMember<void *>(X5000::callback->hwAlignMgr, 0) = X5000::callback->hwAlignMgrVtX5000;

uint64_t X6000::wrapAccelSharedSurfaceCopy(void *that, void *param1, uint64_t param2, void *param3) {
    HWALIGNMGR_ADJUST
    auto ret =
        FunctionCast(wrapAccelSharedSurfaceCopy, callback->orgAccelSharedSurfaceCopy)(that, param1, param2, param3);
    HWALIGNMGR_REVERT
    return ret;
}

uint64_t X6000::wrapAllocateScanoutFB(void *that, uint32_t param1, void *param2, void *param3, void *param4) {
    HWALIGNMGR_ADJUST
    auto ret =
        FunctionCast(wrapAllocateScanoutFB, callback->orgAllocateScanoutFB)(that, param1, param2, param3, param4);
    HWALIGNMGR_REVERT
    return ret;
}

uint64_t X6000::wrapFillUBMSurface(void *that, uint32_t param1, void *param2, void *param3) {
    HWALIGNMGR_ADJUST
    auto ret = FunctionCast(wrapFillUBMSurface, callback->orgFillUBMSurface)(that, param1, param2, param3);
    HWALIGNMGR_REVERT
    return ret;
}

bool X6000::wrapConfigureDisplay(void *that, uint32_t param1, uint32_t param2, void *param3, void *param4) {
    HWALIGNMGR_ADJUST
    auto ret = FunctionCast(wrapConfigureDisplay, callback->orgConfigureDisplay)(that, param1, param2, param3, param4);
    HWALIGNMGR_REVERT
    return ret;
}

uint64_t X6000::wrapGetDisplayInfo(void *that, uint32_t param1, bool param2, bool param3, void *param4, void *param5) {
    HWALIGNMGR_ADJUST
    auto ret =
        FunctionCast(wrapGetDisplayInfo, callback->orgGetDisplayInfo)(that, param1, param2, param3, param4, param5);
    HWALIGNMGR_REVERT
    return ret;
}
