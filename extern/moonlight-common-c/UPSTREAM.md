# Upstream

Vendored from [XITRIX/moonlight-common-c](https://github.com/XITRIX/moonlight-common-c),
branch `borealis`, commit `206c79c240c342229555494a8cadda6c11cad479` (2026-10-19).

This tree is a plain directory in the parent repository, not a submodule, so changes to it
travel with the same commits as the rest of Moonlight-Switch-Turbo. Its two nested
submodules — `enet` (cgutman/enet) and `nanors` (sleepybishop/nanors) — were flattened the
same way.

## Local modifications

- `src/InputStream.c` — gamepad packets are sent with `ENET_PACKET_FLAG_RELIABLE` only
  when the button flags changed; pure stick and trigger movement goes out unreliable.
  A dropped axis sample is replaced by the next frame's full-state packet, while a dropped
  button transition could leave the host holding a key. This also keeps the send thread out
  of the 10 ms reliable-packet backpressure loop in `ControlStream.c`. GFE is unaffected:
  `sendMessageEnet()` forces every packet back to reliable when the host is not Sunshine.

## Syncing with upstream

The commit above belongs to the upstream repository, not this one. To see what changed
since the fork point:

```bash
git clone https://github.com/XITRIX/moonlight-common-c /tmp/mcc
git -C /tmp/mcc checkout borealis
diff -ru /tmp/mcc/src extern/moonlight-common-c/src
```

Merge anything worth keeping by hand — the history is not shared, so `git merge` will not
work across the two trees.
