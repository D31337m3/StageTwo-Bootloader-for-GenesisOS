#include "stage2_wifi.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "STAGE2_WIFI";

typedef struct {
    char ssid[33];
    char psk[65];
    bool disabled;
} known_net_t;

#define MAX_KNOWN 8
static known_net_t s_known[MAX_KNOWN];
static int s_known_count = 0;

static bool s_wifi_ready = false;
static EventGroupHandle_t s_wifi_ev = NULL;
static esp_netif_t *s_netif = NULL;

// UI state (owned by LVGL thread)
typedef struct {
    lv_obj_t *parent;
    lv_obj_t *status;
    lv_obj_t *list;
    lv_obj_t *spinner;
    lv_obj_t *kb;
    lv_obj_t *psk_ta;
    lv_timer_t *timer;
    char pending_ssid[33];
    bool scanning;
} wifi_ui_t;

static bool str_has_prefix(const char *s, const char *p)
{
    return s && p && strncmp(s, p, strlen(p)) == 0;
}

static void trim_line(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = 0;
        n--;
    }
}

static void known_clear(void)
{
    memset(s_known, 0, sizeof(s_known));
    s_known_count = 0;
}

static known_net_t *known_find(const char *ssid)
{
    if (!ssid || !ssid[0]) return NULL;
    for (int i = 0; i < s_known_count; i++) {
        if (strcmp(s_known[i].ssid, ssid) == 0) return &s_known[i];
    }
    return NULL;
}

static known_net_t *known_upsert(const char *ssid)
{
    known_net_t *k = known_find(ssid);
    if (k) return k;
    if (s_known_count >= MAX_KNOWN) return NULL;
    k = &s_known[s_known_count++];
    memset(k, 0, sizeof(*k));
    strncpy(k->ssid, ssid, sizeof(k->ssid) - 1);
    return k;
}

static esp_err_t wifi_known_load(void)
{
    known_clear();

    FILE *f = fopen("/user/wifi.toml", "rb");
    if (!f) return ESP_OK;

    char line[256];
    known_net_t *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        trim_line(line);
        if (!line[0] || line[0] == '#') continue;
        if (str_has_prefix(line, "[[networks]]")) {
            cur = NULL;
            continue;
        }
        if (str_has_prefix(line, "ssid")) {
            const char *q = strchr(line, '\"');
            if (!q) continue;
            const char *q2 = strchr(q + 1, '\"');
            if (!q2) continue;
            char ssid[33] = {0};
            size_t len = (size_t)(q2 - (q + 1));
            if (len >= sizeof(ssid)) len = sizeof(ssid) - 1;
            memcpy(ssid, q + 1, len);
            ssid[len] = 0;
            cur = known_upsert(ssid);
            continue;
        }
        if (!cur) continue;
        if (str_has_prefix(line, "psk")) {
            const char *q = strchr(line, '\"');
            if (!q) continue;
            const char *q2 = strchr(q + 1, '\"');
            if (!q2) continue;
            size_t len = (size_t)(q2 - (q + 1));
            if (len >= sizeof(cur->psk)) len = sizeof(cur->psk) - 1;
            memcpy(cur->psk, q + 1, len);
            cur->psk[len] = 0;
        } else if (str_has_prefix(line, "disabled")) {
            cur->disabled = strstr(line, "true") != NULL;
        }
    }
    fclose(f);
    return ESP_OK;
}

static esp_err_t wifi_known_save(void)
{
    FILE *f = fopen("/user/wifi.toml.tmp", "wb");
    if (!f) return ESP_FAIL;
    fprintf(f, "version = 1\n\n# Saved networks\n");
    for (int i = 0; i < s_known_count; i++) {
        const known_net_t *k = &s_known[i];
        fprintf(f, "\n[[networks]]\n");
        fprintf(f, "ssid = \"%s\"\n", k->ssid);
        fprintf(f, "psk = \"%s\"\n", k->psk);
        fprintf(f, "disabled = %s\n", k->disabled ? "true" : "false");
    }
    fclose(f);
    rename("/user/wifi.toml.tmp", "/user/wifi.toml");
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(s_wifi_ev, BIT0);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_ev, BIT0);
    }
}

esp_err_t stage2_wifi_init(void)
{
    if (s_wifi_ready) return ESP_OK;
    if (!s_wifi_ev) s_wifi_ev = xEventGroupCreate();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi evt reg failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip evt reg failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    (void)wifi_known_load();
    s_wifi_ready = true;
    return ESP_OK;
}

static esp_err_t wifi_connect_ssid(const char *ssid, const char *psk)
{
    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, psk ? psk : "", sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wcfg.sta.pmf_cfg.capable = true;
    wcfg.sta.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wcfg), TAG, "set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect failed");
    return ESP_OK;
}

esp_err_t stage2_wifi_autoconnect(void)
{
    if (!s_wifi_ready) return ESP_ERR_INVALID_STATE;
    (void)wifi_known_load();

    for (int i = 0; i < s_known_count; i++) {
        const known_net_t *k = &s_known[i];
        if (k->disabled) continue;
        if (!k->ssid[0]) continue;
        ESP_LOGI(TAG, "Auto-connect attempt: %s", k->ssid);
        (void)wifi_connect_ssid(k->ssid, k->psk);
        return ESP_OK;
    }
    return ESP_OK;
}

bool stage2_wifi_is_connected(void)
{
    if (!s_wifi_ev) return false;
    EventBits_t bits = xEventGroupGetBits(s_wifi_ev);
    return (bits & BIT0) != 0;
}

static void ui_set_status(wifi_ui_t *ui, const char *txt, lv_color_t col)
{
    if (!ui || !ui->status) return;
    lv_label_set_text(ui->status, txt);
    lv_obj_set_style_text_color(ui->status, col, 0);
}

static void wifi_start_scan(wifi_ui_t *ui)
{
    if (!s_wifi_ready) return;
    if (ui && ui->spinner) lv_obj_clear_flag(ui->spinner, LV_OBJ_FLAG_HIDDEN);
    if (ui && ui->list) lv_obj_add_flag(ui->list, LV_OBJ_FLAG_HIDDEN);
    if (ui) ui->scanning = true;

    wifi_scan_config_t sc = {0};
    sc.show_hidden = true;
    (void)esp_wifi_scan_start(&sc, false);
}

static void kb_event_cb(lv_event_t *e)
{
    wifi_ui_t *ui = (wifi_ui_t *)lv_event_get_user_data(e);
    if (!ui) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        if (ui->kb) {
            lv_obj_add_flag(ui->kb, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(ui->kb, NULL);
        }
        ui_set_status(ui, "Password entry complete. Press Connect.", lv_color_hex(0xA0A4AB));
    } else if (code == LV_EVENT_CANCEL) {
        if (ui->kb) {
            lv_obj_add_flag(ui->kb, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(ui->kb, NULL);
        }
        ui_set_status(ui, "Password entry cancelled", lv_color_hex(0xFFCC33));
    }
}

static void list_btn_cb(lv_event_t *e)
{
    wifi_ui_t *ui = (wifi_ui_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    if (!ui || !btn) return;

    const char *ssid = lv_list_get_btn_text(ui->list, btn);
    if (!ssid || !ssid[0]) return;
    strncpy(ui->pending_ssid, ssid, sizeof(ui->pending_ssid) - 1);
    ui->pending_ssid[sizeof(ui->pending_ssid) - 1] = 0;

    if (!ui->psk_ta) {
        ui->psk_ta = lv_textarea_create(ui->parent);
        lv_textarea_set_one_line(ui->psk_ta, true);
        lv_textarea_set_password_mode(ui->psk_ta, true);
        lv_textarea_set_password_show_time(ui->psk_ta, 800);
        lv_obj_set_width(ui->psk_ta, lv_pct(100));
        lv_obj_align(ui->psk_ta, LV_ALIGN_BOTTOM_MID, 0, -240);
        lv_textarea_set_placeholder_text(ui->psk_ta, "WiFi Password");
    }
    if (!ui->kb) {
        ui->kb = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_textarea(ui->kb, ui->psk_ta);
        lv_obj_add_event_cb(ui->kb, kb_event_cb, LV_EVENT_ALL, ui);
    } else {
        lv_obj_clear_flag(ui->kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ui->kb, ui->psk_ta);
    }
}

static void connect_btn_cb(lv_event_t *e)
{
    wifi_ui_t *ui = (wifi_ui_t *)lv_event_get_user_data(e);
    if (!ui) return;
    if (!ui->pending_ssid[0]) return;
    const char *psk = ui->psk_ta ? lv_textarea_get_text(ui->psk_ta) : "";

    known_net_t *k = known_upsert(ui->pending_ssid);
    if (k) {
        strncpy(k->psk, psk ? psk : "", sizeof(k->psk) - 1);
        k->disabled = false;
        (void)wifi_known_save();
    }

    ui_set_status(ui, "Connecting...", lv_color_hex(0x19FF9C));
    (void)wifi_connect_ssid(ui->pending_ssid, psk);
}

static void refresh_btn_cb(lv_event_t *e)
{
    wifi_ui_t *ui = (wifi_ui_t *)lv_event_get_user_data(e);
    if (!ui) return;
    wifi_start_scan(ui);
}

static void wifi_ui_timer_cb(lv_timer_t *t)
{
    wifi_ui_t *ui = (wifi_ui_t *)t->user_data;
    if (!ui) return;

    if (ui->scanning) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num == 0) return;

        wifi_ap_record_t *recs = (wifi_ap_record_t *)calloc(ap_num, sizeof(wifi_ap_record_t));
        if (!recs) return;
        if (esp_wifi_scan_get_ap_records(&ap_num, recs) == ESP_OK) {
            lv_obj_clean(ui->list);
            for (int i = 0; i < ap_num; i++) {
                char ssid[33] = {0};
                memcpy(ssid, recs[i].ssid, sizeof(recs[i].ssid));
                ssid[32] = 0;
                if (!ssid[0]) continue;
                lv_obj_t *b = lv_list_add_btn(ui->list, LV_SYMBOL_WIFI, ssid);
                lv_obj_add_event_cb(b, list_btn_cb, LV_EVENT_CLICKED, ui);
            }
            ui_set_status(ui, "Tap a network, enter password, then Connect", lv_color_hex(0xA0A4AB));
            lv_obj_clear_flag(ui->list, LV_OBJ_FLAG_HIDDEN);
            if (ui->spinner) lv_obj_add_flag(ui->spinner, LV_OBJ_FLAG_HIDDEN);
            ui->scanning = false;
        }
        free(recs);
    }

    if (stage2_wifi_is_connected()) {
        ui_set_status(ui, "Connected", lv_color_hex(0x19FF9C));
    }
}

static void wifi_cleanup_cb(lv_event_t *e)
{
    wifi_ui_t *ui = (wifi_ui_t *)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->timer) {
        lv_timer_del(ui->timer);
        ui->timer = NULL;
    }
    lv_mem_free(ui);
}

esp_err_t stage2_wifi_build(lv_obj_t *parent)
{
    if (!parent) return ESP_ERR_INVALID_ARG;
    if (!s_wifi_ready) (void)stage2_wifi_init();

    wifi_ui_t *ui = (wifi_ui_t *)lv_mem_alloc(sizeof(wifi_ui_t));
    if (!ui) return ESP_ERR_NO_MEM;
    memset(ui, 0, sizeof(*ui));
    ui->parent = parent;

    ui->status = lv_label_create(parent);
    lv_obj_set_style_text_color(ui->status, lv_color_hex(0xA0A4AB), 0);
    lv_label_set_text(ui->status, "Scanning...");

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *refresh = lv_btn_create(row);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x232634), 0);
    lv_obj_t *rl = lv_label_create(refresh);
    lv_label_set_text(rl, "Refresh");
    lv_obj_center(rl);
    lv_obj_add_event_cb(refresh, refresh_btn_cb, LV_EVENT_CLICKED, ui);

    lv_obj_t *connect = lv_btn_create(row);
    lv_obj_set_style_bg_color(connect, lv_color_hex(0x19FF9C), 0);
    lv_obj_t *cl = lv_label_create(connect);
    lv_label_set_text(cl, "Connect");
    lv_obj_center(cl);
    lv_obj_add_event_cb(connect, connect_btn_cb, LV_EVENT_CLICKED, ui);

    ui->spinner = lv_spinner_create(parent, 800, 60);
    lv_obj_set_size(ui->spinner, 40, 40);
    lv_obj_align(ui->spinner, LV_ALIGN_CENTER, 0, 10);

    ui->list = lv_list_create(parent);
    lv_obj_set_size(ui->list, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(ui->list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui->list, LV_OBJ_FLAG_HIDDEN);

    ui->timer = lv_timer_create(wifi_ui_timer_cb, 300, ui);
    lv_obj_add_event_cb(parent, wifi_cleanup_cb, LV_EVENT_DELETE, ui);

    wifi_start_scan(ui);
    return ESP_OK;
}

