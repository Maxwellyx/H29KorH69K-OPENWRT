/*
 * RK3528 的 HDMI 控制器 glue（插进 dw_hdmi-rockchip.c 的 SoC 表区）
 *
 * 说明见 dts/README.md 与 hdmi-rk3528-port.md。
 * 数据来源：Rockchip BSP develop-6.1 dw_hdmi-rockchip.c 的 rk3528_* 部分，
 * 已适配到 mainline 6.18 的 dw_hdmi_phy_ops / rockchip_hdmi_chip_data 结构。
 */

/* ==== SECTION: BODY ==== */
static const struct dw_hdmi_phy_ops rk3528_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3528_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3528_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3528_chip_data,
	.phy_ops = &rk3528_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.use_drm_infoframe = true,
};

/* ==== SECTION: MATCH ==== */
	{ .compatible = "rockchip,rk3528-dw-hdmi",
	  .data = &rk3528_hdmi_drv_data
	},
