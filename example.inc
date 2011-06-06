
char examplestring[] = "\n\
\n\
    Welcome to the NILM Ethstream examples.\n\
\n\
For the most part, typing \"ethstream\" by itself will sample the first\n\
two channels at 8 kHz on 10V range.  Press CTRL-C to terminate sampling.\n\
\n\
If you want current measurements on the first two phases of NILM\n\
with default sample rate of 8 kHz and 10V range:\n\
\n\
   ethstream -C 0,3\n\
\n\
The device is configured so that channels 0 through 2 are currents for\n\
the three phases and channels 3-5 are for voltages of the three phases.\n\
The current channels sample voltages that will depend on the DIP switch\n\
settings in the NILM box.  The DIP switch positions allow you to convert\n\
ethstream's readings to true current readings.\n\
\n\
If you want only currents at 16 kHz and 10V range:\n\
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
permanent damage to NILM or NerdJack, but be aware of the possibility of\n\
data corruption.\n\
\n\
If you need a higher accuracy but lower range measurement on the voltages:\n\
\n\
    ethstream -R 5,10 -C 3,4,5\n\
\n\
The two numbers to the R command set the range to either 5V or 10V. Above,\n\
we are setting channels 0-5 to 5 V range and channels 6-11 to 10 V range.\n\
Channels 6-11 are unconnected, but they can have range set independently.\n\
\n\
All of the above examples output a digital number from 0 to 65535 with\n\
65535 representing the highest range (5V or 10V).  0 represents the most\n\
negative range (-5V or -10V).  If you want conversion\n\
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
\n\
Labjack only Timer modes are also avaliable.  Read the Labjack UE9 Users Guide\n\
for more information.  Upto 6 timers of various modes can be specified,\n\
they occur on FIO0-FIO5 which are on channels 200-205 respectively in order\n\
of specification.  For 32 bit timer modes, the MSW should be read from\n\
channel 224 imeadiately after the LSW is read from one the timer channel.\n\
A clock frequency divisor is specified on a per device basis.  For example:\n\
\n\
    ethstream -t 4,12 -T 1 -C 200,224,201\n\
\n\
This will enable two timers with the fastest system clock divisor (48 MhZ/1)\n\
and read the two 16 bit words for timer mode 4 and the single 16 bit word of\n\
timer mode 12.  These three words will occupy their own columns in the output\n\
stream.  Digital timer mode channels can be interspersed with analog inouts.\n\
\n\
Labjack only individual analog input channel gain set is also avaliable.\n\
Gain 0 is default on labjack and corresponds to -5.18v to +5.07v.  Gain 1 is\n\
is -0.01 to +5.07v.  Gain 2 is -0.01 to +2.53v.  Gain 4 is -0.01 to +1.26v.\n\
Gain 8 is -0.01 to +0.62v.  Gains on the -g flag should be put in the desired\n\
order corresponding to the channels as specified by the -C flag.  If there are\n\
less gains specified than channels the remainder default to gain 0.  Extra gains\n\
are ignored.  Gains can be specified for digital inputs or timer modes but they\n\
are irrelevant.  A case where one should do this is if there are dital input\n\
channels intersperced within analog input channels; this keeps the order matched\n\
up so later analog input channels have the expected gain.\n\
\n\
    ethstream -t 4 -T 1 -C 0,1,200,224,2,3 -g 2,2,0,0,4,4 -c\n\
\n\
This will set channles 0,1 and 2,3 to gain 2,2 and 4,4, respectively and convert\n\
the data to volts using the firmware stored factory calibrated data on the\n\
labjack. The digital channels 200 and 224 will remain undisturbed as integers.\n\
\n\
";
