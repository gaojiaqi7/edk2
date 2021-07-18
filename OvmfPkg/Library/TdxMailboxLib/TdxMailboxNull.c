/** @file

  Copyright (c) 2008, Intel Corporation. All rights reserved.<BR>
  (C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/DebugLib.h>
#include <Library/TdxMailboxLib.h>


/**
  This function will be called by BSP to get the CPU number.

  @retval   CPU number
**/
UINT32
EFIAPI
GetCpusNum (
  VOID
  )
{
  ASSERT (FALSE);
  return 0;
}

/**
  Get the address of Td mailbox.
**/
volatile VOID *
EFIAPI
GetTdxMailBox (
  VOID
  )
{
  ASSERT (FALSE);
  return (volatile VOID *) NULL;
}

/**
  This function will be called by BSP to wakeup APs the are spinning on mailbox
  in protected mode

  @param[in] Command          Command to send APs
  @param[in] WakeupVector     If used, address for APs to start executing
  @param[in] WakeArgsX        Args to pass to APs for excuting commands
**/
VOID
EFIAPI
MpSendWakeupCommand (
  IN UINT16 Command,
  IN UINT64 WakeupVector,
  IN UINT64 WakeupArgs1,
  IN UINT64 WakeupArgs2,
  IN UINT64 WakeupArgs3,
  IN UINT64 WakeupArgs4
)
{
  ASSERT (FALSE);
}

/**
  BSP wait until all the APs arriving. It means the task triggered by BSP is started.
**/
VOID
EFIAPI
MpSerializeStart (
  VOID
  )
{
  ASSERT (FALSE);
}

/**
  BSP wait until all the APs arriving. It means the task triggered by BSP is ended.
**/
VOID
EFIAPI
MpSerializeEnd (
  VOID
  )
{
  ASSERT (FALSE);
}
