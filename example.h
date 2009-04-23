
char examplestring[] = "\n\
\n\
    Welcome to the NILM Ethstream examples.\n\
\n\
For the most part, typing \"ethstream\" by itself will sample the first\n\
two channels at 8 kHz on 10V range.  Press CTRL-C to terminate sampling.\n\
\n\
If you want a voltage and current measurement on the first phase of NILM\n\
with default sample rate of 8 kHz and 10V range:\n\
\n\
   ethstream -C 0,3\n\
\n\
The device is configured so that channels 0 through 2 are voltages for\n\
the three phases and channels 3-5 are for currents of the three phases\n\
\n\
If you want only voltages at 16 kHz and 10V range:\n\
\n\
    ethstream -n 3 -r 16000\n\
\n\
The -n option samples a number of channels starting at 0. The rate can be\n\
at least 16000 if 12 channels are sampled , but it can do more if\n\
fewer channels are sampled.  The limiting factor is the highest channel\n\
sampled.  Sampling just the top channel (11) is as bad as sampling\n\
all 12 at once.\n\
Ethstream will warn if you approach the limits of the NerdJack with the\n\
given sampled channels.  Sampling outside the range of the NerdJack might\n\
result in corrupt data or crashing of the device.  There will be no\n\
permanent damage to NILM or NerdJack.\n\
\n\
If you need a higher accuracy but lower range measurement on the currents:\n\
\n\
    ethstream -R 5,10 -C 3,4,5\n\
\n\
The two numbers to the R command set the range to either 5V or 10V. Above,\n\
we are setting channels 0-5 to 5 V range and channels 6-11 to 10 V range.\n\
Channels 6-11 are unconnected, but they can have range set independently.\n\
The values here depend on the NILM box settings to the current transducers.\n\
The value read is the voltage seen by the NerdJack.\n\
\n\
All of the above examples output a digital number from 0 to 65535 with\n\
65535 representing the highest range (5V or 10V).  If you want conversion\n\
to volts for all six voltages and currents:\n\
\n\
    ethstream -c -C 0,3,1,4,2,5\n\
\n\
The channels will be output in the order given in the C command.  This\n\
command will group the current and voltage data by phase.\n\
\n\
If you are supplying data from ethstream to another program, you might\n\
want to dump its output to a file and terminate after a certain number of\n\
samples:\n\
\n\
    ethstream -n 6 -r 8000 - l 16000 > outfile.dat\n\
\n\
This will take 16000 samples at 8 kHz (2 seconds of acquisition) of all\n\
channels and write the data to outfile.dat.  This can be directly read\n\
by a package like MATLAB.\n\
\n\
If there are multiple NerdJacks or you have changed the TCP/IP settings\n\
from default, you might have to specify which one you want to talk to:\n\
\n\
    ethstream -a 192.168.1.210\n\
\n\
This will sample two channels at 8 kHz from the NerdJack at 192.168.1.210.\n\
This is the default \"1\" setting on the NerdJack.  If no address is\n\
specified, ethstream connects first to 192.168.1.209.  It then tries\n\
to autodetect the NerdJack.  This should find the device if you are on\n\
the same network, but it will get confused if there are multiple NerdJacks\n\
on the network.\n\
";
