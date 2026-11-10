/* Pure logic of the installer page: no DOM. Tested by tools/ewt/installer.test.mjs against a real release.json.
   Everything the page shows about a release (offsets, file names, flash settings, esptool version) comes
   from release.json, the same data the ESP Web Tools manifest was made from. */
(function (root) {
  "use strict";

  function hex(n) {
    return "0x" + n.toString(16);
  }

  function find(release, id) {
    return (release.flavours || []).find(function (f) {
      return f.id === id;
    });
  }

  /* What the user does after the flash, in words. Unknown values are not guessed at. */
  function nextStep(flavour) {
    if (flavour.next_step === "ble-or-softap" && !flavour.improv) {
      return {
        improv: false,
        title: "Connect it to your WiFi",
        text:
          "This firmware does not support Improv Serial, so the installer will not ask for WiFi credentials. " +
          "After the flash the controller starts WiFi setup by itself: it advertises over Bluetooth LE as " +
          "ZoneFlow-XXXX for 10 minutes (use Espressif's ESP BLE Provisioning app and the setup code from the " +
          "display or the USB serial console), then opens a WiFi access point of the same name with the setup " +
          "code as its password.",
        link: "../../getting-started/provisioning/",
        linkText: "Provisioning guide",
      };
    }
    if (flavour.next_step === "improv" && flavour.improv) {
      return {
        improv: true,
        title: "Connect it to your WiFi",
        text: "After the flash the installer offers to set up WiFi over the USB cable (Improv Serial).",
        link: "",
        linkText: "",
      };
    }
    return { improv: !!flavour.improv, title: "Next step", text: "See the firmware's own documentation.", link: "", linkText: "" };
  }

  /* Commands for people who cannot use the browser installer. */
  function commands(release, id, baseUrl) {
    var f = find(release, id);
    if (!f) throw new Error("no flavour " + id);
    var esptool = "uvx esptool@" + ((release.tools && release.tools.esptool) || "5.6.0");
    var factory = f.images.factory;
    var out = {
      files: [f.images.factory.file, "SHA256SUMS"],
      download: "",
      verify: "",
      factory: "",
      parts: "",
      partsTable: [],
      eraseFlash: esptool + " --chip " + f.chip + " --port PORT erase-flash",
    };
    out.download = out.files
      .map(function (n) {
        return "curl -LO " + baseUrl + n;
      })
      .join("\n");
    out.verify =
      "grep ' " + factory.file + "$' SHA256SUMS | sha256sum --check -    # macOS: shasum -a 256 --check -";
    out.factory =
      esptool + " --chip " + f.chip + " --port PORT --baud 460800 write-flash " + hex(factory.offset) + " " + factory.file;
    if (f.flash && f.partitions) {
      var order = ["bootloader", "partition-table", "otadata", "app", "www"].filter(function (k) {
        return f.images[k];
      });
      order.sort(function (a, b) {
        return f.images[a].offset - f.images[b].offset;
      });
      out.partsTable = order.map(function (k) {
        return { name: k, offset: hex(f.images[k].offset), file: f.images[k].file };
      });
      out.parts =
        esptool + " --chip " + f.chip + " --port PORT --baud 460800 write-flash" +
        " --flash-mode " + f.flash.mode + " --flash-freq " + f.flash.freq + " --flash-size " + f.flash.size + " \\\n  " +
        order
          .map(function (k) {
            return hex(f.images[k].offset) + " " + f.images[k].file;
          })
          .join(" \\\n  ");
    }
    return out;
  }

  var api = { hex: hex, find: find, nextStep: nextStep, commands: commands };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.ZFInstaller = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
