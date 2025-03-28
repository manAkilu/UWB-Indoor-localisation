#define TAG
//#define ANCHOR_U1
//#define ANCHOR_U2
//#define ANCHOR_U3
//#define ANCHOR_U4
#include "uwb.h"
#include "dw3000.h"

dwt_config_t config = {
    5,            /* Channel number. */
    DWT_PLEN_128, /* Preamble length. Used in TX only. */
    DWT_PAC8,     /* Preamble acquisition chunk size. Used in RX only. */
    9,            /* TX preamble code. Used in TX only. */
    9,            /* RX preamble code. Used in RX only. */
    1, /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2
          for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC
                         size).    Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum
                       * dwt_sts_lengths_e
                       */
    DWT_PDOA_M0       /* PDOA mode off */
};
extern dwt_txconfig_t txconfig_options;

uint8_t tx_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 0, 0, UID,
                           0,    0,    0, 0,    0,    0, 0, 0};
uint8_t rx_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 0, 0, UID,
                           0,    0,    0, 0,    0,    0, 0, 0};
uint8_t frame_seq_nb = 0;

#ifdef TAG
uint8_t rx_buffer[NUM_NODES - 1][BUF_LEN];
#else
uint8_t rx_buffer[BUF_LEN];
#endif

uint32_t status_reg = 0;
int counter = 0;
int ret;

#ifdef TAG
uint64_t rx_ts[NUM_NODES - 1];
#else
uint64_t rx_ts;
#endif

uint64_t tx_ts;
uint32_t tx_time;

uint32_t t_reply;
uint64_t t_round;
double tof, distance;
unsigned long previous_debug_millis = 0;
unsigned long current_debug_millis = 0;
int millis_since_last_serial_print;

#ifdef TAG
int target_uids[NUM_NODES - 1];
#endif

void set_target_uids() {
#ifdef TAG
    switch (NUM_NODES) {
        case 5:
            target_uids[4] = U5;
        case 4:
            target_uids[3] = U4;
        case 3:
            target_uids[2] = U3;
        case 2:
            target_uids[1] = U2;
        case 1:
            target_uids[0] = U1;
        default:
            break;
    }
#endif
}

void start_uwb() {
    while (!dwt_checkidlerc()) {
        UART_puts("IDLE FAILED\r\n");
        while (1);
    }

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        UART_puts("INIT FAILED\r\n");
        while (1);
    }

    dwt_setleds(DWT_LEDS_DISABLE);

    if (dwt_configure(&config)) {
        UART_puts("CONFIG FAILED\r\n");
        while (1);
    }

    dwt_configuretxrf(&txconfig_options);
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);
    dwt_setrxaftertxdelay(TX_TO_RX_DLY_UUS);
#ifdef TAG
    dwt_setrxtimeout(RX_TIMEOUT_UUS);
#else
    dwt_setrxtimeout(0);
#endif
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    set_target_uids();
    Serial.println(APP_NAME);
    Serial.println(UID);
    Serial.println("Setup over........");
}

// Kalman Filter Variables
float kalman_gain = 0.0f;
float estimate = 0.0f; // Position estimate
float estimate_covariance = 1.0f; // Initial estimate covariance
float process_noise = 1.0f; // Process noise (error in model)
float measurement_noise = 1.0f; // Measurement noise (error in sensor)

void kalman_filter_update(float measurement) {
    // Prediction Step (we assume the process model is a simple constant velocity model)
    float prediction = estimate; // Assume constant velocity model

    // Kalman Gain Calculation
    kalman_gain = estimate_covariance / (estimate_covariance + measurement_noise);

    // Correction Step
    estimate = prediction + kalman_gain * (measurement - prediction);

    // Update Covariance
    estimate_covariance = (1 - kalman_gain) * estimate_covariance + process_noise;
}

#ifdef TAG
void initiator() {
    if (counter == 0) {
        tx_msg[MSG_SN_IDX] = frame_seq_nb;
        tx_msg[MSG_FUNC_IDX] = FUNC_CODE_TAG;
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
        dwt_writetxdata((uint16_t)(MSG_LEN), tx_msg, 0);
        dwt_writetxfctrl((uint16_t)(MSG_LEN), 0, 1);
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    } else {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }

    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
             (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO |
              SYS_STATUS_ALL_RX_ERR))) {
    };

    if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
        dwt_readrxdata(rx_buffer[counter], MSG_LEN, 0);
        rx_ts[counter] = get_rx_timestamp_u64();
        if (rx_buffer[counter][MSG_SID_IDX] != target_uids[counter]) {
            dwt_write32bitreg(SYS_STATUS_ID,
                              SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
            counter = 0;
            return;
        }
        ++counter;
    } else {
        frame_seq_nb = 0;
        tx_msg[MSG_SN_IDX] = frame_seq_nb;
        tx_msg[MSG_FUNC_IDX] = FUNC_CODE_RESET;
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
        dwt_writetxdata((uint16_t)(MSG_LEN), tx_msg, 0);
        dwt_writetxfctrl((uint16_t)(MSG_LEN), 0, 1);
        dwt_starttx(DWT_START_TX_IMMEDIATE);
        dwt_write32bitreg(SYS_STATUS_ID,
                          SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        counter = 0;
        Sleep(1);
        return;
    }
    if (counter == NUM_NODES - 1) { /* receive msgs from all anchors */
        /* calculate distance */
        float clockOffsetRatio;
        clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);
        tx_ts = get_tx_timestamp_u64();
        current_debug_millis = millis();

        // Print timestamp and frame sequence number
        Serial.print(current_debug_millis - previous_debug_millis);
        Serial.print("ms | ");

        // Kalman Filter Update and Distance Calculation
        for (int i = 0; i < counter; i++) {
            resp_msg_get_ts(&rx_buffer[i][MSG_T_REPLY_IDX], &t_reply);
            t_round = rx_ts[i] - tx_ts;
            tof = ((t_round - t_reply * (1 - clockOffsetRatio)) / 2.0) *
                  DWT_TIME_UNITS;
            distance = tof * SPEED_OF_LIGHT;

            // Kalman Filter for position estimation
            kalman_filter_update(distance);

            Serial.print(i + 1);
            Serial.print(": ");
            snprintf(dist_str, sizeof(dist_str), "%3.2f m", estimate); // Use the estimate from the Kalman filter
            Serial.print(dist_str);

            if (i < counter - 1) Serial.print(", "); // Add comma for all except the last
        }
        Serial.println();

        previous_debug_millis = current_debug_millis;
        counter = 0;
        ++frame_seq_nb;
        Sleep(INTERVAL);
        delay(1000);
    }
}
#else
void responder() {
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
             (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) {
    };

    if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
        dwt_readrxdata(rx_buffer, BUF_LEN, 0);
        if (rx_buffer[MSG_FUNC_IDX] == FUNC_CODE_RESET) {
            counter = 0;
            frame_seq_nb = 0;
            return;
        }
        if (rx_buffer[MSG_FUNC_IDX] == FUNC_CODE_TAG) {
            rx_ts = get_rx_timestamp_u64();
            tx_time = (rx_ts + (RX_TO_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
            dwt_setdelayedtrxtime(tx_time);
            tx_ts = (((uint64_t)(tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;
            resp_msg_set_ts(&tx_msg[MSG_T_REPLY_IDX], (tx_ts - rx_ts));
            tx_msg[MSG_SN_IDX] = frame_seq_nb;
            tx_msg[MSG_FUNC_IDX] = FUNC_CODE_ANCHOR;
            dwt_writetxdata((uint16_t)(MSG_LEN), tx_msg, 0);
            dwt_writetxfctrl((uint16_t)(MSG_LEN), 0, 1);
            ret = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
            if (ret == DWT_SUCCESS) {
                while (!(dwt_read32bitreg(SYS_STATUS_ID) &
                         SYS_STATUS_TXFRS_BIT_MASK)) {
                };
                dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
                ++frame_seq_nb;
            }
        } else {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        }
    }
}
#endif
