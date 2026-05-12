```
bwrap \
  --ro-bind / / \
  --bind "$PWD/build/libSwell.so" /usr/lib/REAPER/libSwell.so \
  --bind "$HOME/.config/REAPER" "$HOME/.config/REAPER" \
  --bind "$HOME/.cache" "$HOME/.cache" \
  --tmpfs /tmp \
  --bind /tmp/.X11-unix /tmp/.X11-unix \
  --dev /dev \
  --proc /proc \
  --setenv GDK_BACKEND x11 \
  -- \
  gdb /usr/lib/REAPER/reaper
```