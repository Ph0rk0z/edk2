/** @file
  This driver is used to manage SD/MMC PCI host controllers which are compliance
  with SD Host Controller Simplified Specification version 3.00.

  It would expose EFI_SD_MMC_PASS_THRU_PROTOCOL for upper layer use.

  Copyright (c) 2015 - 2017, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "SdMmcPciHcDxe.h"

int g_deviceId = 0;


/**
  Dump the content of SD/MMC host controller's Capability Register.

  @param[in]  Slot            The slot number of the SD card to send the command to.
  @param[in]  Capability      The buffer to store the capability data.

**/
VOID
DumpCapabilityReg (
  IN UINT8                Slot,
  IN SD_MMC_HC_SLOT_CAP   *Capability
  )
{
  //
  // Dump Capability Data
  //
  DEBUG ((DEBUG_INFO, " == Slot [%d] Capability is 0x%x ==\n", Slot, Capability));
  DEBUG ((DEBUG_INFO, "   Timeout Clk Freq  %d%a\n", Capability->TimeoutFreq, (Capability->TimeoutUnit) ? "MHz" : "KHz"));
  DEBUG ((DEBUG_INFO, "   Base Clk Freq     %dMHz\n", Capability->BaseClkFreq));
  DEBUG ((DEBUG_INFO, "   Max Blk Len       %dbytes\n", 512 * (1 << Capability->MaxBlkLen)));
  DEBUG ((DEBUG_INFO, "   8-bit Support     %a\n", Capability->BusWidth8 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   ADMA2 Support     %a\n", Capability->Adma2 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   HighSpeed Support %a\n", Capability->HighSpeed ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   SDMA Support      %a\n", Capability->Sdma ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Suspend/Resume    %a\n", Capability->SuspRes ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Voltage 3.3       %a\n", Capability->Voltage33 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Voltage 3.0       %a\n", Capability->Voltage30 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Voltage 1.8       %a\n", Capability->Voltage18 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   64-bit Sys Bus    %a\n", Capability->SysBus64 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Async Interrupt   %a\n", Capability->AsyncInt ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   SlotType          "));
  if (Capability->SlotType == 0x00) {
    DEBUG ((DEBUG_INFO, "%a\n", "Removable Slot"));
  } else if (Capability->SlotType == 0x01) {
    DEBUG ((DEBUG_INFO, "%a\n", "Embedded Slot"));
  } else if (Capability->SlotType == 0x02) {
    DEBUG ((DEBUG_INFO, "%a\n", "Shared Bus Slot"));
  } else {
    DEBUG ((DEBUG_INFO, "%a\n", "Reserved"));
  }
  DEBUG ((DEBUG_INFO, "   SDR50  Support    %a\n", Capability->Sdr50 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   SDR104 Support    %a\n", Capability->Sdr104 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   DDR50  Support    %a\n", Capability->Ddr50 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Driver Type A     %a\n", Capability->DriverTypeA ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Driver Type C     %a\n", Capability->DriverTypeC ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Driver Type D     %a\n", Capability->DriverTypeD ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Driver Type 4     %a\n", Capability->DriverType4 ? "TRUE" : "FALSE"));
  if (Capability->TimerCount == 0) {
    DEBUG ((DEBUG_INFO, "   Retuning TimerCnt Disabled\n", 2 * (Capability->TimerCount - 1)));
  } else {
    DEBUG ((DEBUG_INFO, "   Retuning TimerCnt %dseconds\n", 2 * (Capability->TimerCount - 1)));
  }
  DEBUG ((DEBUG_INFO, "   SDR50 Tuning      %a\n", Capability->TuningSDR50 ? "TRUE" : "FALSE"));
  DEBUG ((DEBUG_INFO, "   Retuning Mode     Mode %d\n", Capability->RetuningMod + 1));
  DEBUG ((DEBUG_INFO, "   Clock Multiplier  M = %d\n", Capability->ClkMultiplier + 1));
  DEBUG ((DEBUG_INFO, "   HS 400            %a\n", Capability->Hs400 ? "TRUE" : "FALSE"));
  return;
}

/**
  Read SlotInfo register from SD/MMC host controller pci config space.

  @param[in]  PciIo        The PCI IO protocol instance.
  @param[out] FirstBar     The buffer to store the first BAR value.
  @param[out] SlotNum      The buffer to store the supported slot number.

  @retval EFI_SUCCESS      The operation succeeds.
  @retval Others           The operation fails.

**/
EFI_STATUS
EFIAPI
SdMmcHcGetSlotInfo (
  IN     EFI_PCI_IO_PROTOCOL   *PciIo,
     OUT UINT8                 *FirstBar,
     OUT UINT8                 *SlotNum
  )
{
  EFI_STATUS                   Status;
  SD_MMC_HC_SLOT_INFO          SlotInfo;

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        SD_MMC_HC_SLOT_OFFSET,
                        sizeof (SlotInfo),
                        &SlotInfo
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *FirstBar = SlotInfo.FirstBar;
  *SlotNum  = SlotInfo.SlotNum + 1;
  ASSERT ((*FirstBar + *SlotNum) < SD_MMC_HC_MAX_SLOT);
  return EFI_SUCCESS;
}

/**
  Read/Write specified SD/MMC host controller mmio register.

  @param[in]      PciIo        The PCI IO protocol instance.
  @param[in]      BarIndex     The BAR index of the standard PCI Configuration
                               header to use as the base address for the memory
                               operation to perform.
  @param[in]      Offset       The offset within the selected BAR to start the
                               memory operation.
  @param[in]      Read         A boolean to indicate it's read or write operation.
  @param[in]      Count        The width of the mmio register in bytes.
                               Must be 1, 2 , 4 or 8 bytes.
  @param[in, out] Data         For read operations, the destination buffer to store
                               the results. For write operations, the source buffer
                               to write data from. The caller is responsible for
                               having ownership of the data buffer and ensuring its
                               size not less than Count bytes.

  @retval EFI_INVALID_PARAMETER The PciIo or Data is NULL or the Count is not valid.
  @retval EFI_SUCCESS           The read/write operation succeeds.
  @retval Others                The read/write operation fails.

**/
EFI_STATUS
EFIAPI
SdMmcHcRwMmio (
  IN     EFI_PCI_IO_PROTOCOL   *PciIo,
  IN     UINT8                 BarIndex,
  IN     UINT32                Offset,
  IN     BOOLEAN               Read,
  IN     UINT8                 Count,
  IN OUT VOID                  *Data
  )
{
  EFI_STATUS                   Status;

  if ((PciIo == NULL) || (Data == NULL))  {
    return EFI_INVALID_PARAMETER;
  }

  if ((Count != 1) && (Count != 2) && (Count != 4) && (Count != 8)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Read) {
    Status = PciIo->Mem.Read (
                          PciIo,
                          EfiPciIoWidthUint8,
                          BarIndex,
                          (UINT64) Offset,
                          Count,
                          Data
                          );
  } else {
    Status = PciIo->Mem.Write (
                          PciIo,
                          EfiPciIoWidthUint8,
                          BarIndex,
                          (UINT64) Offset,
                          Count,
                          Data
                          );
  }

  return Status;
}

/**
  Do OR operation with the value of the specified SD/MMC host controller mmio register.

  @param[in] PciIo             The PCI IO protocol instance.
  @param[in] BarIndex          The BAR index of the standard PCI Configuration
                               header to use as the base address for the memory
                               operation to perform.
  @param[in] Offset            The offset within the selected BAR to start the
                               memory operation.
  @param[in] Count             The width of the mmio register in bytes.
                               Must be 1, 2 , 4 or 8 bytes.
  @param[in] OrData            The pointer to the data used to do OR operation.
                               The caller is responsible for having ownership of
                               the data buffer and ensuring its size not less than
                               Count bytes.

  @retval EFI_INVALID_PARAMETER The PciIo or OrData is NULL or the Count is not valid.
  @retval EFI_SUCCESS           The OR operation succeeds.
  @retval Others                The OR operation fails.

**/
EFI_STATUS
EFIAPI
SdMmcHcOrMmio (
  IN  EFI_PCI_IO_PROTOCOL      *PciIo,
  IN  UINT8                    BarIndex,
  IN  UINT32                   Offset,
  IN  UINT8                    Count,
  IN  VOID                     *OrData
  )
{
  EFI_STATUS                   Status;
  UINT64                       Data;
  UINT64                       Or;

  Status = SdMmcHcRwMmio (PciIo, BarIndex, Offset, TRUE, Count, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Count == 1) {
    Or = *(UINT8*) OrData;
  } else if (Count == 2) {
    Or = *(UINT16*) OrData;
  } else if (Count == 4) {
    Or = *(UINT32*) OrData;
  } else if (Count == 8) {
    Or = *(UINT64*) OrData;
  } else {
    return EFI_INVALID_PARAMETER;
  }

  Data  |= Or;
  Status = SdMmcHcRwMmio (PciIo, BarIndex, Offset, FALSE, Count, &Data);

  return Status;
}

/**
  Do AND operation with the value of the specified SD/MMC host controller mmio register.

  @param[in] PciIo             The PCI IO protocol instance.
  @param[in] BarIndex          The BAR index of the standard PCI Configuration
                               header to use as the base address for the memory
                               operation to perform.
  @param[in] Offset            The offset within the selected BAR to start the
                               memory operation.
  @param[in] Count             The width of the mmio register in bytes.
                               Must be 1, 2 , 4 or 8 bytes.
  @param[in] AndData           The pointer to the data used to do AND operation.
                               The caller is responsible for having ownership of
                               the data buffer and ensuring its size not less than
                               Count bytes.

  @retval EFI_INVALID_PARAMETER The PciIo or AndData is NULL or the Count is not valid.
  @retval EFI_SUCCESS           The AND operation succeeds.
  @retval Others                The AND operation fails.

**/
EFI_STATUS
EFIAPI
SdMmcHcAndMmio (
  IN  EFI_PCI_IO_PROTOCOL      *PciIo,
  IN  UINT8                    BarIndex,
  IN  UINT32                   Offset,
  IN  UINT8                    Count,
  IN  VOID                     *AndData
  )
{
  EFI_STATUS                   Status;
  UINT64                       Data;
  UINT64                       And;

  Status = SdMmcHcRwMmio (PciIo, BarIndex, Offset, TRUE, Count, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Count == 1) {
    And = *(UINT8*) AndData;
  } else if (Count == 2) {
    And = *(UINT16*) AndData;
  } else if (Count == 4) {
    And = *(UINT32*) AndData;
  } else if (Count == 8) {
    And = *(UINT64*) AndData;
  } else {
    return EFI_INVALID_PARAMETER;
  }

  Data  &= And;
  Status = SdMmcHcRwMmio (PciIo, BarIndex, Offset, FALSE, Count, &Data);

  return Status;
}

/**
  Wait for the value of the specified MMIO register set to the test value.

  @param[in]  PciIo         The PCI IO protocol instance.
  @param[in]  BarIndex      The BAR index of the standard PCI Configuration
                            header to use as the base address for the memory
                            operation to perform.
  @param[in]  Offset        The offset within the selected BAR to start the
                            memory operation.
  @param[in]  Count         The width of the mmio register in bytes.
                            Must be 1, 2, 4 or 8 bytes.
  @param[in]  MaskValue     The mask value of memory.
  @param[in]  TestValue     The test value of memory.

  @retval EFI_NOT_READY     The MMIO register hasn't set to the expected value.
  @retval EFI_SUCCESS       The MMIO register has expected value.
  @retval Others            The MMIO operation fails.

**/
EFI_STATUS
EFIAPI
SdMmcHcCheckMmioSet (
  IN  EFI_PCI_IO_PROTOCOL       *PciIo,
  IN  UINT8                     BarIndex,
  IN  UINT32                    Offset,
  IN  UINT8                     Count,
  IN  UINT64                    MaskValue,
  IN  UINT64                    TestValue
  )
{
  EFI_STATUS            Status;
  UINT64                Value;

  //
  // Access PCI MMIO space to see if the value is the tested one.
  //
  Value  = 0;
  Status = SdMmcHcRwMmio (PciIo, BarIndex, Offset, TRUE, Count, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Value &= MaskValue;

  if (Value == TestValue) {
    return EFI_SUCCESS;
  }

  return EFI_NOT_READY;
}

/**
  Wait for the value of the specified MMIO register set to the test value.

  @param[in]  PciIo         The PCI IO protocol instance.
  @param[in]  BarIndex      The BAR index of the standard PCI Configuration
                            header to use as the base address for the memory
                            operation to perform.
  @param[in]  Offset        The offset within the selected BAR to start the
                            memory operation.
  @param[in]  Count         The width of the mmio register in bytes.
                            Must be 1, 2, 4 or 8 bytes.
  @param[in]  MaskValue     The mask value of memory.
  @param[in]  TestValue     The test value of memory.
  @param[in]  Timeout       The time out value for wait memory set, uses 1
                            microsecond as a unit.

  @retval EFI_TIMEOUT       The MMIO register hasn't expected value in timeout
                            range.
  @retval EFI_SUCCESS       The MMIO register has expected value.
  @retval Others            The MMIO operation fails.

**/
EFI_STATUS
EFIAPI
SdMmcHcWaitMmioSet (
  IN  EFI_PCI_IO_PROTOCOL       *PciIo,
  IN  UINT8                     BarIndex,
  IN  UINT32                    Offset,
  IN  UINT8                     Count,
  IN  UINT64                    MaskValue,
  IN  UINT64                    TestValue,
  IN  UINT64                    Timeout
  )
{
  EFI_STATUS            Status;
  BOOLEAN               InfiniteWait;

  if (Timeout == 0) {
    InfiniteWait = TRUE;
  } else {
    InfiniteWait = FALSE;
  }

  while (InfiniteWait || (Timeout > 0)) {
    Status = SdMmcHcCheckMmioSet (
               PciIo,
               BarIndex,
               Offset,
               Count,
               MaskValue,
               TestValue
               );
    if (Status != EFI_NOT_READY) {
      return Status;
    }

    //
    // Stall for 1 microsecond.
    //
    gBS->Stall (1);

    Timeout--;
  }

  return EFI_TIMEOUT;
}

/**
  Software reset the specified SD/MMC host controller and enable all interrupts.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Slot           The slot number of the SD card to send the command to.

  @retval EFI_SUCCESS       The software reset executes successfully.
  @retval Others            The software reset fails.

**/
EFI_STATUS
SdMmcHcReset (
  IN SD_MMC_HC_PRIVATE_DATA *Private,
  IN UINT8                  Slot
  )
{
  EFI_STATUS                Status;
  UINT8                     SwReset;
  EFI_PCI_IO_PROTOCOL       *PciIo;

  //
  // Notify the SD/MMC override protocol that we are about to reset
  // the SD/MMC host controller.
  //
  if (mOverride != NULL && mOverride->NotifyPhase != NULL) {
    Status = mOverride->NotifyPhase (
                          Private->ControllerHandle,
                          Slot,
                          EdkiiSdMmcResetPre,
                          NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN,
        "%a: SD/MMC pre reset notifier callback failed - %r\n",
        __FUNCTION__, Status));
      return Status;
    }
  }

  PciIo   = Private->PciIo;
  SwReset = BIT0;
  Status  = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_SW_RST, sizeof (SwReset), &SwReset);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SdMmcHcReset: write SW Reset for All fails: %r\n", Status));
    return Status;
  }

  Status = SdMmcHcWaitMmioSet (
             PciIo,
             Slot,
             SD_MMC_HC_SW_RST,
             sizeof (SwReset),
             BIT0,
             0x00,
             SD_MMC_HC_GENERIC_TIMEOUT
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "SdMmcHcReset: reset done with %r\n", Status));
    return Status;
  }

  //
  // Enable all interrupt after reset all.
  //
  Status = SdMmcHcEnableInterrupt (PciIo, Slot);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "SdMmcHcReset: SdMmcHcEnableInterrupt done with %r\n",
      Status));
    return Status;
  }

  //
  // Notify the SD/MMC override protocol that we have just reset
  // the SD/MMC host controller.
  //
  if (mOverride != NULL && mOverride->NotifyPhase != NULL) {
    Status = mOverride->NotifyPhase (
                          Private->ControllerHandle,
                          Slot,
                          EdkiiSdMmcResetPost,
                          NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN,
        "%a: SD/MMC post reset notifier callback failed - %r\n",
        __FUNCTION__, Status));
    }
  }

  return Status;
}

/**
  Set all interrupt status bits in Normal and Error Interrupt Status Enable
  register.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.

  @retval EFI_SUCCESS       The operation executes successfully.
  @retval Others            The operation fails.

**/
EFI_STATUS
SdMmcHcEnableInterrupt (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot
  )
{
  EFI_STATUS                Status;
  UINT16                    IntStatus;

  //
  // Enable all bits in Error Interrupt Status Enable Register
  //
  IntStatus = 0xFFFF;
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_ERR_INT_STS_EN, FALSE, sizeof (IntStatus), &IntStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Enable all bits in Normal Interrupt Status Enable Register
  //
  IntStatus = 0xFFFF;
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_NOR_INT_STS_EN, FALSE, sizeof (IntStatus), &IntStatus);

  return Status;
}

/**
  Get the capability data from the specified slot.

  @param[in]  PciIo           The PCI IO protocol instance.
  @param[in]  Slot            The slot number of the SD card to send the command to.
  @param[out] Capability      The buffer to store the capability data.

  @retval EFI_SUCCESS         The operation executes successfully.
  @retval Others              The operation fails.

**/
EFI_STATUS
SdMmcHcGetCapability (
  IN     EFI_PCI_IO_PROTOCOL  *PciIo,
  IN     UINT8                Slot,
     OUT SD_MMC_HC_SLOT_CAP   *Capability
  )
{
  EFI_STATUS                Status;
  UINT64                    Cap;

  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_CAP, TRUE, sizeof (Cap), &Cap);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CopyMem (Capability, &Cap, sizeof (Cap));

  return EFI_SUCCESS;
}

/**
  Get the maximum current capability data from the specified slot.

  @param[in]  PciIo           The PCI IO protocol instance.
  @param[in]  Slot            The slot number of the SD card to send the command to.
  @param[out] MaxCurrent      The buffer to store the maximum current capability data.

  @retval EFI_SUCCESS         The operation executes successfully.
  @retval Others              The operation fails.

**/
EFI_STATUS
SdMmcHcGetMaxCurrent (
  IN     EFI_PCI_IO_PROTOCOL  *PciIo,
  IN     UINT8                Slot,
     OUT UINT64               *MaxCurrent
  )
{
  EFI_STATUS          Status;

  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_MAX_CURRENT_CAP, TRUE, sizeof (UINT64), MaxCurrent);

  return Status;
}

/**
  Detect whether there is a SD/MMC card attached at the specified SD/MMC host controller
  slot.

  Refer to SD Host Controller Simplified spec 3.0 Section 3.1 for details.

  @param[in]  PciIo         The PCI IO protocol instance.
  @param[in]  Slot          The slot number of the SD card to send the command to.
  @param[out] MediaPresent  The pointer to the media present boolean value.

  @retval EFI_SUCCESS       There is no media change happened.
  @retval EFI_MEDIA_CHANGED There is media change happened.
  @retval Others            The detection fails.

**/
EFI_STATUS
SdMmcHcCardDetect (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
     OUT BOOLEAN            *MediaPresent
  )
{
  EFI_STATUS                Status;
  UINT16                    Data;
  UINT32                    PresentState;

  //
  // Check Present State Register to see if there is a card presented.
  //
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_PRESENT_STATE, TRUE, sizeof (PresentState), &PresentState);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((PresentState & BIT16) != 0) {
    *MediaPresent = TRUE;
  } else {
    *MediaPresent = FALSE;
  }

  //
  // Check Normal Interrupt Status Register
  //
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_NOR_INT_STS, TRUE, sizeof (Data), &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Data & (BIT6 | BIT7)) != 0) {
    //
    // Clear BIT6 and BIT7 by writing 1 to these two bits if set.
    //
    Data  &= BIT6 | BIT7;
    Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_NOR_INT_STS, FALSE, sizeof (Data), &Data);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    return EFI_MEDIA_CHANGED;
  }

  return EFI_SUCCESS;
}

/**
  Stop SD/MMC card clock.

  Refer to SD Host Controller Simplified spec 3.0 Section 3.2.2 for details.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.

  @retval EFI_SUCCESS       Succeed to stop SD/MMC clock.
  @retval Others            Fail to stop SD/MMC clock.

**/
EFI_STATUS
SdMmcHcStopClock (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot
  )
{
  EFI_STATUS                Status;
  UINT32                    PresentState;
  UINT16                    ClockCtrl;

  //
  // Ensure no SD transactions are occurring on the SD Bus by
  // waiting for Command Inhibit (DAT) and Command Inhibit (CMD)
  // in the Present State register to be 0.
  //
  Status = SdMmcHcWaitMmioSet (
             PciIo,
             Slot,
             SD_MMC_HC_PRESENT_STATE,
             sizeof (PresentState),
             BIT0 | BIT1,
             0,
             SD_MMC_HC_GENERIC_TIMEOUT
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Set SD Clock Enable in the Clock Control register to 0
  //
  ClockCtrl = (UINT16)~BIT2;
  Status = SdMmcHcAndMmio (PciIo, Slot, SD_MMC_HC_CLOCK_CTRL, sizeof (ClockCtrl), &ClockCtrl);

  return Status;
}

/**
  SD/MMC card clock supply.

  Refer to SD Host Controller Simplified spec 3.0 Section 3.2.1 for details.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] ClockFreq      The max clock frequency to be set. The unit is KHz.
  @param[in] BaseClkFreq    The base clock frequency of host controller in MHz.

  @retval EFI_SUCCESS       The clock is supplied successfully.
  @retval Others            The clock isn't supplied successfully.

**/
EFI_STATUS
SdMmcHcClockSupply (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN UINT64                 ClockFreq,
  IN UINT32                 BaseClkFreq
  )
{
  EFI_STATUS                Status;
  UINT32                    SettingFreq;
  UINT32                    Divisor;
  UINT32                    Remainder;
  UINT16                    ControllerVer;
  UINT16                    ClockCtrl;

  //
  // Calculate a divisor for SD clock frequency
  //
  ASSERT (BaseClkFreq != 0);

  if (ClockFreq == 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (ClockFreq > (BaseClkFreq * 1000)) {
    ClockFreq = BaseClkFreq * 1000;
  }

  //
  // Calculate the divisor of base frequency.
  //
  Divisor     = 0;
  SettingFreq = BaseClkFreq * 1000;
  while (ClockFreq < SettingFreq) {
    Divisor++;

    SettingFreq = (BaseClkFreq * 1000) / (2 * Divisor);
    Remainder   = (BaseClkFreq * 1000) % (2 * Divisor);
    if ((ClockFreq == SettingFreq) && (Remainder == 0)) {
      break;
    }
    if ((ClockFreq == SettingFreq) && (Remainder != 0)) {
      SettingFreq ++;
    }
  }

  DEBUG ((DEBUG_INFO, "BaseClkFreq %dMHz Divisor %d ClockFreq %dKhz\n", BaseClkFreq, Divisor, ClockFreq));

  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_CTRL_VER, TRUE, sizeof (ControllerVer), &ControllerVer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Set SDCLK Frequency Select and Internal Clock Enable fields in Clock Control register.
  //
  if (((ControllerVer & 0xFF) >= SD_MMC_HC_CTRL_VER_300) &&
      ((ControllerVer & 0xFF) <= SD_MMC_HC_CTRL_VER_420)) {
    ASSERT (Divisor <= 0x3FF);
    ClockCtrl = ((Divisor & 0xFF) << 8) | ((Divisor & 0x300) >> 2);
  } else if (((ControllerVer & 0xFF) == 0) || ((ControllerVer & 0xFF) == 1)) {
    //
    // Only the most significant bit can be used as divisor.
    //
    if (((Divisor - 1) & Divisor) != 0) {
      Divisor = 1 << (HighBitSet32 (Divisor) + 1);
    }
    ASSERT (Divisor <= 0x80);
    ClockCtrl = (Divisor & 0xFF) << 8;
  } else {
    DEBUG ((DEBUG_ERROR, "Unknown SD Host Controller Spec version [0x%x]!!!\n", ControllerVer));
    return EFI_UNSUPPORTED;
  }

  //
  // Stop bus clock at first
  //
  Status = SdMmcHcStopClock (PciIo, Slot);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Supply clock frequency with specified divisor
  //
  ClockCtrl |= BIT0;
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_CLOCK_CTRL, FALSE, sizeof (ClockCtrl), &ClockCtrl);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Set SDCLK Frequency Select and Internal Clock Enable fields fails\n"));
    return Status;
  }

  //
  // Wait Internal Clock Stable in the Clock Control register to be 1
  //
  Status = SdMmcHcWaitMmioSet (
             PciIo,
             Slot,
             SD_MMC_HC_CLOCK_CTRL,
             sizeof (ClockCtrl),
             BIT1,
             BIT1,
             SD_MMC_HC_GENERIC_TIMEOUT
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Set SD Clock Enable in the Clock Control register to 1
  //
  ClockCtrl = BIT2;
  Status = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_CLOCK_CTRL, sizeof (ClockCtrl), &ClockCtrl);

  return Status;
}

/**
  SD/MMC bus power control.

  Refer to SD Host Controller Simplified spec 3.0 Section 3.3 for details.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] PowerCtrl      The value setting to the power control register.

  @retval TRUE              There is a SD/MMC card attached.
  @retval FALSE             There is no a SD/MMC card attached.

**/
EFI_STATUS
SdMmcHcPowerControl (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN UINT8                  PowerCtrl
  )
{
  EFI_STATUS                Status;

  //
  // Clr SD Bus Power
  //
  PowerCtrl &= (UINT8)~BIT0;
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_POWER_CTRL, FALSE, sizeof (PowerCtrl), &PowerCtrl);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Set SD Bus Voltage Select and SD Bus Power fields in Power Control Register
  //
  PowerCtrl |= BIT0;
  Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_POWER_CTRL, FALSE, sizeof (PowerCtrl), &PowerCtrl);

  return Status;
}

/**
  Set the SD/MMC bus width.

  Refer to SD Host Controller Simplified spec 3.0 Section 3.4 for details.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] BusWidth       The bus width used by the SD/MMC device, it must be 1, 4 or 8.

  @retval EFI_SUCCESS       The bus width is set successfully.
  @retval Others            The bus width isn't set successfully.

**/
EFI_STATUS
SdMmcHcSetBusWidth (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN UINT16                 BusWidth
  )
{
  EFI_STATUS                Status;
  UINT8                     HostCtrl1;

  if (BusWidth == 1) {
    HostCtrl1 = (UINT8)~(BIT5 | BIT1);
    Status = SdMmcHcAndMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, sizeof (HostCtrl1), &HostCtrl1);
  } else if (BusWidth == 4) {
    Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, TRUE, sizeof (HostCtrl1), &HostCtrl1);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    HostCtrl1 |= BIT1;
    HostCtrl1 &= (UINT8)~BIT5;
    Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, FALSE, sizeof (HostCtrl1), &HostCtrl1);
  } else if (BusWidth == 8) {
    Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, TRUE, sizeof (HostCtrl1), &HostCtrl1);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    HostCtrl1 &= (UINT8)~BIT1;
    HostCtrl1 |= BIT5;
    Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, FALSE, sizeof (HostCtrl1), &HostCtrl1);
  } else {
    ASSERT (FALSE);
    return EFI_INVALID_PARAMETER;
  }

  return Status;
}

/**
  Supply SD/MMC card with lowest clock frequency at initialization.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] BaseClkFreq    The base clock frequency of host controller in MHz.

  @retval EFI_SUCCESS       The clock is supplied successfully.
  @retval Others            The clock isn't supplied successfully.

**/
EFI_STATUS
SdMmcHcInitClockFreq (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN UINT32                 BaseClkFreq
  )
{
  EFI_STATUS                Status;
  UINT32                    InitFreq;

  //
  // According to SDHCI specification ver. 4.2, BaseClkFreq field value of
  // the Capability Register 1 can be zero, which means a need for obtaining
  // the clock frequency via another method. Fail in case it is not updated
  // by SW at this point.
  //
  if (BaseClkFreq == 0) {
    //
    // Don't support get Base Clock Frequency information via another method
    //
    return EFI_UNSUPPORTED;
  }
  //
  // Supply 400KHz clock frequency at initialization phase.
  //
  InitFreq = 400;
  Status = SdMmcHcClockSupply (PciIo, Slot, InitFreq, BaseClkFreq);
  return Status;
}

/**
  Supply SD/MMC card with maximum voltage at initialization.

  Refer to SD Host Controller Simplified spec 3.0 Section 3.3 for details.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] Capability     The capability of the slot.

  @retval EFI_SUCCESS       The voltage is supplied successfully.
  @retval Others            The voltage isn't supplied successfully.

**/
EFI_STATUS
SdMmcHcInitPowerVoltage (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN SD_MMC_HC_SLOT_CAP     Capability
  )
{
  EFI_STATUS                Status;
  UINT8                     MaxVoltage;
  UINT8                     HostCtrl2;

  //
  // Calculate supported maximum voltage according to SD Bus Voltage Select
  //
  if (Capability.Voltage33 != 0) {
    //
    // Support 3.3V
    //
    MaxVoltage = 0x0E;
  } else if (Capability.Voltage30 != 0) {
    //
    // Support 3.0V
    //
    MaxVoltage = 0x0C;
  } else if (Capability.Voltage18 != 0) {
    //
    // Support 1.8V
    //
    MaxVoltage = 0x0A;
    HostCtrl2  = BIT3;
    Status = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL2, sizeof (HostCtrl2), &HostCtrl2);
    gBS->Stall (5000);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else {
    ASSERT (FALSE);
    return EFI_DEVICE_ERROR;
  }

  //
  // Set SD Bus Voltage Select and SD Bus Power fields in Power Control Register
  //
  Status = SdMmcHcPowerControl (PciIo, Slot, MaxVoltage);
  if (BhtHostPciSupport(PciIo)){
    // 1.8V signaling enable
    HostCtrl2  = BIT3;
    Status = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL2, sizeof (HostCtrl2), &HostCtrl2);
    gBS->Stall (5000);
    if (EFI_ERROR (Status)) {
      return Status;
    }	  
  }

  return Status;
}

/**
  Initialize the Timeout Control register with most conservative value at initialization.

  Refer to SD Host Controller Simplified spec 3.0 Section 2.2.15 for details.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.

  @retval EFI_SUCCESS       The timeout control register is configured successfully.
  @retval Others            The timeout control register isn't configured successfully.

**/
EFI_STATUS
SdMmcHcInitTimeoutCtrl (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot
  )
{
  EFI_STATUS                Status;
  UINT8                     Timeout;

  Timeout = 0x0E;
  Status  = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_TIMEOUT_CTRL, FALSE, sizeof (Timeout), &Timeout);

  return Status;
}

/**
  Initial SD/MMC host controller with lowest clock frequency, max power and max timeout value
  at initialization.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Slot           The slot number of the SD card to send the command to.

  @retval EFI_SUCCESS       The host controller is initialized successfully.
  @retval Others            The host controller isn't initialized successfully.

**/
EFI_STATUS
SdMmcHcInitHost (
  IN SD_MMC_HC_PRIVATE_DATA *Private,
  IN UINT8                  Slot
  )
{
  EFI_STATUS                Status;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  SD_MMC_HC_SLOT_CAP        Capability;
  UINT32                    value32;

  //
  // Notify the SD/MMC override protocol that we are about to initialize
  // the SD/MMC host controller.
  //
  if (mOverride != NULL && mOverride->NotifyPhase != NULL) {
    Status = mOverride->NotifyPhase (
                          Private->ControllerHandle,
                          Slot,
                          EdkiiSdMmcInitHostPre,
                          NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN,
        "%a: SD/MMC pre init notifier callback failed - %r\n",
        __FUNCTION__, Status));
      return Status;
    }
  }

  PciIo = Private->PciIo;
  Capability = Private->Capability[Slot];

  if (BhtHostPciSupport(PciIo)){
    UINT8   CardMode;
    UINT16	EmmcVar;
    UINTN 	EmmcVarSize;
    UINT64  Cap;

    DbgMsg("find bht emmc chip\n");

    //unlock PCR write protect
#ifdef DISABLE_L1_2
    PciBhtAnd32(PciIo, 0xd0, ~(BIT31));
    PciBhtAnd32(PciIo, 0x90, ~(BIT1 | BIT0));
		
    value32 = PciBhtRead32(PciIo, 0xe0);
    value32 &= ~(BIT31 | BIT30 | BIT29 | BIT28);
    value32 |= (BIT29 | BIT28);
    PciBhtWrite32(PciIo, 0xe0, value32);

    value32 = PciBhtRead32(PciIo, 0xfc);
    value32 &= ~(BIT19 | BIT18 | BIT17 | BIT16);
    value32 |= (BIT19);
    PciBhtWrite32(PciIo, 0xfc, value32);
		
    value32 = PciBhtRead32(PciIo, 0x3f4);
    value32 &= ~(BIT3 | BIT2 | BIT1 | BIT0);
    value32 |= (BIT3 | BIT1);
    PciBhtWrite32(PciIo, 0x3f4, value32);

    value32 = PciBhtRead32(PciIo, 0x248);
    value32 &= ~(BIT3 | BIT2 | BIT1 | BIT0);
    value32 |= (BIT3 | BIT1);
    PciBhtWrite32(PciIo, 0x248, value32);

    value32 = PciBhtRead32(PciIo, 0x90);
    value32 &= ~(BIT1 | BIT0);
    value32 |= (BIT1);
    PciBhtWrite32(PciIo, 0x90, value32);
#endif

    /* FET on */
    PciBhtOr32(PciIo, 0xEC, 0x3);
    /* Led on */
    //PciBhtAnd32(PciIo, 0x334, (UINT32)~BIT13);
    PciBhtOr32(PciIo, 0xD4, BIT6);
    /* Set 1.8v emmc signaling flag */
    PciBhtOr32(PciIo, 0x308, BIT4);
    /* Set 200MBaseClock */
    value32 = PciBhtRead32(PciIo, 0x304);
    value32 &= 0x0000FFFF;
    value32 |= 0x25100000;
#if !defined(HOST_CLK_DRIVE_STRENGTH) || HOST_CLK_DRIVE_STRENGTH > 7 || HOST_CLK_DRIVE_STRENGTH < 0
#error "HOST_CMD_DRIVE_STRENGTH is undefined or value is invalid"
#else
    EmmcVarSize = sizeof(EmmcVar);
    Status = gRT->GetVariable (
          L"EMMC_CLK_DRIVER_STRENGTH",
          &gEfiGenericVariableGuid,
          NULL,
          &EmmcVarSize,
          &EmmcVar
        );
    if (EFI_ERROR(Status))
      EmmcVar = HOST_CLK_DRIVE_STRENGTH;
    value32 &= 0xFFFFFF8F;
    value32 |= ((EmmcVar & 0x7) << 4);
#endif
#if !defined(HOST_DAT_DRIVE_STRENGTH) || HOST_DAT_DRIVE_STRENGTH > 7 || HOST_DAT_DRIVE_STRENGTH < 0
#error "HOST_DATA_DRIVE_STRENGTH is undefined or value is invalid"
#else
    EmmcVarSize = sizeof(EmmcVar);
    Status = gRT->GetVariable (
          L"EMMC_DATA_DRIVER_STRENGTH",
          &gEfiGenericVariableGuid,
          NULL,
          &EmmcVarSize,
          &EmmcVar
        );
    if (EFI_ERROR(Status))
      EmmcVar = HOST_DAT_DRIVE_STRENGTH;
    value32 &= 0xFFFFFFF1;
    value32 |= ((EmmcVar & 0x7) << 1);
#endif
    PciBhtWrite32(PciIo, 0x304, value32);
    PciBhtOr32(PciIo, 0x3E4, BIT22);

    EmmcVarSize = sizeof(CardMode);
    Status = gRT->GetVariable (
                 L"EMMC_FORCE_CARD_MODE",
                 &gEfiGenericVariableGuid,
                 NULL,
                 &EmmcVarSize,
                 &CardMode
               );
    if (EFI_ERROR(Status) || CardMode > 2) {
      CardMode = 0;
    }

    if (CardMode == 1) {
#if !defined(HS100_ALLPASS_PHASE) || HS100_ALLPASS_PHASE > 10 || HS100_ALLPASS_PHASE < 0
#error "HS200_ALLPASS_PHASE is undefined or value is invalid"
#else
      EmmcVarSize = sizeof(EmmcVar);
      Status = gRT->GetVariable (
            L"EMMC_HS100_ALLPASS_PHASE",
            &gEfiGenericVariableGuid,
            NULL,
            &EmmcVarSize,
            &EmmcVar
          );
    if (EFI_ERROR(Status) || EmmcVar > 10)
      EmmcVar = HS100_ALLPASS_PHASE;
#endif		
    } else if (CardMode == 2) {
#if !defined(HS200_ALLPASS_PHASE) || HS200_ALLPASS_PHASE > 10 || HS200_ALLPASS_PHASE < 0
#error "HS200_ALLPASS_PHASE is undefined or value is invalid"
#else
      EmmcVarSize = sizeof(EmmcVar);
      Status = gRT->GetVariable (
            L"EMMC_HS200_ALLPASS_PHASE",
            &gEfiGenericVariableGuid,
            NULL,
            &EmmcVarSize,
            &EmmcVar
          );
    if (EFI_ERROR(Status) || EmmcVar > 10)
      EmmcVar = HS200_ALLPASS_PHASE;
    }
#endif		
    
	value32 = 0x21000033 | (EmmcVar << 20);
	PciBhtWrite32(PciIo, 0x300, value32);
		
    //enable internal clk
    value32 = BIT0;
    Status = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_CLOCK_CTRL,sizeof(value32), &value32);

    //reset pll start	
    Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, TRUE, sizeof(value32), &value32);
    value32 |= BIT12;	  
    Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, FALSE, sizeof(value32), &value32);		  
    gBS->Stall(1);

    //reset pll end
    Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, TRUE,sizeof(value32), &value32);
    value32 &= ~BIT12;
    value32 |= BIT18;
    Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, FALSE, sizeof(value32), &value32);
		
    //wait BaseClk stable 0x1CC bit14	
    Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, TRUE, sizeof(value32), &value32);
    while(!(value32&BIT14)){
      gBS->Stall(100);
      Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, TRUE, sizeof(value32), &value32);
      DbgMsg("1CC=0x%08x\n", value32);
    }

    if (value32 & BIT18)
    {
      //Wait 2nd Card Detect debounce Finished by wait twice of debounce max time
      while (1) {
        Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_PRESENT_STATE, TRUE, sizeof(value32), &value32);
        if (((value32 >> 16) & 0x01) == ((value32 >> 18) & 0x01))
          break;
      }	
      //force pll active end
      Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, TRUE, sizeof(value32), &value32);		
      value32 &= ~BIT18;
      Status = SdMmcHcRwMmio (PciIo, Slot, 0x1CC, FALSE, sizeof(value32), &value32);				
    }				

    Status = SdMmcHcRwMmio (PciIo, Slot, SD_MMC_HC_CAP, TRUE, sizeof (Cap), &Cap);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    CopyMem (&Capability, &Cap, sizeof (Cap));
	
    Status = SdMmcHcInitPowerVoltage (PciIo, Slot, Capability);
    if (EFI_ERROR (Status)) {
      DbgMsg("emmc host init failure\n");
      return Status;
    }
  }

  Status = SdMmcHcInitClockFreq (PciIo, Slot, Private->BaseClkFreq[Slot]);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!BhtHostPciSupport(PciIo)){
      Status = SdMmcHcInitPowerVoltage (PciIo, Slot, Capability);
      if (EFI_ERROR (Status)) {
        return Status;
      }
  }

  Status = SdMmcHcInitTimeoutCtrl (PciIo, Slot);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Notify the SD/MMC override protocol that we are have just initialized
  // the SD/MMC host controller.
  //
  if (mOverride != NULL && mOverride->NotifyPhase != NULL) {
    Status = mOverride->NotifyPhase (
                          Private->ControllerHandle,
                          Slot,
                          EdkiiSdMmcInitHostPost,
                          NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN,
        "%a: SD/MMC post init notifier callback failed - %r\n",
        __FUNCTION__, Status));
    }
  }
  return Status;
}

/**
  Set SD Host Controler control 2 registry according to selected speed.

  @param[in] ControllerHandle The handle of the controller.
  @param[in] PciIo            The PCI IO protocol instance.
  @param[in] Slot             The slot number of the SD card to send the command to.
  @param[in] Timing           The timing to select.

  @retval EFI_SUCCESS         The timing is set successfully.
  @retval Others              The timing isn't set successfully.
**/
EFI_STATUS
SdMmcHcUhsSignaling (
  IN EFI_HANDLE             ControllerHandle,
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN SD_MMC_BUS_MODE        Timing
  )
{
  EFI_STATUS                 Status;
  UINT8                      HostCtrl2;

  HostCtrl2 = (UINT8)~SD_MMC_HC_CTRL_UHS_MASK;
  Status = SdMmcHcAndMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL2, sizeof (HostCtrl2), &HostCtrl2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (Timing) {
    case SdMmcUhsSdr12:
      HostCtrl2 = SD_MMC_HC_CTRL_UHS_SDR12;
      break;
    case SdMmcUhsSdr25:
      HostCtrl2 = SD_MMC_HC_CTRL_UHS_SDR25;
      break;
    case SdMmcUhsSdr50:
      HostCtrl2 = SD_MMC_HC_CTRL_UHS_SDR50;
      break;
    case SdMmcUhsSdr104:
      HostCtrl2 = SD_MMC_HC_CTRL_UHS_SDR104;
      break;
    case SdMmcUhsDdr50:
      HostCtrl2 = SD_MMC_HC_CTRL_UHS_DDR50;
      break;
    case SdMmcMmcLegacy:
      HostCtrl2 = SD_MMC_HC_CTRL_MMC_LEGACY;
      break;
    case SdMmcMmcHsSdr:
      HostCtrl2 = SD_MMC_HC_CTRL_MMC_HS_SDR;
      break;
    case SdMmcMmcHsDdr:
      HostCtrl2 = SD_MMC_HC_CTRL_MMC_HS_DDR;
      break;
    case SdMmcMmcHs200:
      HostCtrl2 = SD_MMC_HC_CTRL_MMC_HS200;
      break;
    case SdMmcMmcHs400:
      HostCtrl2 = SD_MMC_HC_CTRL_MMC_HS400;
      break;
    default:
     HostCtrl2 = 0;
     break;
  }
  Status = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL2, sizeof (HostCtrl2), &HostCtrl2);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mOverride != NULL && mOverride->NotifyPhase != NULL) {
    Status = mOverride->NotifyPhase (
                          ControllerHandle,
                          Slot,
                          EdkiiSdMmcUhsSignaling,
                          &Timing
                          );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: SD/MMC uhs signaling notifier callback failed - %r\n",
        __FUNCTION__,
        Status
        ));
      return Status;
    }
  }

  return EFI_SUCCESS;
}

/**
  Turn on/off LED.

  @param[in] PciIo          The PCI IO protocol instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] On             The boolean to turn on/off LED.

  @retval EFI_SUCCESS       The LED is turned on/off successfully.
  @retval Others            The LED isn't turned on/off successfully.

**/
EFI_STATUS
SdMmcHcLedOnOff (
  IN EFI_PCI_IO_PROTOCOL    *PciIo,
  IN UINT8                  Slot,
  IN BOOLEAN                On
  )
{
  EFI_STATUS                Status;
  UINT8                     HostCtrl1;

  if (On) {
    HostCtrl1 = BIT0;
    Status    = SdMmcHcOrMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, sizeof (HostCtrl1), &HostCtrl1);
  } else {
    HostCtrl1 = (UINT8)~BIT0;
    Status    = SdMmcHcAndMmio (PciIo, Slot, SD_MMC_HC_HOST_CTRL1, sizeof (HostCtrl1), &HostCtrl1);
  }

  return Status;
}

/**
  Build ADMA descriptor table for transfer.

  Refer to SD Host Controller Simplified spec 3.0 Section 1.13 for details.

  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

  @retval EFI_SUCCESS       The ADMA descriptor table is created successfully.
  @retval Others            The ADMA descriptor table isn't created successfully.

**/
EFI_STATUS
BuildAdmaDescTable (
  IN SD_MMC_HC_TRB          *Trb
  )
{
  EFI_PHYSICAL_ADDRESS      Data;
  UINT64                    DataLen;
  UINT64                    Entries;
  UINT32                    Index;
  UINT64                    Remaining;
  UINT32                    Address;
  UINTN                     TableSize;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  EFI_STATUS                Status;
  UINTN                     Bytes;

  Data    = Trb->DataPhy;
  DataLen = Trb->DataLen;
  PciIo   = Trb->Private->PciIo;
  //
  // Only support 32bit ADMA Descriptor Table
  //
  if ((Data >= 0x100000000ul) || ((Data + DataLen) > 0x100000000ul)) {
    return EFI_INVALID_PARAMETER;
  }
  //
  // Address field shall be set on 32-bit boundary (Lower 2-bit is always set to 0)
  // for 32-bit address descriptor table.
  //
  if ((Data & (BIT0 | BIT1)) != 0) {
    DEBUG ((DEBUG_INFO, "The buffer [0x%x] to construct ADMA desc is not aligned to 4 bytes boundary!\n", Data));
  }

  Entries   = DivU64x32 ((DataLen + ADMA_MAX_DATA_PER_LINE - 1), ADMA_MAX_DATA_PER_LINE);
  TableSize = (UINTN)MultU64x32 (Entries, sizeof (SD_MMC_HC_ADMA_DESC_LINE));
  Trb->AdmaPages = (UINT32)EFI_SIZE_TO_PAGES (TableSize);
  Status = PciIo->AllocateBuffer (
                    PciIo,
                    AllocateAnyPages,
                    EfiBootServicesData,
                    EFI_SIZE_TO_PAGES (TableSize),
                    (VOID **)&Trb->AdmaDesc,
                    0
                    );
  if (EFI_ERROR (Status)) {
    return EFI_OUT_OF_RESOURCES;
  }
  ZeroMem (Trb->AdmaDesc, TableSize);
  Bytes  = TableSize;
  Status = PciIo->Map (
                    PciIo,
                    EfiPciIoOperationBusMasterCommonBuffer,
                    Trb->AdmaDesc,
                    &Bytes,
                    &Trb->AdmaDescPhy,
                    &Trb->AdmaMap
                    );

  if (EFI_ERROR (Status) || (Bytes != TableSize)) {
    //
    // Map error or unable to map the whole RFis buffer into a contiguous region.
    //
    PciIo->FreeBuffer (
             PciIo,
             EFI_SIZE_TO_PAGES (TableSize),
             Trb->AdmaDesc
             );
    return EFI_OUT_OF_RESOURCES;
  }

  if ((UINT64)(UINTN)Trb->AdmaDescPhy > 0x100000000ul) {
    //
    // The ADMA doesn't support 64bit addressing.
    //
    PciIo->Unmap (
      PciIo,
      Trb->AdmaMap
    );
    PciIo->FreeBuffer (
      PciIo,
      EFI_SIZE_TO_PAGES (TableSize),
      Trb->AdmaDesc
    );
    return EFI_DEVICE_ERROR;
  }

  Remaining = DataLen;
  Address   = (UINT32)Data;
  for (Index = 0; Index < Entries; Index++) {
    if (Remaining <= ADMA_MAX_DATA_PER_LINE) {
      Trb->AdmaDesc[Index].Valid = 1;
      Trb->AdmaDesc[Index].Act   = 2;
      Trb->AdmaDesc[Index].Length  = (UINT16)Remaining;
      Trb->AdmaDesc[Index].Address = Address;
      break;
    } else {
      Trb->AdmaDesc[Index].Valid = 1;
      Trb->AdmaDesc[Index].Act   = 2;
      Trb->AdmaDesc[Index].Length  = 0;
      Trb->AdmaDesc[Index].Address = Address;
    }

    Remaining -= ADMA_MAX_DATA_PER_LINE;
    Address   += ADMA_MAX_DATA_PER_LINE;
  }

  //
  // Set the last descriptor line as end of descriptor table
  //
  Trb->AdmaDesc[Index].End = 1;
  return EFI_SUCCESS;
}

/**
  Create a new TRB for the SD/MMC cmd request.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Slot           The slot number of the SD card to send the command to.
  @param[in] Packet         A pointer to the SD command data structure.
  @param[in] Event          If Event is NULL, blocking I/O is performed. If Event is
                            not NULL, then nonblocking I/O is performed, and Event
                            will be signaled when the Packet completes.

  @return Created Trb or NULL.

**/
SD_MMC_HC_TRB *
SdMmcCreateTrb (
  IN SD_MMC_HC_PRIVATE_DATA              *Private,
  IN UINT8                               Slot,
  IN EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet,
  IN EFI_EVENT                           Event
  )
{
  SD_MMC_HC_TRB                 *Trb;
  EFI_STATUS                    Status;
  EFI_TPL                       OldTpl;
  EFI_PCI_IO_PROTOCOL_OPERATION Flag;
  EFI_PCI_IO_PROTOCOL           *PciIo;
  UINTN                         MapLength;

  Trb = AllocateZeroPool (sizeof (SD_MMC_HC_TRB));
  if (Trb == NULL) {
    return NULL;
  }

  Trb->Signature = SD_MMC_HC_TRB_SIG;
  Trb->Slot      = Slot;
  Trb->BlockSize = 0x200;
  Trb->Packet    = Packet;
  Trb->Event     = Event;
  Trb->Started   = FALSE;
  Trb->Timeout   = Packet->Timeout;
  Trb->Private   = Private;

  if ((Packet->InTransferLength != 0) && (Packet->InDataBuffer != NULL)) {
    Trb->Data    = Packet->InDataBuffer;
    Trb->DataLen = Packet->InTransferLength;
    Trb->Read    = TRUE;
  } else if ((Packet->OutTransferLength != 0) && (Packet->OutDataBuffer != NULL)) {
    Trb->Data    = Packet->OutDataBuffer;
    Trb->DataLen = Packet->OutTransferLength;
    Trb->Read    = FALSE;
  } else if ((Packet->InTransferLength == 0) && (Packet->OutTransferLength == 0)) {
    Trb->Data    = NULL;
    Trb->DataLen = 0;
  } else {
    goto Error;
  }

  if ((Trb->DataLen != 0) && (Trb->DataLen < Trb->BlockSize)) {
    Trb->BlockSize = (UINT16)Trb->DataLen;
  }

  if (((Private->Slot[Trb->Slot].CardType == EmmcCardType) &&
       (Packet->SdMmcCmdBlk->CommandIndex == EMMC_SEND_TUNING_BLOCK)) ||
      ((Private->Slot[Trb->Slot].CardType == SdCardType) &&
       (Packet->SdMmcCmdBlk->CommandIndex == SD_SEND_TUNING_BLOCK))) {
    Trb->Mode = SdMmcPioMode;
  } else {
    if (Trb->Read) {
      Flag = EfiPciIoOperationBusMasterWrite;
    } else {
      Flag = EfiPciIoOperationBusMasterRead;
    }

    PciIo = Private->PciIo;
    if (Trb->DataLen != 0) {
      MapLength = Trb->DataLen;
      Status = PciIo->Map (
                        PciIo,
                        Flag,
                        Trb->Data,
                        &MapLength,
                        &Trb->DataPhy,
                        &Trb->DataMap
                        );
      if (EFI_ERROR (Status) || (Trb->DataLen != MapLength)) {
        Status = EFI_BAD_BUFFER_SIZE;
        goto Error;
      }
    }

    if (Trb->DataLen == 0) {
      Trb->Mode = SdMmcNoData;
    } else if (Private->Capability[Slot].Adma2 != 0) {
      Trb->Mode = SdMmcAdmaMode;
      Status = BuildAdmaDescTable (Trb);
      if (EFI_ERROR (Status)) {
        PciIo->Unmap (PciIo, Trb->DataMap);
        goto Error;
      }
    } else if (Private->Capability[Slot].Sdma != 0) {
      Trb->Mode = SdMmcSdmaMode;
    } else {
      Trb->Mode = SdMmcPioMode;
    }
  }

  if (Event != NULL) {
    OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    InsertTailList (&Private->Queue, &Trb->TrbList);
    gBS->RestoreTPL (OldTpl);
  }

  return Trb;

Error:
  SdMmcFreeTrb (Trb);
  return NULL;
}

/**
  Free the resource used by the TRB.

  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

**/
VOID
SdMmcFreeTrb (
  IN SD_MMC_HC_TRB           *Trb
  )
{
  EFI_PCI_IO_PROTOCOL        *PciIo;

  PciIo = Trb->Private->PciIo;

  if (Trb->AdmaMap != NULL) {
    PciIo->Unmap (
      PciIo,
      Trb->AdmaMap
    );
  }
  if (Trb->AdmaDesc != NULL) {
    PciIo->FreeBuffer (
      PciIo,
      Trb->AdmaPages,
      Trb->AdmaDesc
    );
  }
  if (Trb->DataMap != NULL) {
    PciIo->Unmap (
      PciIo,
      Trb->DataMap
    );
  }
  FreePool (Trb);
  return;
}

/**
  Check if the env is ready for execute specified TRB.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

  @retval EFI_SUCCESS       The env is ready for TRB execution.
  @retval EFI_NOT_READY     The env is not ready for TRB execution.
  @retval Others            Some erros happen.

**/
EFI_STATUS
SdMmcCheckTrbEnv (
  IN SD_MMC_HC_PRIVATE_DATA           *Private,
  IN SD_MMC_HC_TRB                    *Trb
  )
{
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet;
  EFI_PCI_IO_PROTOCOL                 *PciIo;
  UINT32                              PresentState;

  Packet = Trb->Packet;

  if ((Packet->SdMmcCmdBlk->CommandType == SdMmcCommandTypeAdtc) ||
      (Packet->SdMmcCmdBlk->ResponseType == SdMmcResponseTypeR1b) ||
      (Packet->SdMmcCmdBlk->ResponseType == SdMmcResponseTypeR5b)) {
    //
    // Wait Command Inhibit (CMD) and Command Inhibit (DAT) in
    // the Present State register to be 0
    //
    PresentState = BIT0 | BIT1;
  } else {
    //
    // Wait Command Inhibit (CMD) in the Present State register
    // to be 0
    //
    PresentState = BIT0;
  }

  PciIo  = Private->PciIo;
  Status = SdMmcHcCheckMmioSet (
             PciIo,
             Trb->Slot,
             SD_MMC_HC_PRESENT_STATE,
             sizeof (PresentState),
             PresentState,
             0
             );

  return Status;
}

/**
  Wait for the env to be ready for execute specified TRB.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

  @retval EFI_SUCCESS       The env is ready for TRB execution.
  @retval EFI_TIMEOUT       The env is not ready for TRB execution in time.
  @retval Others            Some erros happen.

**/
EFI_STATUS
SdMmcWaitTrbEnv (
  IN SD_MMC_HC_PRIVATE_DATA           *Private,
  IN SD_MMC_HC_TRB                    *Trb
  )
{
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet;
  UINT64                              Timeout;
  BOOLEAN                             InfiniteWait;

  //
  // Wait Command Complete Interrupt Status bit in Normal Interrupt Status Register
  //
  Packet  = Trb->Packet;
  Timeout = Packet->Timeout;
  if (Timeout == 0) {
    InfiniteWait = TRUE;
  } else {
    InfiniteWait = FALSE;
  }

  while (InfiniteWait || (Timeout > 0)) {
    //
    // Check Trb execution result by reading Normal Interrupt Status register.
    //
    Status = SdMmcCheckTrbEnv (Private, Trb);
    if (Status != EFI_NOT_READY) {
      return Status;
    }
    //
    // Stall for 1 microsecond.
    //
    gBS->Stall (1);

    Timeout--;
  }

  return EFI_TIMEOUT;
}

/**
  Execute the specified TRB.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

  @retval EFI_SUCCESS       The TRB is sent to host controller successfully.
  @retval Others            Some erros happen when sending this request to the host controller.

**/
EFI_STATUS
SdMmcExecTrb (
  IN SD_MMC_HC_PRIVATE_DATA           *Private,
  IN SD_MMC_HC_TRB                    *Trb
  )
{
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet;
  EFI_PCI_IO_PROTOCOL                 *PciIo;
  UINT16                              Cmd;
  UINT16                              IntStatus;
  UINT32                              Argument;
  UINT16                              BlkCount;
  UINT16                              BlkSize;
  UINT16                              TransMode;
  UINT8                               HostCtrl1;
  UINT32                              SdmaAddr;
  UINT64                              AdmaAddr;

  Packet = Trb->Packet;
  PciIo  = Trb->Private->PciIo;
  //
  // Clear all bits in Error Interrupt Status Register
  //
  IntStatus = 0xFFFF;
  Status    = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_ERR_INT_STS, FALSE, sizeof (IntStatus), &IntStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Clear all bits in Normal Interrupt Status Register excepts for Card Removal & Card Insertion bits.
  //
  IntStatus = 0xFF3F;
  Status    = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_NOR_INT_STS, FALSE, sizeof (IntStatus), &IntStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Set Host Control 1 register DMA Select field
  //
  if (Trb->Mode == SdMmcAdmaMode) {
    HostCtrl1 = BIT4;
    Status = SdMmcHcOrMmio (PciIo, Trb->Slot, SD_MMC_HC_HOST_CTRL1, sizeof (HostCtrl1), &HostCtrl1);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  SdMmcHcLedOnOff (PciIo, Trb->Slot, TRUE);

  if (Trb->Mode == SdMmcSdmaMode) {
    if ((UINT64)(UINTN)Trb->DataPhy >= 0x100000000ul) {
      return EFI_INVALID_PARAMETER;
    }

    SdmaAddr = (UINT32)(UINTN)Trb->DataPhy;
    Status   = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_SDMA_ADDR, FALSE, sizeof (SdmaAddr), &SdmaAddr);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else if (Trb->Mode == SdMmcAdmaMode) {
    AdmaAddr = (UINT64)(UINTN)Trb->AdmaDescPhy;
    Status   = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_ADMA_SYS_ADDR, FALSE, sizeof (AdmaAddr), &AdmaAddr);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  BlkSize = Trb->BlockSize;
  if (Trb->Mode == SdMmcSdmaMode) {
    //
    // Set SDMA boundary to be 512K bytes.
    //
    BlkSize |= 0x7000;
  }

  Status = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_BLK_SIZE, FALSE, sizeof (BlkSize), &BlkSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BlkCount = 0;
  if (Trb->Mode != SdMmcNoData) {
    //
    // Calcuate Block Count.
    //
    BlkCount = (UINT16)(Trb->DataLen / Trb->BlockSize);
  }
  Status   = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_BLK_COUNT, FALSE, sizeof (BlkCount), &BlkCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Argument = Packet->SdMmcCmdBlk->CommandArgument;
  Status   = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_ARG1, FALSE, sizeof (Argument), &Argument);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TransMode = 0;
  if (Trb->Mode != SdMmcNoData) {
    if (Trb->Mode != SdMmcPioMode) {
      TransMode |= BIT0;
    }
    if (Trb->Read) {
      TransMode |= BIT4;
    }
    if (BlkCount > 1) {
      TransMode |= BIT5 | BIT1;
    }
    //
    // Only SD memory card needs to use AUTO CMD12 feature.
    //
    if (Private->Slot[Trb->Slot].CardType == SdCardType) {
      if (BlkCount > 1) {
        TransMode |= BIT2;
      }
    }
  }

  Status = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_TRANS_MOD, FALSE, sizeof (TransMode), &TransMode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Cmd = (UINT16)LShiftU64(Packet->SdMmcCmdBlk->CommandIndex, 8);
  if (Packet->SdMmcCmdBlk->CommandType == SdMmcCommandTypeAdtc) {
    Cmd |= BIT5;
  }
  //
  // Convert ResponseType to value
  //
  if (Packet->SdMmcCmdBlk->CommandType != SdMmcCommandTypeBc) {
    switch (Packet->SdMmcCmdBlk->ResponseType) {
      case SdMmcResponseTypeR1:
      case SdMmcResponseTypeR5:
      case SdMmcResponseTypeR6:
      case SdMmcResponseTypeR7:
        Cmd |= (BIT1 | BIT3 | BIT4);
        break;
      case SdMmcResponseTypeR2:
        Cmd |= (BIT0 | BIT3);
       break;
      case SdMmcResponseTypeR3:
      case SdMmcResponseTypeR4:
        Cmd |= BIT1;
        break;
      case SdMmcResponseTypeR1b:
      case SdMmcResponseTypeR5b:
        Cmd |= (BIT0 | BIT1 | BIT3 | BIT4);
        break;
      default:
        ASSERT (FALSE);
        break;
    }
  }
  //
  // Execute cmd
  //
  Status = SdMmcHcRwMmio (PciIo, Trb->Slot, SD_MMC_HC_COMMAND, FALSE, sizeof (Cmd), &Cmd);
  return Status;
}

/**
  Check the TRB execution result.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

  @retval EFI_SUCCESS       The TRB is executed successfully.
  @retval EFI_NOT_READY     The TRB is not completed for execution.
  @retval Others            Some erros happen when executing this request.

**/
EFI_STATUS
SdMmcCheckTrbResult (
  IN SD_MMC_HC_PRIVATE_DATA           *Private,
  IN SD_MMC_HC_TRB                    *Trb
  )
{
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet;
  UINT16                              IntStatus;
  UINT32                              Response[4];
  UINT32                              SdmaAddr;
  UINT8                               Index;
  UINT8                               SwReset;
  UINT32                              PioLength;

  SwReset = 0;
  Packet  = Trb->Packet;
  //
  // Check Trb execution result by reading Normal Interrupt Status register.
  //
  Status = SdMmcHcRwMmio (
             Private->PciIo,
             Trb->Slot,
             SD_MMC_HC_NOR_INT_STS,
             TRUE,
             sizeof (IntStatus),
             &IntStatus
             );
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  //
  // Check Transfer Complete bit is set or not.
  //
  if ((IntStatus & BIT1) == BIT1) {
    if ((IntStatus & BIT15) == BIT15) {
      //
      // Read Error Interrupt Status register to check if the error is
      // Data Timeout Error.
      // If yes, treat it as success as Transfer Complete has higher
      // priority than Data Timeout Error.
      //
      Status = SdMmcHcRwMmio (
                 Private->PciIo,
                 Trb->Slot,
                 SD_MMC_HC_ERR_INT_STS,
                 TRUE,
                 sizeof (IntStatus),
                 &IntStatus
                 );
      if (!EFI_ERROR (Status)) {
        if ((IntStatus & BIT4) == BIT4) {
          Status = EFI_SUCCESS;
        } else {
          Status = EFI_DEVICE_ERROR;
        }
      }
    }

    goto Done;
  }
  //
  // Check if there is a error happened during cmd execution.
  // If yes, then do error recovery procedure to follow SD Host Controller
  // Simplified Spec 3.0 section 3.10.1.
  //
  if ((IntStatus & BIT15) == BIT15) {
    Status = SdMmcHcRwMmio (
               Private->PciIo,
               Trb->Slot,
               SD_MMC_HC_ERR_INT_STS,
               TRUE,
               sizeof (IntStatus),
               &IntStatus
               );
    if (EFI_ERROR (Status)) {
      goto Done;
    }
    if ((IntStatus & 0x0F) != 0) {
      SwReset |= BIT1;
    }
    if ((IntStatus & 0xF0) != 0) {
      SwReset |= BIT2;
    }

    Status  = SdMmcHcRwMmio (
                Private->PciIo,
                Trb->Slot,
                SD_MMC_HC_SW_RST,
                FALSE,
                sizeof (SwReset),
                &SwReset
                );
    if (EFI_ERROR (Status)) {
      goto Done;
    }
    Status = SdMmcHcWaitMmioSet (
               Private->PciIo,
               Trb->Slot,
               SD_MMC_HC_SW_RST,
               sizeof (SwReset),
               0xFF,
               0,
               SD_MMC_HC_GENERIC_TIMEOUT
               );
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    Status = EFI_DEVICE_ERROR;
    goto Done;
  }
  //
  // Check if DMA interrupt is signalled for the SDMA transfer.
  //
  if ((Trb->Mode == SdMmcSdmaMode) && ((IntStatus & BIT3) == BIT3)) {
    //
    // Clear DMA interrupt bit.
    //
    IntStatus = BIT3;
    Status    = SdMmcHcRwMmio (
                  Private->PciIo,
                  Trb->Slot,
                  SD_MMC_HC_NOR_INT_STS,
                  FALSE,
                  sizeof (IntStatus),
                  &IntStatus
                  );
    if (EFI_ERROR (Status)) {
      goto Done;
    }
    //
    // Update SDMA Address register.
    //
    SdmaAddr = SD_MMC_SDMA_ROUND_UP ((UINT32)(UINTN)Trb->DataPhy, SD_MMC_SDMA_BOUNDARY);
    Status   = SdMmcHcRwMmio (
                 Private->PciIo,
                 Trb->Slot,
                 SD_MMC_HC_SDMA_ADDR,
                 FALSE,
                 sizeof (UINT32),
                 &SdmaAddr
                 );
    if (EFI_ERROR (Status)) {
      goto Done;
    }
    Trb->DataPhy = (UINT32)(UINTN)SdmaAddr;
  }

  if ((Packet->SdMmcCmdBlk->CommandType != SdMmcCommandTypeAdtc) &&
      (Packet->SdMmcCmdBlk->ResponseType != SdMmcResponseTypeR1b) &&
      (Packet->SdMmcCmdBlk->ResponseType != SdMmcResponseTypeR5b)) {
    if ((IntStatus & BIT0) == BIT0) {
      Status = EFI_SUCCESS;
      goto Done;
    }
  }

  if (((Private->Slot[Trb->Slot].CardType == EmmcCardType) &&
       (Packet->SdMmcCmdBlk->CommandIndex == EMMC_SEND_TUNING_BLOCK)) ||
      ((Private->Slot[Trb->Slot].CardType == SdCardType) &&
       (Packet->SdMmcCmdBlk->CommandIndex == SD_SEND_TUNING_BLOCK))) {
    //
    // When performing tuning procedure (Execute Tuning is set to 1) through PIO mode,
    // wait Buffer Read Ready bit of Normal Interrupt Status Register to be 1.
    // Refer to SD Host Controller Simplified Specification 3.0 figure 2-29 for details.
    //
    if ((IntStatus & BIT5) == BIT5) {
      //
      // Clear Buffer Read Ready interrupt at first.
      //
      IntStatus = BIT5;
      SdMmcHcRwMmio (Private->PciIo, Trb->Slot, SD_MMC_HC_NOR_INT_STS, FALSE, sizeof (IntStatus), &IntStatus);
      //
      // Read data out from Buffer Port register
      //
      for (PioLength = 0; PioLength < Trb->DataLen; PioLength += 4) {
        SdMmcHcRwMmio (Private->PciIo, Trb->Slot, SD_MMC_HC_BUF_DAT_PORT, TRUE, 4, (UINT8*)Trb->Data + PioLength);
      }
      Status = EFI_SUCCESS;
      goto Done;
    }
  }

  Status = EFI_NOT_READY;
Done:
  //
  // Get response data when the cmd is executed successfully.
  //
  if (!EFI_ERROR (Status)) {
    if (Packet->SdMmcCmdBlk->CommandType != SdMmcCommandTypeBc) {
      for (Index = 0; Index < 4; Index++) {
        Status = SdMmcHcRwMmio (
                   Private->PciIo,
                   Trb->Slot,
                   SD_MMC_HC_RESPONSE + Index * 4,
                   TRUE,
                   sizeof (UINT32),
                   &Response[Index]
                   );
        if (EFI_ERROR (Status)) {
          SdMmcHcLedOnOff (Private->PciIo, Trb->Slot, FALSE);
          return Status;
        }
      }
      CopyMem (Packet->SdMmcStatusBlk, Response, sizeof (Response));
    }
  }

  if (Status != EFI_NOT_READY) {
    SdMmcHcLedOnOff (Private->PciIo, Trb->Slot, FALSE);
  }

  return Status;
}

/**
  Wait for the TRB execution result.

  @param[in] Private        A pointer to the SD_MMC_HC_PRIVATE_DATA instance.
  @param[in] Trb            The pointer to the SD_MMC_HC_TRB instance.

  @retval EFI_SUCCESS       The TRB is executed successfully.
  @retval Others            Some erros happen when executing this request.

**/
EFI_STATUS
SdMmcWaitTrbResult (
  IN SD_MMC_HC_PRIVATE_DATA           *Private,
  IN SD_MMC_HC_TRB                    *Trb
  )
{
  EFI_STATUS                          Status;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet;
  UINT64                              Timeout;
  BOOLEAN                             InfiniteWait;

  Packet = Trb->Packet;
  //
  // Wait Command Complete Interrupt Status bit in Normal Interrupt Status Register
  //
  Timeout = Packet->Timeout;
  if (Timeout == 0) {
    InfiniteWait = TRUE;
  } else {
    InfiniteWait = FALSE;
  }

  while (InfiniteWait || (Timeout > 0)) {
    //
    // Check Trb execution result by reading Normal Interrupt Status register.
    //
    Status = SdMmcCheckTrbResult (Private, Trb);
    if (Status != EFI_NOT_READY) {
      return Status;
    }
    //
    // Stall for 1 microsecond.
    //
    gBS->Stall (1);

    Timeout--;
  }

  return EFI_TIMEOUT;
}

BOOLEAN BhtHostPciSupport(EFI_PCI_IO_PROTOCOL *PciIo)
{
  PCI_TYPE00		Pci;

  PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32,		
          0, sizeof Pci / sizeof (UINT32), &Pci);

  DEBUG ((DEBUG_INFO, "check device %04x:%04x\n", Pci.Hdr.VendorId, Pci.Hdr.DeviceId));

  if (Pci.Hdr.VendorId != 0x1217)
    goto end;

  switch (Pci.Hdr.DeviceId)
  {
    case 0x8420:	//PCI_DEV_ID_SDS0
    case 0x8421:	//PCI_DEV_ID_SDS1
    case 0x8520:	//PCI_DEV_ID_FJ2
    case 0x8620:	//PCI_DEV_ID_SB0
    case 0x8621:	//PCI_DEV_ID_SB1
      g_deviceId = Pci.Hdr.DeviceId;
      return 1;
    default:
      break;
  }

  end:
  return 0;
}

void DbgNull(IN CONST CHAR16 * fmt, ...)
{
}

UINT32 bht_readl(EFI_PCI_IO_PROTOCOL *PciIo, UINT32 offset)
{
  UINT32 arg;
  PciIo->Mem.Read(PciIo,EfiPciIoWidthUint32,1,offset,1,&arg);
  return arg;
}

void bht_writel(EFI_PCI_IO_PROTOCOL *PciIo, UINT32 offset, UINT32 value)
{
  PciIo->Mem.Write(PciIo,EfiPciIoWidthUint32,1,offset,1,&value);
}


UINT32 PciBhtRead32(EFI_PCI_IO_PROTOCOL *PciIo, UINT32 offset)
{
  UINT32 i = 0;
  UINT32 tmp[3] = {0};

  if((g_deviceId == PCI_DEV_ID_SDS0) ||
      (g_deviceId == PCI_DEV_ID_SDS1) ||
      (g_deviceId == PCI_DEV_ID_FJ2) ||
      (g_deviceId == PCI_DEV_ID_SB0) ||
      (g_deviceId == PCI_DEV_ID_SB1))
  {
    // For Sandstorm, HW implement a mapping method by memory space reg to access PCI reg.
    // Enable mapping
  
    // Check function conflict
    if((g_deviceId == PCI_DEV_ID_SDS0) ||
        (g_deviceId == PCI_DEV_ID_FJ2) ||
        (g_deviceId == PCI_DEV_ID_SB0) ||
        (g_deviceId == PCI_DEV_ID_SB1))
    {
      i = 0;
      bht_writel(PciIo, BHT_PCIRMappingEn, 0x40000000);
      while((bht_readl(PciIo, BHT_PCIRMappingEn) & 0x40000000) == 0)
      {
        if(i == 5)
        {
          //DbgMsg((DRIVERNAME " - %s() function 0 can't lock!\n", __FUNCTION__));
          goto RD_DIS_MAPPING;
        }
            gBS->Stall(1000);
        i++;
          bht_writel(PciIo, BHT_PCIRMappingEn, 0x40000000);
  
      }
    }
    else if(g_deviceId == PCI_DEV_ID_SDS1)
    {
      i = 0;
      bht_writel(PciIo, BHT_PCIRMappingEn, 0x20000000);
      while((bht_readl(PciIo, BHT_PCIRMappingEn) & 0x20000000) == 0)
      {
        if(i == 5)
        {
          //DbgErr((DRIVERNAME " - %s() function 1 can't lock!\n", __FUNCTION__));
          goto RD_DIS_MAPPING;
        }
        gBS->Stall(1000);
        i++;
        bht_writel(PciIo, BHT_PCIRMappingEn, 0x20000000);
      }
    }
  
    // Check last operation is complete
    i = 0;
    while(bht_readl(PciIo, BHT_PCIRMappingCtl) & 0xc0000000)
    {
      if(i == 5)
      {
        //DbgErr((DRIVERNAME " - [204] = 0x%x\n", RegisterRead32(ELN_dPCIRMappingCtl)));
        //DbgErr((DRIVERNAME " - [208] = 0x%x\n", RegisterRead32(ELN_dPCIRMappingEn)));
        //DbgErr((DRIVERNAME " - %s() check last operation complete timeout!!!\n", __FUNCTION__));
        goto RD_DIS_MAPPING;
      }
      gBS->Stall(1000);
      i += 1;
    }
  
    // Set register address
    tmp[0] |= 0x40000000;
    tmp[0] |= offset;
    bht_writel(PciIo, BHT_PCIRMappingCtl, tmp[0]);
  
    // Check read is complete
    i = 0;
    while(bht_readl(PciIo, BHT_PCIRMappingCtl) & 0x40000000)
    {
      if(i == 5)
      {
        //DbgErr((DRIVERNAME " - %s() check read operation complete timeout!!!\n", __FUNCTION__));
        goto RD_DIS_MAPPING;
      }
      gBS->Stall(1000);
      i += 1;
    }
  
    // Get PCIR value
    tmp[1] = bht_readl(PciIo, BHT_PCIRMappingVal);
  
RD_DIS_MAPPING:
    // Disable mapping
    bht_writel(PciIo, BHT_PCIRMappingEn, 0x80000000);
  
    //DbgDebug(L"%s offset=%x Value:%x\n", __FUNCTION__, offset, tmp[1]);
    return tmp[1];
  }
  
  //DbgDebug(L"%s offset=%x Value:%x\n", __FUNCTION__, offset, tmp[0]);
  return tmp[0];	
}

void PciBhtWrite32(EFI_PCI_IO_PROTOCOL *PciIo, UINT32 offset, UINT32 value)
{
  UINT32 tmp = 0;
    UINT32 i = 0;

  if((g_deviceId == PCI_DEV_ID_SDS0) ||
      (g_deviceId == PCI_DEV_ID_SDS1) ||
      (g_deviceId == PCI_DEV_ID_FJ2) ||
      (g_deviceId == PCI_DEV_ID_SB0) ||
      (g_deviceId == PCI_DEV_ID_SB1))
    {
        // For Sandstorm, HW implement a mapping method by memory space reg to access PCI reg.
        // Upper caller doesn't need to set 0xD0.

        // Enable mapping

        // Check function conflict
    if((g_deviceId == PCI_DEV_ID_SDS0) ||
        (g_deviceId == PCI_DEV_ID_FJ2) ||
        (g_deviceId == PCI_DEV_ID_SB0) ||
        (g_deviceId == PCI_DEV_ID_SB1))
        {
            i = 0;
            bht_writel(PciIo, BHT_PCIRMappingEn, 0x40000000);
            while((bht_readl(PciIo, BHT_PCIRMappingEn) & 0x40000000) == 0)
            {
                if(i == 5)
                {
                    //DbgErr((DRIVERNAME " - %s() function 0 can't lock!\n", __FUNCTION__));
                    goto WR_DIS_MAPPING;
                }

                gBS->Stall(1000);
                i++;
                bht_writel(PciIo, BHT_PCIRMappingEn, 0x40000000);
            }
        }
        else if(g_deviceId == PCI_DEV_ID_SDS1)
        {
            i = 0;
            bht_writel(PciIo, BHT_PCIRMappingEn, 0x20000000);

            while((bht_readl(PciIo, BHT_PCIRMappingEn) & 0x20000000) == 0)
            {
                if(i == 5)
                {
                    //DbgErr((DRIVERNAME " - %s() function 0 can't lock!\n", __FUNCTION__));
                    goto WR_DIS_MAPPING;
                }

                gBS->Stall(1000);
                i++;
                bht_writel(PciIo, BHT_PCIRMappingEn, 0x20000000);
            }
        }

        // Enable MEM access
        bht_writel(PciIo, BHT_PCIRMappingVal, 0x80000000);
        bht_writel(PciIo, BHT_PCIRMappingCtl, 0x800000D0);

        // Check last operation is complete
        i = 0;
        while(bht_readl(PciIo, BHT_PCIRMappingCtl) & 0xc0000000)
        {
            if(i == 5)
            {
                //DbgErr((DRIVERNAME " - %s() check last operation complete timeout!!!\n", __FUNCTION__));
                goto WR_DIS_MAPPING;
            }
            gBS->Stall(1000);
            i += 1;
        }

        // Set write value
        bht_writel(PciIo, BHT_PCIRMappingVal, value);
        // Set register address
        tmp |= 0x80000000;
        tmp |= offset;
        bht_writel(PciIo, BHT_PCIRMappingCtl, tmp);

        // Check write is complete
        i = 0;
        while(bht_readl(PciIo, BHT_PCIRMappingCtl) & 0x80000000)
        {
            if(i == 5)
            {
                //DbgErr((DRIVERNAME " - %s() check write operation complete timeout!!!\n", __FUNCTION__));
                goto WR_DIS_MAPPING;
            }
            gBS->Stall(1000);
            i += 1;
        }

WR_DIS_MAPPING:
        // Disable MEM access
        bht_writel(PciIo, BHT_PCIRMappingVal, 0x80000001);
        bht_writel(PciIo, BHT_PCIRMappingCtl, 0x800000D0);

        // Check last operation is complete
        i = 0;
        while(bht_readl(PciIo, BHT_PCIRMappingCtl) & 0xc0000000)
        {
            if(i == 5)
            {
                //DbgErr((DRIVERNAME " - %s() check last operation complete timeout!!!\n", __FUNCTION__));
                break;
            }
            gBS->Stall(1000);
            i += 1;
        }

        // Disable function conflict

        // Disable mapping
        bht_writel(PciIo, BHT_PCIRMappingEn, 0x80000000);
    }
}

void PciBhtOr32(EFI_PCI_IO_PROTOCOL *PciIo, UINT32 offset, UINT32 value)
{
  UINT32 arg;
  arg = PciBhtRead32(PciIo, offset);
  PciBhtWrite32(PciIo, offset, value | arg);
}

void PciBhtAnd32(EFI_PCI_IO_PROTOCOL *PciIo, UINT32 offset, UINT32 value)
{
  UINT32 arg;
  arg = PciBhtRead32(PciIo, offset);
  PciBhtWrite32(PciIo, offset, value & arg);
}
