# Data Flow

How data flows from Moonraker through to pixels on screen.

```mermaid
graph TB
    subgraph Klipper["Klipper / Moonraker"]
        KL["Klipper MCU<br/>(temps, motion, endstops)"]
        MR["Moonraker Server<br/>(API + WebSocket)"]
        KL -->|"status updates"| MR
    end

    subgraph LibHV["libhv Thread (Background)"]
        WS["WebSocket Connection<br/>ws://host:7125/websocket"]
        MC["MoonrakerClient<br/>JSON-RPC 2.0 parser"]
        NQ["Notification Queue<br/>(mutex-protected)"]

        MR <-->|"WebSocket frames"| WS
        WS --> MC
        MC -->|"push notification"| NQ
    end

    subgraph MainThread["Main Thread"]
        subgraph MainLoop["lv_timer_handler() cycle"]
            UQ["UpdateQueue::process_pending()<br/>(runs FIRST, highest priority)"]
            PN["MoonrakerManager::process_notifications()<br/>(dequeue from NQ)"]
            RENDER["lv_refr_now()<br/>(LVGL render)"]
        end

        subgraph StateUpdate["State Processing"]
            PS_UPDATE["PrinterState::update_from_notification()"]
            AS_UPDATE["AmsState::update_from_status()"]
            TS_UPDATE["ToolState::update_from_status()"]
            FS_UPDATE["FilamentSensorManager::update()"]
            SEN_UPDATE["SensorManagers::update()"]
        end

        subgraph Subjects["LVGL Subjects (Reactive Data)"]
            S_TEMP["temp subjects<br/>(nozzle, bed, chamber)"]
            S_POS["position subjects<br/>(X, Y, Z)"]
            S_PRINT["print subjects<br/>(progress, filename, state)"]
            S_FAN["fan subjects<br/>(part, aux, exhaust)"]
            S_AMS["AMS subjects<br/>(slots, active tool)"]
            S_SETTINGS["settings subjects<br/>(dark mode, sound, etc.)"]
            S_OTHER["50+ more subjects..."]
        end

        subgraph XMLBindings["XML Declarative Bindings"]
            BT["bind_text<br/>label ← subject string"]
            BI["bind_int<br/>label ← subject int"]
            BF["bind_flag_if_eq<br/>visibility ← subject == value"]
            BS["bind_style<br/>color ← subject value"]
            ECB["event_cb<br/>button click → C++ callback"]
        end

        subgraph Widgets["LVGL Widgets (Pixels)"]
            W_LABEL["Labels<br/>(temps, position, filenames)"]
            W_BAR["Progress Bars<br/>(print %, heat)"]
            W_IMG["Images<br/>(thumbnails, printer)"]
            W_BTN["Buttons<br/>(home, preheat, cancel)"]
            W_CHART["Charts<br/>(temp history)"]
        end
    end

    %% Main loop flow
    NQ -->|"dequeue"| PN
    PN --> PS_UPDATE
    PS_UPDATE --> AS_UPDATE
    PS_UPDATE --> TS_UPDATE
    PS_UPDATE --> FS_UPDATE
    PS_UPDATE --> SEN_UPDATE

    %% State → Subjects (via queue_update)
    PS_UPDATE -->|"queue_update()"| UQ
    AS_UPDATE -->|"queue_update()"| UQ
    UQ -->|"lv_subject_set_int/str()"| S_TEMP
    UQ -->|"lv_subject_set_int/str()"| S_POS
    UQ -->|"lv_subject_set_int/str()"| S_PRINT
    UQ --> S_FAN
    UQ --> S_AMS

    %% Subjects → XML bindings (automatic)
    S_TEMP -->|"observer fires"| BT
    S_POS -->|"observer fires"| BI
    S_PRINT -->|"observer fires"| BT
    S_PRINT -->|"observer fires"| BF
    S_AMS -->|"observer fires"| BS

    %% XML → Widgets (automatic)
    BT --> W_LABEL
    BI --> W_LABEL
    BF --> W_BAR
    BS --> W_BTN
    BT --> W_BAR

    %% Render
    W_LABEL -->|"dirty"| RENDER
    W_BAR -->|"dirty"| RENDER
    W_BTN -->|"dirty"| RENDER

    %% User input (reverse flow)
    W_BTN -->|"clicked"| ECB
    ECB -->|"MoonrakerAPI call"| MC

    style Klipper fill:#1a1a2e,stroke:#e94560,color:#fff
    style LibHV fill:#4e4376,stroke:#7b2d8e,color:#fff
    style MainThread fill:#0f3460,stroke:#16213e,color:#fff
    style Subjects fill:#2b5876,stroke:#00b4d8,color:#fff
    style XMLBindings fill:#533483,stroke:#16213e,color:#fff
    style Widgets fill:#1b4332,stroke:#16213e,color:#fff
    style MainLoop fill:#16213e,stroke:#0f3460,color:#fff
    style StateUpdate fill:#16213e,stroke:#0f3460,color:#fff
```

## Key Points

1. **Thread boundary**: WebSocket callbacks (libhv thread) NEVER touch LVGL directly. Always go through `queue_update()`.
2. **UpdateQueue runs first**: Before rendering, ensuring subjects are current when widgets draw.
3. **Subjects are reactive**: XML bindings fire automatically when subject values change. No manual widget updates.
4. **Reverse flow**: User button clicks → `event_cb` → `MoonrakerAPI` → WebSocket → Moonraker.
5. **Observer safety**: `observe_int_sync` defers callbacks via `queue_update()` to prevent re-entrant crashes.
