# pfSense QEMU Guest Agent

pfSense package for the Proxmox QEMU Guest Agent (`qemu-ga`) on pfSense 2.8.x.

This package builds `qemu-ga` from official QEMU sources and adds pfSense WebGUI,
service, and dashboard widget integration.

## Scope

- Track stable upstream QEMU releases from `https://download.qemu.org/`.
- Package `/usr/local/bin/qemu-ga` as `pfSense-pkg-qemu-guest-agent`.
- Provide a pfSense rc.d service.
- Provide GUI-based enable/disable configuration.

## Install

To install download latest pkg in /tmp and run:

```sh
pkg add -f /tmp/pfSense-pkg-qemu-guest-agent-*.pkg
```