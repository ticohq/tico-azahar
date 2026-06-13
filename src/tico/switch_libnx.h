// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#if defined(__SWITCH__)

#define Result LibnxResult
#define Service libnx_Service
#define u128 libnx_u128
#define s128 libnx_s128
#define vu128 libnx_vu128
#define vs128 libnx_vs128
#include <switch.h>
#undef Result
#undef Service
#undef u128
#undef s128
#undef vu128
#undef vs128

#ifdef R_SUCCEEDED
#undef R_SUCCEEDED
#endif
#ifdef R_FAILED
#undef R_FAILED
#endif
#ifdef R_MODULE
#undef R_MODULE
#endif
#ifdef R_DESCRIPTION
#undef R_DESCRIPTION
#endif
#ifdef R_VALUE
#undef R_VALUE
#endif
#ifdef MAKERESULT
#undef MAKERESULT
#endif
#ifdef KERNELRESULT
#undef KERNELRESULT
#endif
#ifdef BIT
#undef BIT
#endif
#ifdef BITL
#undef BITL
#endif

#endif
