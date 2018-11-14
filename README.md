# sensorsmonitor

Periodically writes interesting lm_sensor averages to named pipe `$XDG_RUNTIME_DIR/sensorsmonitor` e.g.

```
amdgpu 26C 4W   Tdie 28C   Tctl 55C
```

I show this on my xmobar.

## Collected
- amdgpu
  - temperature
  - power average
- k10temp
  - Tdie
  - Tctl
