# project-name

ESP32-S3 firmware based on ESP-IDF.

## Requirements baseline

The current product and firmware requirements baseline is
[`docs/T6350_需求基线_V0.3.md`](docs/T6350_需求基线_V0.3.md). It is derived from
the 2026-09-09 V0.3 technical specification and IR-SR requirement breakdown.
Items marked as GAP or pending in those sources must not be treated as finalized
requirements.

The cloud time-synchronization payload, POSIX timezone rules, acknowledgements,
and integration test cases are documented in
[`docs/T6350_云端校时接口协议_V1.0.md`](docs/T6350_云端校时接口协议_V1.0.md).

## AiP1620 architecture

AiP1620 follows a strict two-layer design:

- `main/Inf/Inf_AIP1620.*` is a synchronous hardware driver. It only handles
  GPIO timing, frame encoding, brightness and display power state. It never
  creates a FreeRTOS task or queue.
- `main/App/APP_DisplayAIP1620.*` owns the display task and command queue. All
  runtime display operations are serialized through this queue, including
  blinking.
- `main/App/App_ClockDisplay.*` periodically reads the RTC and sends time
  updates to the display service.
- `main/App/App_AIP1620PowerTest.*` contains the optional UART power-test task.
  It is enabled with `CONFIG_AIP1620_POWER_TEST` and must not run together with
  the normal clock-display application.

Application code should include `APP_DisplayAIP1620.h` or
`App_ClockDisplay.h`; it should not call `Inf_AIP1620` directly. Display APIs
return the result of validating and enqueueing a command. Hardware execution
then occurs asynchronously in the display task.

Typical clock startup:

```c
ESP_ERROR_CHECK(App_ClockDisplay_Init());
ESP_ERROR_CHECK(App_ClockDisplay_SetBrightness(
    APP_DISPLAY_AIP1620_BRIGHTNESS_3));
```

Direct application display usage:

```c
ESP_ERROR_CHECK(App_DisplayAIP1620_Init());
ESP_ERROR_CHECK(App_DisplayAIP1620_ShowTime(12U, 34U, true));
ESP_ERROR_CHECK(App_DisplayAIP1620_SetIcons(
    APP_DISPLAY_AIP1620_ICON_1 | APP_DISPLAY_AIP1620_ICON_3));
```

## Build

```text
idf.py set-target esp32s3
idf.py build
```
