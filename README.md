# pfSense QEMU Guest Agent

pfSense package for the Proxmox QEMU Guest Agent (`qemu-ga`) on pfSense 2.8.x / FreeBSD 15 amd64.

This package bundles the `qemu-ga` binary from the FreeBSD 15 latest `qemu-guest-agent` package and adds pfSense WebGUI, service, status page, and dashboard widget integration.

## Scope

- Track FreeBSD 15 amd64 latest `qemu-guest-agent` packages.
- Package `/usr/local/bin/qemu-ga` as `pfSense-pkg-qemu-guest-agent`.
- Provide a pfSense rc.d service.
- Provide GUI-based enable/disable configuration.
- Provide status page and dashboard widget.

## Local Package Build

The package build expects a FreeBSD/amd64 `qemu-ga` binary at `dist/qemu-ga`.

```sh
./build.sh package
```

The GitHub workflow downloads the latest FreeBSD 15 `qemu-guest-agent` package, extracts `qemu-ga`, and creates the pfSense package on FreeBSD 15.
