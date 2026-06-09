# Filesystem

Whirliscope currently builds an ext2 root filesystem image at
`build/rootfs.ext2` from the source tree under `rootfs/` plus built userspace
ELFs staged into `/bin`.

## Shell Commands

Relative paths are resolved against the calling process cwd, so `cat hello.txt`
works from `/`.

`run <name>` searches `/bin/<name>`. `run <path>` executes an ELF from the
given filesystem path. Booting starts `/bin/init`.

## Host Access

Rebuild the image with:

```sh
make -f GNUmakefile iso
```

Inspect it without mounting:

```sh
debugfs -R 'ls -p /' build/rootfs.ext2
debugfs -R 'cat /hello.txt' build/rootfs.ext2
```

Or, mounting it:

```sh
sudo mount -o loop build/rootfs.ext2 /mnt
sudo umount /mnt
```
