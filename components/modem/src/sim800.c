// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "sim800.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_modem_dce_service.h"

#define MODEM_RESULT_CODE_POWERDOWN "POWER DOWN"

static const char *DCE_TAG = "sim800";

/**
 * @brief Macro defined for error checking
 *
 */
#define DCE_CHECK(a, str, goto_tag, ...)                                              \
    do {                                                                              \
        if (!(a)) {                                                                   \
            ESP_LOGE(DCE_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                            \
        }                                                                             \
    } while (0)

/**
 * @brief Handle response from AT+CPOWD=1
 */

static esp_err_t sim800_set_cat1_preferred(modem_dce_t *dce);
esp_err_t sim800_handle_PSM_check(modem_dce_t *dce, const char *line);
static esp_err_t sim800_check_PSM_support(modem_dce_t *dce);

static esp_err_t sim800_handle_power_down(modem_dce_t *dce, const char *line) {
    // gpio_set_level(PWR_PIN, 0);
    // vTaskDelay(pdMS_TO_TICKS(200));
    // gpio_set_level(PWR_PIN, 1);
    // return ESP_OK;

    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_POWERDOWN)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    }
    return err;
}

static esp_err_t sim800_handle_cpin(modem_dce_t *dce, const char *line) {
    // gpio_set_level(PWR_PIN, 0);
    // vTaskDelay(pdMS_TO_TICKS(200));
    // gpio_set_level(PWR_PIN, 1);
    // return ESP_OK;

    esp_err_t err = ESP_FAIL;
    if (strstr(line, "READY")) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    }
    return err;
}

static esp_err_t sim800_check_sim(modem_dce_t *dce) {
    modem_dte_t *dte = dce->dte;
    return ESP_OK;
    // dce->handle_line = esp_modem_dce_handle_response_default;
    // DCE_CHECK(dte->send_cmd(dte, "AT+CFUN=0", MODEM_COMMAND_TIMEOUT_POWEROFF) == ESP_OK, "send command failed", err);

    dce->handle_line = sim800_handle_cpin;
    DCE_CHECK(dte->send_cmd(dte, "AT+CPIN?\r", 1000) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "Cpin failed...", err);
    ESP_LOGI(DCE_TAG, "Check SIM ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}
/**
 * @brief Power down
 *
 * @param sim800_dce sim800 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t sim800_power_down(modem_dce_t *dce) {
    modem_dte_t *dte = dce->dte;

    // dce->handle_line = esp_modem_dce_handle_response_default;
    // DCE_CHECK(dte->send_cmd(dte, "AT+CFUN=0", MODEM_COMMAND_TIMEOUT_POWEROFF) == ESP_OK, "send command failed", err);

    //just do the pins..!

    gpio_set_level(PWR_PIN, PULSE_ON);

    vTaskDelay(pdMS_TO_TICKS(1200));

    gpio_set_level(PWR_PIN, PULSE_OFF);

    vTaskDelay(pdMS_TO_TICKS(500));

    return ESP_OK;

    dce->handle_line = sim800_handle_power_down;
    DCE_CHECK(dte->send_cmd(dte, "AT+CPOWD=1\r", MODEM_COMMAND_TIMEOUT_POWEROFF) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "power down failed", err);
    ESP_LOGD(DCE_TAG, "power down ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Set Working Mode
 *
 * @param dce Modem DCE object
 * @param mode woking mode
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t sim800_set_working_mode(modem_dce_t *dce, modem_mode_t mode) {
    modem_dte_t *dte = dce->dte;

    ESP_LOGI(DCE_TAG, "Working mode is changed! :D ");

    switch (mode) {
        case MODEM_COMMAND_MODE:
            dce->handle_line = esp_modem_dce_handle_exit_data_mode;
            vTaskDelay(pdMS_TO_TICKS(1300));  // spec: 1s delay for the modem to recognize the escape sequence
            if (dte->send_cmd(dte, "+++", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) != ESP_OK) {
                //return ESP_OK;
                // "+++" Could fail if we are already in the command mode.
                // in that case we ignore the timeout and re-sync the modem
                ESP_LOGI(DCE_TAG, "Sending \"+++\" command failed");
                dce->handle_line = esp_modem_dce_handle_response_default;
                esp_err_t sync = ESP_FAIL;
                for (uint8_t i = 0; i < 15; i++) {
                    if (dte->send_cmd(dte, "AT\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK) {
                        sync = ESP_OK;
                        break;
                    }
                }
                if (sync != ESP_OK)
                    goto err;

                DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "sync failed", err);
            } else {
                DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enter command mode failed", err);
            }
            ESP_LOGD(DCE_TAG, "enter command mode ok");
            dce->mode = MODEM_COMMAND_MODE;
            break;
        case MODEM_PPP_MODE:
            dce->handle_line = esp_modem_dce_handle_atd_ppp;
            DCE_CHECK(dte->send_cmd(dte, "ATD*99#\r", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) == ESP_OK, "send command failed", err);
            if (dce->state != MODEM_STATE_SUCCESS) {
                // Initiate PPP mode could fail, if we've already "dialed" the data call before.
                // in that case we retry with "ATO" to just resume the data mode
                ESP_LOGD(DCE_TAG, "enter ppp mode failed, retry with ATO");
                dce->handle_line = esp_modem_dce_handle_atd_ppp;
                DCE_CHECK(dte->send_cmd(dte, "ATO\r", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) == ESP_OK, "send command failed", err);
                DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enter ppp mode failed", err);
            }
            ESP_LOGD(DCE_TAG, "enter ppp mode ok");
            dce->mode = MODEM_PPP_MODE;
            break;
        default:
            ESP_LOGW(DCE_TAG, "unsupported working mode: %d", mode);
            goto err;
            break;
    }
    return ESP_OK;
err:
    return ESP_FAIL;
}

static esp_err_t sim800_set_cat1_preferred(modem_dce_t *dce) {
    modem_dte_t *dte = dce->dte;
    //sendAtCmd("AT+CBANDCFG=\"CAT-M\",3,8,20", "OK", 2, 200, 200);
    //	if (!sendAtCmd("AT+CNMP=38", "OK", 5, 500, 500))
    dce->handle_line = esp_modem_dce_handle_response_default;
    dte->send_cmd(dte, "AT+CBANDCFG=\"CAT-M\",3,8,20\r", 500);

    dte->send_cmd(dte, "AT+CNMP=38\r", 500);

    return ESP_OK;
}

static esp_err_t sim800_set_PSM_param(modem_dce_t *dce, uint8_t enable) {  //we'll always allow PSM
    modem_dte_t *dte = dce->dte;
    //sendAtCmd("AT+CBANDCFG=\"CAT-M\",3,8,20", "OK", 2, 200, 200);
    //	if (!sendAtCmd("AT+CNMP=38", "OK", 5, 500, 500))

    sim800_check_PSM_support(dce);

    dce->handle_line = esp_modem_dce_handle_response_default;

    if (enable && !(dce->PSM))  //only set if not allready activated
        dte->send_cmd(dte, "AT+CPSMS=1,,,\"01011111\",\"00000001\" \r", 500);
    else if (!enable && dce->PSM)
        dte->send_cmd(dte, "AT+CPSMS=0\r", 500);

    dte->send_cmd(dte, "AT+CPSI?\r", 500);
    return ESP_OK;
}

static esp_err_t sim800_set_eDRX(modem_dce_t *dce, uint8_t enable) {
    modem_dte_t *dte = dce->dte;
    //sendAtCmd("AT+CBANDCFG=\"CAT-M\",3,8,20", "OK", 2, 200, 200);
    //	if (!sendAtCmd("AT+CNMP=38", "OK", 5, 500, 500))
    dce->handle_line = esp_modem_dce_handle_response_default;

    if (enable)
        dte->send_cmd(dte, "AT+CEDRXS=1,4,\"0010\"\r", 500);
    else
        dte->send_cmd(dte, "AT+CEDRXS=0\r", 500);
    return ESP_OK;
}

static esp_err_t sim800_check_PSM_support(modem_dce_t *dce) {
    modem_dte_t *dte = dce->dte;
    //sendAtCmd("AT+CBANDCFG=\"CAT-M\",3,8,20", "OK", 2, 200, 200);
    //	if (!sendAtCmd("AT+CNMP=38", "OK", 5, 500, 500))
    dce->handle_line = sim800_handle_PSM_check;
    dte->send_cmd(dte, "AT+CPSMRDP\r", 500);

    return ESP_OK;
}

esp_err_t sim800_handle_eDRX_check(modem_dce_t *dce, const char *line) {
    esp_err_t err = ESP_FAIL;
    esp_modem_dce_t *esp_dce = __containerof(dce, esp_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (strncmp(line, "+CEDRXRDP: 0", strlen("+CEDRXRDP: 0")) == 0) {
        // not supported.
        dce->eDRX = false;

        ESP_LOGI("eDRX CHECK", "Not supported");
    } else {
        // it is supported. We dont care about the timing currently, but it could be parsed at this point.
        dce->eDRX = true;
        ESP_LOGI("eDRX CHECK", "Supported");
    }
    err = ESP_OK;
    return err;
}

esp_err_t sim800_handle_PSM_check(modem_dce_t *dce, const char *line) {
    esp_err_t err = ESP_FAIL;
    esp_modem_dce_t *esp_dce = __containerof(dce, esp_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        //parse response.
        uint32_t mode, requested_active, requested_tau, network_active, Network_T3412_EXT_value, Network_T3412_value;
        char buf[20];
        int matched = sscanf(line, "+CPSMRDP: %d,%d,%d,%d,%d,%d", &mode, &requested_active, &requested_tau, &network_active, &Network_T3412_EXT_value, &Network_T3412_value);

        ESP_LOGI("PSM CHECK", "%s", line);
        ESP_LOGI("PSM CHECK", "%d,%d,%d,%d,%d,%d", mode, requested_active, requested_tau, network_active, Network_T3412_EXT_value, Network_T3412_value);
        ESP_LOGI("PSM CHECK", "Active: %d", network_active);
        if (matched < 5)
            return ESP_FAIL;

        if (network_active != 0) {
            dce->PSM = true;
        } else {
            dce->PSM = false;
        }
    }

    // if (strncmp(line, "+CEDRXRDP: 0", strlen("+CEDRXRDP: 0"))) {  //+CPSMRDP: 0,2,1116000,0,0,3600

    //     // not supported.
    //     dce->PSM = false;
    // } else {
    //     // it is supported. We dont care about the timing currently, but it could be parsed at this point.
    //     dce->PSM = true;
    // }
    err = ESP_OK;
    return err;
}

static esp_err_t sim800_check_eDRX_support(modem_dce_t *dce) {
    modem_dte_t *dte = dce->dte;
    dce->handle_line = sim800_handle_eDRX_check;
    dte->send_cmd(dte, "AT+CEDRXRDP\r", 500);

    return ESP_OK;
}

esp_err_t esp_modem_enable_eDRX(modem_dce_t *dce, uint8_t enable) {
    //be sure that we are attached..!

    esp_err_t err = sim800_set_eDRX(dce, enable);

    err = sim800_check_eDRX_support(dce);

    if (err == ESP_OK && dce->eDRX)
        return ESP_OK;
    else
        return ESP_FAIL;
}

esp_err_t esp_modem_enable_PSM(modem_dce_t *dce, uint8_t enable) {
    //be sure that we are attached..!

    esp_err_t err = sim800_set_PSM_param(dce, enable);

    err = sim800_check_PSM_support(dce);

    return err;
}

/**
 * @brief Deinitialize SIM800 object
 *
 * @param dce Modem DCE object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on fail
 */
static esp_err_t sim800_deinit(modem_dce_t *dce) {
    esp_modem_dce_t *esp_modem_dce = __containerof(dce, esp_modem_dce_t, parent);
    if (dce->dte) {
        dce->dte->dce = NULL;
    }
    free(esp_modem_dce);
    return ESP_OK;
}

modem_dce_t *sim800_init(modem_dte_t *dte) {
    DCE_CHECK(dte, "DCE should bind with a DTE", err);
    /* malloc memory for esp_modem_dce object */
    esp_modem_dce_t *esp_modem_dce = calloc(1, sizeof(esp_modem_dce_t));
    DCE_CHECK(esp_modem_dce, "calloc esp_modem_dce_t failed", err);
    /* Bind DTE with DCE */
    esp_modem_dce->parent.dte = dte;
    dte->dce = &(esp_modem_dce->parent);
    /* Bind methods */
    esp_modem_dce->parent.handle_line = NULL;
    esp_modem_dce->parent.sync = esp_modem_dce_sync;
    esp_modem_dce->parent.echo_mode = esp_modem_dce_echo;
    esp_modem_dce->parent.store_profile = esp_modem_dce_store_profile;
    esp_modem_dce->parent.set_flow_ctrl = esp_modem_dce_set_flow_ctrl;
    esp_modem_dce->parent.define_pdp_context = esp_modem_dce_define_pdp_context;
    esp_modem_dce->parent.hang_up = esp_modem_dce_hang_up;
    esp_modem_dce->parent.get_signal_quality = esp_modem_dce_get_signal_quality;
    esp_modem_dce->parent.get_battery_status = esp_modem_dce_get_battery_status;
    esp_modem_dce->parent.get_operator_name = esp_modem_dce_get_operator_name;
    esp_modem_dce->parent.set_working_mode = sim800_set_working_mode;
    esp_modem_dce->parent.power_down = sim800_power_down;
    esp_modem_dce->parent.deinit = sim800_deinit;
    esp_modem_dce->parent.checkNetwork = esp_modem_dce_get_check_attach;
    esp_modem_dce->parent.attach = esp_modem_dce_attach;
    esp_modem_dce->parent.detach = esp_modem_dce_detach;
    esp_modem_dce->parent.set_default_bands = esp_modem_dce_set_default_bands;
    esp_modem_dce->parent.enable_psm = esp_modem_enable_PSM;
    esp_modem_dce->parent.enable_edrx = esp_modem_enable_eDRX;
    ESP_LOGI(DCE_TAG, "PWR pin %u", PWR_PIN);

    esp_modem_dce->parent.eDRX = false;
    esp_modem_dce->parent.PSM = false;
    esp_modem_dce->parent.psm_enter_notified = false;
    esp_modem_dce->parent.power_down_notified = false;

    gpio_config_t pinCfg;
    pinCfg.mode = GPIO_MODE_OUTPUT;
#ifdef GEN_1
    pinCfg.pin_bit_mask = (1UL << PWR_PIN) | (1UL << RST_PIN) | (1UL << PWR_ON_PIN);  //power key // reset key // power on // on board led
#else
    pinCfg.pin_bit_mask = (1UL << PWR_PIN);  //power key // reset key // power on // on board led
#endif

    pinCfg.intr_type = GPIO_INTR_DISABLE;
    pinCfg.pull_up_en = 0;
    pinCfg.pull_down_en = 0;
    gpio_config(&pinCfg);
#ifdef GEN_1
    gpio_set_level(RST_PIN, 1);
    gpio_set_level(PWR_ON_PIN, 1);
#endif
    gpio_set_level(PWR_PIN, PULSE_OFF);

    //do some power testing (e.g. can we sync.)

    esp_err_t modemPower = ESP_FAIL;
    for (uint8_t i = 0; i < 2; i++) {
        modemPower = esp_modem_dce_power_test(&(esp_modem_dce->parent));

        if (modemPower == ESP_OK)
            break;

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (modemPower == ESP_OK)
        ESP_LOGI(DCE_TAG, "Modem is on!");
    else
        ESP_LOGI(DCE_TAG, "Modem power not detected....");

    if (modemPower != ESP_OK) {
        //power on the modem...

        ESP_LOGI(DCE_TAG, "Power on modem");
        vTaskDelay(pdMS_TO_TICKS(100));

        gpio_set_level(PWR_PIN, PULSE_ON);

        vTaskDelay(pdMS_TO_TICKS(1200));

        gpio_set_level(PWR_PIN, PULSE_OFF);

        vTaskDelay(pdMS_TO_TICKS(3000));

        for (int i = 0; i < 100; i++) {
            if (esp_modem_dce_power_test(&(esp_modem_dce->parent)) == ESP_OK)
                break;
        }
    }

    /* Sync between DTE and DCE */
    DCE_CHECK(esp_modem_dce_sync(&(esp_modem_dce->parent)) == ESP_OK, "sync failed", err_io);
    /* Close echo */
    DCE_CHECK(esp_modem_dce_echo(&(esp_modem_dce->parent), false) == ESP_OK, "close echo mode failed", err_io);
    /* Get Module name */
    DCE_CHECK(esp_modem_dce_get_module_name(&(esp_modem_dce->parent)) == ESP_OK, "get module name failed", err_io);
    /* Get IMEI number */
    DCE_CHECK(esp_modem_dce_get_imei_number(&(esp_modem_dce->parent)) == ESP_OK, "get imei failed", err_io);
    /* Get IMSI number */

    if (sim800_check_sim(&(esp_modem_dce->parent)) != ESP_OK)
        goto err_io;
    //DCE_CHECK(esp_modem_dce_get_imsi_number(&(esp_modem_dce->parent)) == ESP_OK, "get imsi failed", err_io);
    /* Get operator name */
    DCE_CHECK(esp_modem_dce_get_operator_name(&(esp_modem_dce->parent)) == ESP_OK, "get operator name failed", err_io);

    DCE_CHECK(esp_modem_dce_get_check_attach(&(esp_modem_dce->parent)) == ESP_OK, "get operator name failed", err_io);

    sim800_set_cat1_preferred(&(esp_modem_dce->parent));

    ESP_LOGI(DCE_TAG, "Modem INIT OK, %p", &(esp_modem_dce->parent));

    return &(esp_modem_dce->parent);
err_io:
    free(esp_modem_dce);
    dte->dce = NULL;
err:
    return NULL;
}
