#include <iostream>
#include <csignal>

#include "BasicRtpGenerator.h"
#include "ScreamServerSingle.h"
#include "MsgTypeConverter.h"
#include "UdpSocket.h"
#include "TcpClient.h"
#include "TcpServer.h"

#include "logger.h"
#include "scream_utils.h"

bool stop = false;
void signalHandler( int signum ) {
    logger::log(logger::INFO, "Interrupt signal (", signum, ") received");
    stop = true;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    logger::setMinimalLogLevel(logger::DEBUG);

    /*------------------------------------------------------------------------------------------------------------------
     * video chain
    ------------------------------------------------------------------------------------------------------------------*/
    /*BasicRtpGenerator generator("video encoder");
    generator.init({
        {"type", "video"},
        {"bitrate", "1000000"},
        {"framerate", "30"},
        {"ssrc", "100"},
    });*/
    UdpSocket server_side_video_rtp("server side video rtp");
    server_side_video_rtp.init({
            {"src_addr", "127.0.0.1"},
            {"src_port", "10002"},
            {"dst_addr", "127.0.0.1"},
            {"dst_port", "0"},
    });
    MsgTypeConverter<Msg::RAW, Msg::RTP_PACKET> video_rtp_converter("video rtp message converter");
    video_rtp_converter.init({});
    ScreamServerSingle scream("scream server", true);
    scream.init({
        {"src_addr", "0.0.0.0"},
        {"src_port", "30002"},
        {"dst_addr", "192.168.1.17"},
        {"dst_port", "30002"},
        {"min_bitrate", "500000"},
        {"max_bitrate", "30000000"},
        {"start_bitrate", "10000000"},
    });
    //generator.registerQueue(Msg::RTP_PACKET, scream.getQueue());
    //scream.registerQueue(Msg::BITRATE_REQUEST, generator.getQueue());
    server_side_video_rtp.registerQueue(Msg::RAW, video_rtp_converter.getQueue());
    video_rtp_converter.registerQueue(Msg::RTP_PACKET, scream.getQueue());

    UdpSocket server_side_video_rtcp("server side audio rtcp");
    server_side_video_rtcp.init({
        {"src_addr", "127.0.0.1"},
        {"src_port", "10003"},
        {"dst_addr", "127.0.0.1"},
        {"dst_port", "0"},
    });
    UdpSocket client_side_video_rtcp("client side audio rtcp");
    client_side_video_rtcp.init({
        {"src_addr", "0.0.0.0"},
        {"src_port", "30003"},
        {"dst_addr", "192.168.1.17"},
        {"dst_port", "30003"},
    });
    server_side_video_rtcp.registerQueue(Msg::RAW, client_side_video_rtcp.getQueue());
    client_side_video_rtcp.registerQueue(Msg::RAW, server_side_video_rtcp.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * audio chain
    ------------------------------------------------------------------------------------------------------------------*/
    UdpSocket server_side_audio_rtp("server side audio rtp");
    server_side_audio_rtp.init({
        {"src_addr", "127.0.0.1"},
        {"src_port", "10000"},
        {"dst_addr", "127.0.0.1"},
        {"dst_port", "0"},
    });
    UdpSocket client_side_audio_rtp("client side audio rtp");
    client_side_audio_rtp.init({
        {"src_addr", "0.0.0.0"},
        {"src_port", "30000"},
        {"dst_addr", "192.168.1.17"},
        {"dst_port", "30000"},
    });
    server_side_audio_rtp.registerQueue(Msg::RAW, client_side_audio_rtp.getQueue());
    client_side_audio_rtp.registerQueue(Msg::RAW, server_side_audio_rtp.getQueue());

    UdpSocket server_side_audio_rtcp("server side audio rtcp");
    server_side_audio_rtcp.init({
        {"src_addr", "127.0.0.1"},
        {"src_port", "10001"},
        {"dst_addr", "127.0.0.1"},
        {"dst_port", "0"},
    });
    UdpSocket client_side_audio_rtcp("client side audio rtcp");
    client_side_audio_rtcp.init({
        {"src_addr", "0.0.0.0"},
        {"src_port", "30001"},
        {"dst_addr", "192.168.1.17"},
        {"dst_port", "30001"},
    });
    server_side_audio_rtcp.registerQueue(Msg::RAW, client_side_audio_rtcp.getQueue());
    client_side_audio_rtcp.registerQueue(Msg::RAW, server_side_audio_rtcp.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * input chain
    ------------------------------------------------------------------------------------------------------------------*/
    UdpSocket server_side_input_stream("server side udp inputs");
    server_side_input_stream.init({
        {"src_addr", "127.0.0.1"},
        {"src_port", "19999"},
        {"dst_addr", "127.0.0.1"},
        {"dst_port", "9999"},
    });
    UdpSocket client_side_input_stream("client side udp inputs");
    client_side_input_stream.init({
        {"src_addr", "0.0.0.0"},
        {"src_port", "29999"},
        {"dst_addr", "192.168.1.17"},
        {"dst_port", "29999"},
    });
    server_side_input_stream.registerQueue(Msg::RAW, client_side_input_stream.getQueue());
    client_side_input_stream.registerQueue(Msg::RAW, server_side_input_stream.getQueue());

    /*------------------------------------------------------------------------------------------------------------------
     * command chain
    ------------------------------------------------------------------------------------------------------------------*/

    TcpServer tcp_server("client side tcp commands");
    tcp_server.init({
        {"src_addr", "0.0.0.0"},
        {"src_port", "29999"},
    });
    MsgTypeConverter<Msg::BITRATE_REQUEST, Msg::RAW> brm_converter("bitrate request message converter");
    brm_converter.init({});
    TcpClient tcp_client("server side tcp commands");
    tcp_client.init({
        {"src_addr", "127.0.0.1"},
        {"src_port", "19999"},
        {"dst_addr", "127.0.0.1"},
        {"dst_port", "9999"},
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
    //generator.start();
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
    //generator.stop();
    scream.stop();

    logger::log(logger::INFO, "all done");
    return 0;
}
