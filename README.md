# PipewireSimpleConfigurator
This tool makes it easy to configure simple pipewire settings from a gui.


It takes the pipewire configs pipewire.conf pipewire-pulse.conf and client.conf as templates and then saved them in ~/.config/pipewire .

With apply the new settings are saved and applied and the services pipewire.service, pipewire-pulse.server, pipewire.socket and pipewire-pulse.socket are restarted.


# Build instructions

```
mkdir build && cd build
cmake ..
make -j$(nproc)
cpack
```

And then you can just install the package or use the binary directly

