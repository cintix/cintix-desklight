(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var state = { mode: "solid", color: "#6a0a7f", brightness: 80, bpm: 124, alwaysOn: false };
  var sending = false;
  var timer = null;

  function hexToGlow(hex) {
    var r = parseInt(hex.substr(1, 2), 16);
    var g = parseInt(hex.substr(3, 2), 16);
    var b = parseInt(hex.substr(5, 2), 16);
    return "rgba(" + r + "," + g + "," + b + ",.6)";
  }

  function applyUi() {
    var buttons = document.querySelectorAll(".modes button");
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].classList.toggle("active", buttons[i].getAttribute("data-mode") === state.mode);
    }
    var rainbow = (state.mode === "rainbow");
    $("color").value = state.color;
    $("color").disabled = rainbow;
    $("color-field").classList.toggle("disabled", rainbow);
    $("brightness").value = state.brightness;
    $("brightness-val").textContent = state.brightness + " %";
    $("bpm").value = state.bpm;
    $("bpm-val").textContent = state.bpm;
    $("bpm-field").hidden = (state.mode !== "beat");
    $("always-on").checked = !!state.alwaysOn;
    $("pulse").style.setProperty("--pulse-color", state.color);
    $("pulse").style.setProperty("--pulse-glow", hexToGlow(state.color));
    $("pulse").style.setProperty("--beat-ms", Math.round(60000 / state.bpm) + "ms");
  }

  function setStatus(ip, isAp, host, alwaysOn) {
    var el = $("status");
    var text = "Tilsluttet" + (isAp ? " (Adgangspunkt)" : "") + " • " + ip;
    el.classList.add("online");
    // Reachable over WiFi but dark because the host PC is gone - that is not the
    // same as having lost the device, so it gets its own wording and colour.
    el.classList.toggle("nohost", host === false && !alwaysOn);
    if (host === false) {
      text += alwaysOn ? " • Ingen værts-PC (altid tændt)"
                       : " • Ingen værts-PC – lyset er slukket";
    }
    el.textContent = text;
  }

  function setOffline() {
    var el = $("status");
    el.classList.remove("online");
    el.classList.remove("nohost");
    el.textContent = "Ingen forbindelse til enheden";
  }

  function queueSend() {
    if (timer) clearTimeout(timer);
    timer = setTimeout(send, 120);
  }

  function send() {
    if (sending) return;
    sending = true;
    var body = new URLSearchParams(state);
    fetch("/api/control", { method: "POST", body: body })
      .then(function (r) { return r.json(); })
      .then(function (d) { if (d && d.ip) setStatus(d.ip, !!d.ap, d.host, !!d.alwaysOn); })
      .catch(function () { setOffline(); })
      .then(function () { sending = false; });
  }

  var modeButtons = document.querySelectorAll(".modes button");
  for (var j = 0; j < modeButtons.length; j++) {
    modeButtons[j].addEventListener("click", function () {
      state.mode = this.getAttribute("data-mode");
      applyUi();
      send();
    });
  }
  $("color").addEventListener("input", function () {
    state.color = this.value;
    applyUi();
    queueSend();
  });
  $("brightness").addEventListener("input", function () {
    state.brightness = parseInt(this.value, 10);
    applyUi();
    queueSend();
  });
  $("bpm").addEventListener("input", function () {
    state.bpm = parseInt(this.value, 10);
    applyUi();
    queueSend();
  });
  $("always-on").addEventListener("change", function () {
    state.alwaysOn = this.checked;
    applyUi();
    send();
  });

  fetch("/api/state")
    .then(function (r) { return r.json(); })
    .then(function (d) {
      if (!d) return;
      if (d.mode) state.mode = d.mode;
      if (d.color) state.color = d.color;
      if (typeof d.brightness === "number") state.brightness = d.brightness;
      if (typeof d.bpm === "number") state.bpm = d.bpm;
      if (typeof d.alwaysOn === "boolean") state.alwaysOn = d.alwaysOn;
      applyUi();
      if (d.ip) setStatus(d.ip, !!d.ap, d.host, !!d.alwaysOn);
    })
    .catch(function () { setOffline(); });
})();
