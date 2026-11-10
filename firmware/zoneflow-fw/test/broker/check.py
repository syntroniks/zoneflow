# SPDX-License-Identifier: Apache-2.0
"""Drives harness.cpp against a local Mosquitto and checks the result.

Usage: check.py <mosquitto binary> <harness binary>
Exit status 0 when every check passes. Prints one line per check.
"""
import json
import os
import signal
import subprocess
import sys
import tempfile
import time

import paho.mqtt.client as mqtt

PORT = 18831
ROOT = "zoneflow/a1b2c3d4e5f6"
DISCOVERY = "homeassistant/device/zoneflow_a1b2c3d4e5f6/config"

failures = []


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        failures.append(name)


class Observer:
    def __init__(self):
        self.messages = []  # (topic, payload, retain, time)
        self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="observer")
        self.c.on_message = self.on_message
        self.c.connect("127.0.0.1", PORT)
        self.c.subscribe([(ROOT + "/#", 1), ("homeassistant/#", 1)])
        self.c.loop_start()

    def on_message(self, _c, _u, m):
        self.messages.append((m.topic, m.payload.decode("utf-8", "replace"), m.retain, time.monotonic()))

    def pub(self, topic, payload, retain=False):
        self.c.publish(topic, payload, qos=1, retain=retain).wait_for_publish(2)

    def on(self, topic, since=0.0):
        return [(p, r) for (t, p, r, ts) in self.messages if t == topic and ts >= since]

    def wait(self, pred, timeout=5.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if pred():
                return True
            time.sleep(0.05)
        return pred()

    def results(self, since=0.0):
        return [json.loads(p) for p, _ in self.on(ROOT + "/result", since) if p]


def main():
    broker_bin, harness_bin = sys.argv[1], sys.argv[2]
    conf = tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False)
    conf.write(f"listener {PORT} 127.0.0.1\nallow_anonymous true\npersistence false\n")
    conf.close()
    broker = subprocess.Popen([broker_bin, "-c", conf.name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    harness = None
    try:
        obs = Observer()
        time.sleep(0.3)
        # A retained command left on the broker before the device connects.
        obs.pub(ROOT + "/cmd/zone/1/run", "600", retain=True)
        harness = subprocess.Popen([harness_bin, str(PORT), "60"])

        check("availability online (retained)", obs.wait(lambda: ("online", False) in obs.on(ROOT + "/status")
                                                         or ("online", True) in obs.on(ROOT + "/status")))
        check("discovery published", obs.wait(lambda: len(obs.on(DISCOVERY)) >= 1))
        docs = [json.loads(p) for p, _ in obs.on(DISCOVERY) if p]
        full = docs[-1] if docs else {}
        cmps = full.get("components", {})
        check("discovery has device, origin and components",
              "device" in full and "origin" in full and len(cmps) > 10, str(list(full.keys())))
        check("discovery is not retained", all(not r for _, r in obs.on(DISCOVERY)))
        check("disabled zone 3 removed with a platform-only entry",
              any(d.get("components", {}).get("zone_3_valve") == {"platform": "valve"} for d in docs))
        check("zone 1 valve announced", cmps.get("zone_1_valve", {}).get("platform") == "valve")
        print(f"     discovery document: {len(obs.on(DISCOVERY)[-1][0])} bytes, {len(cmps)} components")

        check("retained command ignored",
              obs.wait(lambda: any(r.get("status") == "ignored_retained" for r in obs.results())),
              str(obs.results()))
        time.sleep(0.5)
        zone1 = [json.loads(p) for p, _ in obs.on(ROOT + "/zone/1") if p]
        check("zone 1 never opened by the retained command", zone1 and all(z["valve"] == "closed" for z in zone1))
        # Clearing it: the broker forwards the empty message, not retained.
        tc = time.monotonic()
        obs.pub(ROOT + "/cmd/zone/1/run", "", retain=True)
        check("clearing the retained command is not a command",
              obs.wait(lambda: any(r.get("error") == "empty_payload" for r in obs.results(tc))), str(obs.results(tc)))
        time.sleep(0.5)
        check("zone 1 not opened by the clear", not any('"zone":1,"state":"open"' in p
                                                         for p, _ in obs.on(ROOT + "/event", tc)))

        t0 = time.monotonic()
        obs.pub(ROOT + "/cmd/zone/2/run", "60")
        check("run zone 2 queued", obs.wait(lambda: any(r.get("command") == "zone_run" and r.get("status") == "queued"
                                                        for r in obs.results(t0))))
        check("zone 2 opens", obs.wait(lambda: any(json.loads(p)["valve"] == "open"
                                                   for p, _ in obs.on(ROOT + "/zone/2", t0) if p)),
              str(obs.on(ROOT + "/zone/2", t0)) + str(obs.results(t0)) + str(obs.on(ROOT + "/event", t0)))

        t1 = time.monotonic()
        obs.pub(ROOT + "/cmd/program/4/run", "RUN")
        time.sleep(0.3)
        obs.pub(ROOT + "/cmd/program/4/run", "RUN")
        check("second program run rejected by the core (CommandRejected)",
              obs.wait(lambda: any(r.get("status") == "rejected" and r.get("error") == "already_queued"
                                   for r in obs.results(t1))), str(obs.results(t1)))

        t2 = time.monotonic()
        for bad in [("zone/2/run", "99999"), ("zone/9/run", "60"), ("zone/3/run", "60"), ("nonsense", "1")]:
            obs.pub(ROOT + "/cmd/" + bad[0], bad[1])
        check("invalid commands answered with errors", obs.wait(lambda: sorted(
            r.get("error") for r in obs.results(t2) if r.get("status") == "invalid") ==
            sorted(["duration_out_of_range", "no_such_zone", "zone_disabled", "unknown_command"])),
            str(obs.results(t2)))

        time.sleep(1.5)  # let the rate limiter refill
        t3 = time.monotonic()
        for _ in range(200):
            obs.c.publish(ROOT + "/cmd/pause", "PRESS", qos=0)
        obs.wait(lambda: len(obs.results(t3)) >= 200, 5)
        res = obs.results(t3)
        limited = sum(1 for r in res if r.get("status") == "rate_limited")
        queued = sum(1 for r in res if r.get("status") == "queued")
        check("flood of 200 commands rate-limited", limited >= 150 and queued <= 30, f"queued={queued} limited={limited}")
        t4 = time.monotonic()
        check("device keeps publishing after the flood",
              obs.wait(lambda: len(obs.on(ROOT + "/device", t4)) >= 1, 7))

        t5 = time.monotonic()
        obs.pub("homeassistant/status", "online")
        check("discovery republished 1 to 5 s after a Home Assistant birth",
              obs.wait(lambda: len(obs.on(DISCOVERY, t5)) >= 1, 7))
        delay = obs.on(DISCOVERY, t5) and [ts for (t, _, _, ts) in obs.messages if t == DISCOVERY and ts >= t5][0] - t5
        check("birth delay within 0.9..5.5 s", bool(delay) and 0.9 <= delay <= 5.5, f"{delay}")

        t6 = time.monotonic()
        harness.send_signal(signal.SIGKILL)
        harness.wait()
        check("last will 'offline' after the device vanishes",
              obs.wait(lambda: ("offline", False) in obs.on(ROOT + "/status", t6), 12))

        late = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="late")
        got = {}
        late.on_message = lambda _c, _u, m: got.setdefault(m.topic, (m.payload.decode(), m.retain))
        late.connect("127.0.0.1", PORT)
        late.subscribe(ROOT + "/#")
        late.loop_start()
        time.sleep(1.0)
        late.loop_stop()
        check("late subscriber sees retained status offline", got.get(ROOT + "/status") == ("offline", True))
        check("late subscriber sees retained zone and device state",
              got.get(ROOT + "/device", ("", False))[1] and got.get(ROOT + "/zone/2", ("", False))[1])
        check("no retained command left", ROOT + "/cmd/zone/1/run" not in got)
    finally:
        if harness and harness.poll() is None:
            harness.kill()
        broker.terminate()
        broker.wait()
        os.unlink(conf.name)
    print(f"{len(failures)} failed" if failures else "all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
