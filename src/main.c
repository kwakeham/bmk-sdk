#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_assert.h"
#include "app_error.h"
#include "ble.h"
#include "ble_err.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advertising.h"
#include "ble_advdata.h"
#include "ble_dis.h"
#include "ble_conn_params.h"
#include "app_scheduler.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "app_timer.h"
#include "peer_manager.h"
#include "fds.h"
#include "ble_conn_state.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_pwr_mgmt.h"
#include "peer_manager_handler.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "nrf_gpio.h"

#include "nrf_delay.h"

#include "config/firmware_config.h"
#include "config/keyboard.h"
#include "config/keymap.h"

#ifdef MASTER
#include "ble_hids.h"
#ifdef HAS_SLAVE
#include "ble_db_discovery.h"
#include "nrf_ble_scan.h"
#include "kb_link_c.h"
#endif
#endif
#ifdef SLAVE
#include "kb_link.h"
#endif

/*
 * Variables declaration.
 */
// nRF52 variables.
APP_TIMER_DEF(m_scan_timer_id);
NRF_BLE_GATT_DEF(m_gatt);
NRF_BLE_QWR_DEF(m_qwr);
BLE_ADVERTISING_DEF(m_advertising);
#ifdef MASTER
BLE_HIDS_DEF(m_hids, NRF_SDH_BLE_TOTAL_LINK_COUNT, INPUT_REPORT_KEYS_MAX_LEN, OUTPUT_REPORT_MAX_LEN, FEATURE_REPORT_MAX_LEN);
#ifdef HAS_SLAVE
NRF_BLE_SCAN_DEF(m_scan);
BLE_DB_DISCOVERY_DEF(m_db_disc);
KB_LINK_C_DEF(m_kb_link_c);
#endif
#endif
#ifdef SLAVE
KB_LINK_DEF(m_kb_link);
#endif

static bool m_hids_in_boot_mode = false; // Current protocol mode.
static bool m_caps_lock_on = false; // Variable to indicate if Caps Lock is turned on.

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; // Handle of the current connection.
static pm_peer_id_t m_peer_id;                           // Device reference handle to the current bonded central.
static ble_uuid_t m_adv_master_uuids = {BLE_UUID_HUMAN_INTERFACE_DEVICE_SERVICE, BLE_UUID_TYPE_BLE};
#if defined(SLAVE) || (defined(MASTER) && defined(HAS_SLAVE))
static ble_uuid_t m_adv_slave_uuids = {KB_LINK_SERVICE_UUID, BLE_UUID_TYPE_VENDOR_BEGIN};
#endif

// Firmware variables.
uint8_t ROWS[] = MATRIX_ROW_PINS;
uint8_t COLS[] = MATRIX_COL_PINS;

bool key_pressed[MATRIX_ROW_NUM][MATRIX_COL_NUM] = {false};
int debounce[MATRIX_ROW_NUM][MATRIX_COL_NUM] = {KEY_PRESS_DEBOUNCE};

#ifdef MASTER
typedef struct key_index_s {
    int8_t index;
    uint8_t source;
    bool translated;
    bool has_modifiers;
    bool is_key;
    uint8_t modifiers;
    uint8_t key;
} key_index_t;

key_index_t keys[KEY_NUM];
int next_key = 0;
#endif

/*
 * Functions declaration.
 */
// nRF52 functions.
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name);
static void error_handler(uint32_t nrf_error);
static void log_init(void);

static void timers_init(void);
static void scan_timeout_handler(void *p_context);

static void power_management_init(void);

static void ble_stack_init(void);
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context);

static void scheduler_init(void);
static void gap_params_init(void);
static void gatt_init(void);

static void advertising_init(void);
static void adv_evt_handler(ble_adv_evt_t ble_adv_evt);
static void identities_set(pm_peer_id_list_skip_t skip);

static void qwr_init(void);
static void dis_init(void);

#ifdef MASTER
static void hids_init(void);
static void hids_evt_handler(ble_hids_t *p_hids, ble_hids_evt_t *p_evt);
static void on_hid_rep_char_write(ble_hids_evt_t *p_evt);
static void hids_send_keyboard_report(uint8_t *report);

#ifdef HAS_SLAVE
static void scan_init(void);
static void scan_start(void);
static void scan_evt_handler(const scan_evt_t * p_scan_evt);

static void db_discovery_init(void);
static void db_disc_handler(ble_db_discovery_evt_t *p_evt);
static void kbl_c_init(void);
static void kbl_c_evt_handler(kb_link_c_t *p_kb_link_c, kb_link_c_evt_t const * p_evt);
#endif
#endif
#ifdef SLAVE
static void kbl_init(void);
#endif

static void conn_params_init(void);

static void peer_manager_init(void);
static void pm_evt_handler(pm_evt_t const *p_evt);
static void whitelist_set(pm_peer_id_list_skip_t skip);

static void timers_start(void);
static void advertising_start(bool erase_bonds);
static void delete_bonds(void);
static void idle_state_handle(void);

// Firmware functions.
static void pins_init(void);
static void scan_matrix_task(void *data, uint16_t size);
#ifdef MASTER
static void firmware_init(void);
static bool update_key_index(int8_t index, uint8_t source);
static void translate_key_index(void);
static void generate_hid_report_task(void *data, uint16_t size);
#ifdef HAS_SLAVE
static void process_slave_key_index_task(void *data, uint16_t size);
#endif
#endif

int main(void) {
    // Initialize.
    // nRF52.
    log_init();
    timers_init();
    power_management_init();
    ble_stack_init();
    scheduler_init();
    gap_params_init();
    gatt_init();
    qwr_init();
    dis_init();
#ifdef MASTER
    hids_init();
#ifdef HAS_SLAVE
    db_discovery_init();
    kbl_c_init();
    scan_init();
#endif
#endif
#ifdef SLAVE
    kbl_init();
#endif
    // Init advertising after all services.
    advertising_init();
    conn_params_init();
    peer_manager_init();

    // Firmware.
    pins_init();

    // Start.
    timers_start();
    advertising_start(false);
#if defined(MASTER) && defined(HAS_SLAVE)
    scan_start();
#endif

    NRF_LOG_INFO("main; started.");

    // Enter main loop.
    for (;;) {
        idle_state_handle();
    }
}

/*
 * nRF52 section.
 */

void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

static void error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

static void log_init(void) {
    ret_code_t err_code;
    
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void timers_init(void) {
    ret_code_t err_code;

    err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // Matrix scan timer
    err_code = app_timer_create(&m_scan_timer_id, APP_TIMER_MODE_REPEATED, scan_timeout_handler);
    APP_ERROR_CHECK(err_code);
}

static void scan_timeout_handler(void *p_context) {
    UNUSED_PARAMETER(p_context);
    ret_code_t err_code;

    err_code = app_sched_event_put(NULL, 0, scan_matrix_task);
    APP_ERROR_CHECK(err_code);
}

static void power_management_init(void) {
    ret_code_t err_code;

    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

static void ble_stack_init(void) {
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings. Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
    ret_code_t err_code;

    NRF_LOG_INFO("ble_evt_handler; evt: %X.", p_ble_evt->header.evt_id);

    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_INFO("Connected.");
            if (p_ble_evt->evt.gap_evt.params.connected.role == BLE_GAP_ROLE_PERIPH) {
                NRF_LOG_INFO("As peripheral.");
                m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
                err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
                APP_ERROR_CHECK(err_code);
            }
#if defined(MASTER) && defined(HAS_SLAVE)
            else if (p_ble_evt->evt.gap_evt.params.connected.role == BLE_GAP_ROLE_CENTRAL) {
                NRF_LOG_INFO("As central.");
                err_code = kb_link_c_handles_assign(&m_kb_link_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
                APP_ERROR_CHECK(err_code);

                err_code = ble_db_discovery_start(&m_db_disc, p_ble_evt->evt.gap_evt.conn_handle);
                APP_ERROR_CHECK(err_code);
            }
#endif
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected.");
            if (p_ble_evt->evt.gap_evt.params.connected.role == BLE_GAP_ROLE_PERIPH) {
                m_conn_handle = BLE_CONN_HANDLE_INVALID;
                advertising_start(false);
            }
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys = {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        }
        break;

        case BLE_GATTS_EVT_HVN_TX_COMPLETE:
            break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            NRF_LOG_DEBUG("GATT client timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            NRF_LOG_DEBUG("GATT server simeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            break;
    }
}

static void scheduler_init(void) {
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}

static void gap_params_init(void) {
    ret_code_t err_code;
    ble_gap_conn_params_t gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_appearance_set(BLE_APPEARANCE_HID_KEYBOARD);
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));
    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

static void gatt_init(void) {
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);
}

static void advertising_init(void) {
    uint32_t err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));
    init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance = true;
    init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
#ifdef MASTER
    init.advdata.uuids_complete.uuid_cnt = 1;
    init.advdata.uuids_complete.p_uuids = &m_adv_master_uuids;
#endif
#ifdef SLAVE
    init.advdata.uuids_complete.uuid_cnt = 1;
    init.advdata.uuids_complete.p_uuids = &m_adv_slave_uuids;
#endif

    init.config.ble_adv_whitelist_enabled = true;
    init.config.ble_adv_directed_high_duty_enabled = true;
    init.config.ble_adv_directed_enabled = false;
    init.config.ble_adv_directed_interval = 0;
    init.config.ble_adv_directed_timeout = 0;
    init.config.ble_adv_fast_enabled = true;
    init.config.ble_adv_fast_interval = APP_ADV_FAST_INTERVAL;
    init.config.ble_adv_fast_timeout = APP_ADV_FAST_DURATION;
    init.config.ble_adv_slow_enabled = true;
    init.config.ble_adv_slow_interval = APP_ADV_SLOW_INTERVAL;
    init.config.ble_adv_slow_timeout = APP_ADV_SLOW_DURATION;

    init.evt_handler = adv_evt_handler;
    init.error_handler = error_handler;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

static void adv_evt_handler(const ble_adv_evt_t ble_adv_evt) {
    ret_code_t err_code;

    NRF_LOG_INFO("adv_evt_handler; evt: %X.", ble_adv_evt);
    switch (ble_adv_evt) {
        case BLE_ADV_EVT_DIRECTED_HIGH_DUTY:
            NRF_LOG_INFO("High Duty Directed advertising.");
            break;

        case BLE_ADV_EVT_DIRECTED:
            NRF_LOG_INFO("Directed advertising.");
            break;

        case BLE_ADV_EVT_FAST:
            NRF_LOG_INFO("Fast advertising.");
            break;

        case BLE_ADV_EVT_SLOW:
            NRF_LOG_INFO("Slow advertising.");
            break;

        case BLE_ADV_EVT_FAST_WHITELIST:
            NRF_LOG_INFO("Fast advertising with whitelist.");
            break;

        case BLE_ADV_EVT_SLOW_WHITELIST:
            NRF_LOG_INFO("Slow advertising with whitelist.");
            break;

        case BLE_ADV_EVT_IDLE:
            // TODO: Go to sleep.
            //sleep_mode_enter();
            break;

        case BLE_ADV_EVT_WHITELIST_REQUEST: {
            ble_gap_addr_t whitelist_addrs[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
            ble_gap_irk_t whitelist_irks[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
            uint32_t addr_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
            uint32_t irk_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

            err_code = pm_whitelist_get(whitelist_addrs, &addr_cnt, whitelist_irks, &irk_cnt);
            APP_ERROR_CHECK(err_code);

            NRF_LOG_DEBUG("pm_whitelist_get; ret: %d addr in whitelist, %d irk whitelist", addr_cnt, irk_cnt);

            // Set the correct identities list (no excluding peers with no Central Address Resolution).
            identities_set(PM_PEER_ID_LIST_SKIP_NO_IRK);

            // Apply the whitelist.
            err_code = ble_advertising_whitelist_reply(&m_advertising, whitelist_addrs, addr_cnt, whitelist_irks, irk_cnt);
            APP_ERROR_CHECK(err_code);
        }
        break;

        case BLE_ADV_EVT_PEER_ADDR_REQUEST: {
            NRF_LOG_INFO("Peer address request.");
            pm_peer_data_bonding_t peer_bonding_data;

            // Only Give peer address if we have a handle to the bonded peer.
            if (m_peer_id != PM_PEER_ID_INVALID)
            {
                err_code = pm_peer_data_bonding_load(m_peer_id, &peer_bonding_data);
                if (err_code != NRF_ERROR_NOT_FOUND)
                {
                    APP_ERROR_CHECK(err_code);

                    // Manipulate identities to exclude peers with no Central Address Resolution.
                    identities_set(PM_PEER_ID_LIST_SKIP_ALL);

                    ble_gap_addr_t *p_peer_addr = &(peer_bonding_data.peer_ble_id.id_addr_info);
                    err_code = ble_advertising_peer_addr_reply(&m_advertising, p_peer_addr);
                    APP_ERROR_CHECK(err_code);
                }
            }
        }
        break;

        default:
            break;
    }
}

static void identities_set(pm_peer_id_list_skip_t skip) {
    pm_peer_id_t peer_ids[BLE_GAP_DEVICE_IDENTITIES_MAX_COUNT];
    uint32_t peer_id_count = BLE_GAP_DEVICE_IDENTITIES_MAX_COUNT;

    ret_code_t err_code = pm_peer_id_list(peer_ids, &peer_id_count, PM_PEER_ID_INVALID, skip);
    APP_ERROR_CHECK(err_code);

    err_code = pm_device_identities_list_set(peer_ids, peer_id_count);
    APP_ERROR_CHECK(err_code);
}

static void qwr_init(void) {
    ret_code_t err_code;
    nrf_ble_qwr_init_t qwr_init_obj = {0};

    qwr_init_obj.error_handler = error_handler;

    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init_obj);
    APP_ERROR_CHECK(err_code);
}

static void dis_init(void) {
    ret_code_t err_code;
    ble_dis_init_t dis_init_obj;
    ble_dis_pnp_id_t pnp_id;

    pnp_id.vendor_id_source = PNP_ID_VENDOR_ID_SOURCE;
    pnp_id.vendor_id = PNP_ID_VENDOR_ID;
    pnp_id.product_id = PNP_ID_PRODUCT_ID;
    pnp_id.product_version = PNP_ID_PRODUCT_VERSION;

    memset(&dis_init_obj, 0, sizeof(dis_init_obj));

    ble_srv_ascii_to_utf8(&dis_init_obj.manufact_name_str, MANUFACTURER_NAME);
    dis_init_obj.p_pnp_id = &pnp_id;

    dis_init_obj.dis_char_rd_sec = SEC_JUST_WORKS;

    err_code = ble_dis_init(&dis_init_obj);
    APP_ERROR_CHECK(err_code);
}

#ifdef MASTER
static void hids_init(void) {
    ret_code_t err_code;
    ble_hids_init_t hids_init_obj;
    ble_hids_inp_rep_init_t *p_input_report;
    ble_hids_outp_rep_init_t *p_output_report;
    ble_hids_feature_rep_init_t *p_feature_report;
    uint8_t hid_info_flags;

    static ble_hids_inp_rep_init_t input_report_array[1];
    static ble_hids_outp_rep_init_t output_report_array[1];
    static ble_hids_feature_rep_init_t feature_report_array[1];
    static uint8_t report_map_data[] = {
        0x05, 0x01,       // Usage Page (Generic Desktop).
        0x09, 0x06,       // Usage (Keyboard).
        0xA1, 0x01,       // Collection (Application).
        0x05, 0x07,       // Usage Page (Key Codes).
        0x19, 0xe0,       // Usage Minimum (224).
        0x29, 0xe7,       // Usage Maximum (231).
        0x15, 0x00,       // Logical Minimum (0).
        0x25, 0x01,       // Logical Maximum (1).
        0x75, 0x01,       // Report Size (1).
        0x95, 0x08,       // Report Count (8).
        0x81, 0x02,       // Input (Data, Variable, Absolute).

        0x95, 0x01,       // Report Count (1).
        0x75, 0x08,       // Report Size (8).
        0x81, 0x01,       // Input (Constant) reserved byte(1).

        0x95, 0x05,       // Report Count (5).
        0x75, 0x01,       // Report Size (1).
        0x05, 0x08,       // Usage Page (Page# for LEDs).
        0x19, 0x01,       // Usage Minimum (1).
        0x29, 0x05,       // Usage Maximum (5).
        0x91, 0x02,       // Output (Data, Variable, Absolute), Led report.
        0x95, 0x01,       // Report Count (1).
        0x75, 0x03,       // Report Size (3).
        0x91, 0x01,       // Output (Data, Variable, Absolute), Led report padding.

        0x95, 0x06,       // Report Count (6).
        0x75, 0x08,       // Report Size (8).
        0x15, 0x00,       // Logical Minimum (0).
        0x25, 0x65,       // Logical Maximum (101).
        0x05, 0x07,       // Usage Page (Key codes).
        0x19, 0x00,       // Usage Minimum (0).
        0x29, 0x65,       // Usage Maximum (101).
        0x81, 0x00,       // Input (Data, Array) Key array(6 bytes).

        0x09, 0x05,       // Usage (Vendor Defined).
        0x15, 0x00,       // Logical Minimum (0).
        0x26, 0xFF, 0x00, // Logical Maximum (255).
        0x75, 0x08,       // Report Size (8 bit).
        0x95, 0x02,       // Report Count (2).
        0xB1, 0x02,       // Feature (Data, Variable, Absolute).

        0xC0              // End Collection (Application).
    };

    memset((void *)input_report_array, 0, sizeof(ble_hids_inp_rep_init_t));
    memset((void *)output_report_array, 0, sizeof(ble_hids_outp_rep_init_t));
    memset((void *)feature_report_array, 0, sizeof(ble_hids_feature_rep_init_t));

    // Initialize HID Service.
    p_input_report = &input_report_array[INPUT_REPORT_KEYS_INDEX];
    p_input_report->max_len = INPUT_REPORT_KEYS_MAX_LEN;
    p_input_report->rep_ref.report_id = INPUT_REP_REF_ID;
    p_input_report->rep_ref.report_type = BLE_HIDS_REP_TYPE_INPUT;

    p_input_report->sec.cccd_wr = SEC_JUST_WORKS;
    p_input_report->sec.wr = SEC_JUST_WORKS;
    p_input_report->sec.rd = SEC_JUST_WORKS;

    p_output_report = &output_report_array[OUTPUT_REPORT_INDEX];
    p_output_report->max_len = OUTPUT_REPORT_MAX_LEN;
    p_output_report->rep_ref.report_id = OUTPUT_REP_REF_ID;
    p_output_report->rep_ref.report_type = BLE_HIDS_REP_TYPE_OUTPUT;

    p_output_report->sec.wr = SEC_JUST_WORKS;
    p_output_report->sec.rd = SEC_JUST_WORKS;

    p_feature_report = &feature_report_array[FEATURE_REPORT_INDEX];
    p_feature_report->max_len = FEATURE_REPORT_MAX_LEN;
    p_feature_report->rep_ref.report_id = FEATURE_REP_REF_ID;
    p_feature_report->rep_ref.report_type = BLE_HIDS_REP_TYPE_FEATURE;

    p_feature_report->sec.rd = SEC_JUST_WORKS;
    p_feature_report->sec.wr = SEC_JUST_WORKS;

    hid_info_flags = HID_INFO_FLAG_REMOTE_WAKE_MSK | HID_INFO_FLAG_NORMALLY_CONNECTABLE_MSK;

    memset(&hids_init_obj, 0, sizeof(hids_init_obj));

    hids_init_obj.evt_handler = hids_evt_handler;
    hids_init_obj.error_handler = error_handler;
    hids_init_obj.is_kb = true;
    hids_init_obj.is_mouse = false;
    hids_init_obj.inp_rep_count = 1;
    hids_init_obj.p_inp_rep_array = input_report_array;
    hids_init_obj.outp_rep_count = 1;
    hids_init_obj.p_outp_rep_array = output_report_array;
    hids_init_obj.feature_rep_count = 1;
    hids_init_obj.p_feature_rep_array = feature_report_array;
    hids_init_obj.rep_map.data_len = sizeof(report_map_data);
    hids_init_obj.rep_map.p_data = report_map_data;
    hids_init_obj.hid_information.bcd_hid = BASE_USB_HID_SPEC_VERSION;
    hids_init_obj.hid_information.b_country_code = 0;
    hids_init_obj.hid_information.flags = hid_info_flags;
    hids_init_obj.included_services_count = 0;
    hids_init_obj.p_included_services_array = NULL;

    hids_init_obj.rep_map.rd_sec = SEC_JUST_WORKS;
    hids_init_obj.hid_information.rd_sec = SEC_JUST_WORKS;

    hids_init_obj.boot_kb_inp_rep_sec.cccd_wr = SEC_JUST_WORKS;
    hids_init_obj.boot_kb_inp_rep_sec.rd = SEC_JUST_WORKS;

    hids_init_obj.boot_kb_outp_rep_sec.rd = SEC_JUST_WORKS;
    hids_init_obj.boot_kb_outp_rep_sec.wr = SEC_JUST_WORKS;

    hids_init_obj.protocol_mode_rd_sec = SEC_JUST_WORKS;
    hids_init_obj.protocol_mode_wr_sec = SEC_JUST_WORKS;
    hids_init_obj.ctrl_point_wr_sec = SEC_JUST_WORKS;

    err_code = ble_hids_init(&m_hids, &hids_init_obj);
    APP_ERROR_CHECK(err_code);
}

static void hids_evt_handler(ble_hids_t *p_hids, ble_hids_evt_t *p_evt) {
    NRF_LOG_INFO("hids_evt_handler; evt: %X.", p_evt->evt_type);

    switch (p_evt->evt_type) {
        case BLE_HIDS_EVT_BOOT_MODE_ENTERED:
            NRF_LOG_INFO("Boot mode entered.");
            m_hids_in_boot_mode = true;
            break;

        case BLE_HIDS_EVT_REPORT_MODE_ENTERED:
            NRF_LOG_INFO("Report mode entered.");
            m_hids_in_boot_mode = false;
            break;

        case BLE_HIDS_EVT_REP_CHAR_WRITE:
            NRF_LOG_INFO("Rep char write.");
            on_hid_rep_char_write(p_evt);
            break;

        case BLE_HIDS_EVT_NOTIF_ENABLED:
            NRF_LOG_INFO("Notify enabled.");
            break;

        default:
            // No implementation needed.
            break;
    }
}

static void on_hid_rep_char_write(ble_hids_evt_t *p_evt) {
    if (p_evt->params.char_write.char_id.rep_type == BLE_HIDS_REP_TYPE_OUTPUT) {
        ret_code_t err_code;
        uint8_t report_val;
        uint8_t report_index = p_evt->params.char_write.char_id.rep_index;

        if (report_index == OUTPUT_REPORT_INDEX) {
            // This code assumes that the output report is one byte long. Hence the following static assert is made.
            STATIC_ASSERT(OUTPUT_REPORT_MAX_LEN == 1);

            err_code = ble_hids_outp_rep_get(&m_hids, report_index, OUTPUT_REPORT_MAX_LEN, 0, m_conn_handle, &report_val);
            APP_ERROR_CHECK(err_code);

            // Set Caps Lock indicator here.
            if (!m_caps_lock_on && ((report_val & OUTPUT_REPORT_BIT_MASK_CAPS_LOCK) != 0)) {
                // Caps Lock is turned On.
                NRF_LOG_INFO("Caps Lock is turned On!");
                m_caps_lock_on = true;
            } else if (m_caps_lock_on && ((report_val & OUTPUT_REPORT_BIT_MASK_CAPS_LOCK) == 0)) {
                // Caps Lock is turned Off.
                NRF_LOG_INFO("Caps Lock is turned Off!");
                m_caps_lock_on = false;
            } else {
                // The report received is not supported by this application. Do nothing.
            }
        }
    }
}

static void hids_send_keyboard_report(uint8_t *report) {
    ret_code_t err_code;

    if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        if (m_hids_in_boot_mode) {
            err_code = ble_hids_boot_kb_inp_rep_send(&m_hids, INPUT_REPORT_KEYS_MAX_LEN, report, m_conn_handle);
        } else {
            err_code = ble_hids_inp_rep_send(&m_hids, INPUT_REPORT_KEYS_INDEX, INPUT_REPORT_KEYS_MAX_LEN, report, m_conn_handle);
        }

        NRF_LOG_INFO("HIDs report; ret: %X.", err_code);
        APP_ERROR_CHECK(err_code);
    }
}

#ifdef HAS_SLAVE
static void scan_init(void) {
    ret_code_t err_code;
    nrf_ble_scan_init_t init;

    memset(&init, 0, sizeof(init));

    init.connect_if_match = true;
    init.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;

    err_code = nrf_ble_scan_init(&m_scan, &init, scan_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_UUID_FILTER, &m_adv_slave_uuids);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_UUID_FILTER, true);
    APP_ERROR_CHECK(err_code);
}

static void scan_start(void) {
    ret_code_t err_code;

    err_code = nrf_ble_scan_start(&m_scan);
    APP_ERROR_CHECK(err_code);
}

static void scan_evt_handler(const scan_evt_t * p_scan_evt) {
    NRF_LOG_INFO("scan_evt_handler; evt: %X.", p_scan_evt->scan_evt_id);
}

void db_discovery_init(void) {
    ret_code_t err_code = ble_db_discovery_init(db_disc_handler);
    APP_ERROR_CHECK(err_code);
};

static void db_disc_handler(ble_db_discovery_evt_t *p_evt) {
    kb_link_c_on_db_disc_evt(&m_kb_link_c, p_evt);
};

static void kbl_c_init(void) {
    ret_code_t err_code;
    kb_link_c_init_t init;

    init.evt_handler = kbl_c_evt_handler;

    err_code = kb_link_c_init(&m_kb_link_c, &init);
    APP_ERROR_CHECK(err_code);
};

static void kbl_c_evt_handler(kb_link_c_t *p_kb_link_c, kb_link_c_evt_t const * p_evt) {
    ret_code_t err_code;

    switch (p_evt->evt_type) {
        case KB_LINK_C_EVT_DISCOVERY_COMPLETE:
            NRF_LOG_INFO("KB link discovery complete.");
            err_code = kb_link_c_handles_assign(p_kb_link_c, p_evt->conn_handle, &p_evt->handles);
            APP_ERROR_CHECK(err_code);

            NRF_LOG_INFO("Try to enable notification.");
            err_code = kb_link_c_key_index_notif_enable(p_kb_link_c);
            APP_ERROR_CHECK(err_code);
            break;

        case KB_LINK_C_EVT_KEY_INDEX_UPDATE:
            NRF_LOG_INFO("Receive notification from KB link; len: %d.", p_evt->len);
            app_sched_event_put(p_evt->p_data, p_evt->len, process_slave_key_index_task);
            break;

        case KB_LINK_C_EVT_DISCONNECTED:
            NRF_LOG_INFO("KB link disconnected.");
            scan_start();
            break;
    }
}
#endif
#endif

#ifdef SLAVE
static void kbl_init(void) {
    ret_code_t err_code;
    kb_link_init_t init;

    memset(&init, 0, sizeof(init));
    init.len = 0;
    init.key_index = NULL;

    err_code = kb_link_init(&m_kb_link, &init);
    APP_ERROR_CHECK(err_code);
}
#endif

static void conn_params_init(void) {
    ret_code_t err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail = false;
    cp_init.evt_handler = NULL;
    cp_init.error_handler = error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

static void peer_manager_init(void) {
    ble_gap_sec_params_t sec_param;
    ret_code_t err_code;

    err_code = pm_init();
    APP_ERROR_CHECK(err_code);

    memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

    // Security parameters to be used for all security procedures.
    sec_param.bond = SEC_PARAM_BOND;
    sec_param.mitm = SEC_PARAM_MITM;
    sec_param.lesc = SEC_PARAM_LESC;
    sec_param.keypress = SEC_PARAM_KEYPRESS;
    sec_param.io_caps = SEC_PARAM_IO_CAPABILITIES;
    sec_param.oob = SEC_PARAM_OOB;
    sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
    sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
    sec_param.kdist_own.enc = 1;
    sec_param.kdist_own.id = 1;
    sec_param.kdist_peer.enc = 1;
    sec_param.kdist_peer.id = 1;

    err_code = pm_sec_params_set(&sec_param);
    APP_ERROR_CHECK(err_code);

    err_code = pm_register(pm_evt_handler);
    APP_ERROR_CHECK(err_code);
}

static void pm_evt_handler(pm_evt_t const *p_evt) {
    pm_handler_on_pm_evt(p_evt);
    pm_handler_flash_clean(p_evt);

    switch (p_evt->evt_id) {
        case PM_EVT_PEERS_DELETE_SUCCEEDED:
            advertising_start(false);
            break;

        case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
            if (p_evt->params.peer_data_update_succeeded.flash_changed && (p_evt->params.peer_data_update_succeeded.data_id == PM_PEER_DATA_ID_BONDING)) {
                NRF_LOG_INFO("New Bond, add the peer to the whitelist if possible.");
                // Note: You should check on what kind of white list policy your application should use.

                whitelist_set(PM_PEER_ID_LIST_SKIP_NO_ID_ADDR);
            }
            break;

        default:
            break;
    }
}

static void whitelist_set(pm_peer_id_list_skip_t skip) {
    pm_peer_id_t peer_ids[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
    uint32_t peer_id_count = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

    ret_code_t err_code = pm_peer_id_list(peer_ids, &peer_id_count, PM_PEER_ID_INVALID, skip);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_INFO("m_whitelist_peer_cnt: %d, MAX_PEERS_WLIST: %d.", peer_id_count + 1, BLE_GAP_WHITELIST_ADDR_MAX_COUNT);

    err_code = pm_whitelist_set(peer_ids, peer_id_count);
    APP_ERROR_CHECK(err_code);
}

static void timers_start(void) {
    ret_code_t err_code;

    err_code = app_timer_start(m_scan_timer_id, SCAN_DELAY_TICKS, NULL);
    APP_ERROR_CHECK(err_code);
}

static void advertising_start(bool erase_bonds) {
    if (erase_bonds == true) {
        delete_bonds();
        // Advertising is started by PM_EVT_PEERS_DELETE_SUCCEEDED event.
    } else {
        whitelist_set(PM_PEER_ID_LIST_SKIP_NO_ID_ADDR);

        ret_code_t ret = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
        APP_ERROR_CHECK(ret);
    }
}

static void delete_bonds(void) {
    ret_code_t err_code;

    NRF_LOG_INFO("Erase bonds!");

    err_code = pm_peers_delete();
    APP_ERROR_CHECK(err_code);
}

static void idle_state_handle(void) {
    app_sched_execute();
    if (NRF_LOG_PROCESS() == false) {
        nrf_pwr_mgmt_run();
    }
}

/*
 * Firmware section.
 */
static void pins_init(void) {
    NRF_LOG_INFO("pins_init.");

    for (int i = 0; i < MATRIX_COL_NUM; i++) {
        nrf_gpio_cfg_output(COLS[i]);
        nrf_gpio_pin_clear(COLS[i]);
    }

    for (int i = 0; i < MATRIX_ROW_NUM; i++) {
        nrf_gpio_cfg_input(ROWS[i], NRF_GPIO_PIN_PULLDOWN);
    }
}

static void scan_matrix_task(void *data, uint16_t size) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(size);

    ret_code_t err_code;
#ifdef MASTER
    bool has_key_press = false;
    bool has_key_release = false;
#endif
#ifdef SLAVE
    static bool buffer_updated = false;
    int buffer_len = 0;
    int8_t buffer[SLAVE_KEY_NUM];
    memset(&buffer, 0, sizeof(buffer));
#endif

    for (int col = 0; col < MATRIX_COL_NUM; col++) {
        nrf_gpio_pin_set(COLS[col]);
        nrf_delay_us(100);

        for (int row = 0; row < MATRIX_ROW_NUM; row++) {
            bool pressed = nrf_gpio_pin_read(ROWS[row]) > 0;

            if (key_pressed[row][col] == pressed) {
                if (pressed) {
                    debounce[row][col] = KEY_RELEASE_DEBOUNCE;
                } else {
                    debounce[row][col] = KEY_PRESS_DEBOUNCE;
                }
            } else {
                if (debounce[row][col] <= 0) {
                    if (pressed) {
                        // On key press
                        key_pressed[row][col] = true;
                        debounce[row][col] = KEY_RELEASE_DEBOUNCE;

                        NRF_LOG_INFO("Key press: %i.", MATRIX[row][col]);
#ifdef MASTER
                        has_key_press = true;
                        update_key_index(MATRIX[row][col], SOURCE);
#endif
#ifdef SLAVE
                        if (buffer_len < SLAVE_KEY_NUM) {
                            buffer_updated = true;
                            buffer[buffer_len++] = MATRIX[row][col];
                        }
#endif
                    } else {
                        // On key release
                        key_pressed[row][col] = false;
                        debounce[row][col] = KEY_PRESS_DEBOUNCE;

                        NRF_LOG_INFO("Key release: %i.", MATRIX[row][col]);
#ifdef MASTER
                        has_key_release = true;
                        update_key_index(-MATRIX[row][col], SOURCE);
#endif
#ifdef SLAVE
                        if (buffer_len < SLAVE_KEY_NUM) {
                            buffer_updated = true;
                            buffer[buffer_len++] = -MATRIX[row][col];
                        }
#endif
                    }
                } else {
                    debounce[row][col] -= SCAN_DELAY;
                }
            }
        }

        nrf_gpio_pin_clear(COLS[col]);
    }

#ifdef MASTER
    if (has_key_press) {
        // If has key press, translate it first.
        translate_key_index();
    } else if (has_key_release) {
        // If has only key release, just sent it to device.
        err_code = app_sched_event_put(NULL, 0, generate_hid_report_task);
        APP_ERROR_CHECK(err_code);
    }
#endif
#ifdef SLAVE
    if (buffer_updated) {
        buffer_updated = false;
        // Set key index characteristics
        kb_link_key_index_update(&m_kb_link, (uint8_t *)buffer, buffer_len);
    }
#endif
}

#ifdef MASTER
static void firmware_init(void) {
    memset(&keys, 0, sizeof(keys));
}

static bool update_key_index(int8_t index, uint8_t source) {
    key_index_t key;

    memset(&key, 0, sizeof(key_index_t));

    key.index = index;
    key.source = source;

    if (next_key < KEY_NUM && key.index > 0) {
        keys[next_key++] = key;
    } else if (next_key > 0 && key.index < 0) {
        int i = 0;
        key.index = -key.index;

        while (i < next_key) {
            while (keys[i].index != key.index || keys[i].source != key.source) {
                i++;
            }

            if (i < next_key) {
                for (i; i < next_key - 1; i++) {
                    keys[i] = keys[i + 1];
                }

                memset(&keys[--next_key], 0, sizeof(key_index_t));
            }
        }
    }
}

static void translate_key_index(void) {
    ret_code_t err_code;
    uint8_t layer = _BASE_LAYER;

    for (int i = 0; i < next_key; i++) {
        if (keys[i].translated) {
            continue;
        }

        int8_t index = keys[i].index - 1;
        uint32_t code = KEYMAP[layer][index];

        if (IS_LAYER(code)) {
            NRF_LOG_INFO("Layer key.");

            layer = LAYER(code);
            continue;
        }

        if (code == KC_TRANSPARENT) {
            NRF_LOG_INFO("Transparent key.");

            keys[i].translated = true;
            uint8_t temp_layer = layer;

            while (temp_layer >= 0 && KEYMAP[temp_layer][index] == KC_TRANSPARENT) {
                temp_layer--;
            }

            if (temp_layer < 0) {
                continue;
            } else {
                code = KEYMAP[temp_layer][index];
            }
        }

        if (IS_MOD(code)) {
            NRF_LOG_INFO("Modifier key.");

            keys[i].translated = true;
            keys[i].has_modifiers = true;
            keys[i].modifiers = MOD_BIT(code);

            code = MOD_CODE(code);
        }

        if (IS_KEY(code)) {
            NRF_LOG_INFO("Normal key.");

            keys[i].translated = true;
            keys[i].is_key = true;
            keys[i].key = code;

            continue;
        }
    }

    // Schedule hid report task
    err_code = app_sched_event_put(NULL, 0, generate_hid_report_task);
    APP_ERROR_CHECK(err_code);
}

static void generate_hid_report_task(void *data, uint16_t size) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(size);

    int report_index = 2;
    uint8_t report[INPUT_REPORT_KEYS_MAX_LEN];

    memset(&report, 0, sizeof(report));

    for (int i = 0; i < next_key; i++) {
        if (keys[i].has_modifiers) {
            report[0] |= keys[i].modifiers;
        }

        if (keys[i].is_key && report_index < INPUT_REPORT_KEYS_MAX_LEN) {
            report[report_index++] = keys[i].key;
        }
    }

    NRF_LOG_INFO("generate_hid_report_task; len: %d", report_index - 2);
    hids_send_keyboard_report((uint8_t *)report);
}

static void process_slave_key_index_task(void *data, uint16_t size) {
    int8_t *key_index = (int8_t *)data;

    for (int i = 0; i < size; i++) {
        NRF_LOG_INFO("process_slave_key_index_task; key: %i.", key_index[i]);
        update_key_index(key_index[i], SOURCE_SLAVE);
    }

    translate_key_index();
}
#endif
