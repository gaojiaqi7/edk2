#include <uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/PcdLib.h>


//ShellCEntryLib call user interface ShellAppMain
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
)
{
  EFI_STATUS                         Status = EFI_SUCCESS;
  UINTN                              DataSize;
  EFI_PHYSICAL_ADDRESS               Data;

  DataSize = 0x1000;
  Status = gBS->AllocatePages (
                AllocateAnyPages,
                EfiBootServicesData,
                DataSize,
                &Data
                );
  DEBUG ((DEBUG_INFO," [MyMemoryTest]: AllocatePages Status %r, Data: 0x%lx.\n", Status, Data));

  DataSize = 0x10000;
  Data = 0x50000000;
  Status = gBS->AllocatePages (
                AllocateAddress,
                EfiBootServicesData,
                DataSize,
                &Data
                );
  DEBUG ((DEBUG_INFO," [MyMemoryTest]: AllocatePages Status %r, Data: 0x%lx.\n", Status, Data));

  return EFI_SUCCESS;
}
