# Docker

`one` is also a container image, for amd64 and ARM. It holds the compiler and its
license and nothing else, so there's nothing in it to fall out of date.

```sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" ghcr.io/da0x/uione check .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" ghcr.io/da0x/uione build .
```

The folder you're in is the project. `--user` makes the files `one build` writes
yours rather than root's.

`ghcr.io/da0x/uione:latest` is the latest release, and each release has its own
tag, like `ghcr.io/da0x/uione:0.7.1`. In CI, pin the release:

```yaml
- run: docker run --rm -v "$PWD:/work" ghcr.io/da0x/uione:0.7.1 check .
```
