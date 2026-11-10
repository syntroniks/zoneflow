# Provisioning

!!! warning "Untested on hardware"
    This page describes what the standalone firmware is written to do. No
    board exists yet, so none of these steps has been tried with a real phone,
    app or router. Tracked in [issue #6](https://github.com/syntroniks/zoneflow/issues/6).
    The ESPHome package provisions differently (Improv over BLE), see
    [issue #7](https://github.com/syntroniks/zoneflow/issues/7).

Provisioning joins the controller to your WiFi network. The controller waters
on schedule without a network, so you can do this at any time.

## Before you start

- A **2.4 GHz** WiFi network. The ESP32-S3 has no 5 GHz radio. WPA2 or WPA3
  personal, or an open network. Mesh systems are fine: the controller does
  not lock to one access point or channel.
- The **setup code**: 10 characters (letters and digits, never 0, O, 1 or I).
  The controller shows it on its display during setup and prints it on the
  USB serial console. It is the same code on every setup; a factory reset
  keeps it.
- A phone with Espressif's provisioning app, or a computer with a browser.

## Path 1: Bluetooth with the Espressif app (default)

When the controller has no WiFi credentials it advertises over Bluetooth LE
as `ZoneFlow-XXXX` (the last four hex digits of its MAC address) for
**10 minutes** after power-up.

1. Install **ESP BLE Provisioning** (Espressif Systems; Android and iOS).
2. In the app choose to provision a new device and scan the QR code the
   controller shows on its display. The QR code carries the device name, the
   user name `wifiprov`, the setup code and the transport, in the format the
   app reads.
3. Without the QR code: pick the device by name and type the setup code when
   the app asks for the proof of possession. Some app versions list only
   devices whose name starts with a configurable prefix (default `PROV_`,
   unverified); set the prefix to `ZoneFlow` in the app settings, or use the
   QR code.
4. Choose your network and type its password. The app sends it over an
   encrypted session (Security 2: SRP6a with the setup code, then AES-GCM).
5. The app reports success when the controller has connected. If the
   password was wrong the app says so and you can try again; the controller
   keeps advertising.

After success the controller turns Bluetooth off and frees its memory until
the next setup.

From a computer, the ESP-IDF `esp_prov` tool works too:

```sh
python $IDF_PATH/tools/esp_prov/esp_prov.py --transport ble \
    --service_name ZoneFlow-AB12 --sec_ver 2 \
    --sec2_username wifiprov --sec2_pwd K7Q2M9XW4R \
    --ssid "Your network" --passphrase "your wifi password"
```

## Path 2: the setup access point

If nobody connects over Bluetooth within 10 minutes, the controller switches
to a WiFi access point and stays there until it is set up:

1. Join the network `ZoneFlow-XXXX` from a phone or laptop. Its password is
   the **setup code**.
2. Most phones open the setup page by themselves ("sign in to network"). If
   not, browse to `http://192.168.4.1/`.
3. Pick your network, type its password, and choose an admin password for the
   controller (8 to 64 characters). Press **Connect**.
4. The page reports when the controller has joined your network and gives its
   address, `http://zoneflow-xxxx.local/`. The access point then closes.

The **ESP SoftAP Provisioning** app (Espressif) also works on this access
point with the same setup code.

## After provisioning

- Open `http://zoneflow-xxxx.local/` (or the address your router shows).
- If you did not set an admin password on the setup page, the controller asks
  for the setup code and a new password on first visit. Nothing can be changed
  over the network until a password is set.

## Changing networks later

- **On the controller:** a long press on the controls restarts it into
  Bluetooth setup (the button handling belongs to the display firmware, which
  calls the network module's re-provision command).
- **From the web UI or API:** `POST /api/v1/network/reprovision` with
  `{"transport": "ble"}` or `{"transport": "softap"}`.

Either way the controller restarts into setup. The stored network stays until
new credentials work; if nobody completes setup within **15 minutes**, the
controller goes back to the stored network. Watering stops during the
restart and resumes on schedule.

## Factory reset

`POST /api/v1/system/factory-reset` with `{"confirm": "factory-reset"}`
(admin session), or from the controller's menu once the display firmware
offers it. The controller stops watering, then erases the configuration,
programs, WiFi credentials, admin password and web settings, and restarts
into setup. The setup code is kept so a printed label stays valid.

## If it does not work

| Symptom | Check |
|---|---|
| App does not find the device | More than 10 minutes since power-up: use the setup access point, or power-cycle. Bluetooth on the phone, location permission on Android. |
| App reports wrong proof of possession | Retype the setup code; it has no 0, O, 1 or I. |
| "AP not found" | 5 GHz-only network, or the SSID was mistyped (case matters). |
| Connected, but `zoneflow-xxxx.local` does not open | Some networks block mDNS: use the IP address from your router. |
