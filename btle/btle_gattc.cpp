/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "btle_gattc.h"
#include "UUID.h"

#define BLE_DB_DISCOVERY_MAX_SRV          4  /**< Maximum number of services supported by this module. This also indicates the maximum number of users allowed to be registered to this module. (one user per service). */
#define BLE_DB_DISCOVERY_MAX_CHAR_PER_SRV 4  /**< Maximum number of characteristics per service supported by this module. */

#define SRV_DISC_START_HANDLE  0x0001                    /**< The start handle value used during service discovery. */


void launchServiceDiscovery(Gap::Handle_t connectionHandle);
void launchCharacteristicDiscovery(Gap::Handle_t connectionHandle, Gap::Handle_t startHandle, Gap::Handle_t endHandle);


/**@brief Structure for holding information about the service and the characteristics found during
 *        the discovery process.
 */
struct DiscoveredService {
    void setup(ShortUUIDBytes_t uuidIn, Gap::Handle_t start, Gap::Handle_t end) {
        uuid        = uuidIn;
        startHandle = start;
        endHandle   = end;
    }

    ShortUUIDBytes_t uuid;        /**< UUID of the service. */
    Gap::Handle_t    startHandle; /**< Service Handle Range. */
    Gap::Handle_t    endHandle;   /**< Service Handle Range. */
};

/**@brief Structure for holding information about the service and the characteristics found during
 *        the discovery process.
 */
struct DiscoveredCharacteristic {
    void setup(ShortUUIDBytes_t uuidIn, ble_gatt_char_props_t propsIn, Gap::Handle_t declHandleIn, Gap::Handle_t valueHandleIn) {
        uuid        = uuidIn;
        props       = propsIn;
        declHandle  = declHandleIn;
        valueHandle = valueHandleIn;
    }

    ShortUUIDBytes_t      uuid;
    ble_gatt_char_props_t props;
    Gap::Handle_t         declHandle; /**< Service Handle Range. */
    Gap::Handle_t         valueHandle;   /**< Service Handle Range. */
};

struct DiscoveryStatus {
    void terminateServiceDiscovery(void) {
        serviceDiscoveryInProgress = false;
        printf("end of service discovery\r\n");
    }

    void terminateCharacteristicDiscovery(void) {
        characteristicDiscoveryInProgress = false;
        serviceDiscoveryInProgress        = true;
        currSrvInd++; /* Progress service index to keep discovery alive. */
    }

    void resetDiscoveredServices(void) {
        srvCount   = 0;
        currSrvInd = 0;
        memset(services, 0, sizeof(DiscoveredService) * BLE_DB_DISCOVERY_MAX_SRV);
    }

    void resetDiscoveredCharacteristics(void) {
        charCount   = 0;
        currCharInd = 0;
        memset(characteristics, 0, sizeof(DiscoveredCharacteristic) * BLE_DB_DISCOVERY_MAX_CHAR_PER_SRV);
    }

    void serviceDiscoveryStarted(Gap::Handle_t connectionHandle) {
        connHandle                        = connectionHandle;
        resetDiscoveredServices();
        serviceDiscoveryInProgress        = true;
        characteristicDiscoveryInProgress = false;
    }

    void characteristicDiscoveryStarted(Gap::Handle_t connectionHandle) {
        connHandle                        = connectionHandle;
        resetDiscoveredCharacteristics();
        characteristicDiscoveryInProgress = true;
        serviceDiscoveryInProgress        = false;
    }

    void setupDiscoveredServices(const ble_gattc_evt_prim_srvc_disc_rsp_t *response) {
        currSrvInd = 0;
        srvCount   = response->count;

        for (unsigned serviceIndex = 0; serviceIndex < srvCount; serviceIndex++) {
            services[serviceIndex].setup(response->services[serviceIndex].uuid.uuid,
                                         response->services[serviceIndex].handle_range.start_handle,
                                         response->services[serviceIndex].handle_range.end_handle);
        }
    }

    void setupDiscoveredCharacteristics(const ble_gattc_evt_char_disc_rsp_t *response) {
        currCharInd = 0;
        charCount   = response->count;

        for (unsigned charIndex = 0; charIndex < charCount; charIndex++) {
            characteristics[charIndex].setup(response->chars[charIndex].uuid.uuid,
                                             response->chars[charIndex].char_props,
                                             response->chars[charIndex].handle_decl,
                                             response->chars[charIndex].handle_value);
        }
    }

    void progressCharacteristicDiscovery() {
        while (characteristicDiscoveryInProgress && (currCharInd < charCount)) {
            printf("%x [%u]\r\n",
                characteristics[currCharInd].uuid,
                characteristics[currCharInd].valueHandle);
            currCharInd++;
        }

        if (characteristicDiscoveryInProgress) {
            Gap::Handle_t startHandle = characteristics[currCharInd - 1].valueHandle + 1;
            Gap::Handle_t endHandle   = services[currSrvInd].endHandle;
            resetDiscoveredCharacteristics();

            if (startHandle < endHandle) {
                ble_gattc_handle_range_t handleRange = {
                    .start_handle = startHandle,
                    .end_handle   = endHandle
                };
                printf("char discovery returned %u\r\n", sd_ble_gattc_characteristics_discover(connHandle, &handleRange));
            } else {
               terminateCharacteristicDiscovery();
            }
        }
    }

    void progressServiceDiscovery() {
        while (serviceDiscoveryInProgress && (currSrvInd < srvCount)) {
            printf("%x [%u %u]\r\n",
                services[currSrvInd].uuid,
                services[currSrvInd].startHandle,
                services[currSrvInd].endHandle);

            launchCharacteristicDiscovery(connHandle, services[currSrvInd].startHandle, services[currSrvInd].endHandle);
        }
        if (serviceDiscoveryInProgress && (srvCount > 0) && (currSrvInd > 0)) {
            Gap::Handle_t endHandle = services[currSrvInd - 1].endHandle;
            resetDiscoveredServices();

            printf("services discover returned %u\r\n",
                sd_ble_gattc_primary_services_discover(connHandle, endHandle, NULL));
        }
    }

    DiscoveredService        services[BLE_DB_DISCOVERY_MAX_SRV];  /**< Information related to the current service being discovered.
                                                                   *  This is intended for internal use during service discovery. */
    DiscoveredCharacteristic characteristics[BLE_DB_DISCOVERY_MAX_CHAR_PER_SRV];

    uint16_t connHandle;  /**< Connection handle as provided by the SoftDevice. */
    uint8_t  currSrvInd;  /**< Index of the current service being discovered. This is intended for internal use during service discovery.*/
    uint8_t  srvCount;    /**< Number of services at the peers GATT database.*/
    uint8_t  currCharInd; /**< Index of the current characteristic being discovered. This is intended for internal use during service discovery.*/
    uint8_t  charCount;    /**< Number of characteristics within the service.*/

    bool     serviceDiscoveryInProgress;
    bool     characteristicDiscoveryInProgress;
};

static DiscoveryStatus discoveryStatus;

void launchServiceDiscovery(Gap::Handle_t connectionHandle)
{
    discoveryStatus.serviceDiscoveryStarted(connectionHandle);
    printf("launch service discovery returned %u\r\n", sd_ble_gattc_primary_services_discover(connectionHandle, SRV_DISC_START_HANDLE, NULL));
}

void launchCharacteristicDiscovery(Gap::Handle_t connectionHandle, Gap::Handle_t startHandle, Gap::Handle_t endHandle) {
    discoveryStatus.characteristicDiscoveryStarted(connectionHandle);

    ble_gattc_handle_range_t handleRange = {
        .start_handle = startHandle,
        .end_handle   = endHandle
    };
    printf("launch characteristic discovery returned %u\r\n", sd_ble_gattc_characteristics_discover(connectionHandle, &handleRange));
}

void bleGattcEventHandler(const ble_evt_t *p_ble_evt)
{
    switch (p_ble_evt->header.evt_id) {
        case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:
            switch (p_ble_evt->evt.gattc_evt.gatt_status) {
                case BLE_GATT_STATUS_SUCCESS: {
                    discoveryStatus.setupDiscoveredServices(&p_ble_evt->evt.gattc_evt.params.prim_srvc_disc_rsp);
                    break;
                }

                case BLE_GATT_STATUS_ATTERR_ATTRIBUTE_NOT_FOUND: {
                    discoveryStatus.terminateServiceDiscovery();
                    break;
                }

                default: {
                    discoveryStatus.serviceDiscoveryInProgress = false;
                    printf("gatt failure status: %u\r\n", p_ble_evt->evt.gattc_evt.gatt_status);
                    break;
                }
            }
            break;

        case BLE_GATTC_EVT_CHAR_DISC_RSP: {
            switch (p_ble_evt->evt.gattc_evt.gatt_status) {
                case BLE_GATT_STATUS_SUCCESS: {
                    discoveryStatus.setupDiscoveredCharacteristics(&p_ble_evt->evt.gattc_evt.params.char_disc_rsp);
                    break;
                }

                case BLE_GATT_STATUS_ATTERR_ATTRIBUTE_NOT_FOUND: {
                    discoveryStatus.terminateCharacteristicDiscovery();
                    break;
                }

                default:
                    printf("char response: gatt failure status: %u\r\n", p_ble_evt->evt.gattc_evt.gatt_status);
                    break;
            }
            break;
        }
    }

    discoveryStatus.progressCharacteristicDiscovery();
    discoveryStatus.progressServiceDiscovery();
}