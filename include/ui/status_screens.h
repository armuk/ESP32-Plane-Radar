#pragma once

/** Setup hotspot instructions, with its WPA2 password. */
void statusScreenPortal(const char* ap_password);
/** A newly generated settings password, until BOOT is tapped. */
void statusScreenSettingsPassword(const char* password);
void statusScreenConnectFailed();
void statusScreenWifiReset();
void statusScreenFirmwareUpdate();

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();
