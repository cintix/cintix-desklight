(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var state = { mode: "solid", color: "#6a0a7f", brightness: 80, bpm: 60, alwaysOn: false };
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
    // Guarded: a browser holding a cached page from before this field existed
    // would otherwise throw here, and every handler calls applyUi() *before*
    // send() - so a missing checkbox would silently stop all saving.
    if ($("always-on")) $("always-on").checked = !!state.alwaysOn;
    $("pulse").style.setProperty("--pulse-color", state.color);
    $("pulse").style.setProperty("--pulse-glow", hexToGlow(state.color));
    // 0 BPM is the slider's minimum and means "no tempo": the lamp holds a
    // steady faint glow, so the preview stops throbbing too. Guarded because
    // 60000/0 is Infinity, which is not a usable CSS duration.
    if (state.bpm > 0) {
      $("pulse").style.setProperty("--beat-ms", Math.round(60000 / state.bpm) + "ms");
      $("pulse").classList.remove("idle");
    } else {
      $("pulse").classList.add("idle");
    }
  }

  function setStatus(connected, host, alwaysOn) {
    var el = $("status");
    el.classList.toggle("online", connected);
    // Reachable over USB but dark because the lamp sees no live host PC - that
    // is not the same as having lost the lamp, so it gets its own wording.
    el.classList.toggle("nohost", connected && host === false && !alwaysOn);
    if (!connected) {
      el.textContent = "Ingen forbindelse til lampen";
      return;
    }
    var text = "Lampen er tilsluttet via USB";
    if (host === false) {
      text += alwaysOn ? " • Ingen PC (altid tændt)"
                       : " • Ingen PC – lyset er slukket";
    }
    el.textContent = text;
  }

  function setOffline() {
    var el = $("status");
    el.classList.remove("online");
    el.classList.remove("nohost");
    el.textContent = "Ingen forbindelse til tjenesten";
  }

  function applyRemote(d) {
    if (!d) return;
    var before = JSON.stringify(state);
    if (d.mode) state.mode = d.mode;
    if (d.color) state.color = d.color;
    if (typeof d.brightness === "number") state.brightness = d.brightness;
    if (typeof d.bpm === "number") state.bpm = d.bpm;
    if (typeof d.alwaysOn === "boolean") state.alwaysOn = d.alwaysOn;
    if (JSON.stringify(state) !== before) applyUi();
  }

  function refresh() {
    fetch("/api/state")
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) return;
        setStatus(!!d.connected, d.host, !!d.alwaysOn);
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
        setStatus(!!d.connected, d.host, !!d.alwaysOn);
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
  if ($("always-on")) {
    $("always-on").addEventListener("change", function () {
      state.alwaysOn = this.checked;
      applyUi();
      send();
    });
  }

  refresh();
  setInterval(refresh, 2000);
})();
