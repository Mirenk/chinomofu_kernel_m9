/*
 *
 * Copyright (C) 2011 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <linux/gpio.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>
#include <linux/power/htc_battery_core.h>
#include <linux/alarmtimer.h>
#include <linux/htc_flags.h>
#if 0//FIXME
#include <linux/android_alarm.h>
#include <mach/devices_cmdline.h>
#include <mach/devices_dtb.h>
#include <linux/qpnp/qpnp-charger.h>
#endif

extern int smbchg_set_otg_pulse_skip(int val);
extern int smbchg_get_otg_pulse_skip(void);

#define USB_MA_0       (0)
#define USB_MA_500     (500)
#define USB_MA_1500    (1500)
#define USB_MA_1600    (1600)

// TODO: need USB owner to provide a common API
#define DWC3_DCP	2

static ssize_t htc_battery_show_property(struct device *dev,
					struct device_attribute *attr,
					char *buf);

static ssize_t htc_battery_rt_attr_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);

static int htc_power_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val);
#if 0//FIXME
static int htc_power_usb_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val);
#endif
static int htc_battery_property_is_writeable(struct power_supply *psy,
				enum power_supply_property psp);
#if 0//FIXME

static int htc_power_property_is_writeable(struct power_supply *psy,
				enum power_supply_property psp);
#endif
static int htc_battery_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

static int htc_battery_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val);

static ssize_t htc_battery_charger_ctrl_timer(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count);
#if 0//FIXME
extern int htc_battery_is_support_qc20(void);
extern int htc_battery_check_cable_type_from_usb(void);
extern int board_ftm_mode(void);
#endif

#if 1
#define HTC_BATTERY_ATTR(_name)                                             \
{                                                                           \
	.attr = { .name = #_name, .mode = S_IRUGO},  \
	.show = htc_battery_show_property,                                  \
	.store = NULL,                                                      \
}
#else
#define HTC_BATTERY_ATTR(_name)                                             \
{                                                                           \
	.attr = { .name = #_name, .mode = S_IRUGO, .owner = THIS_MODULE },  \
	.show = htc_battery_show_property,                                  \
	.store = NULL,                                                      \
}
#endif

struct htc_battery_core_info {
	int present;
	int htc_charge_full;
	unsigned long update_time;
	struct mutex info_lock;
	struct battery_info_reply rep;
	struct htc_battery_core func;
};

static struct htc_battery_core_info battery_core_info;
static int battery_register = 1;
static int battery_over_loading;

static struct alarm batt_charger_ctrl_alarm;
static struct work_struct batt_charger_ctrl_work;
struct workqueue_struct *batt_charger_ctrl_wq;
static unsigned int charger_ctrl_stat;
static unsigned int ftm_charger_ctrl_stat;
static unsigned int safety_timer_disable_stat;
static unsigned int navigation_stat;

static int test_power_monitor;
static int test_ftm_mode;

extern int chg_limit_reason;
extern int chg_dis_reason;

static enum power_supply_property htc_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_OVERLOAD,
	POWER_SUPPLY_PROP_CURRENT_NOW,
#if 0//FIXME
	POWER_SUPPLY_PROP_USB_OVERHEAT,
#endif
};

static enum power_supply_property htc_power_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_TYPE,
};

static char *supply_list[] = {
	"battery",
};

static struct power_supply htc_power_supplies[] = {
	{
		.name = "battery",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = htc_battery_properties,
		.num_properties = ARRAY_SIZE(htc_battery_properties),
		.get_property = htc_battery_get_property,
		.set_property = htc_battery_set_property,
		.property_is_writeable = htc_battery_property_is_writeable,
	},
#if 0//FIXME
	{
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
		.set_property = htc_power_usb_set_property,
		.property_is_writeable = htc_power_property_is_writeable,
	},
#endif
	{
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
	},
	{
		.name = "wireless",
		.type = POWER_SUPPLY_TYPE_WIRELESS,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
	},
};

static BLOCKING_NOTIFIER_HEAD(wireless_charger_notifier_list);
int register_notifier_wireless_charger(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&wireless_charger_notifier_list, nb);
}

int unregister_notifier_wireless_charger(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&wireless_charger_notifier_list, nb);
}

/*
 *  For Off-mode charging animation,
 *  add a function for display driver to inform the charging animation mode.
 */
static int zcharge_enabled;
int htc_battery_get_zcharge_mode(void)
{
#if 0	/* Off-mode charging animation is defult on */
	return zcharge_enabled;
#else
	return 1;
#endif
}
static int __init enable_zcharge_setup(char *str)
{
	int rc;
	unsigned long cal;

	rc = strict_strtoul(str, 10, &cal);

	if (rc)
		return rc;

	zcharge_enabled = cal;
	return 1;
}
__setup("enable_zcharge=", enable_zcharge_setup);

static int htc_battery_get_charging_status(void)
{
	enum charger_type_t charger;
	int ret;

	mutex_lock(&battery_core_info.info_lock);
	charger = battery_core_info.rep.charging_source;
	mutex_unlock(&battery_core_info.info_lock);

	if (battery_core_info.rep.batt_id == 255)
		charger = CHARGER_UNKNOWN;

	switch (charger) {
	case CHARGER_BATTERY:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case CHARGER_USB:
	case CHARGER_AC:
	case CHARGER_9V_AC:
	case CHARGER_WIRELESS:
	case CHARGER_MHL_AC:
	case CHARGER_DETECTING:
	case CHARGER_UNKNOWN_USB:
	case CHARGER_NOTIFY:
		if (battery_core_info.htc_charge_full)
			ret = POWER_SUPPLY_STATUS_FULL;
		else {
			if (battery_core_info.rep.charging_enabled != 0)
				ret = POWER_SUPPLY_STATUS_CHARGING;
			else
				ret = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
       case CHARGER_MHL_UNKNOWN:
               ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
               break;
       case CHARGER_MHL_100MA:
       case CHARGER_MHL_500MA:
       case CHARGER_MHL_900MA:
       case CHARGER_MHL_1500MA:
       case CHARGER_MHL_2000MA:
		if (battery_core_info.rep.charging_enabled != 0)
			ret = POWER_SUPPLY_STATUS_CHARGING;
		else
			ret = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		ret = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return ret;
}

static ssize_t htc_battery_show_batt_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return battery_core_info.func.func_show_batt_attr(attr, buf);
}

static ssize_t htc_battery_show_cc_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return battery_core_info.func.func_show_cc_attr(attr, buf);
}

static ssize_t htc_battery_show_htc_extension_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	if (battery_core_info.func.func_show_htc_extension_attr)
		return battery_core_info.func.func_show_htc_extension_attr(attr, buf);
	return 0;
}

static ssize_t htc_charger_type_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return battery_core_info.func.func_show_charger_type_attr(attr, buf);
}

static ssize_t htc_thermal_batt_temp_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return battery_core_info.func.func_show_thermal_batt_temp_attr(attr, buf);
}

static ssize_t htc_batt_bidata_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return battery_core_info.func.func_show_batt_bidata_attr(attr, buf);
}

static ssize_t htc_consist_data_attr(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        return battery_core_info.func.func_show_consist_data_attr(attr, buf);
}

static ssize_t htc_cycle_data_attr(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        return battery_core_info.func.func_show_cycle_data_attr(attr, buf);
}

static ssize_t htc_battery_set_delta(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long delta = 0;

	delta = simple_strtoul(buf, NULL, 10);

	if (delta > 100)
		return -EINVAL;

	return count;
}

static ssize_t htc_battery_debug_flag(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long debug_flag;
	debug_flag = simple_strtoul(buf, NULL, 10);

	if (debug_flag > 100 || debug_flag == 0)
		return -EINVAL;

	return 0;
}

static ssize_t htc_battery_set_full_level(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int rc = 0;
	unsigned long percent = 100;

	rc = strict_strtoul(buf, 10, &percent);
	if (rc)
		return rc;

	if (percent > 100 || percent == 0)
		return -EINVAL;

	if (!battery_core_info.func.func_set_full_level) {
		BATT_ERR("No set full level function!");
		return -ENOENT;
	}

	battery_core_info.func.func_set_full_level(percent);

	return count;
}

static ssize_t htc_battery_set_full_level_dis_batt_chg(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int rc = 0;
	unsigned long percent = 100;

	rc = strict_strtoul(buf, 10, &percent);
	if (rc)
		return rc;

	if (percent > 100 || percent == 0)
		return -EINVAL;

	if (!battery_core_info.func.func_set_full_level_dis_batt_chg) {
		BATT_ERR("No set full level (disable battery charging only) function!");
		return -ENOENT;
	}

	battery_core_info.func.func_set_full_level_dis_batt_chg(percent);

	return count;
}

int htc_battery_charger_disable()
{
	int rc = 0;

	if (!battery_core_info.func.func_charger_control) {
		BATT_ERR("No charger control function!");
		return -ENOENT;
	}
	rc = battery_core_info.func.func_charger_control(STOP_CHARGER);
	if (rc < 0)
		BATT_ERR("charger control failed!");

	return rc;
}

int htc_battery_pwrsrc_disable()
{
	int rc = 0;

	if (!battery_core_info.func.func_charger_control) {
		BATT_ERR("No charger control function!");
		return -ENOENT;
	}
	rc = battery_core_info.func.func_charger_control(DISABLE_PWRSRC);
	if (rc < 0)
		BATT_ERR("charger control failed!");

	return rc;
}

int htc_battery_charger_switch_internal(int enable)
{
	int rc = 0;

	if (!battery_core_info.func.func_charger_control) {
		BATT_ERR("No charger control function!");
		return -ENOENT;
	}

	switch (enable) {
	case ENABLE_PWRSRC_FINGERPRINT:
	case DISABLE_PWRSRC_FINGERPRINT:
		rc = battery_core_info.func.func_charger_control(enable);
		break;
	default:
		pr_info("%s: invalid type, enable=%d\n", __func__, enable);
		return -EINVAL;
	}

	if (rc < 0)
		BATT_ERR("charger control failed!");

	return rc;
}

int htc_battery_set_max_input_current(int target_ma)
{
	int rc = 0;

	if (!battery_core_info.func.func_set_max_input_current) {
		BATT_ERR("No max input current function!");
		return -ENOENT;
	}
	rc = battery_core_info.func.func_set_max_input_current(target_ma);
	if (rc < 0)
		BATT_ERR("max input current control failed!");

	return rc;
}

static ssize_t htc_battery_charger_stat(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int i = 0;

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", charger_ctrl_stat);

	return i;
}

static ssize_t htc_battery_charger_switch(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long enable = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &enable);
	if (rc)
		return rc;

	BATT_EMBEDDED("Set charger_control:%lu", enable);
	if (enable >= END_CHARGER)
		return -EINVAL;

	if (!battery_core_info.func.func_charger_control) {
		BATT_ERR("No charger control function!");
		return -ENOENT;
	}

	rc = battery_core_info.func.func_charger_control(enable);
	if (rc < 0) {
		BATT_ERR("charger control failed!");
		return rc;
	}
	charger_ctrl_stat = enable;

	alarm_cancel(&batt_charger_ctrl_alarm);

	return count;
}

static ssize_t htc_battery_ftm_charger_stat(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int i = 0;

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", ftm_charger_ctrl_stat);

	return i;
}

static ssize_t htc_battery_ftm_charger_switch(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long enable = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &enable);
	if (rc)
		return rc;

	BATT_LOG("Set charger_control:%lu", enable);
	if (enable >= FTM_END_CHARGER)
		return -EINVAL;

	if (!battery_core_info.func.func_ftm_charger_control) {
		BATT_ERR("No charger control function!");
		return -ENOENT;
	}

	rc = battery_core_info.func.func_ftm_charger_control(enable);
	if (rc < 0) {
		BATT_ERR("charger control failed!");
		return rc;
	}
	ftm_charger_ctrl_stat = enable;

	alarm_cancel(&batt_charger_ctrl_alarm);

	return count;
}

static ssize_t htc_battery_safety_timer_disable_stat(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int i = 0;

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", safety_timer_disable_stat);

	return i;
}

static ssize_t htc_battery_safety_timer_disable_switch(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long disable = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &disable);
	if (rc)
		return rc;

	BATT_LOG("Set safety_timer_disable:%lu", disable);

	if (!battery_core_info.func.func_safety_timer_disable) {
		BATT_ERR("No safety timer disable function!");
		return -ENOENT;
	}

	rc = battery_core_info.func.func_safety_timer_disable((int)disable);
	if (rc < 0) {
		BATT_ERR("safety timer disable failed!");
		return rc;
	}
	safety_timer_disable_stat = disable;

	//alarm_cancel(&batt_charger_ctrl_alarm);

	return count;
}

static ssize_t htc_battery_set_phone_call(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long phone_call = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &phone_call);
	if (rc)
		return rc;

	BATT_LOG("set context phone_call=%lu", phone_call);

	if (!battery_core_info.func.func_context_event_handler) {
		BATT_ERR("No context_event_notify function!");
		return -ENOENT;
	}

	if (phone_call)
		battery_core_info.func.func_context_event_handler(EVENT_TALK_START);
	else
		battery_core_info.func.func_context_event_handler(EVENT_TALK_STOP);

	return count;
}

static ssize_t htc_battery_set_net_call(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long net_call = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &net_call);
	if (rc)
		return rc;

	BATT_LOG("set context net_call=%lu", net_call);

	if (!battery_core_info.func.func_context_event_handler) {
		BATT_ERR("No context_event_notify function!");
		return -ENOENT;
	}

	if (net_call)
		battery_core_info.func.func_context_event_handler(EVENT_NET_TALK_START);
	else
		battery_core_info.func.func_context_event_handler(EVENT_NET_TALK_STOP);

	return count;
}

static ssize_t htc_battery_set_play_music(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long play_music = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &play_music);
	if (rc)
		return rc;

	BATT_LOG("set context play music=%lu", play_music);

	if (!battery_core_info.func.func_context_event_handler) {
		BATT_ERR("No context_event_notify function!");
		return -ENOENT;
	}

	if (play_music)
		battery_core_info.func.func_context_event_handler(EVENT_MUSIC_START);
	else
		battery_core_info.func.func_context_event_handler(EVENT_MUSIC_STOP);

	return count;
}

static ssize_t htc_battery_set_network_search(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long network_search = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &network_search);
	if (rc)
		return rc;

	BATT_LOG("Set context network_search=%lu", network_search);

	if (!battery_core_info.func.func_context_event_handler) {
		BATT_ERR("No context_event_notify function!");
		return -ENOENT;
	}

	if (network_search) {
		battery_core_info.func.func_context_event_handler(
									EVENT_NETWORK_SEARCH_START);
	} else {
		battery_core_info.func.func_context_event_handler(
									EVENT_NETWORK_SEARCH_STOP);
	}

	return count;
}

static ssize_t htc_battery_navigation_stat(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int i = 0;

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", navigation_stat);

	return i;
}

static ssize_t htc_battery_set_navigation(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long navigation = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &navigation);
	if (rc)
		return rc;

	BATT_LOG("Set context navigation=%lu", navigation);

	if (!battery_core_info.func.func_context_event_handler) {
		BATT_ERR("No context_event_notify function!");
		return -ENOENT;
	}

	navigation_stat = (unsigned int)navigation;

	if (navigation) {
		battery_core_info.func.func_context_event_handler(
									EVENT_NAVIGATION_START);
	} else {
		battery_core_info.func.func_context_event_handler(
									EVENT_NAVIGATION_STOP);
	}

	return count;
}
static ssize_t htc_battery_set_context_event(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long event = 0;
	int rc = 0;

	rc = strict_strtoul(buf, 10, &event);
	if (rc)
		return rc;

	BATT_LOG("Set context event = %lu", event);

	if (!battery_core_info.func.func_context_event_handler) {
		BATT_ERR("No context_event_notify function!");
		return -ENOENT;
	}

	battery_core_info.func.func_context_event_handler(event);

	return count;
}

static ssize_t htc_battery_trigger_store_battery_data(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int rc = 0;
	unsigned long trigger_flag = 0;

	rc = strict_strtoul(buf, 10, &trigger_flag);
	if (rc)
		return rc;

	BATT_LOG("Set context trigger_flag = %lu", trigger_flag);

	if((trigger_flag != 0) && (trigger_flag != 1))
		return -EINVAL;

	if (!battery_core_info.func.func_trigger_store_battery_data) {
		BATT_ERR("No set trigger store battery data function!");
		return -ENOENT;
	}

	battery_core_info.func.func_trigger_store_battery_data(trigger_flag);

	return count;
}

static ssize_t htc_battery_qb_mode_shutdown_status(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int rc = 0;
	unsigned long trigger_flag = 0;

	rc = strict_strtoul(buf, 10, &trigger_flag);
	if (rc)
		return rc;

	BATT_LOG("Set context trigger_flag = %lu", trigger_flag);

	if((trigger_flag != 0) && (trigger_flag != 1))
		return -EINVAL;

	if (!battery_core_info.func.func_qb_mode_shutdown_status) {
		BATT_ERR("No set trigger qb mode shutdown status function!");
		return -ENOENT;
	}

	battery_core_info.func.func_qb_mode_shutdown_status(trigger_flag);

	return count;
}

static struct device_attribute htc_battery_attrs[] = {
	HTC_BATTERY_ATTR(batt_id),
	HTC_BATTERY_ATTR(batt_vol),
	HTC_BATTERY_ATTR(batt_temp),
	HTC_BATTERY_ATTR(batt_current),
	HTC_BATTERY_ATTR(charging_source),
	HTC_BATTERY_ATTR(charging_enabled),
	HTC_BATTERY_ATTR(full_bat),
	HTC_BATTERY_ATTR(over_vchg),
	HTC_BATTERY_ATTR(batt_state),
	HTC_BATTERY_ATTR(batt_cable_in),
	HTC_BATTERY_ATTR(usb_temp),
	HTC_BATTERY_ATTR(usb_overheat),

	__ATTR(batt_attr_text, S_IRUGO, htc_battery_show_batt_attr, NULL),
	__ATTR(batt_power_meter, S_IRUGO, htc_battery_show_cc_attr, NULL),
	__ATTR(htc_extension, S_IRUGO, htc_battery_show_htc_extension_attr, NULL),
	__ATTR(charger_type, S_IRUGO, htc_charger_type_attr, NULL),
	__ATTR(thermal_batt_temp, S_IRUGO, htc_thermal_batt_temp_attr, NULL),
	__ATTR(htc_batt_data, S_IRUGO, htc_batt_bidata_attr, NULL),
	__ATTR(consist_data, S_IRUGO, htc_consist_data_attr, NULL),
	__ATTR(cycle_data, S_IRUGO, htc_cycle_data_attr, NULL),
};

static struct device_attribute htc_set_delta_attrs[] = {
	__ATTR(delta, S_IWUSR | S_IWGRP, NULL, htc_battery_set_delta),
	__ATTR(full_level, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_full_level),
	__ATTR(full_level_dis_batt_chg, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_full_level_dis_batt_chg),
	__ATTR(batt_debug_flag, S_IWUSR | S_IWGRP, NULL,
		htc_battery_debug_flag),
	__ATTR(charger_control, S_IWUSR | S_IWGRP, htc_battery_charger_stat,
		htc_battery_charger_switch),
	__ATTR(charger_timer, S_IWUSR | S_IWGRP, NULL,
		htc_battery_charger_ctrl_timer),
	__ATTR(phone_call, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_phone_call),
	__ATTR(net_call, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_net_call),
	__ATTR(play_music, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_play_music),
	__ATTR(network_search, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_network_search),
	__ATTR(navigation, S_IWUSR | S_IWGRP, htc_battery_navigation_stat,
		htc_battery_set_navigation),
	__ATTR(context_event, S_IWUSR | S_IWGRP, NULL,
		htc_battery_set_context_event),
	__ATTR(store_battery_data, S_IWUSR | S_IWGRP, NULL,
		htc_battery_trigger_store_battery_data),
	__ATTR(qb_mode_shutdown, S_IWUSR | S_IWGRP, NULL,
		htc_battery_qb_mode_shutdown_status),
	__ATTR(ftm_charger_control, S_IWUSR | S_IWGRP, htc_battery_ftm_charger_stat,
		htc_battery_ftm_charger_switch),
	__ATTR(safety_timer_disable, S_IWUSR | S_IWGRP, htc_battery_safety_timer_disable_stat,
		htc_battery_safety_timer_disable_switch)
};

static struct device_attribute htc_battery_rt_attrs[] = {
	__ATTR(batt_vol_now, S_IRUGO, htc_battery_rt_attr_show, NULL),
	__ATTR(batt_current_now, S_IRUGO, htc_battery_rt_attr_show, NULL),
#if defined(CONFIG_HTC_BATT_CORE_TEMP)
	__ATTR(temp, S_IRUGO, htc_battery_rt_attr_show, NULL),
#else
        __ATTR(batt_temp_now, S_IRUGO, htc_battery_rt_attr_show, NULL),
#endif
	__ATTR(voltage_now, S_IRUGO, htc_battery_rt_attr_show, NULL),
#if defined(CONFIG_MACH_B2_WLJ)
	__ATTR(usb_temp_now, S_IRUGO, htc_battery_rt_attr_show, NULL),
#endif
	__ATTR(batt_id_now, S_IRUGO, htc_battery_rt_attr_show, NULL),
};


static int htc_battery_create_attrs(struct device *dev)
{
	int i = 0, j = 0, k = 0, rc = 0;

	for (i = 0; i < ARRAY_SIZE(htc_battery_attrs); i++) {
		rc = device_create_file(dev, &htc_battery_attrs[i]);
		if (rc)
			goto htc_attrs_failed;
	}

	for (j = 0; j < ARRAY_SIZE(htc_set_delta_attrs); j++) {
		rc = device_create_file(dev, &htc_set_delta_attrs[j]);
		if (rc)
			goto htc_delta_attrs_failed;
	}

	for (k = 0; k < ARRAY_SIZE(htc_battery_rt_attrs); k++) {
		rc = device_create_file(dev, &htc_battery_rt_attrs[k]);
		if (rc)
			goto htc_rt_attrs_failed;
	}

	goto succeed;

htc_rt_attrs_failed:
	while (k--)
		device_remove_file(dev, &htc_battery_rt_attrs[k]);
htc_delta_attrs_failed:
	while (j--)
		device_remove_file(dev, &htc_set_delta_attrs[j]);
htc_attrs_failed:
	while (i--)
		device_remove_file(dev, &htc_battery_attrs[i]);
succeed:
	return rc;
}

static int htc_battery_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	int ret = 0;

	if (!val) {
		pr_err("%s: val is null, return!\n", __func__);
		return -EINVAL;
	}
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
/* disable it due to hvdcp & chg_monitor will use to control iusb_max & vin_min
   but in hTC algorithm, we will control both by ourself */
#if 0
		if (battery_core_info.func.func_set_chg_property)
			ret = battery_core_info.func.func_set_chg_property(psp,
				val->intval / 1000);
		else {
			pr_info("%s: function doesn't exist! psp=%d\n", __func__, psp);
			return ret;
		}
		break;
#else
		return ret;
#endif
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		if (battery_core_info.func.func_set_chg_property)
			ret = battery_core_info.func.func_set_chg_property(psp, val->intval);
		else {
			pr_info("%s: function doesn't exist! psp=%d\n", __func__, psp);
			return ret;
		}
		break;
	case POWER_SUPPLY_PROP_OTG_PULSE_SKIP_ENABLE:
		ret = smbchg_set_otg_pulse_skip(val->intval);
		return ret;
	default:
		pr_info("%s: invalid type, psp=%d\n", __func__, psp);
		return -EINVAL;
	}
	pr_info("%s: batt power_supply_changed, psp=%d, intval=%d, ret=%d\n",
		__func__, psp, val->intval, ret);
	power_supply_changed(&htc_power_supplies[BATTERY_SUPPLY]);
	return ret;
}

static int htc_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = htc_battery_get_charging_status();
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		/* Fix me: temperature criteria should depend on projects,
			   but not hard code. */
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		if (battery_core_info.rep.temp_fault != -1) {
			if (battery_core_info.rep.temp_fault == 1)
				val->intval =  POWER_SUPPLY_HEALTH_OVERHEAT;
		}
		else if (battery_core_info.rep.batt_temp >= 550 ||
			battery_core_info.rep.batt_temp <= 0)
			val->intval =  POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery_core_info.present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		mutex_lock(&battery_core_info.info_lock);
		val->intval = battery_core_info.rep.level;
		mutex_unlock(&battery_core_info.info_lock);
		break;
	case POWER_SUPPLY_PROP_OVERLOAD:
		val->intval = battery_core_info.rep.overload;
		break;
	case POWER_SUPPLY_PROP_FLASH_CURRENT_MAX :
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (battery_core_info.func.func_get_chg_status) {
			val->intval = battery_core_info.func.func_get_chg_status(psp);
			if (val->intval == (-EINVAL))
				pr_info("%s: function not ready. psp=%d\n", __func__, psp);
		} else
			pr_info("%s: function doesn't exist! psp=%d\n", __func__, psp);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battery_core_info.rep.batt_current;
		break;
#if 0//FIXME
	case POWER_SUPPLY_PROP_USB_OVERHEAT:
		val->intval = battery_core_info.rep.usb_overheat;
		break;
#endif
	case POWER_SUPPLY_PROP_OTG_PULSE_SKIP_ENABLE:
		val->intval = smbchg_get_otg_pulse_skip();
		break;
	default:
		pr_info("%s: invalid type, psp=%d\n", __func__, psp);
		return -EINVAL;
	}

	return 0;
}


static int usb_voltage_max = 0;
static int htc_power_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	enum charger_type_t charger;

	mutex_lock(&battery_core_info.info_lock);

	charger = battery_core_info.rep.charging_source;

#if 0
	if (battery_core_info.rep.batt_id == 255)
		charger = CHARGER_BATTERY;
#endif

	mutex_unlock(&battery_core_info.info_lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
			if (charger == CHARGER_AC ||
			    charger == CHARGER_9V_AC
			    ||  charger == CHARGER_MHL_AC)
				val->intval = 1;
			else
				val->intval = 0;
		} else if (psy->type == POWER_SUPPLY_TYPE_USB) {
			if (charger == CHARGER_USB ||
			    charger == CHARGER_UNKNOWN_USB ||
			    charger == CHARGER_DETECTING)
				val->intval = 1;
			else
				val->intval = 0;
		} else if (psy->type == POWER_SUPPLY_TYPE_WIRELESS)
			val->intval = (charger == CHARGER_WIRELESS ? 1 : 0);
		else if (psy->type == POWER_SUPPLY_TYPE_USB_DCP)
			val->intval = (charger ==  CHARGER_AC ? 1 : 0);
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX :
		if (charger == CHARGER_BATTERY)
			val->intval = USB_MA_0;
		else if (charger == CHARGER_AC
				|| charger == CHARGER_9V_AC
				|| charger == CHARGER_WIRELESS)
			val->intval = USB_MA_1500 * 1000;
		else
			val->intval = USB_MA_500 * 1000;

		if (psy->type == POWER_SUPPLY_TYPE_USB_DCP)
			val->intval = USB_MA_1600 * 1000;
		break;
	/* align to battery_core_info.rep.charging_source */
	case POWER_SUPPLY_PROP_PRESENT :
		if (charger == CHARGER_BATTERY)
			val->intval  = 0;
		else
			val->intval  = 1;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (psy->type == POWER_SUPPLY_TYPE_USB_DCP ||
				psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = usb_voltage_max;
		else
#if 1/* FIXME: temp wa */
			return 0;
#else
			return -EINVAL;
#endif
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = psy->type;
		break;
	default:
		pr_info("%s: invalid type, psp=%d\n", __func__, psp);
		return -EINVAL;
	}

	return 0;
}

#if 0//FIXME
static int htc_power_usb_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		usb_voltage_max = val->intval;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		psy->type = val->intval;
		break;
	default:
		return -EINVAL;
	}

	power_supply_changed(&htc_power_supplies[USB_SUPPLY]);
	return 0;
}
#endif

static int htc_battery_property_is_writeable(struct power_supply *psy,
				enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return 1;
	default:
		break;
	}

	return 0;
}
#if 0//FIXME

static int htc_power_property_is_writeable(struct power_supply *psy,
				enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}
#endif
static ssize_t htc_battery_show_property(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int i = 0;
	const ptrdiff_t off = attr - htc_battery_attrs;

	mutex_lock(&battery_core_info.info_lock);

	switch (off) {
	case BATT_ID:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.batt_id);
		break;
	case BATT_VOL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.batt_vol);
		break;
	case BATT_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.batt_temp);
		break;
	case BATT_CURRENT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.batt_current);
		break;
	case CHARGING_SOURCE:
		if(battery_core_info.rep.charging_source == CHARGER_MHL_AC) {
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", CHARGER_AC);
		}
		else {
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.charging_source);
		}
		break;
	case CHARGING_ENABLED:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.charging_enabled);
		break;
	case FULL_BAT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.full_bat);
		break;
	case OVER_VCHG:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.over_vchg);
		break;
	case BATT_STATE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.batt_state);
		break;
	case BATT_CABLEIN:
		if(battery_core_info.rep.charging_source == CHARGER_BATTERY ||
				battery_core_info.rep.charging_source == CHARGER_MHL_UNKNOWN)
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", 0);
		else
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", 1);
		break;
	case USB_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.usb_temp);
		break;
	case USB_OVERHEAT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
				battery_core_info.rep.usb_overheat);
		break;
	default:
		i = -EINVAL;
	}
	mutex_unlock(&battery_core_info.info_lock);

	if (i < 0)
		BATT_ERR("%s: battery: attribute is not supported",
			__func__);
	return i;
}

static ssize_t htc_battery_rt_attr_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int i = 0;
	int val = 0;
	int rc = 0;
	const ptrdiff_t attr_index = attr - htc_battery_rt_attrs;

	if (!battery_core_info.func.func_get_batt_rt_attr) {
		BATT_ERR("%s: func_get_batt_rt_attr does not exist", __func__);
		return -EINVAL;
	}

	rc = battery_core_info.func.func_get_batt_rt_attr(attr_index, &val);
	if (rc) {
		BATT_ERR("%s: get_batt_rt_attrs failed", __func__);
		return -EINVAL;
	}

	i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n", val);

	return i;
}

static ssize_t htc_battery_charger_ctrl_timer(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int rc;
	unsigned long time_out = 0;
	ktime_t interval;
	ktime_t next_alarm;

	rc = strict_strtoul(buf, 10, &time_out);
	if (rc)
		return rc;

	BATT_EMBEDDED("%s: user set charger timer: %lu", __func__, time_out);

	if (time_out > 65536)
		return -EINVAL;

	if (time_out > 0) {
		rc = battery_core_info.func.func_charger_control(STOP_CHARGER);
		if (rc < 0) {
			BATT_ERR("charger control failed!");
			return rc;
		}
		interval = ktime_set(time_out, 0);

		next_alarm = ktime_add(ktime_get_real(), interval);
		alarm_start(&batt_charger_ctrl_alarm, next_alarm);

		charger_ctrl_stat = STOP_CHARGER;
	} else if (time_out == 0) {
		rc = battery_core_info.func.func_charger_control(
							ENABLE_CHARGER);
		if (rc < 0) {
			BATT_ERR("charger control failed!");
			return rc;
		}
		alarm_cancel(&batt_charger_ctrl_alarm);
		charger_ctrl_stat = ENABLE_CHARGER;
	}

	return count;
}

static void batt_charger_ctrl_func(struct work_struct *work)
{
	int rc;

	rc = battery_core_info.func.func_charger_control(ENABLE_CHARGER);
	if (rc) {
		BATT_ERR("charger control failed!");
		return;
	}

	charger_ctrl_stat = (unsigned int)ENABLE_CHARGER;
}

static enum alarmtimer_restart
batt_charger_ctrl_alarm_handler(struct alarm *alarm, ktime_t time)
{
	BATT_LOG("charger control alarm is timeout.");

	queue_work(batt_charger_ctrl_wq, &batt_charger_ctrl_work);

	return 0;
}

void htc_battery_update_batt_uevent(void)
{
	power_supply_changed(&htc_power_supplies[BATTERY_SUPPLY]);
	BATT_LOG("%s: power_supply_changed: battery", __func__);
}

static unsigned int batt_thermal_thres_val = 400;
module_param_named(batt_thermal_threshold, batt_thermal_thres_val,
														uint, S_IRUGO | S_IWUSR);

int htc_battery_core_update_changed(void)
{
	struct battery_info_reply new_batt_info_rep;
	int is_send_batt_uevent = 0;
	int is_send_usb_uevent = 0;
	int is_send_ac_uevent = 0;
	int is_send_wireless_charger_uevent = 0;
	static int batt_temp_over_68c_count = 0;
	struct power_supply *usb_psy;

	if (battery_register) {
		BATT_ERR("No battery driver exists.");
		return -1;
	}

	mutex_lock(&battery_core_info.info_lock);
	memcpy(&new_batt_info_rep, &battery_core_info.rep, sizeof(struct battery_info_reply));
	mutex_unlock(&battery_core_info.info_lock);
	if (battery_core_info.func.func_get_battery_info) {
		battery_core_info.func.func_get_battery_info(&new_batt_info_rep);
	} else {
		BATT_ERR("no func_get_battery_info hooked.");
		return -EINVAL;
	}

	mutex_lock(&battery_core_info.info_lock);
	if (battery_core_info.rep.charging_source != new_batt_info_rep.charging_source) {
		if (CHARGER_BATTERY == battery_core_info.rep.charging_source ||
			CHARGER_BATTERY == new_batt_info_rep.charging_source)
			is_send_batt_uevent = 1;
		if (CHARGER_USB == battery_core_info.rep.charging_source ||
			CHARGER_USB == new_batt_info_rep.charging_source)
			is_send_usb_uevent = 1;
		if (CHARGER_AC == battery_core_info.rep.charging_source ||
			CHARGER_AC == new_batt_info_rep.charging_source)
			is_send_ac_uevent = 1;
		if (CHARGER_MHL_AC == battery_core_info.rep.charging_source ||
			CHARGER_MHL_AC == new_batt_info_rep.charging_source)
			is_send_ac_uevent = 1;
		if (CHARGER_WIRELESS == battery_core_info.rep.charging_source ||
			CHARGER_WIRELESS == new_batt_info_rep.charging_source)
			is_send_wireless_charger_uevent = 1;

		if((CHARGER_MHL_UNKNOWN == battery_core_info.rep.charging_source) ||
				(CHARGER_MHL_UNKNOWN == new_batt_info_rep.charging_source))
			is_send_batt_uevent = 1;
		if((CHARGER_MHL_UNKNOWN < battery_core_info.rep.charging_source) ||
				(CHARGER_MHL_UNKNOWN < new_batt_info_rep.charging_source))
			is_send_ac_uevent = 1;

#if 0//FIXME
		/* change power supply type */
		if (htc_battery_is_support_qc20()) {
			if (CHARGER_AC == new_batt_info_rep.charging_source) {
				if (htc_battery_check_cable_type_from_usb() == DWC3_DCP) {
					power_supply_set_supply_type(&htc_power_supplies[USB_SUPPLY], POWER_SUPPLY_TYPE_USB_DCP);
					is_send_usb_uevent = 1;
					is_send_ac_uevent = 0;
				}
			} else if (CHARGER_AC == battery_core_info.rep.charging_source) {
				if (htc_power_supplies[USB_SUPPLY].type == POWER_SUPPLY_TYPE_USB_DCP) {
					power_supply_set_supply_type(&htc_power_supplies[USB_SUPPLY], POWER_SUPPLY_TYPE_USB);
					is_send_usb_uevent = 1;
					is_send_ac_uevent = 0;
				}
			}
		}
#endif
	}
	if ((!is_send_batt_uevent) &&
		((battery_core_info.rep.level != new_batt_info_rep.level) ||
		(battery_core_info.rep.batt_vol != new_batt_info_rep.batt_vol) ||
		(battery_core_info.rep.over_vchg != new_batt_info_rep.over_vchg) ||
		(battery_core_info.rep.batt_temp != new_batt_info_rep.batt_temp) ||
		/* 8994 PMIC FG design, it would update batt vol & temp only in every 30s,
		 * so it need to add sending a uevent when chg_en changed.
		 */
		(battery_core_info.rep.charging_enabled != new_batt_info_rep.charging_enabled) ||
		(battery_core_info.rep.htc_extension != new_batt_info_rep.htc_extension))) {
		is_send_batt_uevent = 1;
	}

	/* To make sure that device is under over loading scenario, accumulate
	   variable battery_over_loading only when device has been under charging
	   and level is decreased. */
	if ((battery_core_info.rep.charging_enabled != 0) &&
		(new_batt_info_rep.charging_enabled != 0)) {
		if (battery_core_info.rep.level > new_batt_info_rep.level)
			battery_over_loading++;
		else
			battery_over_loading = 0;
	}

	/* Notify charging status to pnpmgr for performance */
	if (battery_core_info.func.func_notify_pnpmgr_charging_enabled) {
		if (battery_core_info.rep.charging_enabled !=
				new_batt_info_rep.charging_enabled)
			battery_core_info.func.func_notify_pnpmgr_charging_enabled(
										new_batt_info_rep.charging_enabled);
	}

	/* Notify batt_temp to pnpmgr if batt_temp >= batt_thermal_thres_val degree */
	if (battery_core_info.func.func_notify_pnpmgr_batt_thermal) {
		if (new_batt_info_rep.batt_temp >= batt_thermal_thres_val) {
			battery_core_info.func.func_notify_pnpmgr_batt_thermal(
										new_batt_info_rep.batt_temp);
		} else {
			battery_core_info.func.func_notify_pnpmgr_batt_thermal(0);
		}
	}
	memcpy(&battery_core_info.rep, &new_batt_info_rep, sizeof(struct battery_info_reply));

	/*
	 * To avoid one time error reading shutdown device.
	 * Let userspace to see Tbatt > 68 C if it's > 68 C consuccessively 3 times
	 */
	if (battery_core_info.rep.batt_temp > 680) {
		batt_temp_over_68c_count++;
		if (batt_temp_over_68c_count < 3) {
			pr_info("[BATT] batt_temp_over_68c_count=%d, (temp=%d)\n",
					batt_temp_over_68c_count, battery_core_info.rep.batt_temp);
			battery_core_info.rep.batt_temp = 680;
		}
	} else {
		/* reset count */
		batt_temp_over_68c_count = 0;
	}

	/* overwrite fake info if test by power monitor flag is set */
	/*  overwrite fake info if the FTM mode is FTM1 */
	if (test_power_monitor || (test_ftm_mode == 1)) {
		BATT_LOG("test_power_monitor(%d) or test_ftm_mode(%d) is set: overwrite fake batt info.",
				test_power_monitor, test_ftm_mode);
		battery_core_info.rep.batt_id = 77;
		battery_core_info.rep.batt_temp = 330;
		battery_core_info.rep.level = 77;
		battery_core_info.rep.temp_fault = 0;
	}

	if (battery_core_info.rep.charging_source <= 0) {
		/* ignore id fault if charger is not connected:
		 * send fake valid if to userspace */
		if (battery_core_info.rep.batt_id == 255) {
			pr_info("[BATT] Ignore invalid id when no charging_source");
			battery_core_info.rep.batt_id = 66;
		}
	}
#if 0
	battery_core_info.rep.batt_vol = new_batt_info_rep.batt_vol;
	battery_core_info.rep.batt_id = new_batt_info_rep.batt_id;
	battery_core_info.rep.batt_temp = new_batt_info_rep.batt_temp;
	battery_core_info.rep.batt_current = new_batt_info_rep.batt_current;
	battery_core_info.rep.batt_discharg_current = new_batt_info_rep.batt_discharg_current;
	battery_core_info.rep.level = new_batt_info_rep.level;
	battery_core_info.rep.charging_source = new_batt_info_rep.charging_source;
	battery_core_info.rep.charging_enabled = new_batt_info_rep.charging_enabled;
	battery_core_info.rep.full_bat = new_batt_info_rep.full_bat;
	battery_core_info.rep.over_vchg = new_batt_info_rep.over_vchg;
	battery_core_info.rep.temp_fault = new_batt_info_rep.temp_fault;
	battery_core_info.rep.batt_state = new_batt_info_rep.batt_state;
#endif

	if (battery_core_info.rep.charging_source == CHARGER_BATTERY)
		battery_core_info.htc_charge_full = 0;
	else {
		if (battery_core_info.htc_charge_full &&
				(battery_core_info.rep.level == 100))
			battery_core_info.htc_charge_full = 1;
		else {
			if (battery_core_info.rep.level == 100)
				battery_core_info.htc_charge_full = 1;
			else
				battery_core_info.htc_charge_full = 0;
		}

		/* Clear htc_charge_full while over loading is happened. */
		if (battery_over_loading >= 2) {
			battery_core_info.htc_charge_full = 0;
			battery_over_loading = 0;
		}
	}

	battery_core_info.update_time = jiffies;
	mutex_unlock(&battery_core_info.info_lock);

	BATT_EMBEDDED("ID=%d,"
			"level=%d,"
			"level_raw=%d,"
			"volt=%dmV,"
			"current=%duA,"
			"temp=%d,"
			"chg_src=%d,"
			"chg_en=%d,"
			"overload=%d,"
			"duration=%dmin,"
			"vbus=%dmV,"
			"max_iusb=%dmA,"
			"chg_limit_reason=%d,"
			"chg_stop_reason=%d,"
			"consistent=%d,"
			"flag=0x%08X,"
			"AICL=%dmA,"
			"htc_ext=0x%02X,"
			"level_accu=%d,"
			"ui_chg_full=%d,"
			"usb_temp=%d,"
			"usb_overheat=%d,"
			,
			battery_core_info.rep.batt_id,
			battery_core_info.rep.level,
			battery_core_info.rep.level_raw,
			battery_core_info.rep.batt_vol,
			battery_core_info.rep.batt_current,
			battery_core_info.rep.batt_temp,
			battery_core_info.rep.charging_source,
			battery_core_info.rep.charging_enabled,
			battery_core_info.rep.overload,
			0,
			battery_core_info.rep.vbus,
			battery_core_info.rep.max_iusb,
			battery_core_info.rep.chg_limit_reason,
			battery_core_info.rep.chg_stop_reason,
			battery_core_info.rep.consistent,
			get_kernel_flag(),
			battery_core_info.rep.aicl_ma,
			battery_core_info.rep.htc_extension,
			battery_core_info.rep.level_accu,
			battery_core_info.htc_charge_full,
			battery_core_info.rep.usb_temp,
			battery_core_info.rep.usb_overheat
	);

	/* send uevent if need */
	if (is_send_batt_uevent) {
		power_supply_changed(&htc_power_supplies[BATTERY_SUPPLY]);
		BATT_EMBEDDED("power_supply_changed: battery");
	}
	if (is_send_ac_uevent) {
		power_supply_changed(&htc_power_supplies[AC_SUPPLY]);
		BATT_EMBEDDED("power_supply_changed: ac");
	}
	if (is_send_wireless_charger_uevent) {
		power_supply_changed(&htc_power_supplies[WIRELESS_SUPPLY]);
		BATT_EMBEDDED("power_supply_changed: wireless");
	}

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		BATT_EMBEDDED("USB supply not found, exist!\n");
		return -EINVAL;
	}
	if (is_send_usb_uevent) {
		power_supply_changed(usb_psy);
		BATT_EMBEDDED("power_supply_changed: usb");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(htc_battery_core_update_changed);

int htc_battery_core_register(struct device *dev,
				struct htc_battery_core *htc_battery)
{
	int i, rc = 0;

	if (!battery_register) {
		BATT_ERR("Only one battery driver could exist.");
		return -1;
	}
	battery_register = 0;

	test_power_monitor =
		(get_kernel_flag() & KERNEL_FLAG_TEST_PWR_SUPPLY) ? 1 : 0;

#if 0//FIXME
	test_ftm_mode = board_ftm_mode();
#else
	test_ftm_mode = 0;
#endif

	mutex_init(&battery_core_info.info_lock);
	if (htc_battery->func_get_batt_rt_attr)
		battery_core_info.func.func_get_batt_rt_attr =
					htc_battery->func_get_batt_rt_attr;
	if (htc_battery->func_show_batt_attr)
		battery_core_info.func.func_show_batt_attr =
					htc_battery->func_show_batt_attr;
	if (htc_battery->func_show_cc_attr)
		battery_core_info.func.func_show_cc_attr =
					htc_battery->func_show_cc_attr;
	if (htc_battery->func_show_htc_extension_attr)
		battery_core_info.func.func_show_htc_extension_attr =
					htc_battery->func_show_htc_extension_attr;
	if (htc_battery->func_show_charger_type_attr)
		battery_core_info.func.func_show_charger_type_attr =
					htc_battery->func_show_charger_type_attr;
	if (htc_battery->func_show_thermal_batt_temp_attr)
		battery_core_info.func.func_show_thermal_batt_temp_attr =
					htc_battery->func_show_thermal_batt_temp_attr;
	if (htc_battery->func_show_batt_bidata_attr)
		battery_core_info.func.func_show_batt_bidata_attr =
					htc_battery->func_show_batt_bidata_attr;
	if (htc_battery->func_show_consist_data_attr)
		battery_core_info.func.func_show_consist_data_attr =
					htc_battery->func_show_consist_data_attr;
	if (htc_battery->func_show_cycle_data_attr)
		battery_core_info.func.func_show_cycle_data_attr =
					htc_battery->func_show_cycle_data_attr;
	if (htc_battery->func_get_battery_info)
		battery_core_info.func.func_get_battery_info =
					htc_battery->func_get_battery_info;
	if (htc_battery->func_charger_control)
		battery_core_info.func.func_charger_control =
					htc_battery->func_charger_control;
	if (htc_battery->func_set_max_input_current)
		battery_core_info.func.func_set_max_input_current =
					htc_battery->func_set_max_input_current;
	if (htc_battery->func_context_event_handler)
		battery_core_info.func.func_context_event_handler =
					htc_battery->func_context_event_handler;

	if (htc_battery->func_set_full_level)
		battery_core_info.func.func_set_full_level =
					htc_battery->func_set_full_level;
	if (htc_battery->func_set_full_level_dis_batt_chg)
		battery_core_info.func.func_set_full_level_dis_batt_chg =
					htc_battery->func_set_full_level_dis_batt_chg;
	if (htc_battery->func_notify_pnpmgr_charging_enabled)
		battery_core_info.func.func_notify_pnpmgr_charging_enabled =
					htc_battery->func_notify_pnpmgr_charging_enabled;
	if (htc_battery->func_notify_pnpmgr_batt_thermal)
		battery_core_info.func.func_notify_pnpmgr_batt_thermal =
					htc_battery->func_notify_pnpmgr_batt_thermal;
	if (htc_battery->func_get_chg_status)
		battery_core_info.func.func_get_chg_status =
					htc_battery->func_get_chg_status;
	if (htc_battery->func_set_chg_property)
		battery_core_info.func.func_set_chg_property =
					htc_battery->func_set_chg_property;
	if (htc_battery->func_trigger_store_battery_data)
		battery_core_info.func.func_trigger_store_battery_data =
					htc_battery->func_trigger_store_battery_data;
	if (htc_battery->func_qb_mode_shutdown_status)
		battery_core_info.func.func_qb_mode_shutdown_status =
					htc_battery->func_qb_mode_shutdown_status;
	if (htc_battery->func_ftm_charger_control)
		battery_core_info.func.func_ftm_charger_control =
					htc_battery->func_ftm_charger_control;
	if (htc_battery->func_safety_timer_disable)
		battery_core_info.func.func_safety_timer_disable =
					htc_battery->func_safety_timer_disable;

	/* init power supplier framework */
	for (i = 0; i < ARRAY_SIZE(htc_power_supplies); i++) {
		rc = power_supply_register(dev, &htc_power_supplies[i]);
		if (rc)
			BATT_ERR("Failed to register power supply"
				" (%d)\n", rc);
	}

	/* create htc detail attributes */
	htc_battery_create_attrs(htc_power_supplies[CHARGER_BATTERY].dev);

	/* init charger_ctrl_timer */
	charger_ctrl_stat = ENABLE_CHARGER;
	INIT_WORK(&batt_charger_ctrl_work, batt_charger_ctrl_func);
	alarm_init(&batt_charger_ctrl_alarm,
			ALARM_REALTIME,
			batt_charger_ctrl_alarm_handler);
	batt_charger_ctrl_wq =
			create_singlethread_workqueue("charger_ctrl_timer");

	/* init battery parameters. */
	battery_core_info.update_time = jiffies;
	battery_core_info.present = 1;
	battery_core_info.htc_charge_full = 0;
	battery_core_info.rep.charging_source = CHARGER_BATTERY;
	battery_core_info.rep.batt_id = 1;
	battery_core_info.rep.batt_vol = 4000;
	battery_core_info.rep.batt_temp = 285;
	battery_core_info.rep.batt_current = 162;
	battery_core_info.rep.level = 66;
	battery_core_info.rep.level_raw = 0;
	battery_core_info.rep.full_bat = 1580000;
	battery_core_info.rep.full_level = 100;
	battery_core_info.rep.full_level_dis_batt_chg = 100;
	/* initial state = -1, valid values: 0 or 1 */
	battery_core_info.rep.temp_fault = -1;
	/* zero means battey info is not ready */
	battery_core_info.rep.batt_state = 0;
	battery_core_info.rep.cable_ready = 0;
	battery_core_info.rep.overload = 0;
	battery_core_info.rep.usb_temp = 285;
	battery_core_info.rep.usb_overheat = 0;

	battery_over_loading = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(htc_battery_core_register);

const struct battery_info_reply* htc_battery_core_get_batt_info_rep(void)
{
	return &battery_core_info.rep;
}
EXPORT_SYMBOL_GPL(htc_battery_core_get_batt_info_rep);
