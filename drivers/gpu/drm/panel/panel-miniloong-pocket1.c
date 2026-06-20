// SPDX-License-Identifier: GPL-2.0

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define MIPI_DSI_DCS_SHORT_WRITE        0x05
#define MIPI_DSI_DCS_SHORT_WRITE_PARAM  0x15
#define MIPI_DSI_DCS_LONG_WRITE         0x39

struct miniloong_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	bool prepared;
};

static inline struct miniloong_panel *to_miniloong_panel(struct drm_panel *panel)
{
	return container_of(panel, struct miniloong_panel, panel);
}

static int miniloong_write_seq(struct miniloong_panel *ctx, const char *name)
{
	struct device *dev = &ctx->dsi->dev;
	const u8 *seq;
	int len, i = 0, ret;

	seq = of_get_property(dev->of_node, name, &len);
	if (!seq || !len)
		return 0;

	while (i < len) {
		u8 type, delay, size;
		const u8 *payload;

		if (i + 3 > len)
			return -EINVAL;

		type = seq[i++];
		delay = seq[i++];
		size = seq[i++];

		if (i + size > len)
			return -EINVAL;

		payload = &seq[i];

		switch (type) {
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_LONG_WRITE:
			ret = mipi_dsi_dcs_write_buffer(ctx->dsi, payload, size);
			break;
		default:
			dev_err(dev, "unsupported DSI packet type 0x%02x\n", type);
			return -EINVAL;
		}

		if (ret < 0)
			return ret;

		if (delay)
			msleep(delay);

		i += size;
	}

	return 0;
}

static int miniloong_prepare(struct drm_panel *panel)
{
	struct miniloong_panel *ctx = to_miniloong_panel(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	if (ctx->enable_gpio) {
		gpiod_set_value_cansleep(ctx->enable_gpio, 1);
		msleep(120);
	}

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(120);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(120);
	}

	ret = miniloong_write_seq(ctx, "panel-init-sequence");
	if (ret < 0)
		return ret;

	ctx->prepared = true;
	return 0;
}

static int miniloong_unprepare(struct drm_panel *panel)
{
	struct miniloong_panel *ctx = to_miniloong_panel(panel);

	if (!ctx->prepared)
		return 0;

	miniloong_write_seq(ctx, "panel-exit-sequence");

	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	if (ctx->enable_gpio)
		gpiod_set_value_cansleep(ctx->enable_gpio, 0);

	ctx->prepared = false;
	return 0;
}

static int miniloong_enable(struct drm_panel *panel)
{
	if (panel->backlight)
		backlight_enable(panel->backlight);

	return 0;
}

static int miniloong_disable(struct drm_panel *panel)
{
	if (panel->backlight)
		backlight_disable(panel->backlight);

	return 0;
}

static const struct drm_display_mode miniloong_mode = {
	.clock = 47000,

	.hdisplay = 720,
	.hsync_start = 720 + 15,
	.hsync_end = 720 + 15 + 14,
	.htotal = 720 + 15 + 14 + 20,

	.vdisplay = 960,
	.vsync_start = 960 + 30,
	.vsync_end = 960 + 30 + 8,
	.vtotal = 960 + 30 + 8 + 20,

	.width_mm = 229,
	.height_mm = 143,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int miniloong_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &miniloong_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 229;
	connector->display_info.height_mm = 143;

	return 1;
}

static const struct drm_panel_funcs miniloong_panel_funcs = {
	.prepare = miniloong_prepare,
	.unprepare = miniloong_unprepare,
	.enable = miniloong_enable,
	.disable = miniloong_disable,
	.get_modes = miniloong_get_modes,
};

static int miniloong_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct miniloong_panel *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;

	ctx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM |
			  MIPI_DSI_MODE_EOT_PACKET;

	drm_panel_init(&ctx->panel, dev, &miniloong_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "MiniLoong Pocket 1 panel attached\n");

	return 0;
}

static int miniloong_remove(struct mipi_dsi_device *dsi)
{
	struct miniloong_panel *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id miniloong_of_match[] = {
	{ .compatible = "miniloong,pocket1-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, miniloong_of_match);

static struct mipi_dsi_driver miniloong_driver = {
	.probe = miniloong_probe,
	.remove = miniloong_remove,
	.driver = {
		.name = "panel-miniloong-pocket1",
		.of_match_table = miniloong_of_match,
	},
};
module_mipi_dsi_driver(miniloong_driver);

MODULE_AUTHOR("dArkOS");
MODULE_DESCRIPTION("MiniLoong Pocket 1 MIPI-DSI panel driver");
MODULE_LICENSE("GPL");