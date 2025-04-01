# embeddedTS Examples
Various example applications used on our platforms demonstrating various interfaces and features.


## Building
This project uses `meson` and `ninja`, ensure these build utilities are installed on the device. By default, all tools and examples will be built. See below for information on how to modify this behavior.


### Native Build

```
git clone https://github.com/embeddedTS/embeddedTS-examples
cd embeddedTS-examples
meson setup build/
ninja -C build/
# To install built binaries
ninja install -C build/
```


#### Specifying Options
By default, all tools and examples contained in this repo will be built. However, specific tools and examples can be configured.

Available options can be viewed with:
```
# If build directory not already created
meson setup build/
meson configure build/
```

The options listed under `Project options` are what can be configured. These are set up by configuring the build directory. For example, to enable the CAN tool:
```
meson configure build/ -Dcan=enabled
```

Then build as normal. If any options are set to enabled, then only those options will be built and all other tools and examples will not be built.
