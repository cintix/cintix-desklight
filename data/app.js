(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var state = { mode: "solid", color: "#6a0a7f", brightness: 80, bpm: 124 };
  var sending = false;
  var timer = null;
  var changedAt = 0;  // last local change - avoids fights with the poll

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
    $("pulse").style.setProperty("--pulse-color", state.color);
    $("pulse").style.setProperty("--pulse-glow", hexToGlow(state.color));
    $("pulse").style.setProperty("--beat-ms", Math.round(60000 / state.bpm) + "ms");
  }

  function setStatus(connected) {
    var el = $("status");
    el.classList.toggle("online", connected);
    el.textContent = connected
      ? "Lampen er tilsluttet via USB"
      : "Ingen forbindelse til lampen";
  }

  function setOffline() {
    var el = $("status");
    el.classList.remove("online");
    el.textContent = "Ingen forbindelse til tjenesten";
  }

  function applyRemote(d) {
    if (!d) return;
    var before = JSON.stringify(state);
    if (d.mode) state.mode = d.mode;
    if (d.color) state.color = d.color;
    if (typeof d.brightness === "number") state.brightness = d.brightness;
    if (typeof d.bpm === "number") state.bpm = d.bpm;
    if (JSON.stringify(state) !== before) applyUi();
  }

  function refresh() {
    fetch("/api/state")
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) return;
        setStatus(!!d.connected);
        if (!sending && Date.now() - changedAt > 300) applyRemote(d);
      })
      .catch(function () { setOffline(); });
  }

  function queueSend() {
    changedAt = Date.now();
    if (timer) clearTimeout(timer);
    timer = setTimeout(send, 120);
  }

  function send() {
    if (sending) return;
    sending = true;
    var body = new URLSearchParams(state);
    fetch("/api/control", { method: "POST", body: body })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) return;
        setStatus(!!d.connected);
        if (d.connected) applyRemote(d);
      })
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

  refresh();
  setInterval(refresh, 2000);
})();
