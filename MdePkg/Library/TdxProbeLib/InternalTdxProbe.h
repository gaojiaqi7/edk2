/** @file
  Copyright (c) 2020 - 2021, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef INTERNAL_TDX_PROBE_H_
#define INTERNAL_TDX_PROBE_H_

#define PROBE_IS_TD_GUEST     0
#define PROBE_NOT_TD_GUEST    1

/**
  The internal Td Probe implementation.

  @return 0       TD guest
  @return others  Non-TD guest
**/
UINTN
EFIAPI
TdProbe (
  VOID
  );

#endif
