/*
 * Copyright 2004 ATI Technologies Inc., Markham, Ontario
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 * THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86PciInfo.h"
#include "radeon.h"
#include "radeon_reg.h"
#include "radeon_macros.h"
#include "radeon_probe.h"
#include "radeon_atombios.h"
#include "vbe.h"

typedef enum
{
    DDC_NONE_DETECTED,
    DDC_MONID,
    DDC_DVI,
    DDC_VGA,
    DDC_CRT2,
    DDC_LCD,
    DDC_GPIO,
} RADEONLegacyDDCType;

typedef enum
{
    CONNECTOR_NONE_LEGACY,
    CONNECTOR_PROPRIETARY_LEGACY,
    CONNECTOR_CRT_LEGACY,
    CONNECTOR_DVI_I_LEGACY,
    CONNECTOR_DVI_D_LEGACY,
    CONNECTOR_CTV_LEGACY,
    CONNECTOR_STV_LEGACY,
    CONNECTOR_UNSUPPORTED_LEGACY
} RADEONLegacyConnectorType;

static Bool
radeon_read_bios(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info     = RADEONPTR(pScrn);

#ifdef XSERVER_LIBPCIACCESS
    if (pci_device_read_rom(info->PciInfo, info->VBIOS)) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Failed to read PCI ROM!\n");
	return FALSE;
    }
#else
    xf86ReadPciBIOS(0, info->PciTag, 0, info->VBIOS, RADEON_VBIOS_SIZE);
    if (info->VBIOS[0] != 0x55 || info->VBIOS[1] != 0xaa) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Video BIOS not detected in PCI space!\n");
	if (xf86IsEntityPrimary(info->pEnt->index)) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "Attempting to read Video BIOS from "
		       "legacy ISA space!\n");
	    info->BIOSAddr = 0x000c0000;
	    xf86ReadDomainMemory(info->PciTag, info->BIOSAddr,
				 RADEON_VBIOS_SIZE, info->VBIOS);
	}
    }
#endif
    if (info->VBIOS[0] != 0x55 || info->VBIOS[1] != 0xaa)
	return FALSE;
    else
	return TRUE;
}

static Bool
radeon_read_unposted_bios(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info     = RADEONPTR(pScrn);
    RADEONEntPtr pRADEONEnt = RADEONEntPriv(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    Bool ret;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Attempting to read un-POSTed bios\n");

    if (info->ChipFamily >= CHIP_FAMILY_R600) {
	uint32_t viph_control   = INREG(RADEON_VIPH_CONTROL);
	uint32_t bus_cntl       = INREG(RADEON_BUS_CNTL);
	uint32_t d1vga_control  = INREG(AVIVO_D1VGA_CONTROL);
	uint32_t d2vga_control  = INREG(AVIVO_D2VGA_CONTROL);
	uint32_t vga_render_control  = INREG(AVIVO_VGA_RENDER_CONTROL);
	uint32_t rom_cntl       = INREG(R600_ROM_CNTL);
	uint32_t general_pwrmgt = INREG(R600_GENERAL_PWRMGT);
	uint32_t low_vid_lower_gpio_cntl    = INREG(R600_LOW_VID_LOWER_GPIO_CNTL);
	uint32_t medium_vid_lower_gpio_cntl = INREG(R600_MEDIUM_VID_LOWER_GPIO_CNTL);
	uint32_t high_vid_lower_gpio_cntl   = INREG(R600_HIGH_VID_LOWER_GPIO_CNTL);
	uint32_t ctxsw_vid_lower_gpio_cntl  = INREG(R600_CTXSW_VID_LOWER_GPIO_CNTL);
	uint32_t lower_gpio_enable          = INREG(R600_LOWER_GPIO_ENABLE);

	/* disable VIP */
	OUTREG(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));

	/* enable the rom */
	OUTREG(RADEON_BUS_CNTL, (bus_cntl & ~RADEON_BUS_BIOS_DIS_ROM));

        /* Disable VGA mode */
	OUTREG(AVIVO_D1VGA_CONTROL, (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
						       AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	OUTREG(AVIVO_D2VGA_CONTROL, (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
						       AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	OUTREG(AVIVO_VGA_RENDER_CONTROL, (vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	OUTREG(R600_ROM_CNTL, ((rom_cntl & ~R600_SCK_PRESCALE_CRYSTAL_CLK_MASK) |
			       (1 << R600_SCK_PRESCALE_CRYSTAL_CLK_SHIFT) |
			       R600_SCK_OVERWRITE));

	OUTREG(R600_GENERAL_PWRMGT, (general_pwrmgt & ~R600_OPEN_DRAIN_PADS));

	OUTREG(R600_LOW_VID_LOWER_GPIO_CNTL, (low_vid_lower_gpio_cntl & ~0x400));

	OUTREG(R600_MEDIUM_VID_LOWER_GPIO_CNTL, (medium_vid_lower_gpio_cntl & ~0x400));

	OUTREG(R600_HIGH_VID_LOWER_GPIO_CNTL, (high_vid_lower_gpio_cntl & ~0x400));

	OUTREG(R600_CTXSW_VID_LOWER_GPIO_CNTL, (ctxsw_vid_lower_gpio_cntl & ~0x400));

	OUTREG(R600_LOWER_GPIO_ENABLE, (lower_gpio_enable | 0x400));

	ret = radeon_read_bios(pScrn);

	/* restore regs */
	OUTREG(RADEON_VIPH_CONTROL, viph_control);
	OUTREG(RADEON_BUS_CNTL, bus_cntl);
	OUTREG(AVIVO_D1VGA_CONTROL, d1vga_control);
	OUTREG(AVIVO_D2VGA_CONTROL, d2vga_control);
	OUTREG(AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	OUTREG(R600_ROM_CNTL, rom_cntl);
	OUTREG(R600_GENERAL_PWRMGT, general_pwrmgt);
	OUTREG(R600_LOW_VID_LOWER_GPIO_CNTL, low_vid_lower_gpio_cntl);
	OUTREG(R600_MEDIUM_VID_LOWER_GPIO_CNTL, medium_vid_lower_gpio_cntl);
	OUTREG(R600_HIGH_VID_LOWER_GPIO_CNTL, high_vid_lower_gpio_cntl);
	OUTREG(R600_CTXSW_VID_LOWER_GPIO_CNTL, ctxsw_vid_lower_gpio_cntl);
	OUTREG(R600_LOWER_GPIO_ENABLE, lower_gpio_enable);

    } else if (info->ChipFamily >= CHIP_FAMILY_RV515) {
	uint32_t seprom_cntl1   = INREG(RADEON_SEPROM_CNTL1);
	uint32_t viph_control   = INREG(RADEON_VIPH_CONTROL);
	uint32_t bus_cntl       = INREG(RADEON_BUS_CNTL);
	uint32_t d1vga_control  = INREG(AVIVO_D1VGA_CONTROL);
	uint32_t d2vga_control  = INREG(AVIVO_D2VGA_CONTROL);
	uint32_t vga_render_control  = INREG(AVIVO_VGA_RENDER_CONTROL);
	uint32_t gpiopad_a      = INREG(RADEON_GPIOPAD_A);
	uint32_t gpiopad_en     = INREG(RADEON_GPIOPAD_EN);
	uint32_t gpiopad_mask   = INREG(RADEON_GPIOPAD_MASK);

	OUTREG(RADEON_SEPROM_CNTL1, ((seprom_cntl1 & ~RADEON_SCK_PRESCALE_MASK) |
				     (0xc << RADEON_SCK_PRESCALE_SHIFT)));

	OUTREG(RADEON_GPIOPAD_A, 0);
	OUTREG(RADEON_GPIOPAD_EN, 0);
	OUTREG(RADEON_GPIOPAD_MASK, 0);

	/* disable VIP */
	OUTREG(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));

	/* enable the rom */
	OUTREG(RADEON_BUS_CNTL, (bus_cntl & ~RADEON_BUS_BIOS_DIS_ROM));

        /* Disable VGA mode */
	OUTREG(AVIVO_D1VGA_CONTROL, (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
						       AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	OUTREG(AVIVO_D2VGA_CONTROL, (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
						       AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	OUTREG(AVIVO_VGA_RENDER_CONTROL, (vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	ret = radeon_read_bios(pScrn);

	/* restore regs */
	OUTREG(RADEON_SEPROM_CNTL1, seprom_cntl1);
	OUTREG(RADEON_VIPH_CONTROL, viph_control);
	OUTREG(RADEON_BUS_CNTL, bus_cntl);
	OUTREG(AVIVO_D1VGA_CONTROL, d1vga_control);
	OUTREG(AVIVO_D2VGA_CONTROL, d2vga_control);
	OUTREG(AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	OUTREG(RADEON_GPIOPAD_A, gpiopad_a);
	OUTREG(RADEON_GPIOPAD_EN, gpiopad_en);
	OUTREG(RADEON_GPIOPAD_MASK, gpiopad_mask);

    } else {
	uint32_t seprom_cntl1   = INREG(RADEON_SEPROM_CNTL1);
	uint32_t viph_control   = INREG(RADEON_VIPH_CONTROL);
	uint32_t bus_cntl       = INREG(RADEON_BUS_CNTL);
	uint32_t crtc_gen_cntl  = INREG(RADEON_CRTC_GEN_CNTL);
	uint32_t crtc2_gen_cntl = 0;
	uint32_t crtc_ext_cntl  = INREG(RADEON_CRTC_EXT_CNTL);
	uint32_t fp2_gen_cntl   = 0;

	if (PCI_DEV_DEVICE_ID(info->PciInfo) == PCI_CHIP_RV100_QY)
	    fp2_gen_cntl   = INREG(RADEON_FP2_GEN_CNTL);

	if (pRADEONEnt->HasCRTC2)
	    crtc2_gen_cntl = INREG(RADEON_CRTC2_GEN_CNTL);

	OUTREG(RADEON_SEPROM_CNTL1, ((seprom_cntl1 & ~RADEON_SCK_PRESCALE_MASK) |
				     (0xc << RADEON_SCK_PRESCALE_SHIFT)));

	/* disable VIP */
	OUTREG(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));

	/* enable the rom */
	OUTREG(RADEON_BUS_CNTL, (bus_cntl & ~RADEON_BUS_BIOS_DIS_ROM));

        /* Turn off mem requests and CRTC for both controllers */
	OUTREG(RADEON_CRTC_GEN_CNTL, ((crtc_gen_cntl & ~RADEON_CRTC_EN) |
				      (RADEON_CRTC_DISP_REQ_EN_B |
				       RADEON_CRTC_EXT_DISP_EN)));
	if (pRADEONEnt->HasCRTC2)
	    OUTREG(RADEON_CRTC2_GEN_CNTL, ((crtc2_gen_cntl & ~RADEON_CRTC2_EN) |
					   RADEON_CRTC2_DISP_REQ_EN_B));

        /* Turn off CRTC */
	OUTREG(RADEON_CRTC_EXT_CNTL, ((crtc_ext_cntl & ~RADEON_CRTC_CRT_ON) |
				      (RADEON_CRTC_SYNC_TRISTAT |
				       RADEON_CRTC_DISPLAY_DIS)));

	if (PCI_DEV_DEVICE_ID(info->PciInfo) == PCI_CHIP_RV100_QY)
	    OUTREG(RADEON_FP2_GEN_CNTL, (fp2_gen_cntl & ~RADEON_FP2_ON));

	ret = radeon_read_bios(pScrn);

	/* restore regs */
	OUTREG(RADEON_SEPROM_CNTL1, seprom_cntl1);
	OUTREG(RADEON_VIPH_CONTROL, viph_control);
	OUTREG(RADEON_BUS_CNTL, bus_cntl);
	OUTREG(RADEON_CRTC_GEN_CNTL, crtc_gen_cntl);
	if (pRADEONEnt->HasCRTC2)
	    OUTREG(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	OUTREG(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl);
	if (PCI_DEV_DEVICE_ID(info->PciInfo) == PCI_CHIP_RV100_QY)
	    OUTREG(RADEON_FP2_GEN_CNTL, fp2_gen_cntl);
    }
    return ret;
}

Bool
radeon_card_posted(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info     = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    uint32_t reg;

    if (IS_AVIVO_VARIANT) {
	reg = INREG(AVIVO_D1CRTC_CONTROL) | INREG(AVIVO_D2CRTC_CONTROL);
	if (reg & AVIVO_CRTC_EN)
	    return TRUE;
    } else {
	reg = INREG(RADEON_CRTC_GEN_CNTL) | INREG(RADEON_CRTC2_GEN_CNTL);
	if (reg & RADEON_CRTC_EN)
	    return TRUE;
    }

    return FALSE;
}

/* Read the Video BIOS block and the FP registers (if applicable). */
Bool
RADEONGetBIOSInfo(ScrnInfoPtr pScrn, xf86Int10InfoPtr  pInt10)
{
    RADEONInfoPtr info     = RADEONPTR(pScrn);
    int tmp;
    unsigned short dptr;
    Bool posted = TRUE;

#ifdef XSERVER_LIBPCIACCESS
    int size = info->PciInfo->rom_size > RADEON_VBIOS_SIZE ? info->PciInfo->rom_size : RADEON_VBIOS_SIZE;
    info->VBIOS = xalloc(size);
#else
    info->VBIOS = xalloc(RADEON_VBIOS_SIZE);
#endif
    if (!info->VBIOS) {
	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "Cannot allocate space for hold Video BIOS!\n");
	return FALSE;
    } else {
	if (pInt10) {
	    info->BIOSAddr = pInt10->BIOSseg << 4;
	    (void)memcpy(info->VBIOS, xf86int10Addr(pInt10, info->BIOSAddr),
			 RADEON_VBIOS_SIZE);
	} else if (!radeon_read_bios(pScrn)) {
	    (void)radeon_read_unposted_bios(pScrn);
	    posted = FALSE;
	}
    }

    if (info->VBIOS[0] != 0x55 || info->VBIOS[1] != 0xaa) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Unrecognized BIOS signature, BIOS data will not be used\n");
	xfree (info->VBIOS);
	info->VBIOS = NULL;
	return FALSE;
    }

    /* Verify it's an x86 BIOS not OF firmware, copied from radeonfb */
    dptr = RADEON_BIOS16(0x18);
    /* If PCI data signature is wrong assume x86 video BIOS anyway */
    if (RADEON_BIOS32(dptr) != (('R' << 24) | ('I' << 16) | ('C' << 8) | 'P')) {
       xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "ROM PCI data signature incorrect, ignoring\n");
    }
    else if (info->VBIOS[dptr + 0x14] != 0x0) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Not an x86 BIOS ROM image, BIOS data will not be used\n");
	xfree (info->VBIOS);
	info->VBIOS = NULL;
	return FALSE;
    }

    if (info->VBIOS) info->ROMHeaderStart = RADEON_BIOS16(0x48);

    if(!info->ROMHeaderStart) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Invalid ROM pointer, BIOS data will not be used\n");
	xfree (info->VBIOS);
	info->VBIOS = NULL;
	return FALSE;
    }

    tmp = info->ROMHeaderStart + 4;
    if ((RADEON_BIOS8(tmp)   == 'A' &&
	 RADEON_BIOS8(tmp+1) == 'T' &&
	 RADEON_BIOS8(tmp+2) == 'O' &&
	 RADEON_BIOS8(tmp+3) == 'M') ||
	(RADEON_BIOS8(tmp)   == 'M' &&
	 RADEON_BIOS8(tmp+1) == 'O' &&
	 RADEON_BIOS8(tmp+2) == 'T' &&
	 RADEON_BIOS8(tmp+3) == 'A'))
	info->IsAtomBios = TRUE;
    else
	info->IsAtomBios = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s BIOS detected\n",
	       info->IsAtomBios ? "ATOM":"Legacy");

    if (info->IsAtomBios) {
	AtomBiosArgRec atomBiosArg;

	if (RHDAtomBiosFunc(pScrn->scrnIndex, NULL, ATOMBIOS_INIT, &atomBiosArg)
	    == ATOM_SUCCESS) {
	    info->atomBIOS = atomBiosArg.atomhandle;
	}

	atomBiosArg.fb.start = info->FbFreeStart;
	atomBiosArg.fb.size = info->FbFreeSize;
	if (RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS, ATOMBIOS_ALLOCATE_FB_SCRATCH,
			    &atomBiosArg) == ATOM_SUCCESS) {

	    info->FbFreeStart = atomBiosArg.fb.start;
	    info->FbFreeSize = atomBiosArg.fb.size;
	}

	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS, GET_DEFAULT_ENGINE_CLOCK,
			&atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS, GET_DEFAULT_MEMORY_CLOCK,
			&atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS,
			GET_MAX_PIXEL_CLOCK_PLL_OUTPUT, &atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS,
			GET_MIN_PIXEL_CLOCK_PLL_OUTPUT, &atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS,
			GET_MAX_PIXEL_CLOCK_PLL_INPUT, &atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS,
			GET_MIN_PIXEL_CLOCK_PLL_INPUT, &atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS,
			GET_MAX_PIXEL_CLK, &atomBiosArg);
	RHDAtomBiosFunc(pScrn->scrnIndex, info->atomBIOS,
			GET_REF_CLOCK, &atomBiosArg);

	info->MasterDataStart = RADEON_BIOS16 (info->ROMHeaderStart + 32);
    }

    /* We are a bit too quick at using this "unposted" to re-post the
     * card. This causes some problems with VT switch on some machines,
     * so let's work around this for now by only POSTing if none of the
     * CRTCs are enabled
     */
    if ((!posted) && info->VBIOS) {
	posted = radeon_card_posted(pScrn);
    }

    if ((!posted) && info->VBIOS) {
	if (info->IsAtomBios) {
	    if (!rhdAtomASICInit(info->atomBIOS))
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "%s: AsicInit failed.\n",__func__);
	} else {
#if 0
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Attempting to POST via legacy BIOS tables\n");
	    RADEONGetBIOSInitTableOffsets(pScrn);
	    RADEONPostCardFromBIOSTables(pScrn);
#endif
	}
    }

    return TRUE;
}

static Bool RADEONGetATOMConnectorInfoFromBIOS (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);

    if (!info->VBIOS) return FALSE;
    
    if (RADEONGetATOMConnectorInfoFromBIOSObject(pScrn))
	return TRUE;

    if (RADEONGetATOMConnectorInfoFromBIOSConnectorTable(pScrn))
	return TRUE;

    return FALSE;
}

static void RADEONApplyLegacyQuirks(ScrnInfoPtr pScrn, int index)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);

    /* on XPRESS chips, CRT2_DDC and MONID_DCC both use the 
     * MONID gpio, but use different pins.
     * CRT2_DDC uses the standard pinout, MONID_DDC uses
     * something else.
     */
    if ((info->ChipFamily == CHIP_FAMILY_RS400 ||
	 info->ChipFamily == CHIP_FAMILY_RS480) &&
	info->BiosConnector[index].ConnectorType == CONNECTOR_VGA &&
	info->BiosConnector[index].ddc_i2c.mask_clk_reg == RADEON_GPIO_CRT2_DDC) {
	info->BiosConnector[index].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_MONID);
    }

    /* XPRESS desktop chips seem to have a proprietary connector listed for
     * DVI-D, try and do the right thing here.
     */
    if ((!info->IsMobility) &&
	(info->BiosConnector[index].ConnectorType == CONNECTOR_LVDS)) {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Proprietary connector found, assuming DVI-D\n");
	info->BiosConnector[index].DACType = DAC_NONE;
	info->BiosConnector[index].TMDSType = TMDS_EXT;
	info->BiosConnector[index].ConnectorType = CONNECTOR_DVI_D;
    }

    /* Certain IBM chipset RN50s have a BIOS reporting two VGAs,
       one with VGA DDC and one with CRT2 DDC. - kill the CRT2 DDC one */
    if (info->Chipset == PCI_CHIP_RN50_515E &&
	PCI_SUB_VENDOR_ID(info->PciInfo) == 0x1014) {
	if (info->BiosConnector[index].ConnectorType == CONNECTOR_VGA &&
	    info->BiosConnector[index].ddc_i2c.mask_clk_reg == RADEON_GPIO_CRT2_DDC) {
	    info->BiosConnector[index].valid = FALSE;
	}
    }

    /* Some RV100 cards with 2 VGA ports show up with DVI+VGA */
    if (info->Chipset == PCI_CHIP_RV100_QY &&
	PCI_SUB_VENDOR_ID(info->PciInfo) == 0x1002 &&
	PCI_SUB_DEVICE_ID(info->PciInfo) == 0x013a) {
	if (info->BiosConnector[index].ConnectorType == CONNECTOR_DVI_I) {
	    info->BiosConnector[index].ConnectorType = CONNECTOR_VGA;
	}
    }

    /* X300 card with extra non-existent DVI port */
    if (info->Chipset == PCI_CHIP_RV370_5B60 &&
	PCI_SUB_VENDOR_ID(info->PciInfo) == 0x17af &&
	PCI_SUB_DEVICE_ID(info->PciInfo) == 0x201e &&
	index == 2) {
	if (info->BiosConnector[index].ConnectorType == CONNECTOR_DVI_I) {
	    info->BiosConnector[index].valid = FALSE;
	}
    }

}

static Bool RADEONGetLegacyConnectorInfoFromBIOS (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    int offset, i, entry, tmp, tmp0, tmp1;
    RADEONLegacyDDCType DDCType;
    RADEONLegacyConnectorType ConnectorType;

    if (!info->VBIOS) return FALSE;

    offset = RADEON_BIOS16(info->ROMHeaderStart + 0x50);
    if (offset) {
	for (i = 0; i < 4; i++) {
	    entry = offset + 2 + i*2;

	    if (!RADEON_BIOS16(entry)) {
		break;
	    }
	    info->BiosConnector[i].valid = TRUE;
	    tmp = RADEON_BIOS16(entry);
	    info->BiosConnector[i].ConnectorType = (tmp >> 12) & 0xf;
	    ConnectorType = (tmp >> 12) & 0xf;
	    switch (ConnectorType) {
	    case CONNECTOR_PROPRIETARY_LEGACY:
		info->BiosConnector[i].ConnectorType = CONNECTOR_LVDS;
		break;
	    case CONNECTOR_CRT_LEGACY:
		info->BiosConnector[i].ConnectorType = CONNECTOR_VGA;
		break;
	    case CONNECTOR_DVI_I_LEGACY:
		info->BiosConnector[i].ConnectorType = CONNECTOR_DVI_I;
		break;
	    case CONNECTOR_DVI_D_LEGACY:
		info->BiosConnector[i].ConnectorType = CONNECTOR_DVI_D;
		break;
	    case CONNECTOR_CTV_LEGACY:
		info->BiosConnector[i].ConnectorType = CONNECTOR_CTV;
		break;
	    case CONNECTOR_STV_LEGACY:
		info->BiosConnector[i].ConnectorType = CONNECTOR_STV;
		break;
	    default:
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Unknown Connector Type: %d\n", ConnectorType);
		info->BiosConnector[i].valid = FALSE;
		break;
	    }

	    info->BiosConnector[i].ddc_i2c.valid = FALSE;

	    DDCType = (tmp >> 8) & 0xf;
	    switch (DDCType) {
	    case DDC_MONID:
		info->BiosConnector[i].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_MONID);
		break;
	    case DDC_DVI:
		info->BiosConnector[i].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_DVI_DDC);
		break;
	    case DDC_VGA:
		info->BiosConnector[i].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_VGA_DDC);
		break;
	    case DDC_CRT2:
		info->BiosConnector[i].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_CRT2_DDC);
		break;
	    default:
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Unknown DDC Type: %d\n", DDCType);
		break;
	    }

	    if (tmp & 0x1)
		info->BiosConnector[i].DACType = DAC_TVDAC;
	    else
		info->BiosConnector[i].DACType = DAC_PRIMARY;

	    /* For RS300/RS350/RS400 chips, there is no primary DAC. Force VGA port to use TVDAC*/
	    if ((info->ChipFamily == CHIP_FAMILY_RS300) ||
		(info->ChipFamily == CHIP_FAMILY_RS400) ||
		(info->ChipFamily == CHIP_FAMILY_RS480))
		info->BiosConnector[i].DACType = DAC_TVDAC;

	    if ((tmp >> 4) & 0x1)
		info->BiosConnector[i].TMDSType = TMDS_EXT;
	    else
		info->BiosConnector[i].TMDSType = TMDS_INT;

	    RADEONApplyLegacyQuirks(pScrn, i);

	}
    } else {
	xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "No Connector Info Table found!\n");

	/* old radeons and r128 didn't use connector tables you just check
	 * for LVDS, DVI, TV, etc. tables
	 */
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x34);
	if (offset) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "Found DFP table, assuming DVI connector\n");
	    info->BiosConnector[0].valid = TRUE;
	    info->BiosConnector[0].ConnectorType = CONNECTOR_DVI_I;
	    info->BiosConnector[0].DACType = DAC_PRIMARY;
	    info->BiosConnector[0].TMDSType = TMDS_INT;
	    info->BiosConnector[0].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_DVI_DDC);
	} else
	    return FALSE;
    }

    /* check LVDS table */
    if (info->IsMobility) {
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x40);
	if (offset) {
	    info->BiosConnector[4].valid = TRUE;
	    info->BiosConnector[4].ConnectorType = CONNECTOR_LVDS;
	    info->BiosConnector[4].DACType = DAC_NONE;
	    info->BiosConnector[4].TMDSType = TMDS_NONE;
	    info->BiosConnector[4].ddc_i2c.valid = FALSE;

	    tmp = RADEON_BIOS16(info->ROMHeaderStart + 0x42);
	    if (tmp) {
		tmp0 = RADEON_BIOS16(tmp + 0x15);
		if (tmp0) {
		    tmp1 = RADEON_BIOS8(tmp0+2) & 0x07;
		    if (tmp1) {
			DDCType	= tmp1;
			switch (DDCType) {
			case DDC_MONID:
			    info->BiosConnector[4].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_MONID);
			    break;
			case DDC_DVI:
			    info->BiosConnector[4].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_DVI_DDC);
			    break;
			case DDC_VGA:
			    info->BiosConnector[4].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_VGA_DDC);
			    break;
			case DDC_CRT2:
			    info->BiosConnector[4].ddc_i2c = legacy_setup_i2c_bus(RADEON_GPIO_CRT2_DDC);
			    break;
			case DDC_LCD:
			    info->BiosConnector[4].ddc_i2c = legacy_setup_i2c_bus(RADEON_LCD_GPIO_MASK);
			    info->BiosConnector[4].ddc_i2c.mask_clk_mask =
				RADEON_BIOS32(tmp0 + 0x03) | RADEON_BIOS32(tmp0 + 0x07);
			    info->BiosConnector[4].ddc_i2c.mask_data_mask =
				RADEON_BIOS32(tmp0 + 0x03) | RADEON_BIOS32(tmp0 + 0x07);
			    info->BiosConnector[4].ddc_i2c.put_clk_mask = RADEON_BIOS32(tmp0 + 0x03);
			    info->BiosConnector[4].ddc_i2c.put_data_mask = RADEON_BIOS32(tmp0 + 0x07);
			    info->BiosConnector[4].ddc_i2c.get_clk_mask = RADEON_BIOS32(tmp0 + 0x03);
			    info->BiosConnector[4].ddc_i2c.get_data_mask = RADEON_BIOS32(tmp0 + 0x07);
			    break;
			case DDC_GPIO:
			    info->BiosConnector[4].ddc_i2c = legacy_setup_i2c_bus(RADEON_MDGPIO_EN_REG);
			    info->BiosConnector[4].ddc_i2c.mask_clk_mask =
				RADEON_BIOS32(tmp0 + 0x03) | RADEON_BIOS32(tmp0 + 0x07);
			    info->BiosConnector[4].ddc_i2c.mask_data_mask =
				RADEON_BIOS32(tmp0 + 0x03) | RADEON_BIOS32(tmp0 + 0x07);
			    info->BiosConnector[4].ddc_i2c.put_clk_mask = RADEON_BIOS32(tmp0 + 0x03);
			    info->BiosConnector[4].ddc_i2c.put_data_mask = RADEON_BIOS32(tmp0 + 0x07);
			    info->BiosConnector[4].ddc_i2c.get_clk_mask = RADEON_BIOS32(tmp0 + 0x03);
			    info->BiosConnector[4].ddc_i2c.get_data_mask = RADEON_BIOS32(tmp0 + 0x07);
			    break;
			default:
			    xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Unknown DDC Type: %d\n", DDCType);
			    break;
			}
			xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "LCD DDC Info Table found!\n");
		    }
		}
	    } else {
		info->BiosConnector[4].ddc_i2c.valid = FALSE;
	    }
	}
    }

    /* check TV table */
    if (info->InternalTVOut) {
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x32);
	if (offset) {
	    if (RADEON_BIOS8(offset + 6) == 'T') {
		info->BiosConnector[5].valid = TRUE;
		/* assume s-video for now */
		info->BiosConnector[5].ConnectorType = CONNECTOR_STV;
		info->BiosConnector[5].DACType = DAC_TVDAC;
		info->BiosConnector[5].TMDSType = TMDS_NONE;
		info->BiosConnector[5].ddc_i2c.valid = FALSE;
	    }
	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Bios Connector table: \n");
    for (i = 0; i < RADEON_MAX_BIOS_CONNECTOR; i++) {
	if (info->BiosConnector[i].valid) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Port%d: DDCType-0x%x, DACType-%d, TMDSType-%d, ConnectorType-%d\n",
		       i, (unsigned int)info->BiosConnector[i].ddc_i2c.mask_clk_reg, info->BiosConnector[i].DACType,
		       info->BiosConnector[i].TMDSType, info->BiosConnector[i].ConnectorType);
	}
    }

    return TRUE;
}

Bool RADEONGetConnectorInfoFromBIOS (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);

    if(!info->VBIOS) return FALSE;

    if (info->IsAtomBios)
	return RADEONGetATOMConnectorInfoFromBIOS(pScrn);
    else
	return RADEONGetLegacyConnectorInfoFromBIOS(pScrn);
}

Bool RADEONGetTVInfoFromBIOS (xf86OutputPtr output) {
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    int offset, refclk, stds;

    if (!info->VBIOS) return FALSE;

    if (info->IsAtomBios) {
        return RADEONGetATOMTVInfo(output);
    } else {
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x32);
	if (offset) {
	    if (RADEON_BIOS8(offset + 6) == 'T') {
		switch (RADEON_BIOS8(offset + 7) & 0xf) {
		case 1:
		    radeon_output->default_tvStd = TV_STD_NTSC;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Default TV standard: NTSC\n");
		    break;
		case 2:
		    radeon_output->default_tvStd = TV_STD_PAL;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Default TV standard: PAL\n");
		    break;
		case 3:
		    radeon_output->default_tvStd = TV_STD_PAL_M;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Default TV standard: PAL-M\n");
		    break;
		case 4:
		    radeon_output->default_tvStd = TV_STD_PAL_60;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Default TV standard: PAL-60\n");
		    break;
		case 5:
		    radeon_output->default_tvStd = TV_STD_NTSC_J;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Default TV standard: NTSC-J\n");
		    break;
		case 6:
		    radeon_output->default_tvStd = TV_STD_SCART_PAL;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Default TV standard: SCART-PAL\n");
		    break;
		default:
		    radeon_output->default_tvStd = TV_STD_NTSC;
		    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Unknown TV standard; defaulting to NTSC\n");
		    break;
		}
		radeon_output->tvStd = radeon_output->default_tvStd;

		refclk = (RADEON_BIOS8(offset + 9) >> 2) & 0x3;
		if (refclk == 0)
		    radeon_output->TVRefClk = 29.498928713; /* MHz */
		else if (refclk == 1)
		    radeon_output->TVRefClk = 28.636360000;
		else if (refclk == 2)
		    radeon_output->TVRefClk = 14.318180000;
		else if (refclk == 3)
		    radeon_output->TVRefClk = 27.000000000;

		radeon_output->SupportedTVStds = radeon_output->default_tvStd;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "TV standards supported by chip: ");
		stds = RADEON_BIOS8(offset + 10) & 0x1f;
		if (stds & TV_STD_NTSC) {
		    radeon_output->SupportedTVStds |= TV_STD_NTSC;
		    ErrorF("NTSC ");
		}
		if (stds & TV_STD_PAL) {
		    radeon_output->SupportedTVStds |= TV_STD_PAL;
		    ErrorF("PAL ");
		}
		if (stds & TV_STD_PAL_M) {
		    radeon_output->SupportedTVStds |= TV_STD_PAL_M;
		    ErrorF("PAL-M ");
		}
		if (stds & TV_STD_PAL_60) {
		    radeon_output->SupportedTVStds |= TV_STD_PAL_60;
		    ErrorF("PAL-60 ");
		}
		if (stds & TV_STD_NTSC_J) {
		    radeon_output->SupportedTVStds |= TV_STD_NTSC_J;
		    ErrorF("NTSC-J ");
		}
		if (stds & TV_STD_SCART_PAL) {
		    radeon_output->SupportedTVStds |= TV_STD_SCART_PAL;
		    ErrorF("SCART-PAL");
		}
		ErrorF("\n");

		return TRUE;
	    }
	}
    }
    return FALSE;
}

/* Read PLL parameters from BIOS block.  Default to typical values if there
   is no BIOS. */
Bool RADEONGetClockInfoFromBIOS (ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    RADEONPLLPtr pll = &info->pll;
    uint16_t pll_info_block;

    if (!info->VBIOS) {
	return FALSE;
    } else {
	if (info->IsAtomBios) {
	    pll_info_block = RADEON_BIOS16 (info->MasterDataStart + 12);

	    pll->reference_freq = RADEON_BIOS16 (pll_info_block + 82);
	    pll->reference_div = 0; /* Need to derive from existing setting
					or use a new algorithm to calculate
					from min_input and max_input
				     */
	    pll->pll_out_min = RADEON_BIOS16 (pll_info_block + 78);
	    pll->pll_out_max = RADEON_BIOS32 (pll_info_block + 32);

	    if (pll->pll_out_min == 0) {
		if (IS_AVIVO_VARIANT)
		    pll->pll_out_min = 64800;
		else
		    pll->pll_out_min = 20000;
	    }

	    pll->pll_in_min = RADEON_BIOS16 (pll_info_block + 74);
	    pll->pll_in_max = RADEON_BIOS16 (pll_info_block + 76);

	    pll->xclk = RADEON_BIOS16 (pll_info_block + 72);

	    info->sclk = RADEON_BIOS32(pll_info_block + 8) / 100.0;
	    info->mclk = RADEON_BIOS32(pll_info_block + 12) / 100.0;
	} else {
	    int rev;

	    pll_info_block = RADEON_BIOS16 (info->ROMHeaderStart + 0x30);

	    rev = RADEON_BIOS8(pll_info_block);

	    pll->reference_freq = RADEON_BIOS16 (pll_info_block + 0x0e);
	    pll->reference_div = RADEON_BIOS16 (pll_info_block + 0x10);
	    pll->pll_out_min = RADEON_BIOS32 (pll_info_block + 0x12);
	    pll->pll_out_max = RADEON_BIOS32 (pll_info_block + 0x16);

	    if (rev > 9) {
		pll->pll_in_min = RADEON_BIOS32(pll_info_block + 0x36);
		pll->pll_in_max = RADEON_BIOS32(pll_info_block + 0x3a);
	    } else {
		pll->pll_in_min = 40;
		pll->pll_in_max = 500;
	    }

	    pll->xclk = RADEON_BIOS16(pll_info_block + 0x08);

	    info->sclk = RADEON_BIOS16(pll_info_block + 8) / 100.0;
	    info->mclk = RADEON_BIOS16(pll_info_block + 10) / 100.0;
	}

	if (info->sclk == 0) info->sclk = 200;
	if (info->mclk == 0) info->mclk = 200;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ref_freq: %d, min_out_pll: %u, "
	       "max_out_pll: %u, min_in_pll: %u, max_in_pll: %u, xclk: %d, "
	       "sclk: %f, mclk: %f\n",
	       pll->reference_freq, (unsigned)pll->pll_out_min,
	       (unsigned)pll->pll_out_max, (unsigned)pll->pll_in_min,
	       (unsigned)pll->pll_in_max, pll->xclk, info->sclk, info->mclk);

    return TRUE;
}

Bool RADEONGetDAC2InfoFromBIOS (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    int offset, rev, bg, dac;

    if (!info->VBIOS) return FALSE;

    if (xf86ReturnOptValBool(info->Options, OPTION_DEFAULT_TVDAC_ADJ, FALSE))
	return FALSE;

    if (info->IsAtomBios) {
	/* not implemented yet */
	return FALSE;
    } else {
	/* first check TV table */
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x32);
        if (offset) {
	    rev = RADEON_BIOS8(offset + 0x3);
	    if (rev > 4) {
		bg = RADEON_BIOS8(offset + 0xc) & 0xf;
		dac = RADEON_BIOS8(offset + 0xd) & 0xf;
		radeon_output->ps2_tvdac_adj = (bg << 16) | (dac << 20);

		bg = RADEON_BIOS8(offset + 0xe) & 0xf;
		dac = RADEON_BIOS8(offset + 0xf) & 0xf;
		radeon_output->pal_tvdac_adj = (bg << 16) | (dac << 20);

		bg = RADEON_BIOS8(offset + 0x10) & 0xf;
		dac = RADEON_BIOS8(offset + 0x11) & 0xf;
		radeon_output->ntsc_tvdac_adj = (bg << 16) | (dac << 20);

		return TRUE;
	    } else if (rev > 1) {
		bg = RADEON_BIOS8(offset + 0xc) & 0xf;
		dac = (RADEON_BIOS8(offset + 0xc) >> 4) & 0xf;
		radeon_output->ps2_tvdac_adj = (bg << 16) | (dac << 20);

		bg = RADEON_BIOS8(offset + 0xd) & 0xf;
		dac = (RADEON_BIOS8(offset + 0xd) >> 4) & 0xf;
		radeon_output->pal_tvdac_adj = (bg << 16) | (dac << 20);

		bg = RADEON_BIOS8(offset + 0xe) & 0xf;
		dac = (RADEON_BIOS8(offset + 0xe) >> 4) & 0xf;
		radeon_output->ntsc_tvdac_adj = (bg << 16) | (dac << 20);

		return TRUE;
	    }
	}
	/* then check CRT table */
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x60);
        if (offset) {
	    rev = RADEON_BIOS8(offset) & 0x3;
	    if (rev < 2) {
		bg = RADEON_BIOS8(offset + 0x3) & 0xf;
		dac = (RADEON_BIOS8(offset + 0x3) >> 4) & 0xf;
		radeon_output->ps2_tvdac_adj = (bg << 16) | (dac << 20);
		radeon_output->pal_tvdac_adj = radeon_output->ps2_tvdac_adj;
		radeon_output->ntsc_tvdac_adj = radeon_output->ps2_tvdac_adj;

		return TRUE;
	    } else {
		bg = RADEON_BIOS8(offset + 0x4) & 0xf;
		dac = RADEON_BIOS8(offset + 0x5) & 0xf;
		radeon_output->ps2_tvdac_adj = (bg << 16) | (dac << 20);
		radeon_output->pal_tvdac_adj = radeon_output->ps2_tvdac_adj;
		radeon_output->ntsc_tvdac_adj = radeon_output->ps2_tvdac_adj;

		return TRUE;
	    }
	}
    }

    return FALSE;
}

Bool RADEONGetLVDSInfoFromBIOS (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    unsigned long tmp, i;

    if (!info->VBIOS) return FALSE;

    if (info->IsAtomBios) {
	if((tmp = RADEON_BIOS16 (info->MasterDataStart + 16))) {

	    radeon_output->PanelXRes = RADEON_BIOS16(tmp+6);
	    radeon_output->PanelYRes = RADEON_BIOS16(tmp+10);
	    radeon_output->DotClock   = RADEON_BIOS16(tmp+4)*10;
	    radeon_output->HBlank     = RADEON_BIOS16(tmp+8);
	    radeon_output->HOverPlus  = RADEON_BIOS16(tmp+14);
	    radeon_output->HSyncWidth = RADEON_BIOS16(tmp+16);
	    radeon_output->VBlank     = RADEON_BIOS16(tmp+12);
	    radeon_output->VOverPlus  = RADEON_BIOS16(tmp+18);
	    radeon_output->VSyncWidth = RADEON_BIOS16(tmp+20);
	    radeon_output->PanelPwrDly = RADEON_BIOS16(tmp+40);

	    if (radeon_output->PanelPwrDly > 2000 || radeon_output->PanelPwrDly < 0)
		radeon_output->PanelPwrDly = 2000;

	    radeon_output->Flags = 0;
	} else {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "No LVDS Info Table found in BIOS!\n");
	    return FALSE;
	}
    } else {

	tmp = RADEON_BIOS16(info->ROMHeaderStart + 0x40);

	if (!tmp) {
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		       "No Panel Info Table found in BIOS!\n");
	    return FALSE;
	} else {
	    char  stmp[30];
	    int   tmp0;

	    for (i = 0; i < 24; i++)
	    stmp[i] = RADEON_BIOS8(tmp+i+1);
	    stmp[24] = 0;

	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "Panel ID string: %s\n", stmp);

	    radeon_output->PanelXRes = RADEON_BIOS16(tmp+25);
	    radeon_output->PanelYRes = RADEON_BIOS16(tmp+27);
	    xf86DrvMsg(0, X_INFO, "Panel Size from BIOS: %dx%d\n",
		       radeon_output->PanelXRes, radeon_output->PanelYRes);
	
	    radeon_output->PanelPwrDly = RADEON_BIOS16(tmp+44);
	    if (radeon_output->PanelPwrDly > 2000 || radeon_output->PanelPwrDly < 0)
		radeon_output->PanelPwrDly = 2000;

	    /* some panels only work well with certain divider combinations.
	     */
	    info->RefDivider = RADEON_BIOS16(tmp+46);
	    info->PostDivider = RADEON_BIOS8(tmp+48);
	    info->FeedbackDivider = RADEON_BIOS16(tmp+49);
	    if ((info->RefDivider != 0) &&
		(info->FeedbackDivider > 3)) {
		info->UseBiosDividers = TRUE;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "BIOS provided dividers will be used.\n");
	    }

	    /* We don't use a while loop here just in case we have a corrupted BIOS image.
	       The max number of table entries is 23 at present, but may grow in future.
	       To ensure it works with future revisions we loop it to 32.
	    */
	    for (i = 0; i < 32; i++) {
		tmp0 = RADEON_BIOS16(tmp+64+i*2);
		if (tmp0 == 0) break;
		if ((RADEON_BIOS16(tmp0) == radeon_output->PanelXRes) &&
		    (RADEON_BIOS16(tmp0+2) == radeon_output->PanelYRes)) {
		    radeon_output->HBlank     = (RADEON_BIOS16(tmp0+17) -
					RADEON_BIOS16(tmp0+19)) * 8;
		    radeon_output->HOverPlus  = (RADEON_BIOS16(tmp0+21) -
					RADEON_BIOS16(tmp0+19) - 1) * 8;
		    radeon_output->HSyncWidth = RADEON_BIOS8(tmp0+23) * 8;
		    radeon_output->VBlank     = (RADEON_BIOS16(tmp0+24) -
					RADEON_BIOS16(tmp0+26));
		    radeon_output->VOverPlus  = ((RADEON_BIOS16(tmp0+28) & 0x7ff) -
					RADEON_BIOS16(tmp0+26));
		    radeon_output->VSyncWidth = ((RADEON_BIOS16(tmp0+28) & 0xf800) >> 11);
		    radeon_output->DotClock   = RADEON_BIOS16(tmp0+9) * 10;
		    radeon_output->Flags = 0;
		}
	    }
	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	       "LVDS Info:\n"
	       "XRes: %d, YRes: %d, DotClock: %d\n"
	       "HBlank: %d, HOverPlus: %d, HSyncWidth: %d\n"
	       "VBlank: %d, VOverPlus: %d, VSyncWidth: %d\n",
	       radeon_output->PanelXRes, radeon_output->PanelYRes, radeon_output->DotClock,
	       radeon_output->HBlank, radeon_output->HOverPlus, radeon_output->HSyncWidth,
	       radeon_output->VBlank, radeon_output->VOverPlus, radeon_output->VSyncWidth);

    return TRUE;
}

Bool RADEONGetHardCodedEDIDFromBIOS (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    unsigned long tmp;
    char EDID[256];

    if (!info->VBIOS) return FALSE;

    if (info->IsAtomBios) {
	/* Not yet */
	return FALSE;
    } else {
	if (!(tmp = RADEON_BIOS16(info->ROMHeaderStart + 0x4c))) {
	    return FALSE;
	}

	memcpy(EDID, (char*)(info->VBIOS + tmp), 256);

	radeon_output->DotClock = (*(uint16_t*)(EDID+54)) * 10;
	radeon_output->PanelXRes = (*(uint8_t*)(EDID+56)) + ((*(uint8_t*)(EDID+58))>>4)*256;
	radeon_output->HBlank = (*(uint8_t*)(EDID+57)) + ((*(uint8_t*)(EDID+58)) & 0xf)*256;
	radeon_output->HOverPlus = (*(uint8_t*)(EDID+62)) + ((*(uint8_t*)(EDID+65)>>6)*256);
	radeon_output->HSyncWidth = (*(uint8_t*)(EDID+63)) + (((*(uint8_t*)(EDID+65)>>4) & 3)*256);
	radeon_output->PanelYRes = (*(uint8_t*)(EDID+59)) + ((*(uint8_t*)(EDID+61))>>4)*256;
	radeon_output->VBlank = ((*(uint8_t*)(EDID+60)) + ((*(uint8_t*)(EDID+61)) & 0xf)*256);
	radeon_output->VOverPlus = (((*(uint8_t*)(EDID+64))>>4) + (((*(uint8_t*)(EDID+65)>>2) & 3)*16));
	radeon_output->VSyncWidth = (((*(uint8_t*)(EDID+64)) & 0xf) + ((*(uint8_t*)(EDID+65)) & 3)*256);
	radeon_output->Flags      = V_NHSYNC | V_NVSYNC; /**(uint8_t*)(EDID+71);*/
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Hardcoded EDID data will be used for TMDS panel\n");
    }
    return TRUE;
}

Bool RADEONGetTMDSInfoFromBIOS (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr  info       = RADEONPTR(pScrn);
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    uint32_t tmp, maxfreq;
    int i, n;

    if (!info->VBIOS) return FALSE;

    if (info->IsAtomBios) {
	if((tmp = RADEON_BIOS16 (info->MasterDataStart + 18))) {

	    maxfreq = RADEON_BIOS16(tmp+4);
	    
	    for (i=0; i<4; i++) {
		radeon_output->tmds_pll[i].freq = RADEON_BIOS16(tmp+i*6+6);
		/* This assumes each field in TMDS_PLL has 6 bit as in R300/R420 */
		radeon_output->tmds_pll[i].value = ((RADEON_BIOS8(tmp+i*6+8) & 0x3f) |
					   ((RADEON_BIOS8(tmp+i*6+10) & 0x3f)<<6) |
					   ((RADEON_BIOS8(tmp+i*6+9) & 0xf)<<12) |
					   ((RADEON_BIOS8(tmp+i*6+11) & 0xf)<<16));
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, 
			   "TMDS PLL from BIOS: %u %x\n", 
			   (unsigned)radeon_output->tmds_pll[i].freq,
			   (unsigned)radeon_output->tmds_pll[i].value);
		       
		if (maxfreq == radeon_output->tmds_pll[i].freq) {
		    radeon_output->tmds_pll[i].freq = 0xffffffff;
		    break;
		}
	    }
	    return TRUE;
	}
    } else {

	tmp = RADEON_BIOS16(info->ROMHeaderStart + 0x34);
	if (tmp) {
	    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		       "DFP table revision: %d\n", RADEON_BIOS8(tmp));
	    if (RADEON_BIOS8(tmp) == 3) {
		n = RADEON_BIOS8(tmp + 5) + 1;
		if (n > 4) n = 4;
		for (i=0; i<n; i++) {
		    radeon_output->tmds_pll[i].value = RADEON_BIOS32(tmp+i*10+0x08);
		    radeon_output->tmds_pll[i].freq = RADEON_BIOS16(tmp+i*10+0x10);
		}
		return TRUE;
	    } else if (RADEON_BIOS8(tmp) == 4) {
	        int stride = 0;
		n = RADEON_BIOS8(tmp + 5) + 1;
		if (n > 4) n = 4;
		for (i=0; i<n; i++) {
		    radeon_output->tmds_pll[i].value = RADEON_BIOS32(tmp+stride+0x08);
		    radeon_output->tmds_pll[i].freq = RADEON_BIOS16(tmp+stride+0x10);
		    if (i == 0) stride += 10;
		    else stride += 6;
		}
		return TRUE;
	    }
	}
    }
    return FALSE;
}

Bool RADEONGetExtTMDSInfoFromBIOS (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    int offset, table_start, max_freq, gpio_reg, flags;

    if (!info->VBIOS) return FALSE;

    if (info->IsAtomBios) {
	return FALSE;
    } else {
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x58);
	if (offset) {
	     xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			"External TMDS Table revision: %d\n",
			RADEON_BIOS8(offset));
	    table_start = offset+4;
	    max_freq = RADEON_BIOS16(table_start);
	    radeon_output->dvo_i2c_slave_addr = RADEON_BIOS8(table_start+2);
	    radeon_output->dvo_i2c.valid = FALSE;
	    gpio_reg = RADEON_BIOS8(table_start+3);
	    if (gpio_reg == 1)
		radeon_output->dvo_i2c = legacy_setup_i2c_bus(RADEON_GPIO_MONID);
	    else if (gpio_reg == 2)
		radeon_output->dvo_i2c = legacy_setup_i2c_bus(RADEON_GPIO_DVI_DDC);
	    else if (gpio_reg == 3)
		radeon_output->dvo_i2c = legacy_setup_i2c_bus(RADEON_GPIO_VGA_DDC);
	    else if (gpio_reg == 4)
		radeon_output->dvo_i2c = legacy_setup_i2c_bus(RADEON_GPIO_CRT2_DDC);
	    else if (gpio_reg == 5)
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "unsupported MM gpio_reg\n");
	    else {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Unknown gpio reg: %d\n", gpio_reg);
		return FALSE;
	    }
	    flags = RADEON_BIOS8(table_start+5);
	    radeon_output->dvo_duallink = flags & 0x01;
	    if (radeon_output->dvo_duallink) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "Duallink TMDS detected\n");
	    }
	    return TRUE;
	}
    }

    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
	       "No External TMDS Table found\n");

    return FALSE;
}

Bool RADEONInitExtTMDSInfoFromBIOS (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    RADEONOutputPrivatePtr radeon_output = output->driver_private;
    int offset, index, id;
    uint32_t val, reg, andmask, ormask;

    if (!info->VBIOS) return FALSE;

    if (info->IsAtomBios) {
	return FALSE;
    } else {
	offset = RADEON_BIOS16(info->ROMHeaderStart + 0x58);
	if (offset) {
	    index = offset+10;
	    id = RADEON_BIOS16(index);
	    while (id != 0xffff) {
		index += 2;
		switch(id >> 13) {
		case 0:
		    reg = id & 0x1fff;
		    val = RADEON_BIOS32(index);
		    index += 4;
		    ErrorF("WRITE INDEXED: 0x%x 0x%x\n",
			   (unsigned)reg, (unsigned)val);
		    /*OUTREG(reg, val);*/
		    break;
		case 2:
		    reg = id & 0x1fff;
		    andmask = RADEON_BIOS32(index);
		    index += 4;
		    ormask = RADEON_BIOS32(index);
		    index += 4;
		    val = INREG(reg);
		    val = (val & andmask) | ormask;
		    ErrorF("MASK DIRECT: 0x%x 0x%x 0x%x\n",
			   (unsigned)reg, (unsigned)andmask, (unsigned)ormask);
		    /*OUTREG(reg, val);*/
		    break;
		case 4:
		    val = RADEON_BIOS16(index);
		    index += 2;
		    ErrorF("delay: %u\n", (unsigned)val);
		    usleep(val);
		    break;
		case 5:
		    reg = id & 0x1fff;
		    andmask = RADEON_BIOS32(index);
		    index += 4;
		    ormask = RADEON_BIOS32(index);
		    index += 4;
		    ErrorF("MASK PLL: 0x%x 0x%x 0x%x\n",
			   (unsigned)reg, (unsigned)andmask, (unsigned)ormask);
		    /*val = INPLL(pScrn, reg);
		    val = (val & andmask) | ormask;
		    OUTPLL(pScrn, reg, val);*/
		    break;
		case 6:
		    reg = id & 0x1fff;
		    val = RADEON_BIOS8(index);
		    index += 1;
		    ErrorF("i2c write: 0x%x, 0x%x\n", (unsigned)reg,
			   (unsigned)val);
		    RADEONDVOWriteByte(radeon_output->DVOChip, reg, val);
		    break;
		default:
		    ErrorF("unknown id %d\n", id>>13);
		    return FALSE;
		};
		id = RADEON_BIOS16(index);
	    }
	    return TRUE;
	}
    }

    return FALSE;
}

/* support for init from bios tables
 *
 * Based heavily on the netbsd radeonfb driver
 * Written by Garrett D'Amore
 * Copyright (c) 2006 Itronix Inc.
 *
 */

/* bios table defines */

#define RADEON_TABLE_ENTRY_FLAG_MASK    0xe000
#define RADEON_TABLE_ENTRY_INDEX_MASK   0x1fff
#define RADEON_TABLE_ENTRY_COMMAND_MASK 0x00ff

#define RADEON_TABLE_FLAG_WRITE_INDEXED 0x0000
#define RADEON_TABLE_FLAG_WRITE_DIRECT  0x2000
#define RADEON_TABLE_FLAG_MASK_INDEXED  0x4000
#define RADEON_TABLE_FLAG_MASK_DIRECT   0x6000
#define RADEON_TABLE_FLAG_DELAY         0x8000
#define RADEON_TABLE_FLAG_SCOMMAND      0xa000

#define RADEON_TABLE_SCOMMAND_WAIT_MC_BUSY_MASK       0x03
#define RADEON_TABLE_SCOMMAND_WAIT_MEM_PWRUP_COMPLETE 0x08

#define RADEON_PLL_FLAG_MASK      0xc0
#define RADEON_PLL_INDEX_MASK     0x3f

#define RADEON_PLL_FLAG_WRITE     0x00
#define RADEON_PLL_FLAG_MASK_BYTE 0x40
#define RADEON_PLL_FLAG_WAIT      0x80

#define RADEON_PLL_WAIT_150MKS                    1
#define RADEON_PLL_WAIT_5MS                       2
#define RADEON_PLL_WAIT_MC_BUSY_MASK              3
#define RADEON_PLL_WAIT_DLL_READY_MASK            4
#define RADEON_PLL_WAIT_CHK_SET_CLK_PWRMGT_CNTL24 5

static uint16_t
RADEONValidateBIOSOffset(ScrnInfoPtr pScrn, uint16_t offset)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    uint8_t revision = RADEON_BIOS8(offset - 1);

    if (revision > 0x10) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Bad revision %d for BIOS table\n", revision);
        return 0;
    }

    if (offset < 0x60) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Bad offset 0x%x for BIOS Table\n", offset);
        return 0;
    }

    return offset;
}

Bool
RADEONGetBIOSInitTableOffsets(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    uint8_t val;

    if (!info->VBIOS) {
	return FALSE;
    } else {
	if (info->IsAtomBios) {
	    return FALSE;
	} else {
	    info->BiosTable.revision = RADEON_BIOS8(info->ROMHeaderStart + 4);
	    info->BiosTable.rr1_offset = RADEON_BIOS16(info->ROMHeaderStart + 0x0c);
	    if (info->BiosTable.rr1_offset) {
		info->BiosTable.rr1_offset =
		    RADEONValidateBIOSOffset(pScrn, info->BiosTable.rr1_offset);
	    }
	    if (info->BiosTable.revision > 0x09)
		return TRUE;
	    info->BiosTable.rr2_offset = RADEON_BIOS16(info->ROMHeaderStart + 0x4e);
	    if (info->BiosTable.rr2_offset) {
		info->BiosTable.rr2_offset =
		    RADEONValidateBIOSOffset(pScrn, info->BiosTable.rr2_offset);
	    }
	    info->BiosTable.dyn_clk_offset = RADEON_BIOS16(info->ROMHeaderStart + 0x52);
	    if (info->BiosTable.dyn_clk_offset) {
		info->BiosTable.dyn_clk_offset =
		    RADEONValidateBIOSOffset(pScrn, info->BiosTable.dyn_clk_offset);
	    }
	    info->BiosTable.pll_offset = RADEON_BIOS16(info->ROMHeaderStart + 0x46);
	    if (info->BiosTable.pll_offset) {
		info->BiosTable.pll_offset =
		    RADEONValidateBIOSOffset(pScrn, info->BiosTable.pll_offset);
	    }
	    info->BiosTable.mem_config_offset = RADEON_BIOS16(info->ROMHeaderStart + 0x48);
	    if (info->BiosTable.mem_config_offset) {
		info->BiosTable.mem_config_offset =
		    RADEONValidateBIOSOffset(pScrn, info->BiosTable.mem_config_offset);
	    }
	    if (info->BiosTable.mem_config_offset) {
		info->BiosTable.mem_reset_offset = info->BiosTable.mem_config_offset;
		if (info->BiosTable.mem_reset_offset) {
		    while (RADEON_BIOS8(info->BiosTable.mem_reset_offset))
			info->BiosTable.mem_reset_offset++;
		    info->BiosTable.mem_reset_offset++;
		    info->BiosTable.mem_reset_offset += 2;
		}
	    }
	    if (info->BiosTable.mem_config_offset) {
		info->BiosTable.short_mem_offset = info->BiosTable.mem_config_offset;
		if ((info->BiosTable.short_mem_offset != 0) &&
		    (RADEON_BIOS8(info->BiosTable.short_mem_offset - 2) <= 64))
		    info->BiosTable.short_mem_offset +=
			RADEON_BIOS8(info->BiosTable.short_mem_offset - 3);
	    }
	    if (info->BiosTable.rr2_offset) {
		info->BiosTable.rr3_offset = info->BiosTable.rr2_offset;
		if (info->BiosTable.rr3_offset) {
		    while ((val = RADEON_BIOS8(info->BiosTable.rr3_offset + 1)) != 0) {
			if (val & 0x40)
			    info->BiosTable.rr3_offset += 10;
			else if (val & 0x80)
			    info->BiosTable.rr3_offset += 4;
			else
			    info->BiosTable.rr3_offset += 6;
		    }
		    info->BiosTable.rr3_offset += 2;
		}
	    }

	    if (info->BiosTable.rr3_offset) {
		info->BiosTable.rr4_offset = info->BiosTable.rr3_offset;
		if (info->BiosTable.rr4_offset) {
		    while ((val = RADEON_BIOS8(info->BiosTable.rr4_offset + 1)) != 0) {
			if (val & 0x40)
			    info->BiosTable.rr4_offset += 10;
			else if (val & 0x80)
			    info->BiosTable.rr4_offset += 4;
			else
			    info->BiosTable.rr4_offset += 6;
		    }
		    info->BiosTable.rr4_offset += 2;
		}
	    }

	    if (info->BiosTable.rr3_offset + 1 == info->BiosTable.pll_offset) {
		info->BiosTable.rr3_offset = 0;
		info->BiosTable.rr4_offset = 0;
	    }

	    return TRUE;

	}
    }
}

static void
RADEONRestoreBIOSRegBlock(ScrnInfoPtr pScrn, uint16_t table_offset)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    uint16_t offset = table_offset;
    uint16_t value, flag, index, count;
    uint32_t andmask, ormask, val, channel_complete_mask;
    uint8_t  command;

    if (offset == 0)
	return;

    while ((value = RADEON_BIOS16(offset)) != 0) {
	flag = value & RADEON_TABLE_ENTRY_FLAG_MASK;
	index = value & RADEON_TABLE_ENTRY_INDEX_MASK;
	command = value & RADEON_TABLE_ENTRY_COMMAND_MASK;

	offset += 2;

	switch (flag) {
	case RADEON_TABLE_FLAG_WRITE_INDEXED:
	    val = RADEON_BIOS32(offset);
	    ErrorF("WRITE INDEXED: 0x%x 0x%x\n",
		   index, (unsigned)val);
	    OUTREG(RADEON_MM_INDEX, index);
	    OUTREG(RADEON_MM_DATA, val);
	    offset += 4;
	    break;

	case RADEON_TABLE_FLAG_WRITE_DIRECT:
	    val = RADEON_BIOS32(offset);
	    ErrorF("WRITE DIRECT: 0x%x 0x%x\n", index, (unsigned)val);
	    OUTREG(index, val);
	    offset += 4;
	    break;

	case RADEON_TABLE_FLAG_MASK_INDEXED:
	    andmask = RADEON_BIOS32(offset);
	    offset += 4;
	    ormask = RADEON_BIOS32(offset);
	    offset += 4;
	    ErrorF("MASK INDEXED: 0x%x 0x%x 0x%x\n",
		   index, (unsigned)andmask, (unsigned)ormask);
	    OUTREG(RADEON_MM_INDEX, index);
	    val = INREG(RADEON_MM_DATA);
	    val = (val & andmask) | ormask;
	    OUTREG(RADEON_MM_DATA, val);
	    break;

	case RADEON_TABLE_FLAG_MASK_DIRECT:
	    andmask = RADEON_BIOS32(offset);
	    offset += 4;
	    ormask = RADEON_BIOS32(offset);
	    offset += 4;
	    ErrorF("MASK DIRECT: 0x%x 0x%x 0x%x\n",
		   index, (unsigned)andmask, (unsigned)ormask);
	    val = INREG(index);
	    val = (val & andmask) | ormask;
	    OUTREG(index, val);
	    break;

	case RADEON_TABLE_FLAG_DELAY:
	    count = RADEON_BIOS16(offset);
	    ErrorF("delay: %d\n", count);
	    usleep(count);
	    offset += 2;
	    break;

	case RADEON_TABLE_FLAG_SCOMMAND:
	    ErrorF("SCOMMAND 0x%x\n", command); 
	    switch (command) {
	    case RADEON_TABLE_SCOMMAND_WAIT_MC_BUSY_MASK:
		count = RADEON_BIOS16(offset);
		ErrorF("SCOMMAND_WAIT_MC_BUSY_MASK %d\n", count);
		while (count--) {
		    if (!(INPLL(pScrn, RADEON_CLK_PWRMGT_CNTL) &
			  RADEON_MC_BUSY))
			break;
		}
		break;

	    case RADEON_TABLE_SCOMMAND_WAIT_MEM_PWRUP_COMPLETE:
		count = RADEON_BIOS16(offset);
		ErrorF("SCOMMAND_WAIT_MEM_PWRUP_COMPLETE %d\n", count);
		/* may need to take into account how many memory channels
		 * each card has
		 */
		if (IS_R300_VARIANT)
		    channel_complete_mask = R300_MEM_PWRUP_COMPLETE;
		else
		    channel_complete_mask = RADEON_MEM_PWRUP_COMPLETE;
		while (count--) {
		    /* XXX: may need indexed access */
		    if ((INREG(RADEON_MEM_STR_CNTL) &
			 channel_complete_mask) ==
		        channel_complete_mask)
			break;
		}
		break;

	    }
	    offset += 2;
	    break;
	}
    }
}

static void
RADEONRestoreBIOSMemBlock(ScrnInfoPtr pScrn, uint16_t table_offset)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    unsigned char *RADEONMMIO = info->MMIO;
    uint16_t offset = table_offset;
    uint16_t count;
    uint32_t ormask, val, channel_complete_mask;
    uint8_t  index;

    if (offset == 0)
	return;

    while ((index = RADEON_BIOS8(offset)) != 0xff) {
	offset++;
	if (index == 0x0f) {
	    count = 20000;
	    ErrorF("MEM_WAIT_MEM_PWRUP_COMPLETE %d\n", count);
	    /* may need to take into account how many memory channels
	     * each card has
	     */
	    if (IS_R300_VARIANT)
		channel_complete_mask = R300_MEM_PWRUP_COMPLETE;
	    else
		channel_complete_mask = RADEON_MEM_PWRUP_COMPLETE;
	    while (count--) {
		/* XXX: may need indexed access */
		if ((INREG(RADEON_MEM_STR_CNTL) &
		     channel_complete_mask) ==
		    channel_complete_mask)
		    break;
	    }
	} else {
	    ormask = RADEON_BIOS16(offset);
	    offset += 2;

	    ErrorF("INDEX RADEON_MEM_SDRAM_MODE_REG %x %x\n",
		   RADEON_SDRAM_MODE_MASK, (unsigned)ormask);

	    /* can this use direct access? */
	    OUTREG(RADEON_MM_INDEX, RADEON_MEM_SDRAM_MODE_REG);
	    val = INREG(RADEON_MM_DATA);
	    val = (val & RADEON_SDRAM_MODE_MASK) | ormask;
	    OUTREG(RADEON_MM_DATA, val);

	    ormask = (uint32_t)index << 24;

	    ErrorF("INDEX RADEON_MEM_SDRAM_MODE_REG %x %x\n",
		   RADEON_B3MEM_RESET_MASK, (unsigned)ormask);

            /* can this use direct access? */
            OUTREG(RADEON_MM_INDEX, RADEON_MEM_SDRAM_MODE_REG);
            val = INREG(RADEON_MM_DATA);
            val = (val & RADEON_B3MEM_RESET_MASK) | ormask;
            OUTREG(RADEON_MM_DATA, val);
	}
    }
}

static void
RADEONRestoreBIOSPllBlock(ScrnInfoPtr pScrn, uint16_t table_offset)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);
    uint16_t offset = table_offset;
    uint8_t  index, shift;
    uint32_t andmask, ormask, val, clk_pwrmgt_cntl;
    uint16_t count;

    if (offset == 0)
	return;

    while ((index = RADEON_BIOS8(offset)) != 0) {
	offset++;

	switch (index & RADEON_PLL_FLAG_MASK) {
	case RADEON_PLL_FLAG_WAIT:
	    switch (index & RADEON_PLL_INDEX_MASK) {
	    case RADEON_PLL_WAIT_150MKS:
		ErrorF("delay: 150 us\n");
		usleep(150);
		break;
	    case RADEON_PLL_WAIT_5MS:
		ErrorF("delay: 5 ms\n");
		usleep(5000);
		break;

	    case RADEON_PLL_WAIT_MC_BUSY_MASK:
		count = 1000;
		ErrorF("PLL_WAIT_MC_BUSY_MASK %d\n", count);
		while (count--) {
		    if (!(INPLL(pScrn, RADEON_CLK_PWRMGT_CNTL) &
			  RADEON_MC_BUSY))
			break;
		}
		break;

	    case RADEON_PLL_WAIT_DLL_READY_MASK:
		count = 1000;
		ErrorF("PLL_WAIT_DLL_READY_MASK %d\n", count);
		while (count--) {
		    if (INPLL(pScrn, RADEON_CLK_PWRMGT_CNTL) &
			RADEON_DLL_READY)
			break;
		}
		break;

	    case RADEON_PLL_WAIT_CHK_SET_CLK_PWRMGT_CNTL24:
		ErrorF("PLL_WAIT_CHK_SET_CLK_PWRMGT_CNTL24\n");
		clk_pwrmgt_cntl = INPLL(pScrn, RADEON_CLK_PWRMGT_CNTL);
		if (clk_pwrmgt_cntl & RADEON_CG_NO1_DEBUG_0) {
		    val = INPLL(pScrn, RADEON_MCLK_CNTL);
		    /* is this right? */
		    val = (val & 0xFFFF0000) | 0x1111; /* seems like we should clear these... */
		    OUTPLL(pScrn, RADEON_MCLK_CNTL, val);
		    usleep(10000);
		    OUTPLL(pScrn, RADEON_CLK_PWRMGT_CNTL,
			   clk_pwrmgt_cntl & ~RADEON_CG_NO1_DEBUG_0);
		    usleep(10000);
		}
		break;
	    }
	    break;
	    
	case RADEON_PLL_FLAG_MASK_BYTE:
	    shift = RADEON_BIOS8(offset) * 8;
	    offset++;

	    andmask =
		(((uint32_t)RADEON_BIOS8(offset)) << shift) |
		~((uint32_t)0xff << shift);
	    offset++;

	    ormask = ((uint32_t)RADEON_BIOS8(offset)) << shift;
	    offset++;

	    ErrorF("PLL_MASK_BYTE 0x%x 0x%x 0x%x 0x%x\n", 
		   index, shift, (unsigned)andmask, (unsigned)ormask);
	    val = INPLL(pScrn, index);
	    val = (val & andmask) | ormask;
	    OUTPLL(pScrn, index, val);
	    break;

	case RADEON_PLL_FLAG_WRITE:
	    val = RADEON_BIOS32(offset);
	    ErrorF("PLL_WRITE 0x%x 0x%x\n", index, (unsigned)val);
	    OUTPLL(pScrn, index, val);
	    offset += 4;
	    break;
	}
    }
}

Bool
RADEONPostCardFromBIOSTables(ScrnInfoPtr pScrn)
{
    RADEONInfoPtr info = RADEONPTR (pScrn);

    if (!info->VBIOS) {
	return FALSE;
    } else {
	if (info->IsAtomBios) {
	    return FALSE;
	} else {
	    if (info->BiosTable.rr1_offset) {
		ErrorF("rr1 restore, 0x%x\n", info->BiosTable.rr1_offset);
		RADEONRestoreBIOSRegBlock(pScrn, info->BiosTable.rr1_offset);
	    }
	    if (info->BiosTable.revision < 0x09) {
		if (info->BiosTable.pll_offset) {
		    ErrorF("pll restore, 0x%x\n", info->BiosTable.pll_offset);
		    RADEONRestoreBIOSPllBlock(pScrn, info->BiosTable.pll_offset);
		}
		if (info->BiosTable.rr2_offset) {
		    ErrorF("rr2 restore, 0x%x\n", info->BiosTable.rr2_offset);
		    RADEONRestoreBIOSRegBlock(pScrn, info->BiosTable.rr2_offset);
		}
		if (info->BiosTable.rr4_offset) {
		    ErrorF("rr4 restore, 0x%x\n", info->BiosTable.rr4_offset);
		    RADEONRestoreBIOSRegBlock(pScrn, info->BiosTable.rr4_offset);
		}
		if (info->BiosTable.mem_reset_offset) {
		    ErrorF("mem reset restore, 0x%x\n", info->BiosTable.mem_reset_offset);
		    RADEONRestoreBIOSMemBlock(pScrn, info->BiosTable.mem_reset_offset);
		}
		if (info->BiosTable.rr3_offset) {
		    ErrorF("rr3 restore, 0x%x\n", info->BiosTable.rr3_offset);
		    RADEONRestoreBIOSRegBlock(pScrn, info->BiosTable.rr3_offset);
		}
		if (info->BiosTable.dyn_clk_offset) {
		    ErrorF("dyn_clk restore, 0x%x\n", info->BiosTable.dyn_clk_offset);
		    RADEONRestoreBIOSPllBlock(pScrn, info->BiosTable.dyn_clk_offset);
		}
	    }
	}
    }
    return TRUE;
}
