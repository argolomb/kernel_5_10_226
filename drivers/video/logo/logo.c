// SPDX-License-Identifier: GPL-2.0-only

/*
 *  Linux logo to be displayed on boot
 *
 *  Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 *  Copyright (C) 1996,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Copyright (C) 2001 Greg Banks <gnb@alphalink.com.au>
 *  Copyright (C) 2001 Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *  Copyright (C) 2003 Geert Uytterhoeven <geert@linux-m68k.org>
 */

#include <linux/linux_logo.h>
#include <linux/stddef.h>
#include <linux/module.h>

#ifdef CONFIG_M68K
#include <asm/setup.h>
#endif

#include <linux/fb.h>

#if defined(CONFIG_DRM) && defined(CONFIG_DRM_FBDEV_EMULATION)
#include <drm/drm_fb_helper.h>
#include <drm/drm_connector.h>
#endif

static bool nologo;
module_param(nologo, bool, 0);
MODULE_PARM_DESC(nologo, "Disables startup logo");

/*
 * Logos are located in the initdata, and will be freed in kernel_init.
 * Use late_init to mark the logos as freed to prevent any further use.
 */

static bool logos_freed;

static int __init fb_logo_late_init(void)
{
	logos_freed = true;
	return 0;
}

late_initcall_sync(fb_logo_late_init);

/* logo's are marked __initdata. Use __ref to tell
 * modpost that it is intended that this function uses data
 * marked __initdata.
 */
const struct linux_logo * __ref fb_find_logo(int depth)
{
	const struct linux_logo *logo = NULL;

	if (nologo || logos_freed)
		return NULL;

	if (depth >= 1) {
#ifdef CONFIG_LOGO_LINUX_MONO
		/* Generic Linux logo */
		logo = &logo_linux_mono;
#endif
#ifdef CONFIG_LOGO_SUPERH_MONO
		/* SuperH Linux logo */
		logo = &logo_superh_mono;
#endif
	}
	
	if (depth >= 4) {
#ifdef CONFIG_LOGO_LINUX_VGA16
		/* Generic Linux logo */
		logo = &logo_linux_vga16;
#endif
#ifdef CONFIG_LOGO_SUPERH_VGA16
		/* SuperH Linux logo */
		logo = &logo_superh_vga16;
#endif
	}
	
	if (depth >= 8) {
#ifdef CONFIG_LOGO_LINUX_CLUT224
		/* Generic Linux logo */
		logo = &logo_linux_clut224;
#endif
#ifdef CONFIG_LOGO_DEC_CLUT224
		/* DEC Linux logo on MIPS/MIPS64 or ALPHA */
		logo = &logo_dec_clut224;
#endif
#ifdef CONFIG_LOGO_MAC_CLUT224
		/* Macintosh Linux logo on m68k */
		if (MACH_IS_MAC)
			logo = &logo_mac_clut224;
#endif
#ifdef CONFIG_LOGO_PARISC_CLUT224
		/* PA-RISC Linux logo */
		logo = &logo_parisc_clut224;
#endif
#ifdef CONFIG_LOGO_SGI_CLUT224
		/* SGI Linux logo on MIPS/MIPS64 */
		logo = &logo_sgi_clut224;
#endif
#ifdef CONFIG_LOGO_SUN_CLUT224
		/* Sun Linux logo */
		logo = &logo_sun_clut224;
#endif
#ifdef CONFIG_LOGO_SUPERH_CLUT224
		/* SuperH Linux logo */
		logo = &logo_superh_clut224;
#endif
	}
	return logo;
}

static const struct linux_logo * __ref fb_find_logo_default(int depth)
{
	const struct linux_logo *logo = NULL;

	if (depth >= 1) {
#ifdef CONFIG_LOGO_LINUX_MONO
		logo = &logo_linux_mono;
#endif
	}
	if (depth >= 4) {
#ifdef CONFIG_LOGO_LINUX_VGA16
		logo = &logo_linux_vga16;
#endif
	}
	if (depth >= 8) {
#ifdef CONFIG_LOGO_LINUX_CLUT224
		logo = &logo_linux_clut224;
#endif
	}
	return logo;
}

static bool __ref fb_logo_use_hdmi(struct fb_info *info)
{
	if (!info)
		return false;

	/*
	 * RG353M internal display is 640x480.
	 * If fbdev console is anything else, assume HDMI boot output.
	 */
	if (info->var.xres == 640 || info->var.xres == 720 || info->var.xres == 960)
		return false;

	return true;
}

const struct linux_logo * __ref fb_find_logo_for_fb(struct fb_info *info, int depth)
{
	const struct linux_logo *logo;

	if (nologo || logos_freed)
		return NULL;

	logo = fb_find_logo_default(depth);

#ifdef CONFIG_LOGO_LINUX_CLUT224
	if (depth >= 8 && fb_logo_use_hdmi(info))
		logo = &logo_hdmi_clut224;
#endif

	return logo;
}
EXPORT_SYMBOL_GPL(fb_find_logo_for_fb);
EXPORT_SYMBOL_GPL(fb_find_logo);
