#include <csignal>
#include <iostream>

#include "basic_rtp_generator.h"
#include "logger.h"
#include "msg_type_converter.h"
#include "scream_server_single.h"
#include "scream_utils.h"
#include "scream_v2_server_single.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "udp_socket.h"

bool stop = false;

void signalHandler(int signum) {
    logger::log(logger::INFO, "Interrupt signal (", signum, ") received");
    stop = true;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr
            << "command format is: " << argv[0]
            << " <game_server_ip> <proxy_client_ip> [game_server_binding_ip (default = 127.0.0.1)] [proxy_client_binding_ip (default = 0.0.0.0)]"
            << std::endl;
        return 1;
    }

    const std::string game_server_ip(argv[1]);
    const std::string proxy_client_ip(argv[2]);
    const std::string game_server_binding_ip(argc >= 4 ? argv[4] : "127.0.0.1");
    const std::string proxy_client_binding_ip(argc >= 5 ? argv[5] : "0.0.0.0");

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    logger::setMinimalLogLevel(logger::DEBUG);

    /*------------------------------------------------------------------------------------------------------------------
     * video chain
    ------------------------------------------------------------------------------------------------------------------*/
    BasicRtpGenerator generator("video encoder");
    /*generator.init({
        {"type", "video"},
        {"bitrate", "1000000"},
        {"framerate", "30"},
        {"ssrc", "100"},
    });*/
    UdpSocket server_side_video_rtp("server side video rtp");
    server_side_video_rtp.init({
        {"local_addr", game_server_binding_ip},
        {"local_port", "10002"},
        {"remote_addr", game_server_ip},
        {"remote_port", "0"},
    }); // server udp port is dynamic
    MsgTypeConverter<Msg::RAW, Msg::RTP_PACKET> video_rtp_converter("video rtp message converter");
    video_rtp_converter.init({});
    ScreamV2ServerSingle scream("scream server", true);
    scream.init({
        {"local_addr", proxy_client_binding_ip},
        {"local_port", "30002"},
        {"remote_addr", proxy_client_ip},
        {"remote_port", "30002"},
        {"min_bitrate", "500000"},
        {"max_bitrate", "30000000"},
        {"start_bitrate", "10000000"},
    });
    // generator.registerQueue(Msg::RTP_PACKET, scream.getQueue());
    // scream.registerQueue(Msg::BITRATE_REQUEST, generator.getQueue());
    server_side_video_rtp.registerQueue(Msg::RAW, video_rtp_converter.getQueue());
    video_rtp_converter.registerQueue(Msg::RTP_PACKET, scream.getQueue());

    UdpSocket server_side_video_rtcp("server side video rtcp");
    server_side_video_rtcp.init({
        {"local_addr", game_server_binding_ip},
        {"local_port", "10003"},
        {"remote_addr", game_server_ip},
        {"remote_port", "0"},
    }); // server udp port is dynamic
    UdpSocket client_side_video_rtcp("client side video rtcp");
    client_side_video_rtcp.init({
        {"local_addr", proxy_client_binding_ip},
        {"local_port", "30003"},
        {"remote_addr", proxy_client_ip},
        {"remote_port", "30003"},
    });
    server_side_video_rtcp.registerQueue(Msg::RAW, client_side_video_rtcp.getQueue());
    client_side_video_rtcp.registerQueue(Msg::RAW, server_side_video_rtcp.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * audio chain
    ------------------------------------------------------------------------------------------------------------------*/
    UdpSocket server_side_audio_rtp("server side audio rtp");
    server_side_audio_rtp.init({
        {"local_addr", game_server_binding_ip},
        {"local_port", "10000"},
        {"remote_addr", game_server_ip},
        {"remote_port", "0"},
    }); // server udp port is dynamic
    UdpSocket client_side_audio_rtp("client side audio rtp");
    client_side_audio_rtp.init({
        {"local_addr", proxy_client_binding_ip},
        {"local_port", "30000"},
        {"remote_addr", proxy_client_ip},
        {"remote_port", "30000"},
    });
    server_side_audio_rtp.registerQueue(Msg::RAW, client_side_audio_rtp.getQueue());
    client_side_audio_rtp.registerQueue(Msg::RAW, server_side_audio_rtp.getQueue());

    UdpSocket server_side_audio_rtcp("server side audio rtcp");
    server_side_audio_rtcp.init({
        {"local_addr", game_server_binding_ip},
        {"local_port", "10001"},
        {"remote_addr", game_server_ip},
        {"remote_port", "0"},
    }); // server udp port is dynamic
    UdpSocket client_side_audio_rtcp("client side audio rtcp");
    client_side_audio_rtcp.init({
        {"local_addr", proxy_client_binding_ip},
        {"local_port", "30001"},
        {"remote_addr", proxy_client_ip},
        {"remote_port", "30001"},
    });
    server_side_audio_rtcp.registerQueue(Msg::RAW, client_side_audio_rtcp.getQueue());
    client_side_audio_rtcp.registerQueue(Msg::RAW, server_side_audio_rtcp.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * input chain
    ------------------------------------------------------------------------------------------------------------------*/
    UdpSocket server_side_input_stream("server side udp inputs");
    server_side_input_stream.init({
        {"local_addr", game_server_binding_ip},
        {"local_port", "19999"},
        {"remote_addr", game_server_ip},
        {"remote_port", "9999"},
    });
    UdpSocket client_side_input_stream("client side udp inputs");
    client_side_input_stream.init({
        {"local_addr", proxy_client_binding_ip},
        {"local_port", "29999"},
        {"remote_addr", proxy_client_ip},
        {"remote_port", "29999"},
    });
    server_side_input_stream.registerQueue(Msg::RAW, client_side_input_stream.getQueue());
    client_side_input_stream.registerQueue(Msg::RAW, server_side_input_stream.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * command chain
    ------------------------------------------------------------------------------------------------------------------*/

    TcpServer tcp_server("client side tcp commands");
    tcp_server.init({
        {"local_addr", proxy_client_binding_ip},
        {"local_port", "29999"},
    });
    MsgTypeConverter<Msg::BITRATE_REQUEST, Msg::RAW> brm_converter("bitrate request message converter");
    brm_converter.init({});
    TcpClient tcp_client("server side tcp commands");
    tcp_client.init({
        {"local_addr", game_server_binding_ip},
        {"local_port", "19999"},
        {"remote_addr", game_server_ip},
        {"remote_port", "9999"},
    });

    tcp_server.registerQueue(Msg::RAW, tcp_client.getQueue());
    tcp_client.registerQueue(Msg::RAW, tcp_server.getQueue());
    scream.registerQueue(Msg::BITRATE_REQUEST, brm_converter.getQueue());
    brm_converter.registerQueue(Msg::RAW, tcp_client.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * start all blocks
    ------------------------------------------------------------------------------------------------------------------*/
    initT0();
    scream.start();
    // generator.start();
    video_rtp_converter.start();
    server_side_video_rtp.start();

    server_side_video_rtcp.start();
    client_side_video_rtcp.start();

    client_side_audio_rtp.start();
    server_side_audio_rtp.start();

    server_side_audio_rtcp.start();
    client_side_audio_rtcp.start();

    server_side_input_stream.start();
    client_side_input_stream.start();

    brm_converter.start();
    tcp_client.start();
    tcp_server.start();

    while (!stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    /*------------------------------------------------------------------------------------------------------------------
     * stop all blocks
    ------------------------------------------------------------------------------------------------------------------*/
    brm_converter.stop();
    tcp_server.stop();
    tcp_client.stop();

    client_side_input_stream.stop();
    server_side_input_stream.stop();

    client_side_audio_rtcp.stop();
    server_side_audio_rtcp.stop();

    server_side_audio_rtp.stop();
    client_side_audio_rtp.stop();

    client_side_video_rtcp.stop();
    server_side_video_rtcp.stop();

    server_side_video_rtp.stop();
    video_rtp_converter.stop();
    // generator.stop();
    scream.stop();

    logger::log(logger::INFO, "all done");
    return 0;
}
