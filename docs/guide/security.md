# Security for home users

!!! warning "Written from the design; not yet verified on hardware"
    This page summarises [firmware security](../design/firmware-security.md)
    for the standalone firmware. The protections are covered by host tests
    of the firmware logic and by a security review of the code. Nothing has
    been tested against a real browser, phone or attacker.

The controller is built for a home network with no cloud service. This page
says, in plain words, what is protected, what is not, and what you can do.

## What is protected

- **Your WiFi password during setup over Bluetooth.** The phone app and the controller agree an encrypted session using the setup code. Someone listening over the air learns neither the code nor your password.
- **Changes need the admin password.** Starting, stopping, editing programs, settings and firmware updates all need a signed-in session, over the web interface or the API. The password is stored as a salted hash, not as text.
- **Password guessing is slowed.** After 5 wrong tries from one address the controller makes each further try wait longer, up to 15 minutes.
- **Other web pages cannot act for you.** A web page you visit cannot make your browser start a valve on the controller, and cannot read the controller's answers by tricks with domain names.
- **Secrets stay out of backups and logs.** WiFi and admin passwords, the MQTT password, the setup code and the HTTPS key are never in a backup file, an API answer or the log.
- **Updates are checked** for the right chip, the right project and an intact file before they are installed, and the controller goes back to the old firmware if the new one does not start properly.
- **The valves stay safe whatever the network does.** No request can exceed a zone's maximum run time. If the firmware crashes, the hardware turns the valves off. Watering on schedule does not depend on the network.

## What is not protected

- **Anyone on your network can see the status** by default: zones, programs, network name and the backup file. Turn on **Settings → Password and security → Require sign-in to view** to close that.
- **Plain HTTP by default.** On your own WiFi, someone who knows the WiFi password and records your phone's traffic can read the admin password when you sign in. HTTPS is available (below) but off by default.
- **The HTTPS certificate is self-made.** Browsers warn on the first visit. HTTPS helps only if you compare the certificate's fingerprint once, shown under **Settings → Password and security**.
- **Firmware images are not signed.** Anyone with the admin password can install any ZoneFlow-shaped firmware, an older one too. Take firmware only from this project's releases.
- **Someone holding the controller can read its secrets.** The flash memory is not encrypted: WiFi password, password hash and keys can be read with the board in hand.
- **The setup code never changes**, and a factory reset keeps it. Anyone who can stand at the controller can read it: **Menu → Setup → Network → Show setup code**, then hold the knob for 3 seconds. That is by design: being at the controller is what the code proves. It is also the password of the setup access point. Someone who records a phone joining that access point can try to guess the code offline; the design's rough estimate is about 14 GPU-years for the 50-bit code, but the attack exists. Prefer Bluetooth setup.
- **One device on your network can lock others out of signing in** for a while, by sending many wrong passwords. Sessions already open keep working, and the display always works.
- **MQTT trusts your broker.** Whoever may publish to the controller's command topics can run valves. Plain MQTT sends the broker password unencrypted.

## What you can do

1. Set a strong admin password at setup. Do not reuse your WiFi password.
2. Use Bluetooth setup rather than the setup access point when you can.
3. Turn on **Require sign-in to view** if other people or untrusted devices share your network.
4. Turn on HTTPS under **Settings → Password and security**. Compare the fingerprint the first time your browser warns.
5. Never forward a port on your router to the controller. It is not built to face the internet.
6. For MQTT, give the controller its own broker user, restrict the topics it may use, and turn on TLS if the broker supports it. See [MQTT and Home Assistant](../firmware/mqtt.md).
7. Keep the controller where strangers cannot get at it, and keep a note of the setup code somewhere private.
8. Install firmware only from the project's [releases](../firmware/releases.md), and check the checksums.

## ESPHome

The ESPHome package uses ESPHome's own security: the API encryption key and
the OTA password you put in your device file. Set both. See the ESPHome
documentation for those options; the ZoneFlow package does not change them.
