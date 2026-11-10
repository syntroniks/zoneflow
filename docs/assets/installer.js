/* Installer page glue. Shows the install button only when a release exists and the browser can use WebSerial.
   Logic is in installer-lib.js. No data is sent anywhere: the page reads release.json from this site. */
(function () {
  "use strict";
  var root = document.getElementById("zf-installer");
  if (!root) return;
  var lib = window.ZFInstaller;
  var base = new URL("../releases/latest/", document.baseURI).href;

  function $(id) {
    return document.getElementById(id);
  }
  function show(id, on) {
    $(id).hidden = !on;
  }
  function text(id, value) {
    $(id).textContent = value;
  }

  show("zf-js-needed", false);

  var hasSerial = "serial" in navigator;
  var chromium = hasSerial && window.isSecureContext;
  if (!hasSerial) show("zf-no-serial", true);
  else if (!window.isSecureContext) show("zf-insecure", true);

  fetch(base + "release.json", { cache: "no-cache" })
    .then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    })
    .then(start, function () {
      show("zf-no-release", true);
    });

  function start(release) {
    if (release.schema !== 1 || !release.flavours || !release.flavours.length) {
      show("zf-no-release", true);
      return;
    }
    show("zf-release", true);
    text("zf-version", release.version);
    text("zf-built", release.built_utc);
    $("zf-notes").href = "https://github.com/" + release.repository + "/releases/tag/" + encodeURIComponent(release.tag);
    show("zf-prerelease", !!release.prerelease);

    var select = $("zf-flavour");
    release.flavours.forEach(function (f) {
      var o = document.createElement("option");
      o.value = f.id;
      o.textContent = f.name;
      select.appendChild(o);
    });
    show("zf-flavour-row", release.flavours.length > 1);
    select.addEventListener("change", function () {
      render(release, select.value);
    });
    render(release, select.value);
  }

  function render(release, id) {
    var f = lib.find(release, id);
    var cmd = lib.commands(release, id, base);
    var step = lib.nextStep(f);
    text("zf-flavour-desc", f.description);
    text("zf-next-title", step.title);
    text("zf-next-text", step.text);
    var link = $("zf-next-link");
    link.hidden = !step.link;
    link.href = step.link;
    link.textContent = step.linkText;

    var slot = $("zf-button-slot");
    slot.textContent = "";
    if (chromium) {
      var btn = document.createElement("esp-web-install-button");
      btn.setAttribute("manifest", base + f.manifest);
      var label = document.createElement("button");
      label.slot = "activate";
      label.className = "md-button md-button--primary";
      label.textContent = "Install " + f.name + " " + release.version;
      btn.appendChild(label);
      var unsup = document.createElement("span");
      unsup.slot = "unsupported";
      unsup.textContent = "This browser cannot flash over USB.";
      btn.appendChild(unsup);
      slot.appendChild(btn);
    }
    text("zf-cmd-download", cmd.download);
    text("zf-cmd-verify", cmd.verify);
    text("zf-cmd-factory", cmd.factory);
    text("zf-cmd-erase", cmd.eraseFlash);
    show("zf-parts", !!cmd.parts);
    text("zf-cmd-parts", cmd.parts);
    var body = $("zf-parts-body");
    body.textContent = "";
    cmd.partsTable.forEach(function (row) {
      var tr = document.createElement("tr");
      [row.offset, row.name, row.file].forEach(function (v, i) {
        var td = document.createElement(i === 2 ? "td" : "td");
        var c = document.createElement("code");
        c.textContent = v;
        td.appendChild(c);
        tr.appendChild(td);
      });
      body.appendChild(tr);
    });
  }
})();
