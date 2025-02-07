# General
This is an RF simulator that allows to test OAI without an RF board. It replaces an actual RF board driver.

As much as possible, it works like an RF board, but not in real-time: It can run faster than real-time if there is enough CPU, or slower (it is CPU-bound instead of real-time RF sampling-bound).

It can be run either in:

- "noS1" mode: the generated IP traffic is sent and received between gNB and UE IP tunnel interfaces ("oaitun") by applications like ping and iperf
- "phy-test" mode: random UL and DL traffic is generated at every scheduling opportunity 


# build

## From [build_oai](../../../doc/BUILD.md) script
The RF simulator is implemented as an OAI device and always built when you build the OAI eNB or the OAI UE.

Using the `-w SIMU` option it is possible to just re-build the RF simulator device. 

Example:
```bash
./build_oai --UE --eNB
Will compile UE
Will compile eNB
CMAKE_CMD=cmake ..
No local radio head and no transport protocol selected
No radio head has been selected (HW set to None)
No transport protocol has been selected (TP set to None)
RF HW set to None
Flags for Deadline scheduler: False
......................
......................
Compiling rfsimulator
Log file for compilation has been written to: /usr/local/oai/rfsimu_config/openairinterface5g/cmake_targets/log/rfsimulator.Rel15.txt
rfsimulator compiled
......................
......................
```

## Add the rfsimulator after initial build
After any regular build you can compile the device from the build directory:
```bash
cd <path to oai sources>/openairinterface5g/cmake_targets/ran_build/build
make rfsimulator
```
This is equivalent to using `-w SIMU` when running the `build_oai` script.

# Usage
To use the RF simulator add the `--rfsim` option to the command line. By default the RF simulator device will try to connect to host 127.0.0.1, port 4043, which is usually the behavior for the UE.

The RF simulator is using the configuration module, and its parameters are defined in a specific section called "rfsimulator".

| parameter            | usage                                                                                                             | default |
|:---------------------|:------------------------------------------------------------------------------------------------------------------|----:|
| serveraddr           | ip address to connect to, or "enb" to behave as a tcp server                                                      | 127.0.0.1 |
| serverport           | port number to connect to or to listen on (eNB, which behaves as a tcp server)                                    | 4043 |
| options              | list of comma separated run-time options, two are supported: `chanmod` to enable channel modeling and `saviq` to write transmitted iqs to a file | all options disabled  |
| modelname            | Name of the channel model to apply on received iqs when the `chanmod` option is enabled                           | AWGN |
| IQfile               | Path to the file to be used to store iqs, when the `saviq` option is enabled                                      | /tmp/rfsimulator.iqs |
        
Setting the env variable RFSIMULATOR can be used instead of using the serveraddr parameter; it is to preserve compatibility with previous version.

## 4G case

For the UE, it should be set to the IP address of the eNB. For example:
```bash
sudo RFSIMULATOR=192.168.2.200 ./lte-uesoftmodem -C 2685000000 -r 50 
```
For the eNB, use a valid configuration file setup for the USRP board tests and start the softmodem as usual, **but**, adding the `--rfsim` option.
```bash
sudo RFSIMULATOR=enb ./lte-softmodem -O <config file> --rfsim
```

Except this, the UE and the eNB can be used as if the RF is real. noS1 mode can also be used with the RF simulator.

If you reach 'RA not active' on UE, be careful to generate a valid SIM.
```bash
$OPENAIR_DIR/targets/bin/conf2uedata -c $OPENAIR_DIR/openair3/NAS/TOOLS/ue_eurecom_test_sfr.conf -o .
```

## 5G case

The 5G RF simulator will be aligned with the 4G as the efforts for merging the 5G specific branches into the develop make progresses.

After regular build, add the simulation driver (do not use ./build_oai -w SIMU until 4G and 5G branches are merged).
```bash
cd ran_build/build
make rfsimulator
```

### Launch gNB in one window

```bash
sudo RFSIMULATOR=server ./nr-softmodem -O ../../../targets/PROJECTS/GENERIC-LTE-EPC/CONF/gnb.band78.tm1.106PRB.usrpn300.conf --parallel-config PARALLEL_SINGLE_THREAD --rfsim --phy-test
```

### Launch UE in another window

```bash
sudo RFSIMULATOR=<TARGET_GNB_INTERFACE_ADDRESS> ./nr-uesoftmodem --rfsim --phy-test --rrc_config_path ../../../ci-scripts/rrc-files 
```
Note:
1. <TARGET_GNB_INTERFACE_ADDRESS> can be 127.0.0.1 if both gNB and nrUE executables run on the same host, OR the IP interface address of the remote host running the gNB executable, if the gNB and nrUE run on separate hosts
2. the --rrc_config_path parameter can be omitted (but not necessarily) if the gNB and nrUE run on the same host, in which case the gNB provides the nrUE with the necessary rrc configuration
3. to enable the noS1 mode --noS1 and --nokrnmod 1 options should be added to the command line


In the UE, you can add `-d` option to get the softscope.

### Testing IP traffic (ping and iperf)

```
ping -I oaitun_enb1 10.0.1.2 (from gNB mchine)
ping -I oaitun_ue1 10.0.1.1 (from nrUE mchine)
``` 

```iperf (Downlink):
Server nrUE machine: iperf -s -i 1 -u -B 10.0.1.2
Client gNB machine: iperf -c 10.0.1.2 -u -b 0.1M --bind 10.0.1.1
```

```iperf (Uplink):
Server gNB machine: iperf -s -i 1 -u -B 10.0.1.1
Client nrUE machine: iperf -c 10.0.1.1 -u -b 0.1M --bind 10.0.1.2
Note: iperf tests can be performed only when running gNB and nrUE on separate hosts. 
```

### Store and replay

You can store emitted I/Q samples. If you set the option `saviq`, the simulator will write all the I/Q samples into this file. Then, you can replay with the executable `replay_node`.

First compile it like other binaries:
```bash
make replay_node
```
You can use this binary as I/Q data source to feed whatever UE or gNB with recorded I/Q samples.

The file format is successive blocks of a header followed by the I/Q array. If you have existing stored I/Q, you can adapt the tool `replay_node` to convert your format to the rfsimulator format.

The format intends to be compatible with the OAI store/replay feature on USRP.

### Channel simulation

When the `chanmod` option is enabled, the RF channel simulator is called.

In the current version all channel parameters are set depending on the model name via a call to:
```bash
new_channel_desc_scm(bridge->tx_num_channels,
                     bridge->rx_num_channels,
                     <model name>,
                     bridge->sample_rate,
                     bridge->tx_bw,
                     0.0, // forgetting_factor
                     0,   // maybe used for TA
                     0);  // path_loss in dB
```
Only the input noise can be changed on command line with the `-s` parameter.

With path loss = 0 set `-s 5` to see a little noise. `-s` is a shortcut to `channelmod.s`. It is expected to enhance the channel modelization flexibility by the addition of more parameters in the channelmod section.

# Caveats
Still issues in power control: txgain, rxgain are not used.
