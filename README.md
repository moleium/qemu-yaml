# qemu-yaml

A lightweight zero-dependency single-source file C utility for configuring and launching QEMU virtual machnes using YAML configuration files.
The YAML parser used is simple that can only handle common YAML structures needed for the configuration only.

## Building

To build, simply run `make` in the repository's root folder,
This will compile the `qemu-config` executable.

## Usage

```
./qemu-config <config_file> [--dry-run]
```

## Configuration 

```yaml
# Comments will be ignored

qemu_binary: qemu-system-x86_64

memory: 2G
cpu: host
smp: cores=2,threads=2

kernel: ./vmlinuz
initrd: ./initrd.img
append: "root=/dev/sda1 console=ttyS0"

hda: ./disk.qcow2
cdrom: ./install.iso

usb: true
vga: virtio
```

## Support

The utility supports most common QEMU parameters, but it is limited in some aspects.

- `memory`, `cpu`, `smp`, `machine`
- `kernel`, `initrd`, `append`, `boot`
- `hda`, `cdrom`, `drive`
- `display`, `vga`
- `net`, `device`
- `serial`, `parallel`, `monitor`
- `name`, `uuid`
- etc..

Refer to the handler function for the list.

To add support for additional QEMU parameters, update the `handle_qemu_parameter` function in the single source file.

**PRs are welcome** for any additional support.

## Limitations

- No validation of parameters
- Lightweight YAML parser, may not handle all YAML features
