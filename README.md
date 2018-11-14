# sensorsmonitor

Periodically writes interesting lm_sensor averages to named pipe `$XDG_RUNTIME_DIR/sensorsmonitor` e.g.

```
amdgpu 26°C 4W   Tdie 28°C   Tctl 55°C
```

I show this on my xmobar.

## Collected
- amdgpu
  - temperature
  - power average
- k10temp
  - Tdie
  - Tctl
