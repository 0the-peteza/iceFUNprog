# iceFUNprog
Programmer for Devantech iCE40 modules, iceFUN and iceWerx

## Building and Installing

To build the tool:
```
make
```

To build and install the programmer:
```
make clean && sudo make install
```

## Using `iceFUNprog`

To program the module:
```
iceFUNprog blinky.bin
```

For more details while programming the module:
```
iceFUNprog -v blinky.bin
```

For too much detail when programming the module:
```
iceFUNprog -vv blinky.bin
```

For build deets:
```
iceFUNprog -V
```


Note: in case the programmer doesn't seem to do anything, make sure you are not accidentally
running software which tries to use your device as a modem 
(e.g. [ModemManager](https://www.freedesktop.org/wiki/Software/ModemManager/)) as this 
interferes with this programmer.
