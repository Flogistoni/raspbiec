raspbiec
========

Commodore serial (IEC) bus interface for Raspberry Pi
-----------------------------------------------------


`raspiec` is intended for connecting Raspberry Pi to the Commodore serial bus.
It can  take the role of the computer or the disk drive. It has been
developed and tested with a Commodore 64 and a 1541-II.


What it can currently do:

* Load and save single PRG files to the disk drive
* Serve single PRG files (load and save) from a Pi directory to the computer
* Serve single PRG files (load and save) from a disk image to the computer

What it cannot yet do:

* Disk drive commands, other file types than PRG
* Support fastloaders


As there obviously is no IEC bus connector on the Pi, an adapter must
be made. The instructions are in the `documents` subdiredctory. As far
as electronics projects go this is quite simple to build. The six pin DIN
connector might be difficult to find; I happened to have an old video
dubbing cable set that had those and which I dismantled and recycled for
this project.

The application itself consists of two parts, a kernel module and a
command-line program. Although constructing a kernel module is not trivial,
it was unavoidable in this case. My first trials were userspace-only code,
but the realtime demands of the bus traffic were simply too much.

The kernel module can be installed with the `instdrv.sh` script.
It is very simple, without error checking, but serves as documentation
on how to install the module and how to make the device node accessible.
The module takes one parameter, debuglevel from 0 to 3.
The debug prints go to `/var/log/messages`.

The command line utility `raspbiec` is used like this:

    pi@raspberrypi ~ $ ./raspbiec 
	As drive:    raspbiec [serve] <directory or disk image> [<command>|<device #>]
					<command> is a computer command below applied to the disk image;
					in this way files can be transferred between the filesystem and the image
	As computer: raspbiec load <filename> [<device #>]
				 raspbiec save <filename> [<device #>]
				 raspbiec cmd <command> [<device #>]
				 raspbiec errch [<device #>]

There is a binary of the kernel module compiled against an old kernel
in the `bin_kernel_...` subdirectory. There are compiling instructions for example in <http://bchavez.bitarmory.com/archive/2013/01/16/compiling-kernel-modules-for-raspberry-pi.aspx>,
and of course more can be found with the help of your favourite search engine.
The makefile expects that the variables `KERNEL_SRC` and `CCPREFIX` have some
meaningful values.

I will not go into a detailed description of the implementation here, as it is
quite well documented in the source files. Briefly, the kernel module
translates traffic from the bus into a byte/control code stream
and sends bytes/control codes to the bus as well. This is executed in a state
machine reacting to GPIO and timer interrupts and device reads and writes.
The userspace side reads and writes those streams via the device node
(`/dev/raspbiec`) and implements the higher level logic.

In hindsight, this project would have been impossible without a logic
analyzer. Fortunately, I had another Pi where I could run
[Panalyzer](https://github.com/richardghirst/Panalyzer).
The GPIO pins labeled DEBUG1 and DEBUG2 in the schematics contain extra
information in addition to the bus traffic: DEBUG1 is high when the kernel
module state machine is busy, DEBUG2 is high when serving a timer interrupt.
