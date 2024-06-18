/** @file
  Set TPM device type

  In SecurityPkg, this module initializes the TPM device type based on a UEFI
  variable and/or hardware detection. In OvmfPkg, the module only performs TPM
  hardware detection.

  Copyright (c) 2015, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>

#include <Guid/TpmInstance.h>
#include <Guid/TcgEventHob.h>
#include <Library/DebugLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/Tpm2DeviceLib.h>
#include <Library/TcgEventLogRecordLib.h>
#include <Ppi/TpmInitialized.h>
#include <Ppi/FirmwareVolumeInfoMeasurementExcluded.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <IndustryStandard/Tdx.h>
#include <IndustryStandard/UefiTcgPlatform.h>
#include <Library/HobLib.h>
#include <Library/Tpm2CommandLib.h>

STATIC CONST EFI_PEI_PPI_DESCRIPTOR  mTpmMmioRangeAccessible = {
  EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gOvmfTpmMmioAccessiblePpiGuid,
  NULL
};

#define TD_VMCALL_SERVICE_L1VTPM_GUID \
  {0x766cf580, 0x8dc3, 0x4cea, { 0xa9, 0x4e, 0xe5, 0x42, 0x4d, 0xa1, 0xda, 0x56 } }

EFI_GUID  mTdVmcallServiceL1vtpmGuid = TD_VMCALL_SERVICE_L1VTPM_GUID;

#define TD_VMCALL_SERVICE_BLOCKING_ACTION  0
#define L1_VTPM_COMMAND_DETECT             1

struct VMCALL_SERVICE_COMMAND_BUFFER {
  EFI_GUID Guid;
  UINT32   Length;
  UINT32   Reserved;
  UINT8    Data[0];
};

struct VMCALL_SERVICE_RESPONSE_BUFFER {
  EFI_GUID Guid;
  UINT32   Length;
  UINT32   Status;
  UINT8    Data[0];
};

struct L1VTPM_COMMAND {
  UINT8 Version;
  UINT8 Command;
  UINT16 Reserved;
};

struct L1VTPM_RESPONSE {
  UINT8 Version;
  UINT8 Command;
  UINT8 Status;
  UINT8 Reserved;
  UINT8 AdditionalData[];
};

/**
  Return size of digest.

  @param[in] HashAlgo  Hash algorithm

  @return size of digest
**/
UINT16
EFIAPI
GetHashSizeFromAlgo (
  IN TPMI_ALG_HASH  HashAlgo
  )
{
  switch(HashAlgo) {
    case TPM_ALG_SHA1:
      return SHA1_DIGEST_SIZE;
    case TPM_ALG_SHA256:
      return SHA256_DIGEST_SIZE;
    case TPM_ALG_SHA384:
      return SHA384_DIGEST_SIZE;
    case TPM_ALG_SHA512:
      return SHA512_DIGEST_SIZE;
    case TPM_ALG_SM3_256:
      return SM3_256_DIGEST_SIZE;
    default:
      break;
  }

  return 0;
}

/**
  Get TPML_DIGEST_VALUES compact binary buffer size.

  @param[in]     DigestListBin    TPML_DIGEST_VALUES compact binary buffer.

  @return TPML_DIGEST_VALUES compact binary buffer size.
**/
UINT32
GetDigestListBinSize (
  IN VOID  *DigestListBin
  )
{
  UINTN          Index;
  UINT16         DigestSize;
  UINT32         TotalSize;
  UINT32         Count;
  TPMI_ALG_HASH  HashAlg;

  Count         = ReadUnaligned32 (DigestListBin);
  TotalSize     = sizeof (Count);
  DigestListBin = (UINT8 *)DigestListBin + sizeof (Count);
  for (Index = 0; Index < Count; Index++) {
    HashAlg       = ReadUnaligned16 (DigestListBin);
    TotalSize    += sizeof (HashAlg);
    DigestListBin = (UINT8 *)DigestListBin + sizeof (HashAlg);

    DigestSize    = GetHashSizeFromAlgo (HashAlg);
    TotalSize    += DigestSize;
    DigestListBin = (UINT8 *)DigestListBin + DigestSize;
  }

  return TotalSize;
}

/**
 * Build GuidHob for vRTM measurements.
 *
 * vRTM measurements include the measurement of vRTM version and TDVF image.
 * They're measured and extended to PCR[0] before the TDVF is loaded.
 * 
 * @param Event         Event log
 * @param EventSize     Size of event log
 *
 * @retval EFI_SUCCESS  Successfully build the GuidHobs
 * @retval Others       Other error as indicated
 */
EFI_STATUS
BuildVrtmMeasurementGuidHob (
  UINT8   *Event,
  UINT32   EventSize
  )
{
  VOID                *EventHobData;

  EventHobData = BuildGuidHob (
              &gTcgEvent2EntryHobGuid,
              EventSize
              );
  if (EventHobData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (EventHobData, Event, EventSize);
  return EFI_SUCCESS;
}

/**
 * Set up the VMCALL service command buffer for L1VTPM.
 *
 * Used to detect the L1 vTPM existence and the l1 TPM event log.
 * 
 * @param CommandBuffer Command buffer
 * @param BufferSize    Size of command buffer
 *
 */
VOID
SetUpVmcallServiceL1vtpmCommandBuffer (
  UINT8   *CommandBuffer,
  UINT32   BufferSize
  )
{
  struct VMCALL_SERVICE_COMMAND_BUFFER *ServiceCommand;
  struct L1VTPM_COMMAND                *L1VtpmCommand;
  UINT32                               Length;

  Length = sizeof(struct VMCALL_SERVICE_COMMAND_BUFFER) + sizeof(struct L1VTPM_COMMAND);

  // Set up service command header
  ServiceCommand = (struct VMCALL_SERVICE_COMMAND_BUFFER *)CommandBuffer;
  CopyMem((UINT8 *)&ServiceCommand->Guid, (UINT8 *)&mTdVmcallServiceL1vtpmGuid, sizeof(EFI_GUID));
  ServiceCommand->Length = Length;
  ServiceCommand->Reserved = 0;

  // Set up service command data
  L1VtpmCommand = (struct L1VTPM_COMMAND *)&ServiceCommand->Data;
  L1VtpmCommand->Version = 0;
  L1VtpmCommand->Command = L1_VTPM_COMMAND_DETECT;
  L1VtpmCommand->Reserved = 0;
}

/**
 * Set up the VMCALL service response buffer for L1VTPM.
 *
 * Used to detect the L1 vTPM existence and the l1 TPM event log.
 * 
 * @param ResponseBuffer Response buffer
 * @param BufferSize     Size of reponse buffer
 *
 */
VOID
SetUpVmcallServiceL1vtpmResponseBuffer (
  UINT8   *ResponseBuffer,
  UINT32   BufferSize
  )
{
  struct VMCALL_SERVICE_RESPONSE_BUFFER *ServiceResponse;

  // Set up service response header
  ServiceResponse = (struct VMCALL_SERVICE_RESPONSE_BUFFER *) ResponseBuffer;
  CopyMem((UINT8 *)&ServiceResponse->Guid, (UINT8 *)&mTdVmcallServiceL1vtpmGuid, sizeof(EFI_GUID));
  ServiceResponse->Length = BufferSize;
}

/**
 * Parse the VMCALL service response buffer of L1VTPM service.
 *
 * Used to detect the L1 vTPM existence and the l1 TPM event log.
 * 
 * @param ResponseBuffer Response buffer
 * @param BufferSize     Size of reponse buffer
 * @param ResponseData   Pointer to the response data
 *
 * @retval EFI_SUCCESS  Successfully build the GuidHobs
 * @retval Others       Other error as indicated
 */
EFI_STATUS
ParseVmcallServiceL1vtpmResponseBuffer (
  UINT8   *ResponseBuffer,
  UINT32  *BufferSize,
  UINT8   **ResponseData
  )
{
  struct VMCALL_SERVICE_RESPONSE_BUFFER *ServiceResponse;
  struct L1VTPM_RESPONSE *VtpmResponse;
  UINT32 HeaderLength = sizeof(struct VMCALL_SERVICE_RESPONSE_BUFFER)
    + sizeof(struct L1VTPM_RESPONSE);

  if (*BufferSize < HeaderLength) {
    return EFI_INVALID_PARAMETER;
  }

  // Set up service response header
  ServiceResponse = (struct VMCALL_SERVICE_RESPONSE_BUFFER *) ResponseBuffer;
  if (!CompareGuid(&ServiceResponse->Guid, &mTdVmcallServiceL1vtpmGuid)) {
    return EFI_INVALID_PARAMETER;
  }

  if (*BufferSize < ServiceResponse->Length) {
    return EFI_INVALID_PARAMETER;
  }
  *BufferSize = ServiceResponse->Length;

  if (ServiceResponse->Status != 0) {
    return EFI_UNSUPPORTED;
  }

  VtpmResponse = (struct L1VTPM_RESPONSE *)&ServiceResponse->Data;
  if (VtpmResponse->Command != L1_VTPM_COMMAND_DETECT) {
    return EFI_INVALID_PARAMETER;
  }

  if (VtpmResponse->Version != 0 || VtpmResponse->Status != 0) {
    return EFI_UNSUPPORTED;
  }

  *ResponseData = VtpmResponse->AdditionalData;
  return EFI_SUCCESS;
}

/**
  In TD Partitioning L2 guest, the vTPM is virtualized by a trusted L1 VMM. The
  L1 VMM initializes the vTPM and extends its version and L2 TDVF image into the
  PCR[0]. This function gets the hashes of events and records it into event log.
 *
 * @retval EFI_SUCCESS Successfully measure the TdHob
 * @retval Others      Other error as indicated
 */
EFI_STATUS
EFIAPI
TdpDetectVirtualTpm (
  VOID
  )
{
  EFI_STATUS             Status;
  UINT8                  *CommandPage;
  UINT8                  *ResponsePage;
  UINT8                  *pHobList;
  EFI_PEI_HOB_POINTERS   Hob;
  UINT8                  *Event;
  UINT32                 EventSize;
  UINT8                  *EventDigestBin;
  UINT8                  *FvEventData;
  UINT8                  FvBlobDescSize;
  UINT64                 FvBlobBase = 0;
  UINT64                 FvBlobLength = 0;
  UINT32                 ResponseSize = EFI_PAGE_SIZE;
  UINT32                 Offset = 0;
  EFI_PEI_FIRMWARE_VOLUME_INFO_MEASUREMENT_EXCLUDED_PPI  *MeasurementExcludedFvPpi;
  EFI_PEI_PPI_DESCRIPTOR                                 *MeasurementExcludedPpiList;

  CommandPage = AllocatePages(1);
  if (CommandPage == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  SetUpVmcallServiceL1vtpmCommandBuffer(CommandPage, EFI_PAGE_SIZE);

  ResponsePage = AllocatePages(1);
  if (ResponsePage == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  SetUpVmcallServiceL1vtpmResponseBuffer(ResponsePage, EFI_PAGE_SIZE);

  Status = TdVmCall (
             TDVMCALL_SERVICE,
             (UINT64)CommandPage,
             (UINT64)ResponsePage,
             TD_VMCALL_SERVICE_BLOCKING_ACTION, // Blocking action
             0, // Timeout
             0
             );

  if (EFI_ERROR (Status)) {
    goto exit;
  }

  Status = ParseVmcallServiceL1vtpmResponseBuffer (ResponsePage, &ResponseSize, &pHobList);

  if (EFI_ERROR (Status)) {
    goto exit;
  }

  while ((Hob.Raw = GetNextGuidHob (&gTcgEvent2EntryHobGuid, pHobList + Offset)) != NULL) {
    Event = Hob.Raw + sizeof(EFI_HOB_GUID_TYPE);
    EventSize = Hob.Guid->Header.HobLength - sizeof(EFI_HOB_GUID_TYPE);
    Status = BuildVrtmMeasurementGuidHob (
      Event,      // EventData
      EventSize   // EventSize
    );
    if (Status != EFI_SUCCESS) {
      goto exit;
    }

    if (((TCG_PCR_EVENT2_HDR*)Event)->EventType == EV_EFI_PLATFORM_FIRMWARE_BLOB2) {
      EventDigestBin = Event + sizeof (TCG_PCRINDEX) + sizeof (TCG_EVENTTYPE);
      FvEventData = EventDigestBin + GetDigestListBinSize (EventDigestBin) + sizeof (UINT32);
      FvBlobDescSize = *(UINT8*)FvEventData;
      FvBlobBase = *(UINT64*)(FvEventData + sizeof(FvBlobDescSize) + FvBlobDescSize);
      FvBlobLength = *(UINT64*)(FvEventData + sizeof(FvBlobDescSize) + FvBlobDescSize + sizeof(FvBlobBase));

      MeasurementExcludedFvPpi = AllocatePool (sizeof (*MeasurementExcludedFvPpi));
      ASSERT (MeasurementExcludedFvPpi != NULL);
      MeasurementExcludedFvPpi->Count          = 1;
      MeasurementExcludedFvPpi->Fv[0].FvBase   = FvBlobBase;
      MeasurementExcludedFvPpi->Fv[0].FvLength = FvBlobLength;

      MeasurementExcludedPpiList = AllocatePool (sizeof (*MeasurementExcludedPpiList));
      ASSERT (MeasurementExcludedPpiList != NULL);
      MeasurementExcludedPpiList->Flags = EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST;
      MeasurementExcludedPpiList->Guid  = &gEfiPeiFirmwareVolumeInfoMeasurementExcludedPpiGuid;
      MeasurementExcludedPpiList->Ppi   = MeasurementExcludedFvPpi;

      Status = PeiServicesInstallPpi (MeasurementExcludedPpiList);
      if (Status != EFI_SUCCESS) {
        goto exit;
      }
    }

    Offset += Hob.Guid->Header.HobLength;
    if (Offset >= ResponseSize) {
      break;
    }
  }

  PcdSet8S (PcdTpm2InitializationPolicy, 0);
  Status = EFI_SUCCESS;

exit:
  FreePages(CommandPage, 1);
  FreePages(ResponsePage, 1);
  return Status;
}

/**
  The entry point for TDP vTPM detection driver.

  @param  FileHandle  Handle of the file being invoked.
  @param  PeiServices Describes the list of possible PEI Services.
**/
EFI_STATUS
EFIAPI
TdpVtpmDetectPeiEntryPoint (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "%a\n", __func__));

  if (TdpIsEnabled()) {
    Status = TdpDetectVirtualTpm();
    if (RETURN_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: failed to detect the vTPM\n",
        __func__
        ));
      ASSERT_RETURN_ERROR (Status);
    }
  }

  //
  // MMIO range available
  //
  Status = PeiServicesInstallPpi (&mTpmMmioRangeAccessible);
  ASSERT_EFI_ERROR (Status);

  return Status;
}
