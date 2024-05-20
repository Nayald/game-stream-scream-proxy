# game-stream-scream-proxy  
This repository contains code for two local proxies to adjust bitrate for our platform using SCReAM CCA for the video stream. 

The point of the proxy design is to be able to easily use an alternative CCA by defining interfaces without needing to integrate the CCA code directly into the code of the platform.

## Server-side SCReAM proxy

![Server-side proxy](https://github.com/Nayald/game-stream-scream-proxy/blob/main/image/PRX_Server.png?raw=true)

 The server-side SCReAM proxy is by far the most critical because it handles high-bitrate multimedia flows. The CG serverinteracts with the local SCReAM proxy through the loopbackinterface (lo). The proxy handles the six flows composinga CG session through dedicated sockets that can be of fourtypes.
 
- UDP Socket: to send or receive UDP segments;
- TCP Client: to send or receive TCP segments;
- TCP Server: likeTCP Clientbut does not initiate a TCPconnection;
- SCReAM: to send or receive RTP packets with congestioncontrol.

Pre-defined port numbers are used for each flow. One must note that only the RTP video flow is actually processed by the SCReAM algorithm. This is because it is by far the most significant flow and the only one that can truly benefit from congestion control to adapt its bitrate by adjusting the video codec parameters (mostly through the quantization factor that defines the level of video compression). The other flows going through the SCReAM proxy are just transferred untouched to the corresponding socket of the client.

The reception sockets extract the payload of packets and pass it to the corresponding sending sockets through a queue. The two TCP sockets (TCP Client and TCP Server) used by the proxy are full duplex. They handle the signaling traffic between the client and server. The SCReAM component is connected to the client to send the video flow and receive its feedback. The packets sent by the CG server are placed in a FIFO queue which reading depends on the network conditions. The sending of packets is driven by SCReAM’s CCA, according to Ericsson’s specifications [7]. It takes into account the length of the RTP queue and the periodic RTCP feedback from the client, and of course ECN network feedback when available. Thanks to this feedback SCReAMcan monitor the network delay, packet loss and the state of the bottleneck. The CCA finally defines a target bitrate to meet the current conditions and that will be given to the video encoder. In particular, SCReAM can issue two commands:

- Bitrate Request: a new target bitrate defined for the encoder;
- I_Frame Request: a new I_Frame is recommended to prevent the propagation of visual artefacts when a packet loss is detected.

Theses commands are translated to a format compatible with the actual encoder by the BitrateConverter component and sent to the server through the loopback.

![Client-side proxy](https://github.com/Nayald/game-stream-scream-proxy/blob/main/image/PRX_Client.png?raw=true)

## Client-side SCReAM proxy

 Like the server-side proxy, most flows are just transferred to the client by the client-side proxy. Only the video stream is processed specifically.When a packet is received, the SCReAM component forwards the UDP payload to theRTP Converter. It extracts some information and computes statistics on the received video flow and periodically creates a RTCP report that is sent back to the server. In particular, the value of the ECN-CE bits in the received packets that denotes an occurring congestion on the path is extracted and is considered by the CCA.


