<div align="center">

# banana

![banana](.github/banana.png)

A tiling window manager.

</div>

## installation

### building

This should be self explanatory but if you don't know how then run this to build & install.

```bash
make release; sudo make install
```

Make sure you have the necessary dependencies installed.

### releases

Releases / tags are currently not being pushed out as banana is not production ready, and/or
reasonably complete. Releases will be pushed out when the project becomes more mature, and
grows over time.

## Q&A

### configuration

Configuration is done at `$HOME/.config/banana/banana.conf`, this file will be automatically
created if it doesn't exist, all available configuration options exist in this file already.
All options are self explanatory and don't need documentation, if you can't figure it out,
then this window manager might not be for you.

Changes to the config file do not require a restart, but changes are not applied immediately,
to apply changes use `alt+r` or whatever you have `reload_config` set to in the config
file. Additionally you can use `banana reload` to reload the config as well.

By default the terminal specified in the config file is set to `alacritty` the keybind to
launch it is `alt+q`. Make sure you have `alacritty` installed, or just change it before
launching.

When checking for config errors use `banana validate` it will validate your config file and
print errors to stdout.

### layouts

Currently Banana only supports 2 layouts `master&stack` & `monocle`, layouts can be changed
in the configuration file via editing the `layout` option in the `general` section. Some
options are different between layouts, for instance `focus_window` will cycle between windows
in the `monocle` layout while in the `master&stack` layout it will simply focus the next window
in the direction. Additionally `cycle_focus` can be used to cycle focus between floating and
a monocle window.

There is an additional "layout", I wouldn't really call it a new layout since it's still master
and stack basically. But it's called `centered_master` if set to true windows are arranged right
to left and the master window is in the center instead of being exclusively on the left.

For cycling between layouts on the fly use `cycle_layouts` it will cycle between the available
layouts, this will be overriden on config reload depending on if a different layout is specified
in your config.

### compositing

By default banana doesn't have rounded corners, opacity, animations, and all of that junk
baked in, you will have to use what is known as a compositor e.g. `picom`.

### swallowing

Banana supports window swallowing, where a parent window hides when its child window opens.
This relies on the `_NET_WM_PID` X11 property and parent-child process relationships. Windows
without a proper PID property cannot be swallowed e.g. xev.