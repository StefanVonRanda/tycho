// Fetch a second asset off the same keep-alive connection and report on it.
(function () {
  "use strict";
  var out = document.getElementById("out");
  if (!out) return;

  var started = performance.now();
  fetch("/data.json", { cache: "no-store" })
    .then(function (res) {
      var ms = (performance.now() - started).toFixed(1);
      return res.json().then(function (body) {
        out.textContent = [
          "GET /data.json -> " + res.status + " " + res.statusText,
          "content-type:   " + res.headers.get("content-type"),
          "server:         " + res.headers.get("server"),
          "round trip:     " + ms + " ms",
          "",
          JSON.stringify(body, null, 2)
        ].join("\n");
      });
    })
    .catch(function (err) {
      out.textContent = "fetch failed: " + err;
    });
})();
