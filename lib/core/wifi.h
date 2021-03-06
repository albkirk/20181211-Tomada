// Expose Espressif SDK functionality
extern "C" {
#include "user_interface.h"
  typedef void (*freedom_outside_cb_t)(uint8 status);
  int  wifi_register_send_pkt_freedom_cb(freedom_outside_cb_t cb);
  void wifi_unregister_send_pkt_freedom_cb(void);
  int  wifi_send_pkt_freedom(uint8 *buf, int len, bool sys_seq);
}

//#include <ESP8266WiFi.h>
#include <functions.h>


// Sniffer CONSTANTs
#define PURGETIME 600000
#define MINRSSI -70
#define MAXDEVICES 100
#define JBUFFER 15 + (MAXDEVICES * 40)
#define SENDTIME 30000
WiFiClient wifiClient;


// WiFi and Sniffer VARIABLEs
int WIFI_state = WL_DISCONNECTED;
unsigned int WIFI_Retry = 120;                    // Timer to retry the WiFi connection
unsigned long WIFI_LastTime = 0;                  // Last WiFi connection attempt time stamp
int WIFI_errors = 0;                              // WiFi errors Counter

unsigned long sendEntry;
char jsonString[JBUFFER];
StaticJsonBuffer<JBUFFER>  wifijsonBuffer;



// Sniffer FUNCTIONS
void purgeDevices() {
  for (int u = 0; u < aps_known_count; u++) {
    if ((millis() - aps_known[u].lastDiscoveredTime) > PURGETIME) {
      Serial.print("purged AP: " );
      Serial.println(formatMac1(aps_known[u].bssid));
      for (int i = u; i < aps_known_count; i++) memcpy(&aps_known[i], &aps_known[i + 1], sizeof(aps_known[i]));
      aps_known_count--;
      aps_known_count_old--;
      break;
    }
  }
  for (int u = 0; u < clients_known_count; u++) {
    if ((millis() - clients_known[u].lastDiscoveredTime) > PURGETIME) {
      Serial.print("purged Station: " );
      Serial.println(formatMac1(clients_known[u].station));
      for (int i = u; i < clients_known_count; i++) memcpy(&clients_known[i], &clients_known[i + 1], sizeof(clients_known[i]));
      clients_known_count--;
      clients_known_count_old--;
      break;
    }
  }
  for (int u = 0; u < probes_known_count; u++) {
    if ((millis() - probes_known[u].lastDiscoveredTime) > PURGETIME) {
      Serial.print("purged Probe: " );
      Serial.println(formatMac1(probes_known[u].bssid));
      for (int i = u; i < probes_known_count; i++) memcpy(&probes_known[i], &probes_known[i + 1], sizeof(probes_known[i]));
      probes_known_count--;
      probes_known_count_old--;
      break;
    }
  }
}


void wifi_showAPs() {
    // Show APs
    Serial.println("");
    Serial.println("-------------------AP DB-------------------");
    Serial.println(aps_known_count);
    for (int u = 0; u < aps_known_count; u++) {
        Serial.print("AP ");
        Serial.print(formatMac1(aps_known[u].bssid));
        Serial.print(" RSSI ");
        Serial.print(aps_known[u].rssi);
        Serial.print(" channel ");
        Serial.print(aps_known[u].channel);
        Serial.print(" SSID ");
        Serial.printf(" [%32s]\n", aps_known[u].ssid);
    }
    Serial.println("");
}


void wifi_showSTAs() {
    // Show Devices
    Serial.println("");
    Serial.println("-------------------Station DB-------------------");
    Serial.println(clients_known_count);
    for (int u = 0; u < clients_known_count; u++) {
        int known = 0;   // Clear known flag
        Serial.print("STA ");
        Serial.print(formatMac1(clients_known[u].station));
        Serial.print(" RSSI ");
        Serial.print(clients_known[u].rssi);
        Serial.print(" channel ");
        Serial.print(clients_known[u].channel);
        Serial.print(" SSID ");
        for (int i = 0; i < aps_known_count; i++) {
            if (! memcmp(aps_known[i].bssid, clients_known[u].bssid, ETH_MAC_LEN)) {
                Serial.printf("[%32s] \n", aps_known[i].ssid);
                known = 1;     // AP known => Set known flag
                break;
            }
        }
        if (! known)  {
            Serial.printf("[%32s] \n", "??");
        };
    }
}

void wifi_showPRBs() {
    // Show Probes
    Serial.println("");
    Serial.println("-------------------Probes DB-------------------");
    Serial.println(probes_known_count);
    for (int u = 0; u < probes_known_count; u++) {
        Serial.print("PRB ");
        Serial.print(formatMac1(probes_known[u].station));
        Serial.print(" RSSI ");
        Serial.print(probes_known[u].rssi);
        Serial.print(" channel ");
        Serial.print(probes_known[u].channel);
        Serial.print(" SSID ");
        Serial.printf(" [%32s]\n", probes_known[u].ssid);
    }
}


String wifi_listAPs() {
    String devices_ap, devices_ssid;

    // Purge and recreate json string
    wifijsonBuffer.clear();
    JsonObject& root = wifijsonBuffer.createObject();
    JsonArray& ap = root.createNestedArray("APs");
    JsonArray& ssid = root.createNestedArray("AP_SSIDs");

    // add APs
    for (int u = 0; u < aps_known_count; u++) {
        if (aps_known[u].rssi > MINRSSI) {
            devices_ap = formatMac1(aps_known[u].bssid);
            devices_ssid = (char*)aps_known[u].ssid;
            ap.add(devices_ap);
            ssid.add(devices_ssid);
        }
    }

    //root.prettyPrintTo(Serial);     // dump pretty format to serial interface
    root.printTo(jsonString);
    // Serial.print("jsonString ready to Publish: "); Serial.println((jsonString));
    return jsonString;
}


String wifi_listSTAs() {
    String devices;

    // Purge and recreate json string
    wifijsonBuffer.clear();
    JsonObject & root = wifijsonBuffer.createObject();
    JsonArray & sta = root.createNestedArray("Stations");

    // Add Clients that has RSSI higher then minimum
    for (int u = 0; u < clients_known_count; u++) {
        devices = formatMac1(clients_known[u].station);
        if (clients_known[u].rssi > MINRSSI) {
            sta.add(devices);
        }
    }

    //root.prettyPrintTo(Serial);                 // dump pretty format to serial interface
    root.printTo(jsonString);
    //Serial.print("jsonString ready to Publish: "); Serial.println((jsonString));
    return jsonString;
}


String wifi_listProbes() {
    String devices_prb, devices_ssid;

    // Purge and recreate json string
    wifijsonBuffer.clear();
    JsonObject& root = wifijsonBuffer.createObject();
    JsonArray& prb = root.createNestedArray("PROBEs");
    JsonArray& ssid = root.createNestedArray("SSIDs");

    // add PROBEs
    for (int u = 0; u < probes_known_count; u++) {
        if (probes_known[u].rssi > MINRSSI) {
            devices_prb = formatMac1(probes_known[u].station);
            devices_ssid = (char*)probes_known[u].ssid;
            prb.add(devices_prb);
            ssid.add(devices_ssid);
        }
    }

    //root.prettyPrintTo(Serial);     // dump pretty format to serial interface
    root.printTo(jsonString);
    Serial.print("jsonString ready to Publish: "); Serial.println((jsonString));
    return jsonString;
}



// Wi-Fi functions
void wifi_connect() {
  //  Connect to WiFi acess point or start as Acess point
  if ( WiFi.status() != WL_CONNECTED ) {
      if (config.STAMode) {
          // Setup ESP8266 in Station mode
          WiFi.mode(WIFI_STA);
          // the IP address for the shield:
          //IPAddress StaticIP(config.IP[0], config.IP[1], config.IP[2], config.IP[3]);                     // required for fast WiFi registration
          //IPAddress Gateway(config.Gateway[0], config.Gateway[1], config.Gateway[2], config.Gateway[3]);  // required for fast WiFi registration
          //IPAddress Subnet(config.Netmask[0], config.Netmask[1], config.Netmask[2], config.Netmask[3]);   // required for fast WiFi registration
          //IPAddress DNS(config.Gateway[0], config.Gateway[1], config.Gateway[2], config.Gateway[3]);      // required for fast WiFi registration
          //WiFi.config(StaticIP, Gateway, Subnet, DNS);                                                    // required for fast WiFi registration
          //delay(1000);                                                                           // required to comment for fast WiFi registration
          WiFi.hostname(config.Location + String("-") + config.DeviceName);
          WiFi.begin(config.ssid.c_str(), config.WiFiKey.c_str());
          WIFI_state = WiFi.waitForConnectResult();
          if ( WIFI_state == WL_CONNECTED ) {
              Serial.print("Connected to WiFi network! " + config.ssid + " IP: "); Serial.println(WiFi.localIP());
          }
      }
      else {
          // Initialize Wifi in AP mode
          WiFi.mode(WIFI_AP_STA);
          WiFi.begin(config.ssid.c_str(), config.WiFiKey.c_str());
          WIFI_state = WiFi.waitForConnectResult();
          if ( WIFI_state == WL_CONNECTED ) {
              Serial.print("Connected to WiFi network! " + config.ssid + " IP: "); Serial.println(WiFi.localIP());
          }
          //WiFi.mode(WIFI_AP);           // comment the 6 lines above if you need AP only
          WiFi.softAP(ESP_SSID.c_str());
          //WiFi.softAP(config.ssid.c_str());
          Serial.print("WiFi in AP mode, with IP: "); Serial.println(WiFi.softAPIP());
      }
  }
}


void wifi_setup() {
    //WiFi.persistent(false);                                                                               // required for fast WiFi registration
    wifi_connect();
}

void wifi_loop() {
    if ( WiFi.status() != WL_CONNECTED ) {
        if ( millis() - WIFI_LastTime > (WIFI_Retry * 1000)) {
            WIFI_errors ++;
            Serial.print( "in loop function WiFI ERROR! #: " + String(WIFI_errors) + "  ==> "); Serial.println( WiFi.status() );
            WIFI_LastTime = millis();
            wifi_connect();
        }
    }
    yield();
}


void wifi_sniffer() {
    wifi_set_opmode(STATION_MODE);            // Promiscuous works only with station mode
    wifi_set_channel(1);
    wifi_promiscuous_enable(false);
    wifi_set_promiscuous_rx_cb(promisc_cb);   // Set up promiscuous callback
    wifi_promiscuous_enable(true);
    boolean NewDevice = false;
    for (uint channel = 1; channel <= 13; channel++) {    // ESP only supports 1 ~ 13
        wifi_set_channel(channel);
        for (int n = 0; n < 200; n++) {   // 200 times delay(1) = 200 ms, which is 2 beacon's of 100ms
            delay(1);  // critical processing timeslice for NONOS SDK!
            if (aps_known_count > aps_known_count_old) {
                aps_known_count_old = aps_known_count;
                NewDevice = true;
            }
            if (clients_known_count > clients_known_count_old) {
                clients_known_count_old = clients_known_count;
                NewDevice = true;
            }
            if (probes_known_count > probes_known_count_old) {
                probes_known_count_old = probes_known_count;
                NewDevice = true;
            }
        }
    }
    purgeDevices();
    if (NewDevice) {
        //wifi_showAPs();
        //wifi_showSTAs();
        wifi_showPRBs();
    }
    Serial.println("Done with the sniffing.");
    wifi_promiscuous_enable(false);
}
