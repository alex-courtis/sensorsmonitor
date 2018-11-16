# sensorsmonitor

Periodically (2sec) writes interesting lm_sensor maximums to named pipe `$XDG_RUNTIME_DIR/sensorsmonitor` e.g.

```
amdgpu 26°C 4W   Tdie 28°C
```

I show this on my xmobar.

## Collected
- amdgpu
  - temperature
  - power average
- k10temp
  - Tdie; Tctl is a legacy reading offset by +20 or +27
