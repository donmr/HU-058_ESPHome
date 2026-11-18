# Home Assistant reference

Needs ESPHome 2026.8.0 or newer.

## Naming

The device is `wifi-clock`, so entities are `<domain>.wifi_clock_<object_id>`
and actions are `esphome.wifi_clock_<name>`.

## Actions

In YAML they are called with `action:` and `data:`.

| Action | Parameters |
| --- | --- |
| `show_seconds` | `duration` int, ms |
| `show_number` | `value` int, `unit` string, `duration` int ms |
| `lamp_test` | `duration` int, ms |

### show_seconds

Shows `:SS` in the minute positions behind a steady colon, then goes back to
the time on its own.

`duration` is milliseconds, clamped to between 100 and 600000. Nothing can
leave the clock stuck not being a clock.

### show_number

Right aligned digits with an optional unit letter in the rightmost position,
so `78F` and `-5C` both fit. An empty `unit` gives the number all four
positions.

There is no decimal point on the panel, so round before sending.

Only these letters render unambiguously on seven segments, and anything else
is ignored: `A b C c d E F H h L n o P r t U u y`.

A unit of `C`, `c` or `F` also lights the degree mark in the gap ahead of the
last digit. No other unit gets it.

Out of range values clamp rather than wrap, because a wrapped temperature is
a wrong reading and a clamped one is visibly pinned.

### lamp_test

Every populated LED, white, at half brightness, then back to the time with the
colors and on or off state it had before. It ignores those settings so
anything still dark is a panel or wiring fault rather than a display setting.

`duration` is milliseconds, clamped like the other temporary displays. The
`Lamp Test` button calls it with 3000. The all-white pattern draws substantial
current, so use it as a momentary check rather than putting it on a loop.

## Entities

| Entity | Type | Values |
| --- | --- | --- |
| `light.wifi_clock_display` | Light | RGB, on/off, brightness, color |
| `switch.wifi_clock_12_hour_time` | Switch | On for 12h, off for 24h |
| `switch.wifi_clock_blink_colon` | Switch | Off means steady, not dark |
| `switch.wifi_clock_auto_brightness` | Switch | On scales the display to the room light |
| `binary_sensor.wifi_clock_button_1` | Binary sensor | Top button on the case |
| `binary_sensor.wifi_clock_button_2` | Binary sensor | Bottom button |
| `button.wifi_clock_lamp_test` | Button | Three seconds of every LED, white |
| `button.wifi_clock_show_seconds` | Button | Five seconds of `:SS` |

## Color and brightness

The `Display` light sets one color for every LED on the panel. Color sets the
duty cycle levels, brightness sets the drive current, and the two are
independent.

With `Auto Brightness` on, the light's brightness is the maximum. The clock
scales itself between 1% in the dark and that maximum in a bright room, with no
automation needed. Automations that set the light's brightness change the
maximum, so remove any that follow the room light.

## Examples

Show a temperature for ten seconds:

```yaml
- action: esphome.wifi_clock_show_number
  data:
    value: "{{ states('sensor.outside_temperature') | round(0) | int }}"
    unit: F
    duration: 10000
```

Set the color:

```yaml
- action: light.turn_on
  target:
    entity_id: light.wifi_clock_display
  data:
    rgb_color: [255, 120, 0]
```

Dim at night without changing the color:

```yaml
- action: light.turn_on
  target:
    entity_id: light.wifi_clock_display
  data:
    brightness_pct: 15
```

## Things worth knowing

Temporary displays expire on their own. `show_seconds`, `show_number` and
`lamp_test` carry a deadline capped at ten minutes, so a failed automation
cannot park the panel on temporary content.

There is no RTC. A cold boot with no network shows four dashes until the time
arrives.

The firmware polls from the ESPHome NTP default servers every 15 minutes: 0.pool.ntp.org, 1.pool.ntp.org, 2.pool.ntp.org. The timezone comes from Home Assitant. 

The upper colon dot drops while the network is down. It is not known how much time will drift while NTP servers can't be reached.
