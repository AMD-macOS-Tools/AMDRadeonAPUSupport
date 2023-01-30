//  Copyright © 2022 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.0. See LICENSE for
//  details.

#pragma once
#include <libkern/c++/OSData.h>

struct FwDesc {
    const char *name;
    const uint8_t *var;
    const uint32_t size;
};

#define RAD_FW(fw_name, fw_var, fw_size) .name = fw_name, .var = fw_var, .size = fw_size

extern const struct FwDesc fwList[];
extern const int fwNumber;

inline const FwDesc *getFWDescByName(const char *name) {
    for (int i = 0; i < fwNumber; i++) {
        if (strcmp(fwList[i].name, name) == 0) { return fwList + i; }
    }
    return nullptr;
}