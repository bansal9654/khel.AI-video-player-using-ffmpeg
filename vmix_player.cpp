
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
}

#include <opencv2/opencv.hpp>

using namespace std;

struct FFPlayer {
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *dec_ctx = nullptr;
    int video_stream_idx = -1;
    AVStream *video_stream = nullptr;
    SwsContext *sws_ctx = nullptr;
    int sws_src_w = -1;
    int sws_src_h = -1;
    AVPixelFormat sws_src_fmt = AV_PIX_FMT_NONE;
    double fps = 0.0;
    AVRational avg_frame_rate{0,1};
    int64_t current_target_ts = 0;
    int64_t last_shown_pts = AV_NOPTS_VALUE;
    ~FFPlayer() {
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (dec_ctx) avcodec_free_context(&dec_ctx);
        if (fmt_ctx) avformat_close_input(&fmt_ctx);
    }
};

static void print_error(const string &msg, int err) {
    char buf[1024] = {0};
    av_strerror(err, buf, sizeof(buf));
    cerr << msg << " : " << buf << '\n';
}

static cv::Mat avframe_to_cvmat(AVFrame *frame, FFPlayer &p) {
    const int width = frame->width;
    const int height = frame->height;
    const AVPixelFormat src_fmt = (AVPixelFormat)frame->format;
    const AVPixelFormat dst_pix_fmt = AV_PIX_FMT_BGR24;

    if (!p.sws_ctx || p.sws_src_w != width || p.sws_src_h != height || p.sws_src_fmt != src_fmt) {
        if (p.sws_ctx) {
            sws_freeContext(p.sws_ctx);
            p.sws_ctx = nullptr;
        }
        p.sws_ctx = sws_getContext(width, height, src_fmt, width, height, dst_pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
        p.sws_src_w = width;
        p.sws_src_h = height;
        p.sws_src_fmt = src_fmt;
    }

    int dst_linesize[4] = {0};
    const int dst_buf_size = av_image_get_buffer_size(dst_pix_fmt, width, height, 1);
    vector<uint8_t> dstbuf(dst_buf_size);
    uint8_t *dst_data[4] = { nullptr, nullptr, nullptr, nullptr };
    av_image_fill_arrays(dst_data, dst_linesize, dstbuf.data(), dst_pix_fmt, width, height, 1);

    sws_scale(p.sws_ctx, frame->data, frame->linesize, 0, height, dst_data, dst_linesize);

    cv::Mat img(height, width, CV_8UC3, dstbuf.data(), dst_linesize[0]);
    return img.clone();
}

static int64_t frame_number_to_stream_ts(int64_t frame_number, AVStream *st) {
    AVRational afr = st->avg_frame_rate.num != 0 ? st->avg_frame_rate : st->r_frame_rate;
    if (afr.num == 0 || afr.den == 0) afr = {25,1};
    AVRational frame_time = av_inv_q(afr);
    return av_rescale_q(frame_number, frame_time, st->time_base);
}

static int64_t pts_to_frame_number(int64_t pts, AVStream *st) {
    AVRational afr = st->avg_frame_rate.num != 0 ? st->avg_frame_rate : st->r_frame_rate;
    if (afr.num == 0 || afr.den == 0) afr = {25,1};
    AVRational frame_time = av_inv_q(afr);
    return av_rescale_q(pts, st->time_base, frame_time);
}

struct AVFrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct AVPacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };

static unique_ptr<AVFrame, AVFrameDeleter> seek_and_decode_frame(FFPlayer &p, int64_t target_frame_number) {
    if (!p.fmt_ctx || !p.dec_ctx || !p.video_stream) return nullptr;

    const int64_t target_ts = frame_number_to_stream_ts(target_frame_number, p.video_stream);
    int ret = av_seek_frame(p.fmt_ctx, p.video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        print_error("av_seek_frame failed", ret);
        return nullptr;
    }

    avcodec_flush_buffers(p.dec_ctx);

    unique_ptr<AVPacket, AVPacketDeleter> packet(av_packet_alloc());
    unique_ptr<AVFrame, AVFrameDeleter> frame(av_frame_alloc());
    unique_ptr<AVFrame, AVFrameDeleter> out_frame(nullptr);

    bool got = false;
    while (!got) {
        ret = av_read_frame(p.fmt_ctx, packet.get());
        if (ret < 0) break;
        if (packet->stream_index != p.video_stream_idx) {
            av_packet_unref(packet.get());
            continue;
        }
        ret = avcodec_send_packet(p.dec_ctx, packet.get());
        av_packet_unref(packet.get());
        if (ret < 0) continue;
        while (ret >= 0) {
            ret = avcodec_receive_frame(p.dec_ctx, frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) { print_error("Error while decoding", ret); break; }
            int64_t pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
            if (pts == AV_NOPTS_VALUE) pts = p.last_shown_pts + 1;
            if (pts >= target_ts) {
                out_frame.reset(av_frame_clone(frame.get()));
                got = true;
                p.last_shown_pts = pts;
                av_frame_unref(frame.get());
                break;
            }
            av_frame_unref(frame.get());
        }
    }
    return out_frame;
}

static unique_ptr<AVFrame, AVFrameDeleter> decode_next_frame(FFPlayer &p) {
    if (!p.fmt_ctx || !p.dec_ctx) return nullptr;

    unique_ptr<AVPacket, AVPacketDeleter> packet(av_packet_alloc());
    unique_ptr<AVFrame, AVFrameDeleter> frame(av_frame_alloc());

    int ret = 0;
    while (true) {
        ret = av_read_frame(p.fmt_ctx, packet.get());
        if (ret < 0) break;
        if (packet->stream_index != p.video_stream_idx) {
            av_packet_unref(packet.get());
            continue;
        }
        ret = avcodec_send_packet(p.dec_ctx, packet.get());
        av_packet_unref(packet.get());
        if (ret < 0) continue;
        while (ret >= 0) {
            ret = avcodec_receive_frame(p.dec_ctx, frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) { print_error("Error while decoding", ret); break; }
            int64_t pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
            if (pts == AV_NOPTS_VALUE) pts = p.last_shown_pts + 1;
            unique_ptr<AVFrame, AVFrameDeleter> out_frame(av_frame_clone(frame.get()));
            p.last_shown_pts = pts;
            av_frame_unref(frame.get());
            return out_frame;
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input.avi>\n";
        return -1;
    }
    string input_filename = argv[1];

    av_log_set_level(AV_LOG_ERROR);

    FFPlayer player;

    int ret = avformat_open_input(&player.fmt_ctx, input_filename.c_str(), nullptr, nullptr);
    if (ret < 0) { print_error("Could not open input", ret); return -1; }

    ret = avformat_find_stream_info(player.fmt_ctx, nullptr);
    if (ret < 0) { print_error("Failed to retrieve stream info", ret); return -1; }

    for (unsigned i = 0; i < player.fmt_ctx->nb_streams; ++i) {
        if (player.fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            player.video_stream_idx = static_cast<int>(i);
            player.video_stream = player.fmt_ctx->streams[i];
            break;
        }
    }
    if (player.video_stream_idx < 0) { cerr << "No video stream found\n"; return -1; }

    AVCodecParameters *codecpar = player.video_stream->codecpar;
    const AVCodec *dec = avcodec_find_decoder(codecpar->codec_id);
    if (!dec) { cerr << "Decoder not found for codec id " << codecpar->codec_id << '\n'; return -1; }

    player.dec_ctx = avcodec_alloc_context3(dec);
    if (!player.dec_ctx) { cerr << "Failed to allocate codec context\n"; return -1; }

    ret = avcodec_parameters_to_context(player.dec_ctx, codecpar);
    if (ret < 0) { print_error("avcodec_parameters_to_context failed", ret); return -1; }

    ret = avcodec_open2(player.dec_ctx, dec, nullptr);
    if (ret < 0) { print_error("Failed to open codec", ret); return -1; }

    AVRational afr = player.video_stream->avg_frame_rate.num != 0 ? player.video_stream->avg_frame_rate : player.video_stream->r_frame_rate;
    if (afr.num == 0 || afr.den == 0) afr = {25,1};
    player.avg_frame_rate = afr;
    player.fps = av_q2d(afr);

    player.last_shown_pts = AV_NOPTS_VALUE;
    int64_t current_frame = 0;

    unique_ptr<AVFrame, AVFrameDeleter> frame = seek_and_decode_frame(player, current_frame);
    if (!frame) { cerr << "Could not decode first frame\n"; return -1; }

    cv::Mat img = avframe_to_cvmat(frame.get(), player);
    string window_name = "vMix AVI Player (q to quit)";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    cv::imshow(window_name, img);

    bool playing = false;
    bool should_quit = false;
    const int delay_ms = static_cast<int>(round(1000.0 / max(1.0, player.fps)));
    current_frame = pts_to_frame_number(player.last_shown_pts, player.video_stream);

    while (!should_quit) {
        int key = cv::waitKey(playing ? delay_ms : 0);
        if (key == -1 && playing) {
            unique_ptr<AVFrame, AVFrameDeleter> nf = decode_next_frame(player);
            if (!nf) {
                cout << "End of file reached\n";
                playing = false;
                continue;
            }
            cv::Mat next_img = avframe_to_cvmat(nf.get(), player);
            current_frame = pts_to_frame_number(player.last_shown_pts, player.video_stream);
            cv::imshow(window_name, next_img);
            continue;
        }

        char c = static_cast<char>(key & 0xFF);
        if (key == 27 || c == 'q') { should_quit = true; break; }
        else if (c == ' ') { playing = !playing; cout << (playing ? "Play\n" : "Pause\n"); }
        else if (c == 'n' || key == 83) {
            int64_t target = current_frame + 1;
            unique_ptr<AVFrame, AVFrameDeleter> nf = seek_and_decode_frame(player, target);
            if (!nf) cout << "Could not decode next frame (maybe EOF)\n";
            else {
                cv::Mat next_img = avframe_to_cvmat(nf.get(), player);
                current_frame = pts_to_frame_number(player.last_shown_pts, player.video_stream);
                cv::imshow(window_name, next_img);
            }
            playing = false;
        }
        else if (c == 'b' || key == 81) {
            int64_t target = (current_frame > 0) ? (current_frame - 1) : 0;
            unique_ptr<AVFrame, AVFrameDeleter> bf = seek_and_decode_frame(player, target);
            if (!bf) cout << "Could not decode backward frame\n";
            else {
                cv::Mat back_img = avframe_to_cvmat(bf.get(), player);
                current_frame = pts_to_frame_number(player.last_shown_pts, player.video_stream);
                cv::imshow(window_name, back_img);
            }
            playing = false;
        }
        else if (c == 's') { playing = true; cout << "Play\n"; }
        else if (c == 'p') { playing = false; cout << "Pause\n"; }
    }

    cv::destroyAllWindows();
    return 0;
}
