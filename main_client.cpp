#include <csignal>
#include <cstring>

#include <iostream>

#include "logger.h"
#include "msg_type_converter.h"
#include "scream_client_single.h"
#include "tcp_client.h"
#include "tcp_server.h"
#include "udp_socket.h"

bool stop = false;

void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    stop = true;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr
            << "command format is: " << argv[0]
            << " <proxy_server_ip> <game_client_ip> [proxy_server_binding_ip (default = 0.0.0.0)] [game_client_binding_ip (default = 127.0.0.1)]"
            << std::endl;
        return 1;
    }

    const std::string proxy_server_ip(argv[1]);
    const std::string game_client_ip(argv[2]);
    const std::string proxy_server_binding_ip(argc >= 4 ? argv[4] : "0.0.0.0");
    const std::string game_client_binding_ip(argc >= 5 ? argv[5] : "127.0.0.1");

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    logger::setMinimalLogLevel(logger::DEBUG);

    /*------------------------------------------------------------------------------------------------------------------
     * video chain
    ------------------------------------------------------------------------------------------------------------------*/
    ScreamClientSingle scream("scream client");
    scream.init({
        {"local_addr", proxy_server_binding_ip},
        {"local_port", "30002"},
        {"remote_addr", proxy_server_ip},
        {"remote_port", "30002"},
    });
    MsgTypeConverter<Msg::RTP_PACKET, Msg::RAW> video_rtp_converter("video rtp_converter");
    video_rtp_converter.init({});
    UdpSocket client_side_video_rtp("client side video rtp");
    client_side_video_rtp.init({
        {"local_addr", game_client_binding_ip},
        {"local_port", "20002"},
        {"remote_addr", game_client_ip},
        {"remote_port", "10002"},
    });
    scream.registerQueue(Msg::RTP_PACKET, video_rtp_converter.getQueue());
    video_rtp_converter.registerQueue(Msg::RAW, client_side_video_rtp.getQueue());

    UdpSocket client_side_video_rtcp("client side video rtcp");
    client_side_video_rtcp.init({
        {"local_addr", game_client_binding_ip},
        {"local_port", "20003"},
        {"remote_addr", game_client_ip},
        {"remote_port", "10003"},
    });
    UdpSocket server_side_video_rtcp("server side video rtcp");
    server_side_video_rtcp.init({
        {"local_addr", proxy_server_binding_ip},
        {"local_port", "30003"},
        {"remote_addr", proxy_server_ip},
        {"remote_port", "30003"},
    });
    client_side_video_rtcp.registerQueue(Msg::RAW, server_side_video_rtcp.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * audio chain
    ------------------------------------------------------------------------------------------------------------------*/
    UdpSocket server_side_audio_rtp("server side audio rtp");
    server_side_audio_rtp.init({
        {"local_addr", proxy_server_binding_ip},
        {"local_port", "30000"},
        {"remote_addr", proxy_server_ip},
        {"local_port", "30000"},
    });
    UdpSocket client_side_audio_rtp("client side audio rtp");
    client_side_audio_rtp.init({
        {"local_addr", game_client_binding_ip},
        {"local_port", "20000"},
        {"remote_addr", game_client_ip},
        {"remote_port", "10000"},
    });
    server_side_audio_rtp.registerQueue(Msg::RAW, client_side_audio_rtp.getQueue());

    UdpSocket client_side_audio_rtcp("client side audio rtcp");
    client_side_audio_rtcp.init({
        {"local_addr", game_client_binding_ip},
        {"local_port", "20001"},
        {"remote_addr", game_client_ip},
        {"remote_port", "10001"},
    });
    UdpSocket server_side_audio_rtcp("server side audio rtcp");
    server_side_audio_rtcp.init({
        {"local_addr", proxy_server_binding_ip},
        {"local_port", "30001"},
        {"remote_addr", proxy_server_ip},
        {"remote_port", "30001"},
    });
    client_side_audio_rtcp.registerQueue(Msg::RAW, server_side_audio_rtcp.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * input chain
    ------------------------------------------------------------------------------------------------------------------*/
    UdpSocket client_side_input_stream("client side input stream");
    client_side_input_stream.init({
        {"local_addr", game_client_binding_ip},
        {"local_port", "19999"},
        {"remote_addr", game_client_ip},
        {"remote_port", "9999"},
    });
    UdpSocket server_side_input_stream("server side input stream");
    server_side_input_stream.init({
        {"local_addr", proxy_server_binding_ip},
        {"local_port", "29999"},
        {"remote_addr", proxy_server_ip},
        {"remote_port", "29999"},
    });
    client_side_input_stream.registerQueue(Msg::RAW, server_side_input_stream.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * command chain
    ------------------------------------------------------------------------------------------------------------------*/
    TcpServer client_side_command_stream("client side command stream");
    client_side_command_stream.init({
        {"local_addr", game_client_binding_ip},
        {"local_port", "19999"},
    });
    TcpClient server_side_command_stream("server side command stream");
    server_side_command_stream.init({
        {"local_addr", proxy_server_binding_ip},
        {"local_port", "29999"},
        {"remote_addr", proxy_server_ip},
        {"remote_port", "29999"},
    });
    client_side_command_stream.registerQueue(Msg::RAW, server_side_command_stream.getQueue());
    server_side_command_stream.registerQueue(Msg::RAW, client_side_command_stream.getQueue());

    client_side_video_rtp.start();
    video_rtp_converter.start();
    scream.start();

    server_side_video_rtcp.start();
    client_side_video_rtcp.start();

    client_side_audio_rtp.start();
    server_side_audio_rtp.start();

    server_side_audio_rtcp.start();
    client_side_audio_rtcp.start();

    server_side_input_stream.start();
    client_side_input_stream.start();

    server_side_command_stream.start();
    client_side_command_stream.start();
    /*auto msg = std::make_shared<Msg>();
    msg->type = Msg::RAW;
    msg->data = malloc(64);
    msg->size = 64;
    server_side_command_stream.getQueue()->enqueue(msg);
*/
    while (!stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    client_side_command_stream.stop();
    server_side_command_stream.stop();

    client_side_input_stream.stop();
    server_side_input_stream.stop();

    client_side_audio_rtcp.stop();
    server_side_audio_rtcp.stop();

    server_side_audio_rtp.stop();
    client_side_audio_rtp.stop();

    client_side_video_rtcp.stop();
    server_side_video_rtcp.stop();

    scream.stop();
    video_rtp_converter.stop();
    client_side_video_rtp.stop();

    return 0;
}
