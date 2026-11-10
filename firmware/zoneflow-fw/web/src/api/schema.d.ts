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
         * @description A fault whose cause is still present is raised again on the next sample.
         */
        post: operations["clearFaults"];
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
         * @description Only while SoftAP provisioning runs (409 `not_provisioning` otherwise). The setup code is the WPA2 passphrase of that access point, so a client on it has already proven it knows the code.
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
         * @description Only while SoftAP provisioning runs. Optionally sets the first admin password.
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
            password_set?: boolean;
            authenticated?: boolean;
            protect_reads?: boolean;
            /** @description Only for a valid cookie session. */
            csrf?: string;
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
            token?: string;
            /** @description Send as X-CSRF-Token on writes that use the cookie. */
            csrf?: string;
            idle_timeout_s?: number;
        };
        Security: {
            protect_reads?: boolean;
            https?: boolean;
            check_host?: boolean;
            password_set?: boolean;
            sessions?: number;
            /** @description Of the self-signed certificate; empty if none. */
            tls_fingerprint_sha256?: string;
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
            api_version?: string;
            device_name?: string;
            hostname?: string;
            mac?: string;
            board?: string;
            firmware?: {
                version?: string;
                project?: string;
                idf_version?: string;
                build_date?: string;
            };
            uptime_s?: number;
            reset_reason?: string;
            /** @description free, min_free and largest_block cover all 8-bit heap, PSRAM included; internal_* cover internal RAM only. */
            heap?: {
                free?: number;
                min_free?: number;
                largest_block?: number;
                internal_free?: number;
                internal_min_free?: number;
            };
            http_stack_free_min?: number;
        };
        Zone: {
            id?: number;
            name?: string;
            enabled?: boolean;
            max_run_s?: number;
            current_min_ma?: number;
            current_max_ma?: number;
            /** @enum {string} */
            state?: "off" | "waiting" | "open";
            remaining_s?: number;
            /** @enum {string} */
            last_stop?: "none" | "completed" | "requested" | "max_run_time" | "fault" | "ac_lost" | "shutdown" | "maintenance";
            fault?: string;
            /** @description Latched fault on this zone; runs are refused until faults are cleared. */
            locked_out?: boolean;
            /** @description The zone reached its maximum run time (summed across back-to-back runs) and must stay closed for driver.min_rest_s (spec section 2, rule 4). */
            resting?: boolean;
            /** @description Seconds of rest left; 0 when not resting. */
            rest_remaining_s?: number;
        };
        ZoneUpdate: {
            name?: string;
            enabled?: boolean;
            max_run_s?: number;
            current_min_ma?: number;
            current_max_ma?: number;
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
            id?: number;
            zone?: number | null;
            duration_s?: number;
            /** @enum {string} */
            source?: "schedule" | "manual_program" | "manual_zone";
            program_id?: number | null;
        };
        Queue: {
            /** @enum {string} */
            state?: "idle" | "running" | "paused";
            length?: number;
            current_remaining_s?: number;
            /** @description Head first; first 8. */
            items?: components["schemas"]["QueueItem"][];
            /** @description schedule.pause_timeout_min: a queue paused this long is stopped (event pause_expired). The time already paused is not reported by the core. */
            pause_timeout_min?: number;
        };
        RainDelay: {
            active?: boolean;
            until_utc?: number | null;
            remaining_s?: number;
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
            rain?: {
                enabled?: boolean;
                active?: boolean;
            };
            spare?: {
                enabled?: boolean;
                active?: boolean;
            };
            flow?: {
                enabled?: boolean;
                rate_lpm?: number;
                total_l?: number;
            };
            ac_present?: boolean;
            valve_current_ma?: number | null;
            /** @description False while the valve-current readings are missing (fault current_sense_unavailable). Zones still run; open and short detection is blind. */
            current_sense_available?: boolean;
            /** @description The hardware over-current comparator removed gate drive (fault over_current_trip, latched until faults are cleared). */
            over_current_tripped?: boolean;
        };
        Faults: {
            active?: ("zone_open_circuit" | "zone_over_current" | "short_circuit" | "current_while_idle" | "ac_lost" | "flow_without_zones" | "config_invalid" | "time_invalid" | "storage_error" | "master_locked_out" | "current_sense_unavailable" | "over_current_trip")[];
            zones?: {
                zone?: number;
                fault?: string;
                locked_out?: boolean;
            }[];
        };
        Status: {
            sequence?: number;
            utc?: number;
            /** @enum {string} */
            time_status?: "unset" | "rtc" | "synced";
            uptime_s?: number;
            zones?: components["schemas"]["Zone"][];
            master?: {
                enabled?: boolean;
                on?: boolean;
                /** @description Locked out after master.fault_lockout_count consecutive current faults on the master; every zone is blocked until faults are cleared. */
                locked_out?: boolean;
                /** @description Consecutive current faults attributed to the master. */
                fault_count?: number;
            };
            queue?: components["schemas"]["Queue"];
            sensors?: components["schemas"]["Sensors"];
            rain_delay?: components["schemas"]["RainDelay"];
            inhibit?: {
                sensor?: boolean;
                no_ac?: boolean;
                maintenance?: boolean;
                master_lockout?: boolean;
            };
            faults?: components["schemas"]["Faults"];
            maintenance?: boolean;
            config_revision?: number;
            network?: components["schemas"]["Network"];
            ota?: components["schemas"]["OtaStatus"];
        };
        /** @description The configuration document described in the firmware architecture, section 6. Missing fields take defaults; unknown fields are ignored. A document whose schema is newer than the firmware is refused with error.code unsupported_schema and path schema. */
        Config: {
            /** @constant */
            schema: 1;
            device_name?: string;
            timezone?: string;
            zone_count?: number;
            zones?: Record<string, never>[];
            master?: {
                /**
                 * @description Consecutive current faults on the master that lock it out.
                 * @default 3
                 */
                fault_lockout_count: number;
            };
            driver?: {
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
            sensors?: Record<string, never>;
            diagnostics?: Record<string, never>;
            schedule?: {
                seasonal_percent?: number;
                missed_start_grace_min?: number;
                programs?: components["schemas"]["Program"][];
                /**
                 * @description A paused queue is stopped after this long.
                 * @default 120
                 */
                pause_timeout_min: number;
            };
        };
        Backup: {
            /** @constant */
            format: "zoneflow-backup";
            /** @constant */
            format_version?: 1;
            created_utc?: number;
            firmware_version?: string;
            hostname?: string;
            config: components["schemas"]["Config"];
        };
        Time: {
            utc?: number;
            /** @enum {string} */
            status?: "unset" | "rtc" | "synced";
            rtc_present?: boolean;
            timezone?: string;
            /** @description Local wall-clock time, ISO 8601 without offset. */
            local?: string | null;
            utc_offset_s?: number;
            dst?: boolean;
        };
        TimeUpdate: {
            utc?: number;
            /** @example CET-1CEST,M3.5.0,M10.5.0/3 */
            timezone?: string;
        };
        Network: {
            /** @enum {string} */
            state?: "off" | "provisioning" | "connecting" | "connected" | "waiting_retry";
            provisioned?: boolean;
            /** @enum {string} */
            provisioning?: "none" | "ble" | "softap";
            hostname?: string;
            mac?: string;
            ssid?: string;
            bssid?: string;
            channel?: number;
            rssi?: number;
            ip?: string;
            netmask?: string;
            gateway?: string;
            reconnect_attempt?: number;
            retry_in_ms?: number;
            /** @description ESP-IDF wifi_err_reason_t. */
            last_disconnect_reason?: number;
            disconnects?: number;
            mdns?: boolean;
        };
        ReprovisionRequest: {
            /**
             * @default ble
             * @enum {string}
             */
            transport: "ble" | "softap";
        };
        ProvisioningState: {
            active?: boolean;
            /** @enum {string} */
            transport?: "none" | "ble" | "softap";
            state?: string;
            provisioned?: boolean;
            password_set?: boolean;
            hostname?: string;
            ip?: string;
        };
        WifiNetwork: {
            ssid?: string;
            rssi?: number;
            secure?: boolean;
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
            phase?: "idle" | "preparing" | "receiving" | "verifying" | "rebooting" | "failed";
            /** @enum {string} */
            error?: "none" | "busy" | "zones_active" | "outputs_not_off" | "bad_image" | "wrong_chip" | "wrong_project" | "too_large" | "truncated" | "flash_error" | "verify_failed" | "download_failed" | "bad_url" | "aborted";
            /** @enum {string} */
            source?: "none" | "upload" | "pull";
            forced?: boolean;
            received_bytes?: number;
            total_bytes?: number;
            new_version?: string;
        };
        Firmware: {
            version?: string;
            idf_version?: string;
            build_date?: string;
            running_slot?: string;
            /** @enum {string} */
            running_state?: "new" | "pending_verify" | "valid" | "invalid" | "aborted" | "undefined";
            next_slot?: string;
            other_slot_version?: string;
            ota?: components["schemas"]["OtaStatus"];
        };
        PullRequest: {
            /** Format: uri */
            url: string;
            /** @default false */
            force: boolean;
        };
        /** @description Error answer of a run command. error.code is the controller's RunError: invalid_zone, unknown_program, zero_duration, queue_full, already_queued, inhibited, nothing_to_run, locked_out, zone_resting, master_locked_out (or inhibited when maintenance or missing 24 VAC is known beforehand). zone_resting answers carry Retry-After with the seconds of rest left. */
        RunRejection: components["schemas"]["Error"];
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
        /** @description Host not this device (`bad_host`), cross-origin write (`bad_origin`) or missing CSRF header (`csrf`) */
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
                "application/json": components["schemas"]["Config"];
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
                "application/json": components["schemas"]["Backup"] | components["schemas"]["Config"];
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
}
