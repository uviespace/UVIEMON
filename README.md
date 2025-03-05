# uviemon

Replacement tool for grmon, low-level software debugging over JTAG via an FT2232H chip.

Used for debugging the GR712RC Dual-Core LEON3FT SPARC V8 Processor on the the Solar wind Magnetosphere Ionosphere Link Explorer, 
or SMILE -- a joint mission between the European Space Agency (ESA) and the Chinese Academy of Sciences (CAS). Anyways, 
probably can be used with any LEON SPARC V8 processors.

GNU readline needs to be installed. 
Fedora:
```text
sudo dnf install readline-devel 
```
Ubuntu:
```text 
sudo apt install libreadline-dev
```

Afterwards it can be build with the following command or simply running the included build script. 

```text
gcc -o uviemon *.c -L./lib/ftdi/build/ -lftd2xx -lreadline -lm -Wall -std=c17
```

**Uses git submodules for some of the included libraries!** After pulling this repo, don't forget to init and update all the submodules!

```text
git submodule update --init --recursive
```
