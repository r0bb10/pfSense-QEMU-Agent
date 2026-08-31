# pfSense QEMU Guest Agent

pfSense package for the Proxmox QEMU Guest Agent (`qemu-ga`) on pfSense 2.8.x.

This package bundles the `qemu-ga` binary from the FreeBSD 15 latest `qemu-guest-agent` package and adds pfSense WebGUI, service, status page, and dashboard widget integration.

## Scope

- Track FreeBSD 15 amd64 latest `qemu-guest-agent` packages.
- Package `/usr/local/bin/qemu-ga` as `pfSense-pkg-qemu-guest-agent`.
- Provide a pfSense rc.d service.
- Provide GUI-based enable/disable configuration.
- Provide status page and dashboard widget.

## Install

To install download latest pkg in /tmp and run:

```sh
pkg add -f /tmp/pfSense-pkg-qemu-guest-agent-*.pkg
```