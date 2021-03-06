/*
 * Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                VA Linux Systems Inc., Fremont, California.
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

#define RADEONCTRACE(x)
/*#define RADEONCTRACE(x) RADEONTRACE(x) */

/*
 * Authors:
 *   Kevin E. Martin <martin@xfree86.org>
 *   Rickard E. Faith <faith@valinux.com>
 *
 * References:
 *
 * !!!! FIXME !!!!
 *   RAGE 128 VR/ RAGE 128 GL Register Reference Manual (Technical
 *   Reference Manual P/N RRG-G04100-C Rev. 0.04), ATI Technologies: April
 *   1999.
 *
 *   RAGE 128 Software Development Manual (Technical Reference Manual P/N
 *   SDK-G04000 Rev. 0.01), ATI Technologies: June 1999.
 *
 */

				/* Driver data structures */
#include "radeon.h"
#include "radeon_version.h"
#include "radeon_reg.h"
#include "radeon_macros.h"

				/* X and server generic header files */
#include "xf86.h"

#define CURSOR_WIDTH	64
#define CURSOR_HEIGHT	64

/*
 * The cursor bits are always 32bpp.  On MSBFirst buses,
 * configure byte swapping to swap 32 bit units when writing
 * the cursor image.  Byte swapping must always be returned
 * to its previous value before returning.
 */
#if X_BYTE_ORDER == X_BIG_ENDIAN

#define CURSOR_SWAPPING_DECL_MMIO   unsigned char *RADEONMMIO = info->MMIO;
#define CURSOR_SWAPPING_START() \
  do { \
    OUTREG(RADEON_SURFACE_CNTL, \
	   (info->ModeReg->surface_cntl | \
	     RADEON_NONSURF_AP0_SWP_32BPP | RADEON_NONSURF_AP1_SWP_32BPP) & \
	   ~(RADEON_NONSURF_AP0_SWP_16BPP | RADEON_NONSURF_AP1_SWP_16BPP)); \
  } while (0)
#define CURSOR_SWAPPING_END()	(OUTREG(RADEON_SURFACE_CNTL, \
					info->ModeReg->surface_cntl))

#else

#define CURSOR_SWAPPING_DECL_MMIO
#define CURSOR_SWAPPING_START()
#define CURSOR_SWAPPING_END()

#endif

static void
avivo_setup_cursor(xf86CrtcPtr crtc, Bool enable)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr  info = RADEONPTR(crtc->scrn);
    unsigned char     *RADEONMMIO = info->MMIO;

    OUTREG(AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset, 0);

    if (enable) {
	OUTREG(AVIVO_D1CUR_SURFACE_ADDRESS + radeon_crtc->crtc_offset,
	       info->fbLocation + radeon_crtc->cursor_offset + pScrn->fbOffset);
	OUTREG(AVIVO_D1CUR_SIZE + radeon_crtc->crtc_offset,
	       ((CURSOR_WIDTH - 1) << 16) | (CURSOR_HEIGHT - 1));
	OUTREG(AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset,
	       AVIVO_D1CURSOR_EN | (AVIVO_D1CURSOR_MODE_24BPP << AVIVO_D1CURSOR_MODE_SHIFT));
    }
}

static void
avivo_lock_cursor(xf86CrtcPtr crtc, Bool lock)
{
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr  info = RADEONPTR(crtc->scrn);
    unsigned char     *RADEONMMIO = info->MMIO;
    uint32_t tmp;

    tmp = INREG(AVIVO_D1CUR_UPDATE + radeon_crtc->crtc_offset);

    if (lock)
	tmp |= AVIVO_D1CURSOR_UPDATE_LOCK;
    else
	tmp &= ~AVIVO_D1CURSOR_UPDATE_LOCK;

    OUTREG(AVIVO_D1CUR_UPDATE + radeon_crtc->crtc_offset, tmp);
}

void
radeon_crtc_show_cursor (xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    int crtc_id = radeon_crtc->crtc_id;
    RADEONInfoPtr      info       = RADEONPTR(pScrn);
    unsigned char     *RADEONMMIO = info->MMIO;

    if (IS_AVIVO_VARIANT) {
	avivo_lock_cursor(crtc, TRUE);
	OUTREG(AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset,
	       INREG(AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset)
	       | AVIVO_D1CURSOR_EN);
	avivo_setup_cursor(crtc, TRUE);
	avivo_lock_cursor(crtc, FALSE);
    } else {
        switch (crtc_id) {
        case 0:
            OUTREG(RADEON_MM_INDEX, RADEON_CRTC_GEN_CNTL);
	    break;
        case 1:
            OUTREG(RADEON_MM_INDEX, RADEON_CRTC2_GEN_CNTL);
	    break;
        default:
            return;
        }

        OUTREGP(RADEON_MM_DATA, RADEON_CRTC_CUR_EN | 2 << 20, 
                ~(RADEON_CRTC_CUR_EN | RADEON_CRTC_CUR_MODE_MASK));
    }
}

void
radeon_crtc_hide_cursor (xf86CrtcPtr crtc)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    int crtc_id = radeon_crtc->crtc_id;
    RADEONInfoPtr      info       = RADEONPTR(pScrn);
    unsigned char     *RADEONMMIO = info->MMIO;

    if (IS_AVIVO_VARIANT) {
	avivo_lock_cursor(crtc, TRUE);
	OUTREG(AVIVO_D1CUR_CONTROL+ radeon_crtc->crtc_offset,
	       INREG(AVIVO_D1CUR_CONTROL + radeon_crtc->crtc_offset)
	       & ~(AVIVO_D1CURSOR_EN));
	avivo_setup_cursor(crtc, FALSE);
	avivo_lock_cursor(crtc, FALSE);
    } else {
	switch(crtc_id) {
    	case 0:
            OUTREG(RADEON_MM_INDEX, RADEON_CRTC_GEN_CNTL);
            break;
    	case 1:
	    OUTREG(RADEON_MM_INDEX, RADEON_CRTC2_GEN_CNTL);
	    break;
        default:
	    return;
        }

        OUTREGP(RADEON_MM_DATA, 0, ~RADEON_CRTC_CUR_EN);
   }
}

void
radeon_crtc_set_cursor_position (xf86CrtcPtr crtc, int x, int y)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    int crtc_id = radeon_crtc->crtc_id;
    RADEONInfoPtr      info       = RADEONPTR(pScrn);
    unsigned char     *RADEONMMIO = info->MMIO;
    int xorigin = 0, yorigin = 0;
    int stride = 256;
    DisplayModePtr mode = &crtc->mode;

    if (x < 0)                        xorigin = -x+1;
    if (y < 0)                        yorigin = -y+1;
    if (xorigin >= CURSOR_WIDTH)  xorigin = CURSOR_WIDTH - 1;
    if (yorigin >= CURSOR_HEIGHT) yorigin = CURSOR_HEIGHT - 1;

    if (IS_AVIVO_VARIANT) {
	/* avivo cursor spans the full fb width */
	if (crtc->rotatedData == NULL) {
	    x += crtc->x;
	    y += crtc->y;
	}
	avivo_lock_cursor(crtc, TRUE);
	OUTREG(AVIVO_D1CUR_POSITION + radeon_crtc->crtc_offset, ((xorigin ? 0 : x) << 16)
	       | (yorigin ? 0 : y));
	OUTREG(AVIVO_D1CUR_HOT_SPOT + radeon_crtc->crtc_offset, (xorigin << 16) | yorigin);
	avivo_lock_cursor(crtc, FALSE);
    } else {
	if (mode->Flags & V_INTERLACE)
	    y /= 2;
	else if (mode->Flags & V_DBLSCAN)
	    y *= 2;

	if (crtc_id == 0) {
	    OUTREG(RADEON_CUR_HORZ_VERT_OFF,  (RADEON_CUR_LOCK
					       | (xorigin << 16)
					       | yorigin));
	    OUTREG(RADEON_CUR_HORZ_VERT_POSN, (RADEON_CUR_LOCK
					       | ((xorigin ? 0 : x) << 16)
					       | (yorigin ? 0 : y)));
	    RADEONCTRACE(("cursor_offset: 0x%x, yorigin: %d, stride: %d, temp %08X\n",
			  radeon_crtc->cursor_offset + pScrn->fbOffset, yorigin, stride, temp));
	    OUTREG(RADEON_CUR_OFFSET,
		   radeon_crtc->cursor_offset + pScrn->fbOffset + yorigin * stride);
	} else if (crtc_id == 1) {
	    OUTREG(RADEON_CUR2_HORZ_VERT_OFF,  (RADEON_CUR2_LOCK
						| (xorigin << 16)
						| yorigin));
	    OUTREG(RADEON_CUR2_HORZ_VERT_POSN, (RADEON_CUR2_LOCK
						| ((xorigin ? 0 : x) << 16)
						| (yorigin ? 0 : y)));
	    RADEONCTRACE(("cursor_offset2: 0x%x, yorigin: %d, stride: %d, temp %08X\n",
			  radeon_crtc->cursor_offset + pScrn->fbOffset, yorigin, stride, temp));
	    OUTREG(RADEON_CUR2_OFFSET,
		   radeon_crtc->cursor_offset + pScrn->fbOffset + yorigin * stride);
	}
    }
}

void
radeon_crtc_set_cursor_colors (xf86CrtcPtr crtc, int bg, int fg)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr info = RADEONPTR(pScrn);
    uint32_t *pixels = (uint32_t *)(pointer)(info->FB + radeon_crtc->cursor_offset);
    int            pixel, i;
    CURSOR_SWAPPING_DECL_MMIO

    RADEONCTRACE(("RADEONSetCursorColors\n"));

#ifdef ARGB_CURSOR
    /* Don't recolour cursors set with SetCursorARGB. */
    if (info->cursor_argb)
       return;
#endif

    fg |= 0xff000000;
    bg |= 0xff000000;

    /* Don't recolour the image if we don't have to. */
    if (fg == info->cursor_fg && bg == info->cursor_bg)
       return;

    CURSOR_SWAPPING_START();

    /* Note: We assume that the pixels are either fully opaque or fully
     * transparent, so we won't premultiply them, and we can just
     * check for non-zero pixel values; those are either fg or bg
     */
    for (i = 0; i < CURSOR_WIDTH * CURSOR_HEIGHT; i++, pixels++)
       if ((pixel = *pixels))
           *pixels = (pixel == info->cursor_fg) ? fg : bg;

    CURSOR_SWAPPING_END();
    info->cursor_fg = fg;
    info->cursor_bg = bg;
}

#ifdef ARGB_CURSOR

void
radeon_crtc_load_cursor_argb (xf86CrtcPtr crtc, CARD32 *image)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;
    RADEONInfoPtr  info = RADEONPTR(pScrn);
    CURSOR_SWAPPING_DECL_MMIO
    uint32_t *d = (uint32_t *)(pointer)(info->FB + radeon_crtc->cursor_offset);

    RADEONCTRACE(("RADEONLoadCursorARGB\n"));

    info->cursor_argb = TRUE;

    CURSOR_SWAPPING_START();

    memcpy (d, image, CURSOR_HEIGHT * CURSOR_WIDTH * 4);

    CURSOR_SWAPPING_END ();
}

#endif


/* Initialize hardware cursor support. */
Bool RADEONCursorInit(ScreenPtr pScreen)
{
    ScrnInfoPtr        pScrn   = xf86Screens[pScreen->myNum];
    RADEONInfoPtr      info    = RADEONPTR(pScrn);
    xf86CrtcConfigPtr  xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int                width;
    int		       width_bytes;
    int                height;
    int                size_bytes;
    uint32_t           cursor_offset = 0;
    int                c;

    size_bytes  = CURSOR_WIDTH * 4 * CURSOR_HEIGHT;
    width       = pScrn->displayWidth;
    width_bytes = width * (pScrn->bitsPerPixel / 8);
    height      = ((size_bytes * xf86_config->num_crtc) + width_bytes - 1) / width_bytes;

#ifdef USE_XAA
    if (!info->useEXA) {
	int align = IS_AVIVO_VARIANT ? 4096 : 256;
	FBAreaPtr          fbarea;

	fbarea = xf86AllocateOffscreenArea(pScreen, width, height,
					   align, NULL, NULL, NULL);

	if (!fbarea) {
	    cursor_offset    = 0;
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
		   "Hardware cursor disabled"
		   " due to insufficient offscreen memory\n");
	    return FALSE;
	} else {
	    cursor_offset  = RADEON_ALIGN((fbarea->box.x1 +
					   fbarea->box.y1 * width) *
					  info->CurrentLayout.pixel_bytes,
					  align);

	    for (c = 0; c < xf86_config->num_crtc; c++) {
		xf86CrtcPtr crtc = xf86_config->crtc[c];
		RADEONCrtcPrivatePtr radeon_crtc = crtc->driver_private;

		radeon_crtc->cursor_offset = cursor_offset + (c * size_bytes);

		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "Using hardware cursor %d (scanline %u)\n", c,
			   (unsigned)(radeon_crtc->cursor_offset / pScrn->displayWidth
				      / info->CurrentLayout.pixel_bytes));
	    }

	}
    }
#endif

    return xf86_cursors_init (pScreen, CURSOR_WIDTH, CURSOR_HEIGHT,
			      (HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
			       HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
			       HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1 |
			       HARDWARE_CURSOR_ARGB));
}
