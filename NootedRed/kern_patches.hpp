//  Copyright © 2022 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.0. See LICENSE for
//  details.

#pragma once
#include <Headers/kern_util.hpp>

/**
 * `AppleGraphicsDevicePolicy`
 * Symbols are stripped so function is unknown.
 * Removes framebuffer count >= 2 check.
 */
static const uint8_t kAGDPFBCountCheckOriginal[] = {0x83, 0xF8, 0x02};
static const uint8_t kAGDPFBCountCheckPatched[] = {0x83, 0xF8, 0x00};

/**
 * `AppleGraphicsDevicePolicy::start`
 * Neutralise access to AGDP configuration by board identifier.
 */
static const char kAGDPBoardIDKeyOriginal[] = "board-id";
static const char kAGDPBoardIDKeyPatched[] = "applesux";

/**
 * `_smu_9_0_1_full_asic_reset`
 * Change SMC message from `0x3B` to `0x1E` as the original one is wrong for SMU 10/12.
 */
static const uint8_t kFullAsicResetOriginal[] = {0x55, 0x48, 0x89, 0xE5, 0x8B, 0x56, 0x04, 0xBE, 0x3B, 0x00, 0x00, 0x00,
    0x5D, 0xE9, 0x51, 0xFE, 0xFF, 0xFF};
static const uint8_t kFullAsicResetPatched[] = {0x55, 0x48, 0x89, 0xE5, 0x8B, 0x56, 0x04, 0xBE, 0x1E, 0x00, 0x00, 0x00,
    0x5D, 0xE9, 0x51, 0xFE, 0xFF, 0xFF};

/**
 * `AmdAtomFwServices::initializeAtomDataTable`
 * Neutralise AmdAtomVramInfo creation null check.
 * We don't have this entry in our VBIOS.
 */
const uint8_t kAmdAtomVramInfoNullCheck1Original[] = {0x48, 0x89, 0x83, 0x90, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F,
    0x84, 0x89, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x7B, 0x18};
const uint8_t kAmdAtomVramInfoNullCheck1Patched[] = {0x48, 0x89, 0x83, 0x90, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x48, 0x8B, 0x7B, 0x18};

/**
 * `AmdAtomFwServices::initializeAtomDataTable`
 * Neutralise AmdAtomPspDirectory creation null check.
 * We don't have this entry in our VBIOS.
 */
const uint8_t kAmdAtomPspDirectoryNullCheckOriginal[] = {0x48, 0x89, 0x83, 0x88, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0,
    0x0F, 0x84, 0xA1, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x7B, 0x18};
const uint8_t kAmdAtomPspDirectoryNullCheckPatched[] = {0x48, 0x89, 0x83, 0x88, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x48, 0x8B, 0x7B, 0x18};

/**
 * `AmdAtomFwServices::getFirmwareInfo`
 * Neutralise AmdAtomVramInfo null check.
 */
const uint8_t kAmdAtomVramInfoNullCheck2Original[] = {0x48, 0x83, 0xBB, 0x90, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x90,
    0x00, 0x00, 0x00, 0x49, 0x89, 0xF7, 0xBA, 0x60, 0x00, 0x00, 0x00};
const uint8_t kAmdAtomVramInfoNullCheck2Patched[] = {0x48, 0x83, 0xBB, 0x90, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x49, 0x89, 0xF7, 0xBA, 0x60, 0x00, 0x00, 0x00};

/**
 * `AMDRadeonX6000_AmdAgdcServices::getVendorInfo`
 * Tell AGDC that we're an iGPU.
 */
const uint8_t kAgdcServicesGetVendorInfoOriginal[] = {0xC7, 0x03, 0x00, 0x00, 0x03, 0x00, 0x48, 0xB8, 0x02, 0x10, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00};
const uint8_t kAgdcServicesGetVendorInfoPatched[] = {0xC7, 0x03, 0x00, 0x00, 0x03, 0x00, 0x48, 0xB8, 0x02, 0x10, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00};

/**
 * `AMDRadeonX5000_AMDHardware::startHWEngines`
 * Make for loop run only once as we only have one SDMA engine.
 */
const uint8_t kStartHWEnginesOriginal[] = {0x49, 0x89, 0xFE, 0x31, 0xDB, 0x48, 0x83, 0xFB, 0x02, 0x74, 0x50};
const uint8_t kStartHWEnginesPatched[] = {0x49, 0x89, 0xFE, 0x31, 0xDB, 0x48, 0x83, 0xFB, 0x01, 0x74, 0x50};

/** Mismatched `getGpuDebugPolicy` virtual calls. */
const uint8_t kGetGpuDebugPolicyCallOriginal[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0xC0, 0x03, 0x00, 0x00};
const uint8_t kGetGpuDebugPolicyCallPatched[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0xC8, 0x03, 0x00, 0x00};

/**
 * `AMDRadeonX6000_AMDHWChannel::submitCommandBuffer`
 * VTable Call to signalGPUWorkSubmitted.
 * Doesn't exist on X5000, but looks like it isn't necessary, so we just NO-OP it.
 */
const uint8_t kHWChannelSubmitCommandBufferOriginal[] = {0x48, 0x8B, 0x7B, 0x18, 0x48, 0x8B, 0x07, 0xFF, 0x90, 0x30,
    0x02, 0x00, 0x00, 0x48, 0x8B, 0x43, 0x50};
const uint8_t kHWChannelSubmitCommandBufferPatched[] = {0x48, 0x8B, 0x7B, 0x18, 0x48, 0x8B, 0x07, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x48, 0x8B, 0x43, 0x50};

/** Mismatched `isDeviceValid` virtual call in `enableTimestampInterrupt` */
const uint8_t kEnableTimestampInterruptOriginal[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0xA0, 0x02, 0x00, 0x00};
const uint8_t kEnableTimestampInterruptPatched[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0x98, 0x02, 0x00, 0x00};

/** Mismatched `getScheduler` virtual calls. */
const uint8_t kGetSchedulerCallOriginal[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0xB8, 0x03, 0x00, 0x00};
const uint8_t kGetSchedulerCallPatched[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0xC0, 0x03, 0x00, 0x00};

/** Mismatched `isDeviceValid` virtual calls. */
const uint8_t find_isDeviceValid[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0xA0, 0x02, 0x00, 0x00, 0x84, 0xC0};
const uint8_t repl_isDeviceValid[] = {0x48, 0x8B, 0x07, 0xFF, 0x90, 0x98, 0x02, 0x00, 0x00, 0x84, 0xC0};

static_assert(arrsize(kAGDPFBCountCheckOriginal) == arrsize(kAGDPFBCountCheckPatched));
static_assert(arrsize(kAGDPBoardIDKeyOriginal) == arrsize(kAGDPBoardIDKeyPatched));
static_assert(arrsize(kFullAsicResetOriginal) == arrsize(kFullAsicResetPatched));
static_assert(arrsize(kAmdAtomVramInfoNullCheck1Original) == arrsize(kAmdAtomVramInfoNullCheck1Patched));
static_assert(arrsize(kAmdAtomPspDirectoryNullCheckOriginal) == arrsize(kAmdAtomPspDirectoryNullCheckPatched));
static_assert(arrsize(kAmdAtomVramInfoNullCheck2Original) == arrsize(kAmdAtomVramInfoNullCheck2Patched));
static_assert(arrsize(kAgdcServicesGetVendorInfoOriginal) == arrsize(kAgdcServicesGetVendorInfoPatched));
static_assert(arrsize(kStartHWEnginesOriginal) == arrsize(kStartHWEnginesPatched));
static_assert(arrsize(kGetGpuDebugPolicyCallOriginal) == arrsize(kGetGpuDebugPolicyCallPatched));
static_assert(arrsize(kHWChannelSubmitCommandBufferOriginal) == arrsize(kHWChannelSubmitCommandBufferPatched));
static_assert(arrsize(kEnableTimestampInterruptOriginal) == arrsize(kEnableTimestampInterruptPatched));
static_assert(arrsize(kGetSchedulerCallOriginal) == arrsize(kGetSchedulerCallPatched));
static_assert(arrsize(find_isDeviceValid) == arrsize(repl_isDeviceValid));
