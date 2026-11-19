// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Teguh Sobirin <teguh@sobir.in>.
 *
 * This driver is for the switch-mode battery charger power delivery
 * and boost hardware found in pm8150b and related PMICs.
 * This work based on pmi8998 charger driver by 
 * Caleb Connolly <caleb.connolly@linaro.org>
 * Should be merged with the existing charger driver in the future.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/suspend.h>
#include <linux/property.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* clang-format off */
#define BATTERY_CHARGER_STATUS_1			0x06
#define ICL_INCR_REQ_FOR_PRECHG_BIT			BIT(7)
#define ZERO_CHARGE_CURRENT_BIT				BIT(6)
#define STEP_CHARGING_STATUS_SHIFT			3
#define STEP_CHARGING_STATUS_MASK			GENMASK(5, 3)
#define BATTERY_CHARGER_STATUS_MASK			GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2			0x07
#define DROP_IN_BATTERY_VOLTAGE_REFERENCE_BIT		BIT(7)
#define VBATT_LTET_RECHARGE_BIT				BIT(6)
#define VBATT_GTET_INHIBIT_BIT				BIT(5)
#define VBATT_GTET_FLOAT_VOLTAGE_BIT			BIT(4)
#define BATT_GT_FULL_ON_BIT				BIT(3)
#define CHARGER_ERROR_STATUS_SFT_EXPIRE_BIT		BIT(2)
#define CHARGER_ERROR_STATUS_BAT_OV_BIT			BIT(1)
#define CHARGER_ERROR_STATUS_BAT_TERM_MISSING_BIT	BIT(0)

#define BATTERY_CHARGER_STATUS_4		0x0A
#define CHARGE_CURRENT_REFERENCE_MASK		GENMASK(7, 0)

#define BATTERY_CHARGER_STATUS_7_REG		0x0D
#define BAT_TEMP_STATUS_SOFT_LIMIT_MASK		GENMASK(5, 4)
#define BAT_TEMP_STATUS_HOT_SOFT_BIT		BIT(5)
#define BAT_TEMP_STATUS_COLD_SOFT_BIT		BIT(4)
#define BAT_TEMP_STATUS_HARD_LIMIT_MASK		GENMASK(3, 2)
#define BAT_TEMP_STATUS_TOO_HOT_BIT		BIT(3)
#define BAT_TEMP_STATUS_TOO_COLD_BIT		BIT(2)
#define BAT_TEMP_STATUS_TOO_HOT_AFP_BIT		BIT(1)
#define BAT_TEMP_STATUS_TOO_COLD_AFP_BIT	BIT(0)

#define CHARGING_ENABLE_CMD			0x42
#define CHARGING_ENABLE_CMD_BIT			BIT(0)
#define CHARGING_ENABLE_POF_BIT			BIT(1)

#define CHGR_CFG2				0x51
#define EN_FAVOR_IN_BIT				BIT(5)
#define BAT_OV_ECC_BIT				BIT(4)
#define I_TERM_BIT				BIT(3)
#define AUTO_RECHG_BIT				BIT(2)
#define SOC_BASED_RECHG_BIT			BIT(1)
#define CHARGER_INHIBIT_BIT			BIT(0)

#define PRE_CHARGE_CURRENT_CFG			0x60
#define PRE_CHARGE_CURRENT_SETTING_MASK		GENMASK(2, 0)

#define FAST_CHARGE_CURRENT_CFG			0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK	GENMASK(7, 0)

#define BATTERY_CHARGER_STATUS_8			0x0e
#define PRE_TERM_BIT				BIT(2)

#define TCCC_CHARGE_CURRENT_TERMINATION_CFG	0x63
#define TCCC_CHARGE_CURRENT_TERMINATION_MASK	GENMASK(3, 0)

#define FLOAT_VOLTAGE_CFG			0x70
#define FLOAT_VOLTAGE_SETTING_MASK		GENMASK(7, 0)
#define FLOAT_VOLTAGE_MIN_UV			3600000
#define FLOAT_VOLTAGE_MAX_UV			4790000
#define FLOAT_VOLTAGE_STEP_UV			10000

#define CHARGE_RCHG_SOC_THRESHOLD_CFG_REG	0x7D
#define CHARGE_RCHG_SOC_THRESHOLD_CFG_MASK	GENMASK(7, 0)

#define ICL_STATUS				0x107
#define ICL_INPUT_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define AICL_STATUS				0x108
#define AICL_INPUT_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define POWER_PATH_STATUS				0x10B
#define P_PATH_INPUT_SS_DONE_BIT			BIT(7)
#define P_PATH_USBIN_SUSPEND_STS_BIT			BIT(6)
#define P_PATH_DCIN_SUSPEND_STS_BIT			BIT(5)
#define P_PATH_USE_USBIN_BIT				BIT(4)
#define P_PATH_USE_DCIN_BIT				BIT(3)
#define P_PATH_POWER_PATH_MASK				GENMASK(2, 1)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT		BIT(0)

#define OTG_CFG					0x153
#define OTG_RESERVED_BIT			BIT(7)
#define FAST_ROLE_SWAP_START_OPTION_BIT		BIT(6)
#define DIS_OTG_ON_TSD_BIT			BIT(5)
#define OTG_CFG_4_BIT				BIT(4)
#define EN_SOC_BASED_OTG_UVLO_BIT		BIT(3)
#define ENABLE_OTG_IN_DEBUG_MODE_BIT		BIT(2)
#define OTG_EN_SRC_CFG_BIT			BIT(1)
#define OTG_HICCUP_CNTR_RST_TIMER_SEL_BIT	BIT(0)

#define APSD_STATUS				0x307
#define APSD_STATUS_7_BIT			BIT(7)
#define HVDCP_CHECK_TIMEOUT_BIT			BIT(6)
#define SLOW_PLUGIN_TIMEOUT_BIT			BIT(5)
#define ENUMERATION_DONE_BIT			BIT(4)
#define VADP_CHANGE_DONE_AFTER_AUTH_BIT		BIT(3)
#define QC_AUTH_DONE_STATUS_BIT			BIT(2)
#define QC_CHARGER_BIT				BIT(1)
#define APSD_DTC_STATUS_DONE_BIT		BIT(0)

#define APSD_RESULT_STATUS			0x308
#define APSD_RESULT_STATUS_7_BIT		BIT(7)
#define APSD_RESULT_STATUS_MASK			GENMASK(6, 0)
#define QC_3P0_BIT				BIT(6)
#define QC_2P0_BIT				BIT(5)
#define FLOAT_CHARGER_BIT			BIT(4)
#define DCP_CHARGER_BIT				BIT(3)
#define CDP_CHARGER_BIT				BIT(2)
#define OCP_CHARGER_BIT				BIT(1)
#define SDP_CHARGER_BIT				BIT(0)

#define USBIN_INT_RT_STS_OFFSET			0x310
#define USBIN_PLUGIN_RT_STS_BIT			BIT(4)

#define USBIN_CMD_IL				0x340
#define USBIN_SUSPEND_BIT			BIT(0)

#define CMD_APSD				0x341
#define APSD_RERUN_BIT				BIT(0)

#define CMD_ICL_OVERRIDE			0x342
#define ICL_OVERRIDE_BIT			BIT(0)

#define TYPE_C_CFG				0x358
#define BC1P2_START_ON_CC_BIT			BIT(7)

#define HVDCP_PULSE_COUNT_MAX			0x35b
#define HVDCP_PULSE_COUNT_MAX_QC2_MASK		GENMASK(7, 6)

#define USBIN_ADAPTER_ALLOW_CFG			0x360
#define USBIN_ADAPTER_ALLOW_MASK		GENMASK(3, 0)

#define USBIN_OPTIONS_1_CFG			0x362
#define HVDCP_AUTH_ALG_EN_CFG_BIT		BIT(6)
#define HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT	BIT(5)
#define BC1P2_SRC_DETECT_BIT			BIT(3)
#define HVDCP_EN_BIT				BIT(2)
#define HVDCP_NO_AUTH_QC3_CFG_BIT		BIT(1)

#define USBIN_LOAD_CFG				0x365
#define ICL_OVERRIDE_AFTER_APSD_BIT		BIT(4)
#define USBIN_AICL_STEP_TIMING_SEL_MASK		GENMASK(3, 2)
#define USBIN_IN_COLLAPSE_GF_SEL_MASK		GENMASK(1, 0)

#define USBIN_ICL_OPTIONS			0x366
#define CFG_USB3P0_SEL_BIT			BIT(2)
#define USB51_MODE_BIT				BIT(1)
#define USBIN_MODE_CHG_BIT			BIT(0)

#define USBIN_CURRENT_LIMIT_CFG			0x370
#define USBIN_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define USBIN_AICL_OPTIONS_CFG			0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT		BIT(7)
#define USBIN_AICL_PERIODIC_RERUN_EN_BIT	BIT(4)
#define USBIN_AICL_ADC_EN_BIT			BIT(3)
#define USBIN_AICL_EN_BIT			BIT(2)

#define USBIN_5V_AICL_THRESHOLD_CFG		0x381
#define USBIN_5V_AICL_THRESHOLD_CFG_MASK	GENMASK(2, 0)

#define USBIN_CONT_AICL_THRESHOLD_CFG		0x384
#define USBIN_CONT_AICL_THRESHOLD_CFG_MASK	GENMASK(5, 0)

#define DCIN_CMD_IL				0x440
#define DCIN_SUSPEND_BIT			BIT(0)

#define TYPE_C_SNK_STATUS			0x506
#define TYPE_C_MISC_STATUS			0x50b
#define TYPEC_SNK_SRC_MODE_BIT			BIT(6)
#define TYPEC_DEBOUNCE_DONE_BIT			BIT(3)
#define TYPEC_CC_ORIENTATION_BIT		BIT(1)
#define TYPEC_CC_ATTACHED_BIT			BIT(0)
#define DETECTED_SRC_TYPE_MASK			GENMASK(6, 0)
#define SNK_RP_STD_DAM_BIT			BIT(6)
#define SNK_RP_1P5_DAM_BIT			BIT(5)
#define SNK_RP_3P0_DAM_BIT			BIT(4)
#define SNK_DAM_MASK				GENMASK(6, 4)
#define SNK_DAM_500MA_BIT			BIT(6)
#define SNK_DAM_1500MA_BIT			BIT(5)
#define SNK_DAM_3000MA_BIT			BIT(4)
#define SNK_RP_STD_BIT				BIT(3)
#define SNK_RP_1P5_BIT				BIT(2)
#define SNK_RP_3P0_BIT				BIT(1)
#define SNK_RP_SHORT_BIT			BIT(0)

#define TYPE_C_MODE_CFG				0x544
#define TYPEC_TRY_MODE_MASK			GENMASK(4, 3)
#define EN_TRY_SNK_BIT				BIT(4)
#define EN_TRY_SRC_BIT				BIT(3)
#define TYPEC_POWER_ROLE_CMD_MASK		GENMASK(2, 0)
#define EN_SRC_ONLY_BIT				BIT(2)
#define EN_SNK_ONLY_BIT				BIT(1)
#define TYPEC_DISABLE_CMD_BIT			BIT(0)

#define TYPEC_TYPE_C_VCONN_CONTROL		0x546
#define VCONN_EN_ORIENTATION_BIT		BIT(2)
#define VCONN_EN_VALUE_BIT			BIT(1)
#define VCONN_EN_SRC_BIT			BIT(0)

#define TYPE_C_DEBUG_ACCESS_SINK		0x54a
#define TYPEC_DEBUG_ACCESS_SINK_MASK		GENMASK(4, 0)

#define DEBUG_ACCESS_SRC_CFG			0x54C
#define EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT	BIT(0)

#define TYPE_C_EXIT_STATE_CFG			0x550
#define BYPASS_VSAFE0V_DURING_ROLE_SWAP_BIT	BIT(3)
#define SEL_SRC_UPPER_REF_BIT			BIT(2)
#define EXIT_SNK_BASED_ON_CC_BIT		BIT(0)

#define TYPE_C_INTERRUPT_EN_CFG_1			0x55e
#define TYPEC_LEGACY_CABLE_INT_EN_BIT			BIT(7)
#define TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT	BIT(6)
#define TYPEC_TRYSOURCE_DETECT_INT_EN_BIT		BIT(5)
#define TYPEC_TRYSINK_DETECT_INT_EN_BIT			BIT(4)
#define TYPEC_CCOUT_DETACH_INT_EN_BIT			BIT(3)
#define TYPEC_CCOUT_ATTACH_INT_EN_BIT			BIT(2)
#define TYPEC_VBUS_DEASSERT_INT_EN_BIT			BIT(1)
#define TYPEC_VBUS_ASSERT_INT_EN_BIT			BIT(0)

#define BARK_BITE_WDOG_PET			0x643
#define BARK_BITE_WDOG_PET_BIT			BIT(0)

#define WD_CFG					0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT		BIT(7)
#define BARK_WDOG_INT_EN_BIT			BIT(6)
#define BITE_WDOG_INT_EN_BIT			BIT(5)
#define SFT_AFTER_WDOG_IRQ_MASK			GENMASK(4, 3)
#define WDOG_IRQ_SFT_BIT			BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT		BIT(1)
#define WDOG_TIMER_EN_BIT			BIT(0)

#define SNARL_BARK_BITE_WD_CFG			0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT	BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK			GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK			GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK			GENMASK(1, 0)
/* Vendor setting: snarl 8 s, bark 64 s, bite 8 s later, bite stops charging */
#define WDOG_POLICY_CFG				(BITE_WDOG_DISABLE_CHARGING_CFG_BIT | \
						 0x70 | (2 << 2) | 0x3)

#define AICL_RERUN_TIME_CFG			0x661
#define AICL_RERUN_TIME_MASK			GENMASK(1, 0)

#define SDP_CURRENT_UA			500000
#define CDP_CURRENT_UA			1500000
#define DCP_CURRENT_UA			1500000
#define RP_1P5_CURRENT_UA		1500000
#define RP_3P0_CURRENT_UA		3000000
#define CURRENT_MAX_UA			3000000
#define FAST_CHARGE_CURRENT_MAX_UA		8000000
#define APSD_MAX_RETRIES			3
/* Time after an attach, and between two samples, before Rp is trusted */
#define SMB5_CC_SETTLE			msecs_to_jiffies(500)
#define SMB5_CC_MAX_SAMPLES		4

/* PM8150B current registers use 50 mA steps. */
#define CURRENT_SCALE_FACTOR		50000
/* clang-format on */

static unsigned int max_input_ua;
module_param(max_input_ua, uint, 0444);
MODULE_PARM_DESC(max_input_ua,
		 "Ceiling on the USB input current limit in uA, 0 for none (min 500000)");

/* BATTERY_CHARGER_STATUS_1 charger states, as in the vendor smb5 headers. */
enum charger_status {
	INHIBIT_CHARGE = 0,
	TRICKLE_CHARGE,
	PRE_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	PAUSE_CHARGE,
	DISABLE_CHARGE,
};

/*
 * Charging policy of the stock OnePlus 8 Pro firmware (oplus_chg with an
 * external fuel gauge), used when the device tree names a fuel gauge.
 *
 * The PM8150B float voltage is set above the level at which the cell is
 * full, because the charger senses the battery behind the charge path. The
 * fuel gauge at the cell decides when the battery is full and stops the
 * charger. Limits depend on the battery temperature. All values are those
 * of the stock device tree (kona-mtp.dtsi of instantnoodlep); temperatures
 * are in 0.1 degC. Where this differs from stock it is stricter.
 */
struct smb5_temp_zone {
	int min_dc;		/* lower bound of the zone */
	int fv_uv;		/* PM8150B float voltage */
	int fcc_ua;		/* fast charge current */
	int full_uv;		/* cell voltage for "full" at low current */
	int over_uv;		/* cell voltage that lowers the float voltage */
	int recharge_uv;	/* recharge this far below the float voltage */
};

static const struct smb5_temp_zone smb5_zones[] = {
	{ -20, 3980000,  300000, 3980000, 3990000, 400000 },	/* cold */
	{   0, 4435000, 1200000, 4385000, 4445000, 300000 },	/* little cold */
	{  50, 4435000, 2000000, 4385000, 4445000, 200000 },	/* cool */
	{ 120, 4435000, 2000000, 4385000, 4445000, 200000 },	/* little cool */
	{ 160, 4435000, 2000000, 4385000, 4445000, 200000 },	/* normal */
	{ 450, 4130000, 1100000, 4080000, 4140000, 200000 },	/* warm */
};

#define ZONE_NONE			-2
#define ZONE_TOO_COLD			-1
#define ZONE_COLD			0
#define ZONE_NORMAL			4
#define ZONE_WARM			5
#define ZONE_TOO_HOT			6
#define POLICY_HOT_DC			500	/* no charging above 50 degC */
#define POLICY_HYST_DC			20
#define POLICY_INTERVAL			(5 * HZ)
/* Stock limits for a battery the gauge cannot vouch for */
#define POLICY_FALLBACK_FV_UV		3980000
#define POLICY_FALLBACK_FCC_UA		500000
#define POLICY_ITERM_UA			230000
#define POLICY_SW_FULL_COUNT		5
#define POLICY_HW_FULL_COUNT		4
#define POLICY_HW_FULL_INC_UV		18000
#define POLICY_RECHARGE_COUNT		4
#define POLICY_OVER_COUNT		2
#define POLICY_FV_STEP_UV		8000
#define POLICY_OVP_MARGIN_UV		10000
/*
 * Without the PMIC fuel gauge the PM8150B has no battery current for its
 * termination check and ends each charge cycle after a few seconds in
 * constant voltage. The cycle is restarted while the cell, measured by the
 * gauge with no current flowing, is this far below the stock full level.
 * Stock ends at that level with 230 mA still flowing, so its resting
 * voltage is lower than the level itself.
 */
#define POLICY_REST_MARGIN_UV		35000
/* Not in stock: stop at once this far above the over-voltage limit */
#define POLICY_HARD_STOP_UV		15000

/* Current advertised by a Type-C source through its Rp pull-up */
enum smb5_rp {
	SMB5_RP_DEFAULT,
	SMB5_RP_1P5,
	SMB5_RP_3P0,
};

struct smb5_input_limit {
	int current_ua;
	enum power_supply_usb_type type;
};

/* Type-C status of the partner, as one sample */
struct smb5_cc {
	bool attached;		/* sink mode, CC attached and debounced */
	bool flipped;		/* CC orientation bit */
	enum smb5_rp rp;	/* default unless attached */
};

/*
 * Type-C classification of a source on USBIN without TCPM. Rp is used only
 * in SMB5_CC_CLASSIFIED; every other state holds the input at 500 mA.
 */
enum smb5_cc_state {
	SMB5_CC_DETACHED,	/* no VBUS, or a plug edge not yet handled */
	SMB5_CC_SETTLING,	/* attach seen, waiting SMB5_CC_SETTLE */
	SMB5_CC_SAMPLED,	/* one sample taken, waiting for a matching one */
	SMB5_CC_CLASSIFIED,	/* two matching samples: cc is in force */
};

struct smb5_cc_track {
	enum smb5_cc_state state;
	unsigned long since;	/* jiffies of the attach or the last sample */
	unsigned int samples;
	unsigned int gen;	/* plug edge count this classification began in */
	bool icl_unconfirmed;	/* a write failed: the PMIC may hold any limit */
	struct smb5_cc cc;	/* last sample, or the classified one */
};

struct smb5_cc_step {
	enum smb5_rp rp;	/* default unless classified */
	bool hold;		/* keep the input at 500 mA */
	bool replugged;		/* a new attach: restart BC1.2 detection */
	unsigned long recheck;	/* jiffies to the next pass, 0 for none */
};

struct smb5_register {
	u16 addr;
	u8 mask;
	u8 val;
};

/**
 * struct smb5_chip - smb5 chip structure
 * @dev:		Device reference for power_supply
 * @name:		The platform device name
 * @base:		Base address for smb5 registers
 * @regmap:		Register map
 * @batt_info:		Battery data from DT
 * @status_change_work: Worker to handle plug/unplug events
 * @cable_irq:		USB plugin IRQ
 * @wakeup_enabled:	If the cable IRQ will cause a wakeup
 * @usb_in_i_chan:	USB_IN current measurement channel
 * @usb_in_v_chan:	USB_IN voltage measurement channel
 * @chg_psy:		Charger power supply instance
 * @cc:			Type-C classification of the source without TCPM
 * @cc_paused:		System sleep: no timed passes of the status work
 * @plug_gen:		Plug edges counted in hard IRQ context
 * @input_ceiling_ua:	No input limit above this is written (max_input_ua)
 */
struct smb5_chip {
	struct device *dev;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;

	struct delayed_work status_change_work;
	int cable_irq;
	bool wakeup_enabled;

	struct iio_channel *usb_in_i_chan;
	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;
	struct mutex lock;
	bool ready;
	bool has_tcpm;
	bool apsd_started;
	unsigned int apsd_retries;
	unsigned int user_current_limit;
	unsigned int source_current_limit;
	enum power_supply_usb_type usb_type;
	struct smb5_cc_track cc;
	bool cc_paused;
	atomic_t plug_gen;
	unsigned int input_ceiling_ua;

	/* Charging policy driven by the external fuel gauge */
	bool policy;
	struct delayed_work policy_work;
	struct power_supply *gauge;
	struct notifier_block pm_nb;
	bool gauge_ok;
	bool suspended;
	bool charging;
	bool user_charge_enable;
	bool policy_charge_enable;
	bool batt_full;
	int zone;
	int fv_set_uv;
	int sw_full_count;
	int hw_full_count;
	int recharge_count;
	int over_count;
	int rest_count;
};

static enum power_supply_property smb5_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
};

static int smb5_get_prop_usb_online(struct smb5_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + USBIN_INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read USBIN_RT_STS rc=%d\n", rc);
		return rc;
	}

	*val = (stat & USBIN_PLUGIN_RT_STS_BIT) ? 1 : 0;
	return 0;
}

/*
 * True when the charger sees VBUS that it did not source itself: USBIN is
 * plugged in and the Type-C block is not in source mode.
 */
static bool smb5_sink_vbus_present(struct smb5_chip *chip)
{
	unsigned int misc;
	int online = 0;

	if (smb5_get_prop_usb_online(chip, &online) || !online)
		return false;
	if (regmap_read(chip->regmap, chip->base + TYPE_C_MISC_STATUS, &misc))
		return false;

	return !(misc & TYPEC_SNK_SRC_MODE_BIT);
}

/*
 * Qualcomm "automatic power source detection" aka APSD
 * tells us what type of charger we're connected to.
 */
static int smb5_apsd_get_charger_type(struct smb5_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd status, rc = %d", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read apsd result, rc = %d", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	switch (stat) {
	case SDP_CHARGER_BIT:
		*val = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case CDP_CHARGER_BIT:
		*val = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case DCP_CHARGER_BIT:
	case OCP_CHARGER_BIT:
	case QC_2P0_BIT:
	case QC_3P0_BIT:
	case DCP_CHARGER_BIT | QC_2P0_BIT:
	case DCP_CHARGER_BIT | QC_3P0_BIT:
		*val = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	default:
		/* Empty, floating and unrecognised results are not an SDP. */
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	}

	return 0;
}

/*
 * Only a single clean Rp bit counts. Several bits, Rp shorted or a debug
 * accessory read as the default USB current.
 */
static enum smb5_rp smb5_rp_from_snk_status(unsigned int stat)
{
	switch (stat & DETECTED_SRC_TYPE_MASK) {
	case SNK_RP_1P5_BIT:
		return SMB5_RP_1P5;
	case SNK_RP_3P0_BIT:
		return SMB5_RP_3P0;
	default:
		return SMB5_RP_DEFAULT;
	}
}

static struct smb5_cc smb5_cc_from_status(unsigned int misc, unsigned int snk)
{
	struct smb5_cc cc = { .rp = SMB5_RP_DEFAULT };

	if (!(misc & TYPEC_CC_ATTACHED_BIT) || (misc & TYPEC_SNK_SRC_MODE_BIT) ||
	    !(misc & TYPEC_DEBOUNCE_DONE_BIT))
		return cc;

	cc.attached = true;
	cc.flipped = misc & TYPEC_CC_ORIENTATION_BIT;
	cc.rp = smb5_rp_from_snk_status(snk);
	return cc;
}

static int smb5_read_cc(struct smb5_chip *chip, struct smb5_cc *cc)
{
	unsigned int misc, snk;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + TYPE_C_MISC_STATUS, &misc);
	if (!rc)
		rc = regmap_read(chip->regmap, chip->base + TYPE_C_SNK_STATUS, &snk);
	if (!rc)
		*cc = smb5_cc_from_status(misc, snk);
	return rc;
}

static bool smb5_cc_equal(const struct smb5_cc *a, const struct smb5_cc *b)
{
	return a->attached == b->attached && a->flipped == b->flipped &&
	       a->rp == b->rp;
}

static void smb5_cc_restart(struct smb5_cc_track *t, unsigned long now)
{
	t->state = SMB5_CC_SETTLING;
	t->since = now;
	t->samples = 0;
}

/*
 * One pass of the Type-C classification. @gen is the plug edge count read
 * before the pass sampled the hardware; an edge since the classification
 * began is a new attach, even before its IRQ thread has run. A changed CC
 * orientation is a new attach too, and any other change of a classified
 * sample restarts the classification. While the input limit is
 * unconfirmed, every pass restarts it.
 */
static struct smb5_cc_step smb5_cc_next(struct smb5_cc_track *t,
					unsigned int gen, bool online,
					const struct smb5_cc *cc,
					unsigned long now)
{
	struct smb5_cc_step step = { .rp = SMB5_RP_DEFAULT, .hold = true };
	bool sampled = t->state == SMB5_CC_SAMPLED ||
		       t->state == SMB5_CC_CLASSIFIED;

	if (!online) {
		t->state = SMB5_CC_DETACHED;
		return step;
	}

	if (t->state == SMB5_CC_DETACHED || t->gen != gen ||
	    (sampled && t->cc.attached && cc->attached &&
	     t->cc.flipped != cc->flipped)) {
		step.replugged = true;
		t->gen = gen;
		smb5_cc_restart(t, now);
	} else if (t->icl_unconfirmed ||
		   (t->state == SMB5_CC_CLASSIFIED && !smb5_cc_equal(&t->cc, cc))) {
		smb5_cc_restart(t, now);
	}

	if (t->state == SMB5_CC_CLASSIFIED) {
		step.hold = false;
		step.rp = t->cc.rp;
		return step;
	}

	if (time_before(now, t->since + SMB5_CC_SETTLE)) {
		step.recheck = t->since + SMB5_CC_SETTLE - now;
		return step;
	}

	if (t->state == SMB5_CC_SAMPLED && smb5_cc_equal(&t->cc, cc)) {
		t->state = SMB5_CC_CLASSIFIED;
		step.hold = false;
		step.rp = cc->rp;
		return step;
	}

	t->cc = *cc;
	t->since = now;
	if (++t->samples >= SMB5_CC_MAX_SAMPLES) {
		/* Never stable: classify without Rp until the next change. */
		t->cc.rp = SMB5_RP_DEFAULT;
		t->state = SMB5_CC_CLASSIFIED;
		step.hold = false;
		return step;
	}
	t->state = SMB5_CC_SAMPLED;
	step.recheck = SMB5_CC_SETTLE;
	return step;
}

/*
 * Called after a limit above 500 mA was written: it stands only if no plug
 * edge was counted since the pass read @gen.
 */
static bool smb5_cc_still_valid(struct smb5_cc_track *t, unsigned int gen)
{
	if (t->gen == gen)
		return true;
	t->state = SMB5_CC_DETACHED;
	return false;
}

/* Only a successful write of at most 500 mA confirms the input limit. */
static void smb5_cc_icl_written(struct smb5_cc_track *t, unsigned int ua, int rc)
{
	if (rc)
		t->icl_unconfirmed = true;
	else if (ua <= SDP_CURRENT_UA)
		t->icl_unconfirmed = false;
}

/*
 * Ceiling for a max_input_ua value, 0 meaning none. The driver holds
 * 500 mA during attach and after unbind, so no ceiling goes below that.
 */
static unsigned int smb5_input_ceiling(unsigned int param_ua)
{
	if (!param_ua)
		return CURRENT_MAX_UA;
	param_ua = clamp_t(unsigned int, param_ua, SDP_CURRENT_UA, CURRENT_MAX_UA);
	return rounddown(param_ua, CURRENT_SCALE_FACTOR);
}

/*
 * Input limit for a BC1.2 result and the Rp level of the source, at most
 * @ceiling_ua. Type-C current of 1.5 A or 3 A takes precedence over BC1.2
 * where it is higher; default Rp leaves the BC1.2 limit.
 */
static struct smb5_input_limit
smb5_input_limit(enum power_supply_usb_type bc12, enum smb5_rp rp,
		 unsigned int ceiling_ua)
{
	struct smb5_input_limit limit = { SDP_CURRENT_UA, bc12 };
	int rp_ua = 0;

	if (bc12 == POWER_SUPPLY_USB_TYPE_CDP)
		limit.current_ua = CDP_CURRENT_UA;
	else if (bc12 == POWER_SUPPLY_USB_TYPE_DCP)
		limit.current_ua = DCP_CURRENT_UA;

	if (rp == SMB5_RP_3P0)
		rp_ua = RP_3P0_CURRENT_UA;
	else if (rp == SMB5_RP_1P5)
		rp_ua = RP_1P5_CURRENT_UA;

	if (rp_ua > limit.current_ua) {
		limit.current_ua = rp_ua;
		limit.type = POWER_SUPPLY_USB_TYPE_C;
	}
	limit.current_ua = min_t(unsigned int, limit.current_ua, ceiling_ua);

	return limit;
}

static int smb5_get_prop_charging_enabled(struct smb5_chip *chip, int *val)
{
	unsigned int charging_enabled;
	int rc = regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &charging_enabled);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging enabled status, rc = %d", rc);
		return rc;
	}
	*val = charging_enabled & CHARGING_ENABLE_CMD_BIT ? 1 : 0;
	return 0;
}

static int smb5_get_prop_status(struct smb5_chip *chip, int *val)
{
	unsigned char stat[2];
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	if (chip->policy) {
		/* The charger state flips with every restarted cycle. */
		if (READ_ONCE(chip->batt_full))
			*val = POWER_SUPPLY_STATUS_FULL;
		else if (READ_ONCE(chip->charging))
			*val = POWER_SUPPLY_STATUS_CHARGING;
		else
			*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	rc = regmap_bulk_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, &stat, 2);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status ret=%d\n",
			rc);
		return rc;
	}

	if (stat[1] & VBATT_GTET_INHIBIT_BIT) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	stat[0] = stat[0] & BATTERY_CHARGER_STATUS_MASK;

	switch (stat[0]) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return rc;
	case PAUSE_CHARGE:
	case DISABLE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return rc;
	case TERMINATE_CHARGE:
	case INHIBIT_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return rc;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return rc;
	}
}

static inline int smb5_get_current_limit(struct smb5_chip *chip,
					 unsigned int *val)
{
	int rc = regmap_read(chip->regmap, chip->base + AICL_STATUS, val);

	if (rc >= 0)
		*val *= CURRENT_SCALE_FACTOR;	
	return rc;
}

/* Unless forced, the register is written only when its value changes. */
static int smb5_set_current_limit(struct smb5_chip *chip, unsigned int val,
				  bool force)
{
	unsigned char val_raw;

	if (val > CURRENT_MAX_UA)
		return -EINVAL;
	/* Every write of the input limit passes here. */
	val = min(val, chip->input_ceiling_ua);
	val_raw = val / CURRENT_SCALE_FACTOR;

	if (force)
		return regmap_write(chip->regmap,
				    chip->base + USBIN_CURRENT_LIMIT_CFG, val_raw);
	return regmap_update_bits(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG,
				  USBIN_CURRENT_LIMIT_MASK, val_raw);
}

static int smb5_set_charge_current(struct smb5_chip *chip, int current_ua)
{
	/* Retain the bring-up default only for boards without a battery limit. */
	if (current_ua <= 0)
		current_ua = 1500000;
	if (current_ua < CURRENT_SCALE_FACTOR ||
	    current_ua > FAST_CHARGE_CURRENT_MAX_UA)
		return -EINVAL;

	return regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
			    current_ua / CURRENT_SCALE_FACTOR);
}

static int smb5_set_prop_charging_enabled(struct smb5_chip *chip, unsigned int val)
{
	return regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
					CHARGING_ENABLE_CMD_BIT, val ? CHARGING_ENABLE_CMD_BIT : 0);
}

static void smb5_queue_status_work(struct smb5_chip *chip)
{
	mutex_lock(&chip->lock);
	if (chip->ready)
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
	mutex_unlock(&chip->lock);
}

static void smb5_external_power_changed(struct power_supply *psy)
{
	smb5_queue_status_work(power_supply_get_drvdata(psy));
}

static int smb5_supplier_property(struct smb5_chip *chip,
				 enum power_supply_property prop, int *value)
{
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property_from_supplier(chip->chg_psy, prop, &val);
	if (!ret)
		*value = val.intval;
	return ret;
}

static int smb5_apply_input_limit(struct smb5_chip *chip, unsigned int current_ua,
				 bool apsd)
{
	int ret;

	/* Suspend before disabling input; never leave a stale large limit. */
	if (!current_ua) {
		ret = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
					 USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
		if (ret)
			return ret;
	}

	ret = smb5_set_current_limit(chip, current_ua, chip->has_tcpm);
	if (ret)
		return ret;
	ret = regmap_update_bits(chip->regmap, chip->base + USBIN_OPTIONS_1_CFG,
				 BC1P2_SRC_DETECT_BIT, apsd ? BC1P2_SRC_DETECT_BIT : 0);
	if (ret)
		return ret;

	/* Software high-current mode, also used by downstream for PD. */
	ret = regmap_update_bits(chip->regmap, chip->base + USBIN_ICL_OPTIONS,
				 USBIN_MODE_CHG_BIT, USBIN_MODE_CHG_BIT);
	if (ret)
		return ret;
	ret = regmap_update_bits(chip->regmap, chip->base + CMD_ICL_OVERRIDE,
				 ICL_OVERRIDE_BIT, 0);
	if (ret)
		return ret;
	ret = regmap_update_bits(chip->regmap, chip->base + USBIN_LOAD_CFG,
				 ICL_OVERRIDE_AFTER_APSD_BIT, ICL_OVERRIDE_AFTER_APSD_BIT);
	if (ret)
		return ret;

	return regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
				  USBIN_SUSPEND_BIT, current_ua ? 0 : USBIN_SUSPEND_BIT);
}

static void smb5_status_change_work(struct work_struct *work)
{
	struct smb5_chip *chip = container_of(work, struct smb5_chip,
					     status_change_work.work);
	int online, type = POWER_SUPPLY_USB_TYPE_UNKNOWN, current_ua = 0;
	int voltage_uv, ret;
	bool apsd = false, start_apsd = false, retry = false;
	struct smb5_cc_step cc_step = { .rp = SMB5_RP_DEFAULT };
	struct smb5_input_limit limit;
	unsigned int gen, icl;
	unsigned long delay;
	struct smb5_cc cc;

	mutex_lock(&chip->lock);
	if (!chip->ready)
		goto unlock;

	if (chip->has_tcpm) {
		ret = smb5_supplier_property(chip, POWER_SUPPLY_PROP_ONLINE, &online);
		if (ret)
			goto fail;
		if (!online) {
			chip->apsd_retries = 0;
			goto apply;
		}
		ret = smb5_supplier_property(chip, POWER_SUPPLY_PROP_CURRENT_NOW,
					     &current_ua);
		if (ret)
			goto fail;
		ret = smb5_supplier_property(chip, POWER_SUPPLY_PROP_USB_TYPE, &type);
		if (ret)
			goto fail;
		ret = smb5_supplier_property(chip, POWER_SUPPLY_PROP_VOLTAGE_NOW,
					     &voltage_uv);
		if (ret)
			goto fail;

		/* Default Rp is reported as zero when TCPM has no BC1.2 limit. */
		if (current_ua < 0)
			goto apply;
		/* This board advertises only fixed 5 V PDOs. */
		if (voltage_uv < 4500000 || voltage_uv > 5500000) {
			ret = -ERANGE;
			goto fail;
		}
		if (type == POWER_SUPPLY_USB_TYPE_PD ||
		    type == POWER_SUPPLY_USB_TYPE_PD_PPS || current_ua > SDP_CURRENT_UA) {
			if (type != POWER_SUPPLY_USB_TYPE_PD &&
			    type != POWER_SUPPLY_USB_TYPE_PD_PPS)
				type = POWER_SUPPLY_USB_TYPE_C;
			chip->apsd_retries = 0;
			goto apply;
		}
	} else {
		gen = atomic_read_acquire(&chip->plug_gen);
		ret = smb5_get_prop_usb_online(chip, &online);
		if (ret)
			goto fail;
		ret = smb5_read_cc(chip, &cc);
		if (ret)
			goto fail;
		cc_step = smb5_cc_next(&chip->cc, gen, online, &cc, jiffies);
		if (cc_step.replugged) {
			/* A new attach that no plug interrupt reported */
			chip->apsd_started = false;
			chip->apsd_retries = 0;
		}
		if (!online) {
			chip->apsd_retries = 0;
			goto apply;
		}
	}

	/* Default Rp or a legacy cable: start with a conservative BC1.2 limit. */
	apsd = true;
	current_ua = SDP_CURRENT_UA;
	if (!chip->apsd_started) {
		/* Ignore any result left by the previous connection or PD mode. */
		type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		start_apsd = true;
		retry = true;
		goto apply;
	}

	ret = smb5_apsd_get_charger_type(chip, &type);
	if (ret == -EAGAIN)
		type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	else if (ret)
		goto fail;

	/*
	 * With TCPM, a non-default Rp has already been handled above. Without
	 * it, hold 500 mA until the Type-C state of this attach is classified.
	 */
	if (cc_step.hold && !chip->has_tcpm)
		limit = smb5_input_limit(POWER_SUPPLY_USB_TYPE_UNKNOWN, SMB5_RP_DEFAULT,
					 chip->input_ceiling_ua);
	else
		limit = smb5_input_limit(type, cc_step.rp, chip->input_ceiling_ua);
	current_ua = limit.current_ua;
	type = limit.type;

	/* Also recheck early completed results; old DTs have no source-change IRQ. */
	if (chip->apsd_retries < APSD_MAX_RETRIES) {
		chip->apsd_retries++;
		retry = true;
	}

apply:
	/*
	 * A sink may always draw pSnkStdby (2.5 W, 500 mA at 5 V). Do not cut
	 * the input completely while an external source still supplies VBUS:
	 * if TCPM loses track of the partner, the phone would otherwise run
	 * from the battery until it is empty, with the charger attached.
	 */
	if (current_ua < SDP_CURRENT_UA && smb5_sink_vbus_present(chip)) {
		/* Routine during attach debounce, so not worth a warning. */
		dev_dbg(chip->dev, "VBUS present without a source limit, using %u uA\n",
			SDP_CURRENT_UA);
		current_ua = SDP_CURRENT_UA;
	}
	chip->source_current_limit = clamp_t(int, current_ua, 0, chip->input_ceiling_ua);
	icl = min(chip->source_current_limit, chip->user_current_limit);
	ret = smb5_apply_input_limit(chip, icl, apsd);
	smb5_cc_icl_written(&chip->cc, icl, ret);
	if (ret)
		goto fail;
	if (!chip->has_tcpm && icl > SDP_CURRENT_UA) {
		/* The edge count must be read after the write has completed. */
		smp_mb();
		if (!smb5_cc_still_valid(&chip->cc, atomic_read(&chip->plug_gen))) {
			/* The IRQ thread of that edge queues the next pass. */
			icl = min(SDP_CURRENT_UA, chip->user_current_limit);
			chip->source_current_limit = SDP_CURRENT_UA;
			ret = smb5_set_current_limit(chip, icl, false);
			smb5_cc_icl_written(&chip->cc, icl, ret);
			if (ret)
				goto fail;
			type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		}
	}
	if (start_apsd) {
		/* Detection must be enabled before requesting a fresh measurement. */
		ret = regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
					 APSD_RERUN_BIT, APSD_RERUN_BIT);
		if (ret)
			goto fail;
		chip->apsd_started = true;
		chip->apsd_retries = 0;
	} else if (!apsd) {
		chip->apsd_started = false;
		chip->apsd_retries = 0;
	}
	chip->usb_type = type;
	delay = retry ? HZ : 0;
	if (cc_step.recheck && (!delay || cc_step.recheck < delay))
		delay = cc_step.recheck;
	if (delay && !chip->cc_paused)
		mod_delayed_work(system_wq, &chip->status_change_work, delay);
	power_supply_changed(chip->chg_psy);
	goto unlock;

fail:
	chip->apsd_started = false;
	chip->apsd_retries = 0;
	chip->cc.state = SMB5_CC_DETACHED;
	chip->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	if (smb5_sink_vbus_present(chip)) {
		/* Keep pSnkStdby so a flat battery is not left to carry the system. */
		chip->source_current_limit = SDP_CURRENT_UA;
		icl = min(chip->source_current_limit, chip->user_current_limit);
		smb5_cc_icl_written(&chip->cc, icl,
				    smb5_apply_input_limit(chip, icl, false));
	} else {
		chip->source_current_limit = 0;
		regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
				   USBIN_SUSPEND_BIT, USBIN_SUSPEND_BIT);
	}
	dev_err_ratelimited(chip->dev, "Unable to apply input power limit: %d\n", ret);
	mod_delayed_work(system_wq, &chip->status_change_work, HZ);
unlock:
	mutex_unlock(&chip->lock);
}

static int smb5_set_float_voltage(struct smb5_chip *chip, int fv_uv)
{
	/* 3.6 V plus 10 mV per step, as in the vendor smb5 driver */
	if (fv_uv < FLOAT_VOLTAGE_MIN_UV || fv_uv > FLOAT_VOLTAGE_MAX_UV)
		return -EINVAL;

	return regmap_update_bits(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
				  FLOAT_VOLTAGE_SETTING_MASK,
				  (fv_uv - FLOAT_VOLTAGE_MIN_UV) / FLOAT_VOLTAGE_STEP_UV);
}

static void smb5_apply_charge_enable(struct smb5_chip *chip)
{
	smb5_set_prop_charging_enabled(chip, chip->user_charge_enable &&
					     chip->policy_charge_enable &&
					     !chip->suspended);
}

static int smb5_gauge_read(struct smb5_chip *chip, enum power_supply_property psp,
			   int *val)
{
	union power_supply_propval pval;
	int ret;

	ret = power_supply_get_property(chip->gauge, psp, &pval);
	if (!ret)
		*val = pval.intval;
	return ret;
}

static int smb5_zone_for_temp(int temp)
{
	int i;

	if (temp > POLICY_HOT_DC)
		return ZONE_TOO_HOT;
	if (temp < smb5_zones[ZONE_COLD].min_dc)
		return ZONE_TOO_COLD;
	for (i = ARRAY_SIZE(smb5_zones) - 1; i > 0; i--)
		if (temp >= smb5_zones[i].min_dc)
			break;
	return i;
}

/*
 * A zone with tighter limits applies at once. Moving back towards the
 * normal zone needs the temperature to be 2 degC inside the new zone.
 */
static int smb5_next_zone(struct smb5_chip *chip, int temp)
{
	int zone = smb5_zone_for_temp(temp);

	if (chip->zone == ZONE_NONE || zone == chip->zone)
		return zone;

	if (zone > chip->zone && zone <= ZONE_NORMAL) {
		zone = smb5_zone_for_temp(temp - POLICY_HYST_DC);
		if (zone < chip->zone)
			zone = chip->zone;
	} else if (zone < chip->zone && zone >= ZONE_NORMAL) {
		zone = smb5_zone_for_temp(temp + POLICY_HYST_DC);
		if (zone > chip->zone)
			zone = chip->zone;
	}

	return zone;
}

static void smb5_policy_reset(struct smb5_chip *chip)
{
	chip->batt_full = false;
	chip->sw_full_count = 0;
	chip->hw_full_count = 0;
	chip->recharge_count = 0;
	chip->over_count = 0;
	chip->rest_count = 0;
}

/* Limits for a battery the fuel gauge cannot vouch for, as in stock. */
static void smb5_policy_fallback(struct smb5_chip *chip)
{
	if (chip->gauge_ok || chip->zone != ZONE_NONE)
		dev_warn(chip->dev, "no fuel gauge data, charging limited to %d uV\n",
			 POLICY_FALLBACK_FV_UV);
	chip->gauge_ok = false;
	chip->zone = ZONE_NONE;
	chip->fv_set_uv = POLICY_FALLBACK_FV_UV;
	smb5_set_float_voltage(chip, chip->fv_set_uv);
	smb5_set_charge_current(chip, POLICY_FALLBACK_FCC_UA);
	smb5_policy_reset(chip);
	chip->policy_charge_enable = true;
}

static void smb5_policy_set_zone(struct smb5_chip *chip, int zone, int vbat)
{
	const struct smb5_temp_zone *z;
	int fcc, old_full_uv = INT_MAX;

	dev_info(chip->dev, "battery temperature zone %d -> %d\n", chip->zone, zone);
	if (chip->zone >= ZONE_COLD && chip->zone <= ZONE_WARM)
		old_full_uv = smb5_zones[chip->zone].full_uv;
	chip->zone = zone;
	chip->over_count = 0;
	if (zone == ZONE_TOO_HOT || zone == ZONE_TOO_COLD)
		return;

	z = &smb5_zones[zone];
	fcc = z->fcc_ua;
	if (chip->batt_info->constant_charge_current_max_ua > 0)
		fcc = min(fcc, chip->batt_info->constant_charge_current_max_ua);
	chip->fv_set_uv = z->fv_uv;
	smb5_set_float_voltage(chip, chip->fv_set_uv);
	smb5_set_charge_current(chip, fcc);

	/* A full state reached under a lower limit does not hold here. */
	if (chip->batt_full && old_full_uv < z->full_uv &&
	    vbat < z->fv_uv - z->recharge_uv)
		smb5_policy_reset(chip);
}

static void smb5_policy_work(struct work_struct *work)
{
	struct smb5_chip *chip = container_of(work, struct smb5_chip,
					     policy_work.work);
	const struct smb5_temp_zone *z;
	int temp, vbat, ibat, online = 0, zone, gauge_status;
	unsigned int stat;
	bool was_full, ovp = false;

	mutex_lock(&chip->lock);
	if (!chip->ready || chip->suspended)
		goto unlock;

	if (!chip->gauge) {
		chip->gauge = power_supply_get_by_reference(dev_fwnode(chip->dev),
							    "qcom,fuel-gauge");
		if (IS_ERR_OR_NULL(chip->gauge))
			chip->gauge = NULL;
		else
			dev_info(chip->dev, "charging policy uses %s\n",
				 chip->gauge->desc->name);
	}

	if (!chip->gauge ||
	    smb5_gauge_read(chip, POWER_SUPPLY_PROP_TEMP, &temp) ||
	    smb5_gauge_read(chip, POWER_SUPPLY_PROP_VOLTAGE_NOW, &vbat) ||
	    smb5_gauge_read(chip, POWER_SUPPLY_PROP_CURRENT_NOW, &ibat) ||
	    temp < -400 || temp > 1000 || vbat < 2000000 || vbat > 5000000) {
		smb5_policy_fallback(chip);
		goto apply;
	}
	chip->gauge_ok = true;

	zone = smb5_next_zone(chip, temp);
	if (zone != chip->zone)
		smb5_policy_set_zone(chip, zone, vbat);

	if (zone == ZONE_TOO_HOT || zone == ZONE_TOO_COLD) {
		chip->policy_charge_enable = false;
		goto apply;
	}

	smb5_get_prop_usb_online(chip, &online);
	if (!online) {
		smb5_policy_reset(chip);
		chip->policy_charge_enable = true;
		goto apply;
	}

	z = &smb5_zones[zone];
	was_full = chip->batt_full;

	/* Near the reduced float voltage of the cold and warm zones: pause. */
	if ((zone == ZONE_COLD || zone == ZONE_WARM) &&
	    vbat >= chip->fv_set_uv - POLICY_OVP_MARGIN_UV)
		ovp = true;

	if (vbat >= z->over_uv + POLICY_HARD_STOP_UV) {
		/* Never charge a cell that is this high, whatever the current. */
		if (!chip->batt_full)
			dev_warn(chip->dev, "battery at %d uV, charging stopped\n", vbat);
		chip->batt_full = true;
		chip->recharge_count = 0;
	} else if (!chip->batt_full) {
		/* Full by gauge: above the limit with only a small current left. */
		if (vbat > z->full_uv && ibat <= POLICY_ITERM_UA) {
			if (++chip->sw_full_count >
			    (ibat > 0 ? POLICY_SW_FULL_COUNT : 2 * POLICY_SW_FULL_COUNT))
				chip->batt_full = true;
		} else {
			chip->sw_full_count = 0;
		}

		/* Full by the gauge's own judgement, not part of stock. */
		if (!smb5_gauge_read(chip, POWER_SUPPLY_PROP_STATUS, &gauge_status) &&
		    gauge_status == POWER_SUPPLY_STATUS_FULL)
			chip->batt_full = true;

		/* Full by voltage: the cell reached the float voltage. */
		if (vbat >= z->fv_uv + POLICY_HW_FULL_INC_UV) {
			if (++chip->hw_full_count >= POLICY_HW_FULL_COUNT)
				chip->batt_full = true;
		} else {
			chip->hw_full_count = 0;
		}

		/* Over the cell limit while charging: lower the float voltage. */
		if (!chip->batt_full && vbat >= z->over_uv) {
			if (++chip->over_count > POLICY_OVER_COUNT &&
			    chip->fv_set_uv - POLICY_FV_STEP_UV >= FLOAT_VOLTAGE_MIN_UV) {
				chip->over_count = 0;
				chip->fv_set_uv -= POLICY_FV_STEP_UV;
				smb5_set_float_voltage(chip, chip->fv_set_uv);
				dev_info(chip->dev, "battery at %d uV, float voltage now %d uV\n",
					 vbat, chip->fv_set_uv);
			}
		} else {
			chip->over_count = 0;
		}
	} else if (vbat < z->fv_uv - z->recharge_uv) {
		if (++chip->recharge_count > POLICY_RECHARGE_COUNT)
			smb5_policy_reset(chip);
	} else {
		chip->recharge_count = 0;
	}

	if (chip->batt_full != was_full)
		dev_info(chip->dev, "battery %s at %d uV, %d uA, %d.%d degC\n",
			 chip->batt_full ? "full" : "recharging", vbat, ibat,
			 temp / 10, abs(temp % 10));

	chip->policy_charge_enable = !chip->batt_full && !ovp;

	/*
	 * No current to speak of: either the charger ended the cycle early
	 * and is restarted, as stock does, or the cell has reached the level
	 * at which nothing more is charged and counts as full.
	 */
	if (chip->policy_charge_enable && chip->user_charge_enable &&
	    ibat <= POLICY_ITERM_UA / 2) {
		if (vbat > z->full_uv - POLICY_REST_MARGIN_UV) {
			if (++chip->rest_count > 2 * POLICY_SW_FULL_COUNT) {
				chip->batt_full = true;
				chip->policy_charge_enable = false;
				dev_info(chip->dev, "battery full at rest, %d uV\n", vbat);
			}
		} else {
			chip->rest_count = 0;
			if (!regmap_read(chip->regmap,
					 chip->base + BATTERY_CHARGER_STATUS_8, &stat) &&
			    (stat & PRE_TERM_BIT)) {
				dev_dbg(chip->dev, "charge ended early at %d uV, restarting\n",
					vbat);
				smb5_set_prop_charging_enabled(chip, 0);
				msleep(100);
			}
		}
	} else {
		chip->rest_count = 0;
	}

apply:
	smb5_apply_charge_enable(chip);
	smb5_get_prop_usb_online(chip, &online);
	WRITE_ONCE(chip->charging, online && chip->policy_charge_enable &&
				   chip->user_charge_enable);

	/*
	 * Only this work pets the charger watchdog. If it stops running, the
	 * watchdog bites and the PM8150B stops charging by itself.
	 */
	regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET,
		     BARK_BITE_WDOG_PET_BIT);
	mod_delayed_work(system_wq, &chip->policy_work, POLICY_INTERVAL);
unlock:
	mutex_unlock(&chip->lock);
}

/*
 * The battery is only charged under supervision. Like the wakelock of the
 * stock firmware, refuse to sleep while it is being charged; once it is
 * full the system may sleep.
 */
static int smb5_pm_notify(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	struct smb5_chip *chip = container_of(nb, struct smb5_chip, pm_nb);

	if ((action == PM_SUSPEND_PREPARE || action == PM_HIBERNATION_PREPARE) &&
	    READ_ONCE(chip->charging)) {
		dev_info_ratelimited(chip->dev, "battery is charging, not suspending\n");
		return notifier_from_errno(-EBUSY);
	}

	return NOTIFY_DONE;
}

static void smb5_stop(void *data)
{
	struct smb5_chip *chip = data;

	mutex_lock(&chip->lock);
	chip->ready = false;
	mutex_unlock(&chip->lock);
	cancel_delayed_work_sync(&chip->status_change_work);
	if (chip->policy) {
		cancel_delayed_work_sync(&chip->policy_work);
		smb5_set_float_voltage(chip, POLICY_FALLBACK_FV_UV);
		if (chip->gauge)
			power_supply_put(chip->gauge);
		unregister_pm_notifier(&chip->pm_nb);
	}
	smb5_set_prop_charging_enabled(chip, 0);
	/*
	 * Leave USB powering the system at the 500 mA default, as the init
	 * sequence does; with USBIN suspended, a phone left on the cable
	 * would drain its battery.
	 */
	smb5_set_current_limit(chip, SDP_CURRENT_UA, true);
	regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
			   USBIN_SUSPEND_BIT, 0);
}

static int smb5_get_iio_chan(struct smb5_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int online, rc;

	/* USB may power the system while battery charging is inhibited or full. */
	rc = smb5_get_prop_usb_online(chip, &online);
	if (rc)
		return rc;
	if (!online) {
		*val = 0;
		return 0;
	}

	if (IS_ERR(chan)) {
		dev_err(chip->dev, "Failed to chan, err = %li", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	return iio_read_channel_processed(chan, val);
}

static int smb5_get_prop_health(struct smb5_chip *chip, int *val)
{
	int rc;
	unsigned int stat;

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status rc=%d\n", rc);
		return rc;
	}

	if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_7_REG,
			 &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status 7 rc=%d\n", rc);
		return rc;
	}

	if (stat & BAT_TEMP_STATUS_TOO_COLD_BIT)
		*val = POWER_SUPPLY_HEALTH_COLD;
	else if (stat & BAT_TEMP_STATUS_TOO_HOT_BIT)
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat & BAT_TEMP_STATUS_COLD_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_COOL;
	else if (stat & BAT_TEMP_STATUS_HOT_SOFT_BIT)
		*val = POWER_SUPPLY_HEALTH_WARM;
	else
		*val = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int smb5_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);
	unsigned int raw;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return smb5_get_current_limit(chip, &val->intval);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = regmap_read(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG, &raw);
		if (!ret)
			val->intval = raw * CURRENT_SCALE_FACTOR;
		return ret;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = smb5_get_iio_chan(chip, chip->usb_in_i_chan, &val->intval);
		/* The current-sense channel is 0.2 V/A, reported in microvolts. */
		if (!ret)
			val->intval *= 5;
		return ret;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb5_get_iio_chan(chip, chip->usb_in_v_chan,
					 &val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb5_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb5_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb5_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = READ_ONCE(chip->usb_type);
		return 0;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		return smb5_get_prop_charging_enabled(chip, &val->intval);
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);

	guard(mutex)(&chip->lock);
	if (!chip->ready)
		return -EAGAIN;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval < 0 || val->intval > CURRENT_MAX_UA)
			return -EINVAL;
		chip->user_current_limit = val->intval;
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
		return 0;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chip->user_charge_enable = !!val->intval;
		smb5_apply_charge_enable(chip);
		return 0;
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t smb5_handle_batt_overvoltage(int irq, void *data)
{
	struct smb5_chip *chip = data;
	unsigned int status;

	regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_2,
		    &status);

	if (status & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		/* The hardware stops charging automatically */
		dev_err(chip->dev, "battery overvoltage detected\n");
		power_supply_changed(chip->chg_psy);
	}

	return IRQ_HANDLED;
}

/*
 * Hard IRQ context: a status pass that is running or about to run sees the
 * edge without waiting for the thread, which may be blocked on chip->lock.
 */
static irqreturn_t smb5_usb_plugin_edge(int irq, void *data)
{
	struct smb5_chip *chip = data;

	atomic_inc(&chip->plug_gen);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t smb5_handle_usb_plugin(int irq, void *data)
{
	struct smb5_chip *chip = data;
	unsigned int icl;
	int rc;

	/* Keep a quick detach/reattach from reusing the last cable's result. */
	mutex_lock(&chip->lock);
	chip->apsd_started = false;
	chip->apsd_retries = 0;
	if (!chip->has_tcpm) {
		/* From here until a classification of the new attach: 500 mA. */
		chip->cc.state = SMB5_CC_DETACHED;
		if (chip->ready && (chip->source_current_limit > SDP_CURRENT_UA ||
				    chip->cc.icl_unconfirmed)) {
			chip->source_current_limit = SDP_CURRENT_UA;
			icl = min(SDP_CURRENT_UA, chip->user_current_limit);
			rc = smb5_set_current_limit(chip, icl, false);
			smb5_cc_icl_written(&chip->cc, icl, rc);
			if (rc)
				dev_err_ratelimited(chip->dev,
						    "Couldn't lower the input limit on plug: %d\n",
						    rc);
		}
	}
	if (chip->ready)
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
	if (chip->ready && chip->policy)
		mod_delayed_work(system_wq, &chip->policy_work, 0);
	mutex_unlock(&chip->lock);
	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_usb_source_change(int irq, void *data)
{
	struct smb5_chip *chip = data;

	/* A late APSD completion must refresh the cached type and allowance. */
	smb5_queue_status_work(chip);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_usb_icl_change(int irq, void *data)
{
	struct smb5_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_wdog_bark(int irq, void *data)
{
	struct smb5_chip *chip = data;
	int rc;

	power_supply_changed(chip->chg_psy);

	/* With the charging policy, only its work may pet the watchdog. */
	if (chip->policy) {
		dev_warn(chip->dev, "charger watchdog bark\n");
		return IRQ_HANDLED;
	}

	rc = regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET,
			  BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't pet the dog rc=%d\n", rc);

	return IRQ_HANDLED;
}

static const struct power_supply_desc smb5_psy_desc = {
	.name = "pm8150b_charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
 		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_C) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_PPS) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb5_properties,
	.num_properties = ARRAY_SIZE(smb5_properties),
	.get_property = smb5_get_property,
	.set_property = smb5_set_property,
	.property_is_writeable = smb5_property_is_writable,
	.external_power_changed = smb5_external_power_changed,
};

/* Init sequence derived from vendor downstream driver */
static const struct smb5_register smb5_init_seq[] = {
	/*
	 * Keep charging off until limits have been programmed, but let USB
	 * power the system at the 500 mA default meanwhile: probing can wait
	 * on the Type-C supplier, and a nearly empty battery must not carry
	 * the boot alone.
	 */
	{ .addr = CHARGING_ENABLE_CMD, .mask = CHARGING_ENABLE_CMD_BIT, .val = 0 },
	{ .addr = USBIN_CMD_IL, .mask = USBIN_SUSPEND_BIT, .val = 0 },
	{ .addr = USBIN_CURRENT_LIMIT_CFG, .mask = USBIN_CURRENT_LIMIT_MASK,
	  .val = SDP_CURRENT_UA / CURRENT_SCALE_FACTOR },
	{ .addr = AICL_RERUN_TIME_CFG, .mask = AICL_RERUN_TIME_MASK, .val = 0 },
	/* TCPM owns the Type-C and VCONN configuration. */
	{ .addr = TYPE_C_CFG, .mask = BC1P2_START_ON_CC_BIT, .val = 0 },
	/* Keep VBUS sourcing under software control, whichever supply provides it. */
	{ .addr = OTG_CFG, .mask = OTG_EN_SRC_CFG_BIT, .val = 0 },
	/*
	 * Use VBAT to determine the recharge threshold when battery is full
	 * rather than the state of charge.
	 */
	{ .addr = CHARGE_RCHG_SOC_THRESHOLD_CFG_REG,
	  .mask = CHARGE_RCHG_SOC_THRESHOLD_CFG_MASK,
	  .val = 98 },
	/* The input policy enables BC1.2 only when TCPM has no better limit. */
	{ .addr = USBIN_OPTIONS_1_CFG,
	  .mask = BC1P2_SRC_DETECT_BIT | HVDCP_AUTH_ALG_EN_CFG_BIT |
		  HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT | HVDCP_EN_BIT |
		  HVDCP_NO_AUTH_QC3_CFG_BIT,
	  .val = 0 },
	/* Set the default SDP charger type to a 500ma USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USBIN_MODE_CHG_BIT,
	  .val = USBIN_MODE_CHG_BIT },
	{ .addr = CMD_ICL_OVERRIDE,
	  .mask = ICL_OVERRIDE_BIT,
	  .val = 0 },
	{ .addr = USBIN_LOAD_CFG,
	  .mask = ICL_OVERRIDE_AFTER_APSD_BIT,
	  .val = 0 },
	/* Disable watchdog */
	{ .addr = SNARL_BARK_BITE_WD_CFG, .mask = 0xff, .val = 0 },
	{ .addr = WD_CFG,
	  .mask = WATCHDOG_TRIGGER_AFP_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
		  BARK_WDOG_INT_EN_BIT,
	  .val = 0 },
	/*
	 * Enable Automatic Input Current Limit, this will slowly ramp up the current
	 * When connected to a wall charger, and automatically stop when it detects
	 * the charger current limit (voltage drop?) or it reaches the programmed limit.
	 */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_ADC_EN_BIT
			| USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT,
	  .val = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_ADC_EN_BIT
			| USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT },
};

static int smb5_init_hw(struct smb5_chip *chip)
{
	int rc, i;
	for (i = 0; i < ARRAY_SIZE(smb5_init_seq); i++) {
		dev_dbg(chip->dev, "%d: Writing 0x%02x to 0x%02x\n", i,
			smb5_init_seq[i].val, smb5_init_seq[i].addr);
		rc = regmap_update_bits(chip->regmap,
					chip->base + smb5_init_seq[i].addr,
					smb5_init_seq[i].mask,
					smb5_init_seq[i].val);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "%s: init command %d failed\n",
					     __func__, i);
	}
	return 0;
}

static int smb5_init_irq(struct smb5_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq %s\n",
				     name);

	if (irq)
		*irq = irqnum;

	return 0;
}

static int smb5_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	struct smb5_chip *chip;
	int rc, irq, fv_uv;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->name = pdev->name;
	mutex_init(&chip->lock);
	chip->has_tcpm = device_property_present(chip->dev, "power-supplies");
	chip->user_current_limit = CURRENT_MAX_UA;
	chip->input_ceiling_ua = smb5_input_ceiling(max_input_ua);
	if (max_input_ua && max_input_ua < SDP_CURRENT_UA)
		dev_warn(chip->dev, "max_input_ua %u is below %d, using %d\n",
			 max_input_ua, SDP_CURRENT_UA, SDP_CURRENT_UA);
	if (max_input_ua)
		dev_info(chip->dev, "input current ceiling %u uA\n",
			 chip->input_ceiling_ua);

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	rc = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read base address\n");

	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usb_in_v_div_16");
	if (IS_ERR(chip->usb_in_v_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_v_chan),
				     "Couldn't get usb_in_v_div_16 IIO channel\n");

	chip->usb_in_i_chan = devm_iio_channel_get(chip->dev, "usb_in_i_uv");
	if (IS_ERR(chip->usb_in_i_chan)) {
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_i_chan),
				     "Couldn't get usb_in_i_uv IIO channel\n");
	}

	rc = smb5_init_hw(chip);
	if (rc < 0)
		return rc;

	supply_config.drv_data = chip;
	supply_config.fwnode = pdev->dev.fwnode;

	desc = devm_kzalloc(chip->dev, sizeof(smb5_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb5_psy_desc, sizeof(smb5_psy_desc));
	desc->name =
		devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger",
			       (const char *)device_get_match_data(chip->dev));
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");

	INIT_DELAYED_WORK(&chip->status_change_work, smb5_status_change_work);
	INIT_DELAYED_WORK(&chip->policy_work, smb5_policy_work);
	chip->policy = device_property_present(chip->dev, "qcom,fuel-gauge");
	chip->user_charge_enable = true;
	chip->policy_charge_enable = true;
	chip->zone = ZONE_NONE;
	if (chip->policy) {
		chip->pm_nb.notifier_call = smb5_pm_notify;
		rc = register_pm_notifier(&chip->pm_nb);
		if (rc)
			return rc;
	}
	rc = devm_add_action_or_reset(chip->dev, smb5_stop, chip);
	if (rc)
		return rc;

	/*
	 * With the charging policy, start from the limits for an unknown
	 * battery; the policy raises them once the fuel gauge has been read.
	 */
	fv_uv = chip->policy ? POLICY_FALLBACK_FV_UV :
			       chip->batt_info->voltage_max_design_uv;
	rc = smb5_set_float_voltage(chip, fv_uv);
	if (rc == -EINVAL)
		return dev_err_probe(chip->dev, rc, "Invalid battery voltage %d uV\n",
				     fv_uv);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set vbat max\n");
	chip->fv_set_uv = fv_uv;
	dev_info(chip->dev, "float voltage %d uV\n", fv_uv);

	rc = smb5_set_charge_current(chip, chip->policy ? POLICY_FALLBACK_FCC_UA :
		chip->batt_info->constant_charge_current_max_ua);
	if (rc)
		return dev_err_probe(chip->dev, rc, "Couldn't set battery current limit\n");

	if (chip->policy) {
		/* Termination current and charger watchdog as in stock. */
		rc = regmap_update_bits(chip->regmap,
					chip->base + TCCC_CHARGE_CURRENT_TERMINATION_CFG,
					TCCC_CHARGE_CURRENT_TERMINATION_MASK,
					POLICY_ITERM_UA / CURRENT_SCALE_FACTOR);
		if (!rc)
			rc = regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET,
					  BARK_BITE_WDOG_PET_BIT);
		if (!rc)
			rc = regmap_write(chip->regmap,
					  chip->base + SNARL_BARK_BITE_WD_CFG,
					  WDOG_POLICY_CFG);
		if (!rc)
			rc = regmap_update_bits(chip->regmap, chip->base + WD_CFG,
						WATCHDOG_TRIGGER_AFP_EN_BIT |
						WDOG_TIMER_EN_ON_PLUGIN_BIT |
						BARK_WDOG_INT_EN_BIT,
						WDOG_TIMER_EN_ON_PLUGIN_BIT |
						BARK_WDOG_INT_EN_BIT);
		if (rc)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't set up the charging policy\n");
	}

	rc = smb5_init_irq(chip, &irq, "bat-ov", smb5_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	chip->cable_irq = platform_get_irq_byname(pdev, "usbin-plugin");
	if (chip->cable_irq < 0)
		return chip->cable_irq;
	rc = devm_request_threaded_irq(chip->dev, chip->cable_irq,
				       smb5_usb_plugin_edge, smb5_handle_usb_plugin,
				       IRQF_ONESHOT, "usbin-plugin", chip);
	if (rc)
		return dev_err_probe(chip->dev, rc, "Couldn't request irq usbin-plugin\n");

	rc = smb5_init_irq(chip, &irq, "usbin-icl-change",
			   smb5_handle_usb_icl_change);
	if (rc < 0)
		return rc;
	/* Optional for compatibility with DTs predating source-change support. */
	irq = platform_get_irq_byname_optional(pdev, "usbin-src-change");
	if (irq >= 0) {
		rc = devm_request_threaded_irq(chip->dev, irq, NULL,
					       smb5_handle_usb_source_change,
					       IRQF_ONESHOT, "usbin-src-change", chip);
		if (rc)
			return dev_err_probe(chip->dev, rc,
					     "Couldn't request source-change IRQ\n");
	} else if (irq != -ENXIO) {
		return irq;
	}

	rc = smb5_init_irq(chip, &irq, "wdog-bark", smb5_handle_wdog_bark);
	if (rc < 0)
		return rc;

	rc = devm_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc, "Couldn't set wake irq\n");

	platform_set_drvdata(pdev, chip);

	rc = smb5_set_prop_charging_enabled(chip, 1);
	if (rc)
		return rc;

	mutex_lock(&chip->lock);
	chip->ready = true;
	if (chip->policy)
		mod_delayed_work(system_wq, &chip->policy_work, 0);
	mutex_unlock(&chip->lock);
	smb5_queue_status_work(chip);

	return 0;
}

/* Nothing supervises the battery while asleep or after shutdown. */
static int smb5_suspend(struct device *dev)
{
	struct smb5_chip *chip = dev_get_drvdata(dev);

	if (!chip->has_tcpm) {
		mutex_lock(&chip->lock);
		chip->cc_paused = true;
		mutex_unlock(&chip->lock);
		cancel_delayed_work_sync(&chip->status_change_work);
	}

	if (!chip->policy)
		return 0;

	mutex_lock(&chip->lock);
	chip->suspended = true;
	smb5_apply_charge_enable(chip);
	mutex_unlock(&chip->lock);
	dev_dbg(dev, "charging off while suspended\n");
	cancel_delayed_work_sync(&chip->policy_work);

	return 0;
}

static int smb5_resume(struct device *dev)
{
	struct smb5_chip *chip = dev_get_drvdata(dev);

	if (!chip->has_tcpm) {
		/* Nothing watched the source while asleep: classify it again. */
		mutex_lock(&chip->lock);
		chip->cc_paused = false;
		if (chip->cc.state != SMB5_CC_DETACHED)
			smb5_cc_restart(&chip->cc, jiffies);
		if (chip->ready)
			mod_delayed_work(system_wq, &chip->status_change_work, 0);
		mutex_unlock(&chip->lock);
	}

	if (!chip->policy)
		return 0;

	mutex_lock(&chip->lock);
	chip->suspended = false;
	if (chip->ready)
		mod_delayed_work(system_wq, &chip->policy_work, 0);
	mutex_unlock(&chip->lock);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(smb5_pm_ops, smb5_suspend, smb5_resume);

static void smb5_shutdown(struct platform_device *pdev)
{
	struct smb5_chip *chip = platform_get_drvdata(pdev);

	if (chip && chip->policy)
		smb5_set_float_voltage(chip, POLICY_FALLBACK_FV_UV);
}

static const struct of_device_id smb5_match_id_table[] = {
	{ .compatible = "qcom,pm8150b-charger", .data = "pm8150b" },
	{ /* sentinal */ }
};
/* Test-only: no MODULE_DEVICE_TABLE, so the module is never autoloaded. */

static struct platform_driver qcom_spmi_smb5 = {
	.probe = smb5_probe,
	.shutdown = smb5_shutdown,
	.driver = {
		.name = "qcom-pm8150b-charger",
		.of_match_table = smb5_match_id_table,
		.pm = pm_sleep_ptr(&smb5_pm_ops),
		},
};

module_platform_driver(qcom_spmi_smb5);

MODULE_AUTHOR("Caleb Connolly <caleb.connolly@linaro.org>");
MODULE_DESCRIPTION("Qualcomm SMB5 Charger Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_CONSUMER");

#if IS_ENABLED(CONFIG_CHARGER_QCOM_SMB5_KUNIT_TEST)
#include "qcom_pm8150b_charger_test.c"
#endif
