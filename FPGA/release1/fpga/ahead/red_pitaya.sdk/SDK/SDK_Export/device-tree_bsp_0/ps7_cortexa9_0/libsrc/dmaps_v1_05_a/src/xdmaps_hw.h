/******************************************************************************
*
* (c) Copyright 2009-2013 Xilinx, Inc. All rights reserved.
*
* This file contains confidential and proprietary information of Xilinx, Inc.
* and is protected under U.S. and international copyright and other
* intellectual property laws.
*
* DISCLAIMER
* This disclaimer is not a license and does not grant any rights to the
* materials distributed herewith. Except as otherwise provided in a valid
* license issued to you by Xilinx, and to the maximum extent permitted by
* applicable law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND WITH ALL
* FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES AND CONDITIONS, EXPRESS,
* IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF
* MERCHANTABILITY, NON-INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE;
* and (2) Xilinx shall not be liable (whether in contract or tort, including
* negligence, or under any other theory of liability) for any loss or damage
* of any kind or nature related to, arising under or in connection with these
* materials, including for any direct, or any indirect, special, incidental,
* or consequential loss or damage (including loss of data, profits, goodwill,
* or any type of loss or damage suffered as a result of any action brought by
* a third party) even if such damage or loss was reasonably foreseeable or
* Xilinx had been advised of the possibility of the same.
*
* CRITICAL APPLICATIONS
* Xilinx products are not designed or intended to be fail-safe, or for use in
* any application requiring fail-safe performance, such as life-support or
* safety devices or systems, Class III medical devices, nuclear facilities,
* applications related to the deployment of airbags, or any other applications
* that could lead to death, personal injury, or severe property or
* environmental damage (individually and collectively, "Critical
* Applications"). Customer assumes the sole risk and liability of any use of
* Xilinx products in Critical Applications, subject only to applicable laws
* and regulations governing limitations on product liability.
*
* THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS PART OF THIS FILE
* AT ALL TIMES.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xdmaps_hw.h
*
* This header file contains the hardware interface of an XDmaPs device.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who   Date     Changes
* ----- ----  -------- ----------------------------------------------
* 1.00a	hbm   08/18/10 First Release
* 1.01a nm    12/20/12 Added definition XDMAPS_CHANNELS_PER_DEV which specifies
*		       the maximum number of channels.
*		       Replaced the usage of XPAR_XDMAPS_CHANNELS_PER_DEV
*                      with XDMAPS_CHANNELS_PER_DEV defined in xdmaps_hw.h
* 1.02a sg    05/16/12 Made changes for doxygen
* </pre>
*
******************************************************************************/

#ifndef XDMAPS_HW_H		/* prevent circular inclusions */
#define XDMAPS_HW_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xil_types.h"
#include "xil_assert.h"
#include "xil_io.h"

/************************** Constant Definitions *****************************/

/** @name Register Map
 *
 * Register offsets for the DMAC.
 * @{
 */

#define XDMAPS_DS_OFFSET		0x000 /* DMA Status Register */
#define XDMAPS_DPC_OFFSET	0x004 /* DMA Program Counter Rregister */
#define XDMAPS_INTEN_OFFSET	0X020 /* DMA Interrupt Enable Register */
#define XDMAPS_ES_OFFSET		0x024 /* DMA Event Status Register */
#define XDMAPS_INTSTATUS_OFFSET	0x028 /* DMA Interrupt Status Register
					       */
#define XDMAPS_INTCLR_OFFSET	0x02c /* DMA Interrupt Clear Register */
#define XDMAPS_FSM_OFFSET 	0x030 /* DMA Fault Status DMA Manager
				       * Register
				       */
#define XDMAPS_FSC_OFFSET	0x034 /* DMA Fault Status DMA Chanel Register
				       */
#define XDMAPS_FTM_OFFSET	0x038 /* DMA Fault Type DMA Manager Register */

#define XDMAPS_FTC0_OFFSET	0x040 /* DMA Fault Type for DMA Channel 0 */
/*
 * The offset for the rest of the FTC registers is calculated as
 * FTC0 + dev_chan_num * 4
 */
#define XDmaPs_FTCn_OFFSET(ch)	(XDMAPS_FTC0_OFFSET + (ch) * 4)

#define XDMAPS_CS0_OFFSET	0x100 /* Channel Status for DMA Channel 0 */
/*
 * The offset for the rest of the CS registers is calculated as
 * CS0 + * dev_chan_num * 0x08
 */
#define XDmaPs_CSn_OFFSET(ch)	(XDMAPS_CS0_OFFSET + (ch) * 8)

#define XDMAPS_CPC0_OFFSET	0x104 /* Channel Program Counter for DMA
				       * Channel 0
				       */
/*
 * The offset for the rest of the CPC registers is calculated as
 * CPC0 + dev_chan_num * 0x08
 */
#define XDmaPs_CPCn_OFFSET(ch)	(XDMAPS_CPC0_OFFSET + (ch) * 8)

#define XDMAPS_SA_0_OFFSET	0x400 /* Source Address Register for DMA
				       * Channel 0
				       */
/* The offset for the rest of the SA registers is calculated as
 * SA_0 + dev_chan_num * 0x20
 */
#define XDmaPs_SA_n_OFFSET(ch)	(XDMAPS_SA_0_OFFSET + (ch) * 0x20)

#define XDMAPS_DA_0_OFFSET	0x404 /* Destination Address Register for
				       * DMA Channel 0
				       */
/* The offset for the rest of the DA registers is calculated as
 * DA_0 + dev_chan_num * 0x20
 */
#define XDmaPs_DA_n_OFFSET(ch)	(XDMAPS_DA_0_OFFSET + (ch) * 0x20)

#define XDMAPS_CC_0_OFFSET	0x408 /* Channel Control Register for
				       * DMA Channel 0
				       */
/*
 * The offset for the rest of the CC registers is calculated as
 * CC_0 + dev_chan_num * 0x20
 */
#define XDmaPs_CC_n_OFFSET(ch)	(XDMAPS_CC_0_OFFSET + (ch) * 0x20)

#define XDMAPS_LC0_0_OFFSET	0x40C /* Loop Counter 0 for DMA Channel 0 */
/*
 * The offset for the rest of the LC0 registers is calculated as
 * LC_0 + dev_chan_num * 0x20
 */
#define XDmaPs_LC0_n_OFFSET(ch)	(XDMAPS_LC0_0_OFFSET + (ch) * 0x20)
#define XDMAPS_LC1_0_OFFSET	0x410 /* Loop Counter 1 for DMA Channel 0 */
/*
 * The offset for the rest of the LC1 registers is calculated as
 * LC_0 + dev_chan_num * 0x20
 */
#define XDmaPs_LC1_n_OFFSET(ch)	(XDMAPS_LC1_0_OFFSET + (ch) * 0x20)

#define XDMAPS_DBGSTATUS_OFFSET	0xD00 /* Debug Status Register */
#define XDMAPS_DBGCMD_OFFSET	0xD04 /* Debug Command Register */
#define XDMAPS_DBGINST0_OFFSET	0xD08 /* Debug Instruction 0 Register */
#define XDMAPS_DBGINST1_OFFSET	0xD0C /* Debug Instruction 1 Register */

#define XDMAPS_CR0_OFFSET	0xE00 /* Configuration Register 0 */
#define XDMAPS_CR1_OFFSET	0xE04 /* Configuration Register 1 */
#define XDMAPS_CR2_OFFSET	0xE08 /* Configuration Register 2 */
#define XDMAPS_CR3_OFFSET	0xE0C /* Configuration Register 3 */
#define XDMAPS_CR4_OFFSET	0xE10 /* Configuration Register 4 */
#define XDMAPS_CRDN_OFFSET	0xE14 /* Configuration Register Dn */

#define XDMAPS_PERIPH_ID_0_OFFSET	0xFE0 /* Peripheral Identification
					       * Register 0
					       */
#define XDMAPS_PERIPH_ID_1_OFFSET	0xFE4 /* Peripheral Identification
					       * Register 1
					       */
#define XDMAPS_PERIPH_ID_2_OFFSET	0xFE8 /* Peripheral Identification
					       * Register 2
					       */
#define XDMAPS_PERIPH_ID_3_OFFSET	0xFEC /* Peripheral Identification
					       * Register 3
					       */
#define XDMAPS_PCELL_ID_0_OFFSET	0xFF0 /* PrimeCell Identification
				       * Register 0
				       */
#define XDMAPS_PCELL_ID_1_OFFSET	0xFF4 /* PrimeCell Identification
				       * Register 1
				       */
#define XDMAPS_PCELL_ID_2_OFFSET	0xFF8 /* PrimeCell Identification
				       * Register 2
				       */
#define XDMAPS_PCELL_ID_3_OFFSET	0xFFC /* PrimeCell Identification
				       * Register 3
				       */

/*
 * Some useful register masks
 */
#define XDMAPS_DS_DMA_STATUS		0x0F /* DMA status mask */
#define XDMAPS_DS_DMA_STATUS_STOPPED	0x00 /* debug status busy mask */

#define XDMAPS_DBGSTATUS_BUSY		0x01 /* debug status busy mask */

#define XDMAPS_CS_ACTIVE_MASK		0x07 /* channel status active mask,
					      * llast 3 bits of CS register
					      */

#define XDMAPS_CR1_I_CACHE_LEN_MASK	0x07 /* i_cache_len mask */


/*
 * XDMAPS_DBGINST0 - constructs the word for the Debug Instruction-0 Register.
 * @b1: Instruction byte 1
 * @b0: Instruction byte 0
 * @ch: Channel number
 * @dbg_th: Debug thread encoding: 0 = DMA manager thread, 1 = DMA channel
 */
#define XDmaPs_DBGINST0(b1, b0, ch, dbg_th) \
	(((b1) << 24) | ((b0) << 16) | (((ch) & 0x7) << 8) | ((dbg_th & 0x1)))

/* @} */

/** @name Control Register
 *
 * The Control register (CR) controls the major functions of the device.
 *
 * Control Register Bit Definition
 */

/* @}*/


#define XDMAPS_CHANNELS_PER_DEV		8


/** @name Mode Register
 *
 * The mode register (MR) defines the mode of transfer as well as the data
 * format. If this register is modified during transmission or reception,
 * data validity cannot be guaranteed.
 *
 * Mode Register Bit Definition
 * @{
 */

/* @} */


/** @name Interrupt Registers
 *
 * Interrupt control logic uses the interrupt enable register (IER) and the
 * interrupt disable register (IDR) to set the value of the bits in the
 * interrupt mask register (IMR). The IMR determines whether to pass an
 * interrupt to the interrupt status register (ISR).
 * Writing a 1 to IER Enbables an interrupt, writing a 1 to IDR disables an
 * interrupt. IMR and ISR are read only, and IER and IDR are write only.
 * Reading either IER or IDR returns 0x00.
 *
 * All four registers have the same bit definitions.
 *
 * @{
 */

/* @} */


#define XDmaPs_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))

/***************************************************************************/
/**
* Write a DMAC register.
*
* @param    BaseAddress contains the base address of the device.
* @param    RegOffset contains the offset from the base address of the device.
* @param    RegisterValue is the value to be written to the register.
*
* @return   None.
*
* @note
* C-Style signature:
*    void XDmaPs_WriteReg(u32 BaseAddress, int RegOffset,
*                          u32 RegisterValue)
******************************************************************************/
#define XDmaPs_WriteReg(BaseAddress, RegOffset, RegisterValue) \
    Xil_Out32((BaseAddress) + (RegOffset), (RegisterValue))
/************************** Variable Definitions *****************************/

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
