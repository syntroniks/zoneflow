// GENERATED from docs/firmware/openapi.json by scripts/gen-api.ts. Do not edit.
// Regenerate with `npm run gen:api`; CI fails if this file is stale.

export interface paths {
    "/auth": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Authentication state
         * @description Whether an admin password exists and whether this request carries a valid session. For a cookie session it also returns the CSRF token, so a reloaded page can recover it.
         */
        get: operations["getAuth"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/auth/login": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Log in
         * @description Rate limited per client address (5 free failures, then 1 s doubling to 15 min) and in total (30 failures in 5 min lock everyone out for 60 s). A refused attempt answers 429 with Retry-After.
         */
        post: operations["login"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/auth/setup": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Set the first admin password
         * @description Only while no password exists. The setup code is shown on the device display and serial log, never over the network; knowing it proves physical access. Rate limited like login.
         */
        post: operations["setupPassword"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/auth/logout": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Log out
         * @description Ends this session and clears the cookie.
         */
        post: operations["logout"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/auth/password": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /** Change the admin password */
        post: operations["changePassword"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/security": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Web security settings */
        get: operations["getSecurity"];
        /**
         * Change web security settings
         * @description Partial update. Turning HTTPS on or off takes effect after a reboot (`restart_required`).
         */
        put: operations["putSecurity"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/info": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Device and firmware information */
        get: operations["getInfo"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/status": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Full status snapshot
         * @description Everything a dashboard needs in one call: zones, master, queue, sensors, rain delay, inhibits, faults, network and OTA state.
         */
        get: operations["getStatus"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/events": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Live event stream (Server-Sent Events)
         * @description On connect the server sends a `state` message (same body as GET /status), then one message per change: `event` for each controller event, `state` when the snapshot changed (at most every 500 ms, and at least every 30 s), `network` and `ota` when those change, and a comment line every 15 s as keep-alive. At most 3 streams are open at once; a fourth gets 503.
         */
        get: operations["getEvents"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/zones": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** List zones */
        get: operations["listZones"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/zones/{zone}": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** One zone */
        get: operations["getZone"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        /**
         * Rename or configure a zone
         * @description Changes the zone's entry in the configuration. Validation errors name the field in `error.path`.
         */
        patch: operations["patchZone"];
        trace?: never;
    };
    "/zones/{zone}/run": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Run a zone for N seconds
         * @description Adds a manual run to the end of the queue. The zone driver clamps the time to the zone's `max_run_s` (the answer shows the clamped value). Refused (409) for a disabled zone (zone_disabled), a locked-out zone (locked_out), a resting zone (zone_resting, with Retry-After), a locked-out master (master_locked_out), without 24 VAC and during maintenance (inhibited). The answer waits up to 250 ms for the control task: a rejection comes back as an error (see RunRejection), an accepted command as 202 with confirmed true. If no verdict arrives in time the answer is 202 with confirmed false and a rejection, if any, appears on the event stream as command_rejected.
         */
        post: operations["runZone"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/zones/{zone}/stop": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Stop a running zone
         * @description Closes the zone if it is the running queue entry (the queue then moves on). A zone that is only queued further back gives 409 `not_head`; use POST /queue/stop.
         */
        post: operations["stopZone"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/programs": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** List programs */
        get: operations["listPrograms"];
        put?: never;
        /**
         * Create a program
         * @description Without `id` the next free id is used. At most 16 programs. Fields left out take their defaults.
         */
        post: operations["createProgram"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/programs/{id}": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** One program */
        get: operations["getProgram"];
        /**
         * Replace a program
         * @description Replaces the whole program; fields left out take their defaults. `error.path` is relative to the program, for example `starts[1]`.
         */
        put: operations["replaceProgram"];
        post?: never;
        /** Delete a program */
        delete: operations["deleteProgram"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/programs/{id}/run": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Run a program now
         * @description Queues every zone of the program (manual program run; seasonal adjustment applies). The answer waits up to 250 ms for the control task: a rejection comes back as an error (see RunRejection), an accepted command as 202 with confirmed true. If no verdict arrives in time the answer is 202 with confirmed false and a rejection, if any, appears on the event stream as command_rejected.
         */
        post: operations["runProgram"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/queue": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Run queue */
        get: operations["getQueue"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/queue/pause": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Pause the queue
         * @description Closes the running zone and keeps its remaining time.
         */
        post: operations["queuePause"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/queue/resume": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Resume the queue
         * @description Reopens the paused zone for its remaining time.
         */
        post: operations["queueResume"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/queue/skip": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Skip the running entry
         * @description Drops the head entry and starts the next.
         */
        post: operations["queueSkip"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/queue/stop": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Stop all watering
         * @description Closes everything and empties the queue.
         */
        post: operations["queueStop"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/rain-delay": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Rain delay */
        get: operations["getRainDelay"];
        /**
         * Set a rain delay
         * @description Scheduled starts are skipped until the time given; manual runs still work. Needs a valid clock (409 `time_not_set` otherwise). At most 30 days ahead.
         */
        put: operations["setRainDelay"];
        post?: never;
        /** Clear the rain delay */
        delete: operations["clearRainDelay"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sensors": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Sensor readings */
        get: operations["getSensors"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/faults": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Active faults */
        get: operations["listFaults"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/faults/clear": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Clear latched faults and lockouts
         * @description A fault whose cause is still present is raised again on the next sample. Output-stage faults are acknowledged as well, except a keep-alive detector fault (faults.hardware.repair_needed), which needs POST /faults/acknowledge-repair. Trip counts are reset.
         */
        post: operations["clearFaults"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/faults/acknowledge-repair": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Confirm that the board was repaired after a keep-alive detector fault
         * @description A fault of a safety layer (hardware design 19.7 steps 1, 2, 2b, 5b and 9: keepalive_detector, keepalive_bridged, relay_gate_bleed; and since the hardware answers to section 19 the rectifier fault of 19.10, rectifier_fault, the over-voltage of rule 18, supply_over_voltage, and a held set input of the latch of 19.8, latch_input_held) means the board needs repair, so it is not cleared with one press. This is the second, deliberate action: the body must say repaired: true. 409 `no_repair_fault` when no such fault is active. The board then learns its detector release times again, and the full self-test runs before any valve opens.
         */
        post: operations["acknowledgeRepair"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/config": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Whole configuration */
        get: operations["getConfig"];
        /**
         * Replace the whole configuration
         * @description Validated as a whole; nothing changes on error and `error.path` names the field. Missing fields take defaults, unknown fields are ignored. Nested deeper than 16 levels is rejected before parsing. A document from newer firmware is refused (400, error.code unsupported_schema, error.path schema, the core's message).
         */
        put: operations["putConfig"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/config/backup": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Download a backup
         * @description The configuration with a small header. Never contains secrets: no WiFi credentials, password hash, sessions, setup code or TLS key.
         */
        get: operations["backupConfig"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/config/restore": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Restore a backup
         * @description A document from newer firmware is refused (400, error.code unsupported_schema, error.path schema, the core's message).
         */
        post: operations["restoreConfig"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/time": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Clock and time zone */
        get: operations["getTime"];
        /**
         * Set the clock or the time zone
         * @description `utc` sets the clock (and the RTC) by hand; SNTP overrides it at the next sync. `timezone` is a POSIX TZ string stored in the configuration.
         */
        put: operations["setTime"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Network status */
        get: operations["getNetwork"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network/reprovision": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Restart into WiFi provisioning
         * @description The stored WiFi credentials stay until new ones connect; after 15 minutes without a new setup the device returns to the stored network. Same as the long-press on the device.
         */
        post: operations["reprovision"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/provision": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Provisioning state
         * @description Used by the setup page on the SoftAP. Never returns the setup code.
         */
        get: operations["getProvisioning"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/provision/networks": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Scan for WiFi networks
         * @description Only while SoftAP provisioning runs (409 `not_provisioning` otherwise), and only from a client joined to the setup access point (403 `setup_ap_only` from the home network, which the station joins before provisioning ends). The setup code is the WPA2 passphrase of that access point, so a client on it has already proven it knows the code.
         */
        get: operations["scanNetworks"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/provision/wifi": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Send WiFi credentials
         * @description Only while SoftAP provisioning runs, and only from a client joined to the setup access point (403 `setup_ap_only` otherwise). Optionally sets the first admin password.
         */
        post: operations["provisionWifi"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/system/reboot": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Reboot
         * @description Outputs are forced off by the restart handler.
         */
        post: operations["reboot"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/system/factory-reset": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Factory reset
         * @description Stops watering, then erases configuration, runtime state, WiFi credentials, the admin password, sessions, web settings and the TLS certificate. The setup code is kept so a printed label stays valid.
         */
        post: operations["factoryReset"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/firmware": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** Firmware, slots and update progress */
        get: operations["getFirmware"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/firmware/upload": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Upload a firmware image
         * @description Refused (409 `zones_active`) while a zone runs unless `force=true`. Before writing, the controller is put in maintenance mode, which closes every valve and blocks starts. The header is checked (ESP32-S3, project `zoneflow-fw`) before anything is written; the image is verified at the end. The new image must confirm itself after boot, otherwise the bootloader rolls back.
         */
        post: operations["uploadFirmware"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/firmware/pull": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Download and install an image from a URL
         * @description HTTPS only, checked against the ESP-IDF certificate bundle; redirects are followed (GitHub release assets redirect to a CDN). Same safety rules as upload.
         */
        post: operations["pullFirmware"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/firmware/assets": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        /**
         * Upload the web UI image
         * @description Replaces the web UI files. Does not touch the firmware or settings.
         */
        post: operations["uploadAssets"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/mqtt": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * MQTT settings and connection state
         * @description The `mqtt` object of the configuration, whether a password and a custom CA certificate are stored (never their values), and the client's connection state. Topic tree and Home Assistant discovery: [MQTT guide](https://syntroniks.github.io/zoneflow/firmware/mqtt/).
         */
        get: operations["getMqtt"];
        /**
         * Change MQTT settings
         * @description Partial update. Connection fields are validated by the core configuration (errors carry the field as `path`) and stored in the configuration; `password` and `ca_cert` are write-only and stored in NVS, outside the configuration and its backup. The client reconnects with the new settings within a second.
         */
        put: operations["putMqtt"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/history": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /**
         * Event history
         * @description Runs (zone, source, planned and actual duration, end reason, current, volume), skipped and missed starts, faults, and system events (boot with reset reason, clock, configuration, rain delay, sensors, updates), newest first. Kept in flash in a ring of 1200 records; written in batches (within 60 s, faults at once). Page back with `before`; fetch new records with `after`. No secrets are recorded.
         */
        get: operations["getHistory"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/setup": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        /**
         * Mark the first-run steps done
         * @description Stores `setup_complete` (shown by GET /auth) in the web settings, outside the configuration and its backup. A factory reset clears it.
         */
        put: operations["putSetup"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
}
export type webhooks = Record<string, never>;
export interface components {
    schemas: {
        Error: {
            error: {
                /** @description Stable machine-readable code, e.g. `invalid_config`, `csrf`. */
                code: string;
                message: string;
                /** @description JSON path of the offending field, when there is one. */
                path?: string;
            };
        };
        Accepted: {
            accepted?: boolean;
            command?: string;
            /** @description Run commands only. true: the control task handled the command and did not reject it. false: no verdict within 250 ms; a rejection, if any, arrives on the event stream as command_rejected. */
            confirmed?: boolean;
        };
        AuthStatus: {
            password_set: boolean;
            authenticated: boolean;
            protect_reads: boolean;
            /** @description Only for a valid cookie session. */
            csrf?: string;
            /** @description The web UI’s first-run steps were finished or skipped (PUT /setup). A hint for clients; it gates nothing. */
            setup_complete: boolean;
        };
        LoginRequest: {
            password: string;
        };
        SetupRequest: {
            setup_code: string;
            password: string;
        };
        PasswordChange: {
            current_password: string;
            new_password: string;
        };
        Session: {
            /** @description Bearer token; the same value is in the cookie. */
            token: string;
            /** @description Send as X-CSRF-Token on writes that use the cookie. */
            csrf: string;
            idle_timeout_s: number;
        };
        Security: {
            protect_reads: boolean;
            https: boolean;
            check_host: boolean;
            password_set: boolean;
            sessions: number;
            /** @description Of the self-signed certificate; empty if none. */
            tls_fingerprint_sha256: string;
            restart_required?: boolean;
        };
        SecurityUpdate: {
            /** @description GET routes need a session too. */
            protect_reads?: boolean;
            /** @description Serve on port 443 with a self-signed certificate. */
            https?: boolean;
            /** @description Refuse requests whose Host is not this device. */
            check_host?: boolean;
        };
        Info: {
            api_version: string;
            device_name: string;
            hostname: string;
            mac: string;
            board: string;
            firmware: {
                version: string;
                project: string;
                idf_version: string;
                build_date: string;
            };
            uptime_s: number;
            reset_reason: string;
            /** @description free, min_free and largest_block cover all 8-bit heap, PSRAM included; internal_* cover internal RAM only. */
            heap: {
                free: number;
                min_free: number;
                largest_block: number;
                internal_free: number;
                internal_min_free: number;
            };
            http_stack_free_min: number;
        };
        Zone: {
            id: number;
            name: string;
            enabled: boolean;
            max_run_s: number;
            current_min_ma: number;
            current_max_ma: number;
            /** @enum {string} */
            state: "off" | "waiting" | "open";
            remaining_s: number;
            /** @enum {string} */
            last_stop: "none" | "completed" | "requested" | "max_run_time" | "fault" | "ac_lost" | "shutdown" | "maintenance";
            fault: string;
            /** @description Latched fault on this zone; runs are refused until faults are cleared. */
            locked_out: boolean;
            /** @description The zone reached its maximum run time (summed across back-to-back runs) and must stay closed for driver.min_rest_s (spec section 2, rule 4). */
            resting: boolean;
            /** @description Seconds of rest left; 0 when not resting. */
            rest_remaining_s: number;
            /**
             * @description Duration of a run started without one (Home Assistant valve open, MQTT run with an empty payload). Clamped like any run.
             * @default 600
             */
            default_run_s: number;
            /** @description Mean current measured with this zone open alone (master share removed), since the last restart; null until it has run alone. Basis for setting current_min_ma and current_max_ma. */
            learned_current_ma: number | null;
        };
        ZoneUpdate: {
            name?: string;
            enabled?: boolean;
            max_run_s?: number;
            current_min_ma?: number;
            current_max_ma?: number;
            /**
             * @description Duration of a run started without one (Home Assistant valve open, MQTT run with an empty payload). Clamped like any run.
             * @default 600
             */
            default_run_s?: number;
        };
        ZoneRun: {
            duration_s: number;
        };
        ZoneRunAccepted: components["schemas"]["Accepted"] & {
            zone?: number;
            /** @description After clamping to max_run_s. */
            duration_s?: number;
        };
        Program: {
            id: number;
            name: string;
            enabled: boolean;
            /** @description Local start times HH:MM. */
            starts: string[];
            days: {
                /** @enum {string} */
                type: "weekdays" | "interval" | "odd" | "even";
                weekdays?: ("sun" | "mon" | "tue" | "wed" | "thu" | "fri" | "sat")[];
                /** @description Interval rule: every N days. */
                every?: number;
                /**
                 * Format: date
                 * @description First day of an interval rule (local).
                 */
                anchor?: string;
            };
            /** @description Seconds per zone; index 0 is zone 1. 0 leaves the zone out. */
            durations_s: number[];
            /** @description Scale by schedule.seasonal_percent. */
            seasonal: boolean;
            /** @description Skip while the rain sensor is active. */
            obey_sensor: boolean;
        };
        /** @description Same as Program. `id` is optional on create and must match the path on replace. */
        ProgramInput: {
            id?: number;
            name?: string;
            enabled?: boolean;
            /** @description Local start times HH:MM. */
            starts?: string[];
            days?: {
                /** @enum {string} */
                type: "weekdays" | "interval" | "odd" | "even";
                weekdays?: ("sun" | "mon" | "tue" | "wed" | "thu" | "fri" | "sat")[];
                /** @description Interval rule: every N days. */
                every?: number;
                /**
                 * Format: date
                 * @description First day of an interval rule (local).
                 */
                anchor?: string;
            };
            /** @description Seconds per zone; index 0 is zone 1. 0 leaves the zone out. */
            durations_s?: number[];
            /** @description Scale by schedule.seasonal_percent. */
            seasonal?: boolean;
            /** @description Skip while the rain sensor is active. */
            obey_sensor?: boolean;
        };
        QueueItem: {
            id: number;
            zone: number | null;
            duration_s: number;
            /** @enum {string} */
            source: "schedule" | "manual_program" | "manual_zone";
            program_id: number | null;
        };
        Queue: {
            /** @enum {string} */
            state: "idle" | "running" | "paused";
            length: number;
            current_remaining_s: number;
            /** @description Head first; first 8. */
            items: components["schemas"]["QueueItem"][];
            /** @description schedule.pause_timeout_min: a queue paused this long is stopped (event pause_expired). */
            pause_timeout_min: number;
            /** @description Seconds since the first pause while paused, else 0. */
            paused_s: number;
            /** @description While paused: seconds until the queue is stopped (pause_timeout_min after the first pause). Null otherwise. */
            stop_in_s: number | null;
        };
        RainDelay: {
            active: boolean;
            until_utc: number | null;
            remaining_s: number;
        };
        /** @description Exactly one of the two fields. */
        RainDelayRequest: {
            hours?: number;
            /** @description Unix seconds, within the next 30 days. */
            until_utc?: number;
        };
        RainDelayAccepted: components["schemas"]["Accepted"] & {
            until_utc?: number;
        };
        Sensors: {
            rain: {
                enabled: boolean;
                active: boolean;
            };
            spare: {
                enabled: boolean;
                active: boolean;
            };
            flow: {
                enabled: boolean;
                rate_lpm: number;
                total_l: number;
            };
            ac_present: boolean;
            valve_current_ma: number | null;
            /** @description False while the valve-current readings are missing (fault current_sense_unavailable). Zones still run; open and short detection is blind. */
            current_sense_available: boolean;
            /** @description The hardware over-current latch removed gate drive and released the relay (fault over_current_trip, latched until faults are cleared). The controller clears the latch itself by holding the keep-alive low for 1.5 s; the output switched on within 500 ms before is blamed, and after three trips in a day it stays off until faults are cleared. */
            over_current_tripped: boolean;
            /** @description The 5 V sensor supply switch reports over-current or over-temperature (fault sensor_supply). Read from the I/O expander during runs and every 10 minutes between them. Sent by firmware with the rev A output stage; optional in this schema so that older firmware and the web UI's mock controller still validate. */
            sensor_supply_fault?: boolean;
        };
        Faults: {
            active: ("zone_open_circuit" | "zone_over_current" | "short_circuit" | "current_while_idle" | "ac_lost" | "flow_without_zones" | "config_invalid" | "time_invalid" | "storage_error" | "master_locked_out" | "current_sense_unavailable" | "over_current_trip" | "keepalive_detector" | "output_expander" | "relay_welded" | "output_shorted" | "trip_latch_stuck" | "sensor_supply" | "zone_dc_current" | "mains_signal" | "keepalive_bridged" | "relay_gate_bleed" | "not_commissioned" | "supply_low" | "supply_high" | "rectifier_fault" | "supply_over_voltage" | "latch_input_held")[];
            zones: {
                zone: number;
                fault: string;
                locked_out: boolean;
            }[];
            /** @description The output stage's latched self-test failure (hardware design 19.7), or null. While it is set nothing runs. repair_needed: one of the layers can no longer be relied on (steps 1, 2, 2b, 5b or 9); POST /faults/clear leaves it, POST /faults/acknowledge-repair clears it, and the board then learns its detector release times again. Sent by firmware with the rev A output stage; optional in this schema so that older firmware and the web UI's mock controller still validate. */
            hardware?: {
                check: string;
                /** @description Self-test step of the check, hardware design 19.7: "1", "2", "2b", "3", "4a", "4b", "4c", "5", "5b", "6", "7", "8" or "9". null for a check that is not a step of the sequence (bus error, the over-current latch of 19.8, a firmware limit, rule 16). A string since review A2 added steps 2b and 5b. */
                step: string | null;
                repair_needed: boolean;
            } | null;
        };
        Status: {
            sequence: number;
            utc: number;
            /** @enum {string} */
            time_status: "unset" | "rtc" | "synced";
            uptime_s: number;
            zones: components["schemas"]["Zone"][];
            master: {
                enabled: boolean;
                on: boolean;
                /** @description Locked out after master.fault_lockout_count consecutive current faults on the master; every zone is blocked until faults are cleared. */
                locked_out: boolean;
                /** @description Consecutive current faults attributed to the master. */
                fault_count: number;
            };
            queue: components["schemas"]["Queue"];
            sensors: components["schemas"]["Sensors"];
            rain_delay: components["schemas"]["RainDelay"];
            inhibit: {
                sensor: boolean;
                no_ac: boolean;
                maintenance: boolean;
                master_lockout: boolean;
                /** @description An output-stage fault blocks every start until acknowledged. Sent by firmware with the rev A output stage; optional in this schema so that older firmware and the web UI's mock controller still validate. */
                hardware?: boolean;
            };
            faults: components["schemas"]["Faults"];
            maintenance: boolean;
            config_revision: number;
            network: components["schemas"]["Network"];
            ota: components["schemas"]["OtaStatus"];
            hardware?: components["schemas"]["Hardware"];
        };
        /** @description The configuration document as stored and returned (every field present). The configuration document described in the firmware architecture, section 6. Missing fields take defaults; unknown fields are ignored. A document whose schema is newer than the firmware is refused with error.code unsupported_schema and path schema. */
        Config: {
            /**
             * @description Documents with schema 1 are accepted and migrated (adds `mqtt` and `zones[].default_run_s`).
             * @constant
             */
            schema: 2;
            device_name: string;
            timezone: string;
            zone_count: number;
            /** @description Always 8 entries; zones above zone_count are kept but not used. */
            zones: components["schemas"]["ConfigZone"][];
            master: components["schemas"]["ConfigMaster"];
            driver: components["schemas"]["ConfigDriver"];
            sensors: components["schemas"]["ConfigSensors"];
            diagnostics: components["schemas"]["ConfigDiagnostics"];
            schedule: components["schemas"]["ConfigSchedule"];
            mqtt: components["schemas"]["MqttSettings"];
        };
        Backup: {
            /** @constant */
            format: "zoneflow-backup";
            /** @constant */
            format_version: 1;
            created_utc: number;
            firmware_version: string;
            hostname: string;
            config: components["schemas"]["Config"];
        };
        Time: {
            utc: number;
            /** @enum {string} */
            status: "unset" | "rtc" | "synced";
            rtc_present: boolean;
            timezone: string;
            /** @description Local wall-clock time, ISO 8601 without offset. */
            local: string | null;
            utc_offset_s: number;
            dst: boolean;
        };
        TimeUpdate: {
            utc?: number;
            /** @example CET-1CEST,M3.5.0,M10.5.0/3 */
            timezone?: string;
        };
        Network: {
            /** @enum {string} */
            state: "off" | "provisioning" | "connecting" | "connected" | "waiting_retry";
            provisioned: boolean;
            /** @enum {string} */
            provisioning: "none" | "ble" | "softap";
            hostname: string;
            mac: string;
            ssid: string;
            bssid: string;
            channel: number;
            rssi: number;
            ip: string;
            netmask: string;
            gateway: string;
            reconnect_attempt: number;
            retry_in_ms: number;
            /** @description ESP-IDF wifi_err_reason_t. */
            last_disconnect_reason: number;
            disconnects: number;
            mdns: boolean;
        };
        ReprovisionRequest: {
            /**
             * @default ble
             * @enum {string}
             */
            transport?: "ble" | "softap";
        };
        ProvisioningState: {
            active: boolean;
            /** @enum {string} */
            transport: "none" | "ble" | "softap";
            state: string;
            provisioned: boolean;
            password_set: boolean;
            hostname: string;
            ip: string;
        };
        WifiNetwork: {
            ssid: string;
            rssi: number;
            secure: boolean;
        };
        ProvisionWifiRequest: {
            ssid: string;
            /** @description Empty or absent for an open network; else 8 to 64. */
            password?: string;
            /** @description Sets the first admin password if none exists. */
            admin_password?: string;
        };
        FactoryResetRequest: {
            /** @constant */
            confirm: "factory-reset";
        };
        OtaStatus: {
            /** @enum {string} */
            phase: "idle" | "preparing" | "receiving" | "verifying" | "rebooting" | "failed";
            /** @enum {string} */
            error: "none" | "busy" | "zones_active" | "outputs_not_off" | "bad_image" | "wrong_chip" | "wrong_project" | "too_large" | "truncated" | "flash_error" | "verify_failed" | "download_failed" | "bad_url" | "aborted";
            /** @enum {string} */
            source: "none" | "upload" | "pull";
            forced: boolean;
            received_bytes: number;
            total_bytes: number;
            new_version: string;
        };
        Firmware: {
            version: string;
            idf_version: string;
            build_date: string;
            running_slot: string;
            /** @enum {string} */
            running_state: "new" | "pending_verify" | "valid" | "invalid" | "aborted" | "undefined";
            next_slot: string;
            other_slot_version: string;
            ota: components["schemas"]["OtaStatus"];
        };
        PullRequest: {
            /** Format: uri */
            url: string;
            /** @default false */
            force?: boolean;
        };
        /** @description Error answer of a run command. error.code is the controller's RunError: invalid_zone, unknown_program, zero_duration, queue_full, already_queued, inhibited, nothing_to_run, locked_out, zone_resting, master_locked_out, hardware_fault (or inhibited when maintenance or missing 24 VAC is known beforehand). zone_resting answers carry Retry-After with the seconds of rest left. */
        RunRejection: components["schemas"]["Error"];
        /** @description The `mqtt` object of the configuration document (schema 2). */
        MqttSettings: {
            /** @default false */
            enabled: boolean;
            /** @description Broker host name or IP address, without scheme or port. Required when enabled. */
            host: string;
            /** @default 1883 */
            port: number;
            username: string;
            /**
             * @description Connect with TLS (mqtts). Usually port 8883.
             * @default false
             */
            tls: boolean;
            /**
             * @description With TLS: verify the broker against the ESP-IDF certificate bundle (public CAs) or against the CA certificate stored with `ca_cert`.
             * @default bundle
             * @enum {string}
             */
            ca: "bundle" | "custom";
            /** @description Empty: `zoneflow-<device id>` (station MAC, 12 hex digits). */
            client_id: string;
            /** @description Root of every topic. Empty: `zoneflow/<device id>`. No wildcards, no leading or trailing `/`. */
            base_topic: string;
            /**
             * @description Home Assistant MQTT discovery.
             * @default true
             */
            discovery: boolean;
            /** @default homeassistant */
            discovery_prefix: string;
            /**
             * @description Home Assistant entity type for zones.
             * @default valve
             * @enum {string}
             */
            ha_zone_entity: "valve" | "switch";
        };
        MqttStatus: {
            /** @enum {string} */
            state: "disabled" | "waiting_network" | "connecting" | "connected" | "waiting_retry";
            /** @example mqtt://192.168.1.10:1883 */
            broker: string;
            /** @example zoneflow/a1b2c3d4e5f6 */
            root_topic: string;
            connected_for_s: number | null;
            /** @description Successful connections since boot. */
            connects: number;
            /** @description Attempts since the last successful connection. */
            reconnect_attempt: number;
            retry_in_ms: number;
            last_error: string;
            published: number;
            publish_failed: number;
            commands_received: number;
            /** @description Invalid, retained, rate-limited or refused by the controller. */
            commands_rejected: number;
            events_dropped: number;
            discovery_published: boolean;
        };
        Mqtt: components["schemas"]["MqttSettings"] & {
            /** @description A password is stored. The password itself is never returned. */
            password_set: boolean;
            /** @description A custom CA certificate is stored. */
            ca_cert_set: boolean;
            status: components["schemas"]["MqttStatus"];
        };
        MqttUpdate: {
            /** @default false */
            enabled?: boolean;
            /** @description Broker host name or IP address, without scheme or port. Required when enabled. */
            host?: string;
            /** @default 1883 */
            port?: number;
            username?: string;
            /**
             * @description Connect with TLS (mqtts). Usually port 8883.
             * @default false
             */
            tls?: boolean;
            /**
             * @description With TLS: verify the broker against the ESP-IDF certificate bundle (public CAs) or against the CA certificate stored with `ca_cert`.
             * @default bundle
             * @enum {string}
             */
            ca?: "bundle" | "custom";
            /** @description Empty: `zoneflow-<device id>` (station MAC, 12 hex digits). */
            client_id?: string;
            /** @description Root of every topic. Empty: `zoneflow/<device id>`. No wildcards, no leading or trailing `/`. */
            base_topic?: string;
            /**
             * @description Home Assistant MQTT discovery.
             * @default true
             */
            discovery?: boolean;
            /** @default homeassistant */
            discovery_prefix?: string;
            /**
             * @description Home Assistant entity type for zones.
             * @default valve
             * @enum {string}
             */
            ha_zone_entity?: "valve" | "switch";
            /** @description Write-only. An empty string or null removes it. */
            password?: string | null;
            /** @description PEM CA certificate for `ca: custom`. Write-only. An empty string or null removes it. */
            ca_cert?: string | null;
        };
        /** @description One entry of `zones` (index 0 is zone 1). */
        ConfigZone: {
            name: string;
            /** @default true */
            enabled: boolean;
            /** @default 3600 */
            max_run_s: number;
            /** @default 80 */
            current_min_ma: number;
            /** @default 700 */
            current_max_ma: number;
            /**
             * @description Duration of a run started without one (Home Assistant valve open, MQTT run with an empty payload).
             * @default 600
             */
            default_run_s: number;
        };
        ConfigMaster: {
            /** @default false */
            enabled: boolean;
            /** @default 1000 */
            pre_open_ms: number;
            /** @default 2000 */
            post_close_ms: number;
            /**
             * @description Consecutive current faults on the master that lock it out.
             * @default 3
             */
            fault_lockout_count: number;
            /** @default 80 */
            current_min_ma: number;
            /** @default 700 */
            current_max_ma: number;
        };
        ConfigDriver: {
            /** @default 2 */
            max_concurrent: number;
            /** @default 2000 */
            inter_zone_delay_ms: number;
            /**
             * @description Closed time a zone needs after reaching its maximum run time.
             * @default 1800
             */
            min_rest_s: number;
            /**
             * @description Turn outputs on at a mains zero crossing when the board has the signal.
             * @default false
             */
            zero_cross_switching: boolean;
        };
        ContactInput: {
            enabled: boolean;
            /**
             * @description Normally open or normally closed.
             * @enum {string}
             */
            contact: "no" | "nc";
            debounce_ms: number;
        };
        SpareInput: components["schemas"]["ContactInput"] & {
            /**
             * @description `inhibit`: acts like the rain sensor.
             * @enum {string}
             */
            function: "none" | "inhibit";
        };
        FlowMeter: {
            /** @default false */
            enabled: boolean;
            /** @default 450 */
            pulses_per_litre: number;
            /** @default 0.5 */
            leak_threshold_lpm: number;
            /** @default 60 */
            leak_detect_s: number;
        };
        ConfigSensors: {
            rain: components["schemas"]["ContactInput"];
            spare: components["schemas"]["SpareInput"];
            flow: components["schemas"]["FlowMeter"];
        };
        ConfigDiagnostics: {
            /** @default true */
            current_enabled: boolean;
            /** @default 500 */
            settle_ms: number;
            /** @default 30 */
            idle_max_ma: number;
            /** @default 3000 */
            short_circuit_ma: number;
            /** @default 3 */
            confirm_samples: number;
        };
        ConfigSchedule: {
            seasonal_percent: number;
            missed_start_grace_min: number;
            programs: components["schemas"]["Program"][];
            /**
             * @description A paused queue is stopped after this long.
             * @default 120
             */
            pause_timeout_min: number;
        };
        /** @description A configuration document to write: `schema` is required; every other field may be left out and takes its default, also inside the objects (whose `required` lists describe what GET returns). The configuration document described in the firmware architecture, section 6. Missing fields take defaults; unknown fields are ignored. A document whose schema is newer than the firmware is refused with error.code unsupported_schema and path schema. */
        ConfigInput: {
            /**
             * @description Documents with schema 1 are accepted and migrated (adds `mqtt` and `zones[].default_run_s`).
             * @constant
             */
            schema: 2;
            device_name?: string;
            timezone?: string;
            zone_count?: number;
            /** @description Always 8 entries; zones above zone_count are kept but not used. */
            zones?: components["schemas"]["ConfigZone"][];
            master?: components["schemas"]["ConfigMaster"];
            driver?: components["schemas"]["ConfigDriver"];
            sensors?: components["schemas"]["ConfigSensors"];
            diagnostics?: components["schemas"]["ConfigDiagnostics"];
            schedule?: components["schemas"]["ConfigSchedule"];
            mqtt?: components["schemas"]["MqttSettings"];
        };
        SetupUpdate: {
            complete: boolean;
        };
        /** @description One opening of one zone. */
        HistoryRun: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "run";
            /** @description Zone number, null when not zone-specific. */
            zone: number | null;
            /** @description Program id, null for a manual zone run. */
            program_id: number | null;
            /** @enum {string} */
            source: "schedule" | "manual_program" | "manual_zone";
            /**
             * @description Why it closed. `requested`: stop, skip or pause (a resumed run is a new record).
             * @enum {string}
             */
            end: "none" | "completed" | "requested" | "max_run_time" | "fault" | "ac_lost" | "shutdown" | "maintenance";
            /** @description What was left to run when it opened (after seasonal adjustment and clamping). */
            planned_s: number;
            /** @description How long it was open. */
            actual_s: number;
            /** @description Mean valve current of this zone while it was open alone; null if not measured. */
            current_ma: number | null;
            /** @description Flow-meter volume while open (0.1 L steps); null without a flow meter. Two zones open together share the volume measured meanwhile. */
            litres: number | null;
        };
        /** @description A program was queued. */
        HistoryProgramStarted: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "program_started";
            /** @description Program id, null for a manual zone run. */
            program_id: number | null;
            /** @enum {string} */
            source: "schedule" | "manual_program" | "manual_zone";
            late: boolean;
        };
        /** @description A scheduled start did not run. */
        HistoryProgramSkipped: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "program_skipped";
            /** @description Program id, null for a manual zone run. */
            program_id: number | null;
            /** @enum {string} */
            reason: "rain_delay" | "sensor_inhibit" | "no_ac" | "maintenance" | "already_queued" | "queue_full" | "nothing_to_run" | "master_locked_out" | "hardware_fault";
        };
        /** @description A start passed while the controller was off or its clock jumped. */
        HistoryProgramMissed: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "program_missed";
            /** @description Program id, null for a manual zone run. */
            program_id: number | null;
            scheduled_utc: number;
        };
        /** @description A queued run could not start. */
        HistoryRunDropped: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "run_dropped";
            /** @description Zone number, null when not zone-specific. */
            zone: number | null;
            /** @description Program id, null for a manual zone run. */
            program_id: number | null;
            /** @enum {string} */
            source: "schedule" | "manual_program" | "manual_zone";
            reason: string;
        };
        /** @description A fault was raised. */
        HistoryFaultRaised: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "fault_raised";
            /** @description Zone number, null when not zone-specific. */
            zone: number | null;
            code: string;
        };
        /** @description A fault cleared (cleared by hand or its cause went away). */
        HistoryFaultCleared: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "fault_cleared";
            /** @description Zone number, null when not zone-specific. */
            zone: number | null;
            code: string;
        };
        /** @description The controller started. */
        HistoryBoot: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "boot";
            /** @enum {string} */
            reset_reason: "unknown" | "power_on" | "software" | "panic" | "watchdog" | "brownout" | "external" | "deep_sleep";
        };
        /** @description The clock source changed. */
        HistoryTimeSet: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "time_set";
            /** @enum {string} */
            status: "unset" | "rtc" | "synced";
        };
        /** @description A configuration change was applied. */
        HistoryConfigChanged: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "config_changed";
        };
        /** @description A rain delay was set (until_utc) or ended (null). */
        HistoryRainDelay: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "rain_delay";
            until_utc: number | null;
        };
        /** @description A sensor input changed. */
        HistorySensor: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "sensor";
            /** @enum {string} */
            sensor: "rain" | "spare" | "ac";
            active: boolean;
        };
        /** @description Maintenance mode (updates, factory reset) began or ended. */
        HistoryMaintenance: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "maintenance";
            on: boolean;
        };
        /** @description A paused queue was stopped after the pause timeout. */
        HistoryPauseExpired: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "pause_expired";
        };
        /** @description A firmware or web UI update. */
        HistoryOta: {
            /** @description Grows by one per record, also across restarts. */
            id: number;
            /** @description When it happened (Unix seconds). */
            utc: number;
            /** @description False if recorded while the clock was not set; utc is then wrong. */
            time_valid: boolean;
            /**
             * @description discriminator enum property added by openapi-typescript
             * @enum {string}
             */
            type: "ota";
            /** @enum {string} */
            stage: "started" | "failed" | "installed";
            /** @enum {string} */
            source: "none" | "upload" | "pull";
            error: string | null;
        };
        HistoryRecord: components["schemas"]["HistoryRun"] | components["schemas"]["HistoryProgramStarted"] | components["schemas"]["HistoryProgramSkipped"] | components["schemas"]["HistoryProgramMissed"] | components["schemas"]["HistoryRunDropped"] | components["schemas"]["HistoryFaultRaised"] | components["schemas"]["HistoryFaultCleared"] | components["schemas"]["HistoryBoot"] | components["schemas"]["HistoryTimeSet"] | components["schemas"]["HistoryConfigChanged"] | components["schemas"]["HistoryRainDelay"] | components["schemas"]["HistorySensor"] | components["schemas"]["HistoryMaintenance"] | components["schemas"]["HistoryPauseExpired"] | components["schemas"]["HistoryOta"];
        HistoryPage: {
            /** @description Newest first. */
            records: components["schemas"]["HistoryRecord"][];
            /** @description Pass as `before` for the next older page; null when there is none. */
            next_before: number | null;
            newest_id: number | null;
            /** @description Oldest id still kept (approximately: a damaged chunk inside the range is skipped). */
            oldest_id: number | null;
            /** @description Records the log holds at most; older ones are overwritten. */
            capacity: number;
            /** @description Records not yet written to flash (lost on a power cut; written within 60 s, faults at once). */
            unsaved: number;
        };
        BackupInput: {
            /** @constant */
            format: "zoneflow-backup";
            /** @constant */
            format_version?: 1;
            created_utc?: number;
            firmware_version?: string;
            hostname?: string;
            config: components["schemas"]["ConfigInput"];
        };
        /** @description The rev A output stage (I/O expander, valve-common relay, two keep-alive detectors) and the current measurement. Hardware design section 19. Nothing here has been measured on a board. Sent by firmware with the rev A output stage; optional in this schema so that older firmware and the web UI's mock controller still validate. */
        Hardware: {
            /**
             * @description none: a board without a sequenced stage. ready: the relay is closed and valves may switch. starting and stopping: the self-test before and after a run. polling: the 10-minute check between runs.
             * @enum {string}
             */
            phase: "none" | "no_ac" | "startup_test" | "idle" | "starting" | "ready" | "stopping" | "polling" | "trip_clear" | "fault" | "halted";
            /**
             * @description From the board's variant strap, read in the start-up self-test; null before.
             * @enum {integer|null}
             */
            zones_fitted: 4 | 8 | null;
            relay_closed: boolean;
            sensor_supply_on: boolean;
            sensor_supply_fault: boolean;
            /** @description The latched failed check, or null. */
            check: string | null;
            /** @description Self-test step of the check (19.7): "1" to "9" with "2b", "4a" to "4c" and "5b". null when no check is latched, or for a check that is not a step (bus error, the over-current latch of 19.8, a firmware limit, rule 16). */
            step: string | null;
            repair_needed: boolean;
            /** @description Complete self-test sequences since start-up. */
            self_tests_passed: number;
            /** @description Last measured detector times, ms (0 = not measured), and this board's stored release times. Pass limits (hardware design 19.7 since the hardware answers to section 19): enable within 150 ms; release 150 to 1000 ms (drive) and 50 to 1600 ms (relay), and each within 0.40 to 1.70 times the stored value once the board is commissioned. */
            detectors: {
                enable_drive_ms: number;
                enable_relay_ms: number;
                release_drive_ms: number;
                release_relay_ms: number;
                /** @description The board has stored release times. false: step 9 applies its absolute limits only, and not_commissioned is active. */
                commissioned: boolean;
                /** @description Stored release time of the gate-drive detector, the median of five stop tests at commissioning; null when not commissioned. */
                reference_drive_ms: number | null;
                /** @description Stored release time of the relay detector; null when not commissioned. */
                reference_relay_ms: number | null;
                /** @description Not commissioned, and the ESP32-S3 die temperature is outside 25 to 50 C or unknown: commissioning waits and is tried again every 10 minutes (hardware design 19.7, Commissioning, item 2). */
                waiting_for_temperature: boolean;
            };
            /** @description From the zero-cross signal; null when unknown. */
            mains_hz: number | null;
            current: {
                /**
                 * @description Amplifier channel of the latest window: low when the high-gain channel clipped.
                 * @enum {string}
                 */
                channel: "high" | "low" | "none";
                /** @description Learned with the relay open; removed in quadrature. */
                noise_floor_ma: number;
                /** @description Mean shift of the latest window against the idle mean: a DC component (fault zone_dc_current). */
                dc_ma: number;
                /** @description Current window before the last over-current trip. */
                last_trip_ma: number | null;
            };
            /** @description Hardware design 19.10: window means, mV, null until read. vin_mon_mv is VIN_MON (the rectified supply divided by 31): step 0 needs 0.60 to 1.50 V; under 0.60 V supply_low, above 1.50 V supply_high, under 0.10 V with mains present for 2 s rectifier_fault. rest_lo_mv and rest_hi_mv are the current channels at rest: limits 700 to 880 mV and 680 to 900 mV; both above 900 mV is supply_over_voltage (rule 18), one outside its limits a lost current measurement (rule 19). */
            supply: {
                vin_mon_mv: number | null;
                rest_lo_mv: number | null;
                rest_hi_mv: number | null;
            };
            /**
             * @description Hardware design rule 19: why the current measurement counts as lost. Anything but none skips steps 5, 5b and 6 and rule 6, zones still run, and current_sense_unavailable is active for as long as it lasts.
             * @enum {string}
             */
            current_lost: "none" | "unavailable" | "no_window" | "rest_level";
            /** @description Starts since boot that skipped steps 5, 5b and 6 for lack of current readings (rule 19). */
            checks_skipped: number;
            /** @description Over-current trips with no valve bit low since boot (hardware design 19.8): not counted against an output; a second one in a row is latch_input_held. */
            idle_trips: number;
        };
        AcknowledgeRepairRequest: {
            /**
             * @description The person states the board was repaired.
             * @constant
             */
            repaired: true;
        };
    };
    responses: {
        /** @description Invalid body or parameter; `error.path` names the field */
        BadRequest: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description No valid session (`unauthorized`), or no admin password yet (`password_not_set`) */
        Unauthorized: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Host not this device (`bad_host`), cross-origin write (`bad_origin`), missing CSRF header (`csrf`), or a provisioning route from outside the setup access point (`setup_ap_only`) */
        Forbidden: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description No such zone, program or route */
        NotFound: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Refused in the current state (zone disabled, inhibited, update running, ...) */
        Conflict: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Body larger than the route allows (checked before reading) */
        TooLarge: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Wrong Content-Type */
        UnsupportedMediaType: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Too many failed attempts; see Retry-After */
        TooManyRequests: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Storage or internal error */
        ServerError: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
        /** @description Command queue full; see Retry-After */
        Busy: {
            headers: {
                [name: string]: unknown;
            };
            content: {
                "application/json": components["schemas"]["Error"];
            };
        };
    };
    parameters: never;
    requestBodies: never;
    headers: never;
    pathItems: never;
}
export type $defs = Record<string, never>;
export interface operations {
    getAuth: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AuthStatus"];
                };
            };
        };
    };
    login: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["LoginRequest"];
            };
        };
        responses: {
            /** @description Logged in. Also sets the `zf_session` cookie (HttpOnly, SameSite=Strict). */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Session"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            429: components["responses"]["TooManyRequests"];
        };
    };
    setupPassword: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["SetupRequest"];
            };
        };
        responses: {
            /** @description Password set and logged in */
            201: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Session"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            429: components["responses"]["TooManyRequests"];
        };
    };
    logout: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": {
                        logged_out?: boolean;
                    };
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
        };
    };
    changePassword: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["PasswordChange"];
            };
        };
        responses: {
            /** @description Changed. Every other session is signed out; this is the new one. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Session"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            429: components["responses"]["TooManyRequests"];
        };
    };
    getSecurity: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Security"];
                };
            };
        };
    };
    putSecurity: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["SecurityUpdate"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Security"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
        };
    };
    getInfo: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Info"];
                };
            };
        };
    };
    getStatus: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Status"];
                };
            };
        };
    };
    getEvents: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description An endless `text/event-stream`. See the API reference page for the event names and payloads. */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "text/event-stream": string;
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            503: components["responses"]["Busy"];
        };
    };
    listZones: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Zone"][];
                };
            };
        };
    };
    getZone: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Zone number as printed on the terminal block, 1 to zone_count. */
                zone: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Zone"];
                };
            };
            404: components["responses"]["NotFound"];
        };
    };
    patchZone: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Zone number as printed on the terminal block, 1 to zone_count. */
                zone: number;
            };
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["ZoneUpdate"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Zone"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            404: components["responses"]["NotFound"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    runZone: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Zone number as printed on the terminal block, 1 to zone_count. */
                zone: number;
            };
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["ZoneRun"];
            };
        };
        responses: {
            /** @description Queued */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["ZoneRunAccepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            /** @description No such zone or program, or rejected with invalid_zone / unknown_program */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RunRejection"];
                };
            };
            /** @description Rejected (RunRejection codes) */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RunRejection"];
                };
            };
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    stopZone: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Zone number as printed on the terminal block, 1 to zone_count. */
                zone: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            404: components["responses"]["NotFound"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    listPrograms: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Program"][];
                };
            };
        };
    };
    createProgram: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["ProgramInput"];
            };
        };
        responses: {
            /** @description Created; Location header names it */
            201: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Program"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    getProgram: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Program id. */
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Program"];
                };
            };
            404: components["responses"]["NotFound"];
        };
    };
    replaceProgram: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Program id. */
                id: number;
            };
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["ProgramInput"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Program"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            404: components["responses"]["NotFound"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    deleteProgram: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Program id. */
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": {
                        deleted?: boolean;
                    };
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            404: components["responses"]["NotFound"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    runProgram: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                /** @description Program id. */
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            /** @description No such zone or program, or rejected with invalid_zone / unknown_program */
            404: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RunRejection"];
                };
            };
            /** @description Rejected (RunRejection codes) */
            409: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RunRejection"];
                };
            };
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    getQueue: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Queue"];
                };
            };
        };
    };
    queuePause: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    queueResume: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    queueSkip: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    queueStop: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    getRainDelay: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RainDelay"];
                };
            };
        };
    };
    setRainDelay: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["RainDelayRequest"];
            };
        };
        responses: {
            /** @description Queued */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RainDelayAccepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    clearRainDelay: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    getSensors: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Sensors"];
                };
            };
        };
    };
    listFaults: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Faults"];
                };
            };
        };
    };
    clearFaults: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    acknowledgeRepair: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["AcknowledgeRepairRequest"];
            };
        };
        responses: {
            /** @description Command queued for the control task */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            503: components["responses"]["Busy"];
        };
    };
    getConfig: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Config"];
                };
            };
        };
    };
    putConfig: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["ConfigInput"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": {
                        applied?: boolean;
                    };
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
            503: components["responses"]["Busy"];
        };
    };
    backupConfig: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Backup file (Content-Disposition: attachment) */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Backup"];
                };
            };
        };
    };
    restoreConfig: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description A backup file from GET /config/backup, or a bare configuration document. */
        requestBody: {
            content: {
                "application/json": components["schemas"]["BackupInput"] | components["schemas"]["ConfigInput"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": {
                        restored?: boolean;
                    };
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
            503: components["responses"]["Busy"];
        };
    };
    getTime: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Time"];
                };
            };
        };
    };
    setTime: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["TimeUpdate"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Time"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
            503: components["responses"]["Busy"];
        };
    };
    getNetwork: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Network"];
                };
            };
        };
    };
    reprovision: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: {
            content: {
                "application/json": components["schemas"]["ReprovisionRequest"];
            };
        };
        responses: {
            /** @description The device restarts after answering */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
        };
    };
    getProvisioning: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["ProvisioningState"];
                };
            };
        };
    };
    scanNetworks: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["WifiNetwork"][];
                };
            };
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
        };
    };
    provisionWifi: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["ProvisionWifiRequest"];
            };
        };
        responses: {
            /** @description Handed to the provisioning manager; poll GET /provision */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
        };
    };
    reboot: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description Reboots after answering */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
        };
    };
    factoryReset: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["FactoryResetRequest"];
            };
        };
        responses: {
            /** @description Erases and reboots after answering */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
        };
    };
    getFirmware: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Firmware"];
                };
            };
        };
    };
    uploadFirmware: {
        parameters: {
            query?: {
                /** @description Update even while watering (outputs are still forced off first). */
                force?: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description The raw application image (`zoneflow-fw.bin` from a release). */
        requestBody: {
            content: {
                "application/octet-stream": string;
            };
        };
        responses: {
            /** @description Verified and set as boot image; the device restarts */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
        };
    };
    pullFirmware: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["PullRequest"];
            };
        };
        responses: {
            /** @description Download started; follow GET /firmware or the `ota` stream event */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            409: components["responses"]["Conflict"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
        };
    };
    uploadAssets: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        /** @description A LittleFS image of the `www` partition (`www.bin` from a release). */
        requestBody: {
            content: {
                "application/octet-stream": string;
            };
        };
        responses: {
            /** @description Written; the UI is served from it at once */
            202: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Accepted"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
        };
    };
    getMqtt: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Mqtt"];
                };
            };
        };
    };
    putMqtt: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                /**
                 * @example {
                 *       "enabled": true,
                 *       "host": "192.168.1.10",
                 *       "port": 1883,
                 *       "username": "zoneflow",
                 *       "password": "secret"
                 *     }
                 */
                "application/json": components["schemas"]["MqttUpdate"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Mqtt"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
            503: components["responses"]["Busy"];
        };
    };
    getHistory: {
        parameters: {
            query?: {
                limit?: number;
                /** @description Only records with a smaller id. */
                before?: number;
                /** @description Only records with a larger id. */
                after?: number;
                /** @description Only records at or after this time (Unix seconds). */
                since?: number;
                /** @description Only records of this zone. */
                zone?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["HistoryPage"];
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            503: components["responses"]["Busy"];
        };
    };
    putSetup: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody: {
            content: {
                "application/json": components["schemas"]["SetupUpdate"];
            };
        };
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": {
                        setup_complete: boolean;
                    };
                };
            };
            400: components["responses"]["BadRequest"];
            401: components["responses"]["Unauthorized"];
            403: components["responses"]["Forbidden"];
            413: components["responses"]["TooLarge"];
            415: components["responses"]["UnsupportedMediaType"];
            500: components["responses"]["ServerError"];
        };
    };
}
