#include "record_desktop_dshow.h"

#include "error_define.h"
#include "log_helper.h"


namespace am {

	record_desktop_dshow::record_desktop_dshow()
	{
		av_register_all();
		avdevice_register_all();

		_fmt_ctx = NULL;
		_input_fmt = NULL;
		_codec_ctx = NULL;
		_codec = NULL;

		_stream_index = -1;
		_data_type = RECORD_DESKTOP_DATA_TYPES::AT_DESKTOP_RGBA;
	}


	record_desktop_dshow::~record_desktop_dshow()
	{
		stop();
		clean_up();
	}

	int record_desktop_dshow::init(const RECORD_DESKTOP_RECT & rect, const int fps)
	{
		int error = AE_NO;
		if (_inited == true) {
			return error;
		}

		_fps = fps;
		_rect = rect;

		char buff_video_size[50] = { 0 };
		sprintf_s(buff_video_size, 50, "%dx%d", rect.right - rect.left, rect.bottom - rect.top);

		AVDictionary *options = NULL;
		av_dict_set_int(&options, "framerate", fps, AV_DICT_MATCH_CASE);
		av_dict_set_int(&options, "offset_x", rect.left, AV_DICT_MATCH_CASE);
		av_dict_set_int(&options, "offset_y", rect.top, AV_DICT_MATCH_CASE);
		av_dict_set(&options, "video_size", buff_video_size, AV_DICT_MATCH_CASE);
		av_dict_set_int(&options, "draw_mouse", 1, AV_DICT_MATCH_CASE);

		int ret = 0;
		do {
			_fmt_ctx = avformat_alloc_context();
			_input_fmt = av_find_input_format("dshow");

			ret = avformat_open_input(&_fmt_ctx, "video=screen-capture-recorder", _input_fmt, NULL);
			if (ret != 0) {
				error = AE_FFMPEG_OPEN_INPUT_FAILED;
				break;
			}

			ret = avformat_find_stream_info(_fmt_ctx, NULL);
			if (ret < 0) {
				error = AE_FFMPEG_FIND_STREAM_FAILED;
				break;
			}

			int stream_index = -1;
			for (int i = 0; i < _fmt_ctx->nb_streams; i++) {
				if (_fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
					stream_index = i;
					break;
				}
			}

			if (stream_index == -1) {
				error = AE_FFMPEG_FIND_STREAM_FAILED;
				break;
			}

			_stream_index = stream_index;
			_codec_ctx = _fmt_ctx->streams[stream_index]->codec;
			_codec = avcodec_find_decoder(_codec_ctx->codec_id);
			if (_codec == NULL) {
				error = AE_FFMPEG_FIND_DECODER_FAILED;
				break;
			}

			ret = avcodec_open2(_codec_ctx, _codec, NULL);
			if (ret != 0) {
				error = AE_FFMPEG_OPEN_CODEC_FAILED;
				break;
			}

			_inited = true;
		} while (0);

		if (error != AE_NO) {
			al_debug("%s,error: %d %ld", err2str(error), ret, GetLastError());
			clean_up();
		}

		av_dict_free(&options);

		return error;
	}

	int record_desktop_dshow::start()
	{
		if (_running == true) {
			al_warn("record desktop gdi is already running");
			return AE_NO;
		}

		if (_inited == false) {
			return AE_NEED_INIT;
		}


		_running = true;
		_thread = std::thread(std::bind(&record_desktop_dshow::record_func, this));

		return AE_NO;
	}

	int record_desktop_dshow::pause()
	{
		return 0;
	}

	int record_desktop_dshow::resume()
	{
		return 0;
	}

	int record_desktop_dshow::stop()
	{
		_running = false;
		if (_thread.joinable())
			_thread.join();

		return AE_NO;
	}

	const AVRational & record_desktop_dshow::get_time_base()
	{
		if (_inited && _fmt_ctx && _stream_index != -1) {
			return _fmt_ctx->streams[_stream_index]->time_base;
		}
		else {
			return{ 1,90000 };
		}
	}

	int64_t record_desktop_dshow::get_start_time()
	{
		return _fmt_ctx->streams[_stream_index]->start_time;
	}

	AVPixelFormat record_desktop_dshow::get_pixel_fmt()
	{
		return _fmt_ctx->streams[_stream_index]->codec->pix_fmt;
	}

	void record_desktop_dshow::clean_up()
	{
		if (_codec_ctx)
			avcodec_close(_codec_ctx);

		if (_fmt_ctx)
			avformat_close_input(&_fmt_ctx);

		_fmt_ctx = NULL;
		_input_fmt = NULL;
		_codec_ctx = NULL;
		_codec = NULL;

		_stream_index = -1;
		_inited = false;
	}

	void record_desktop_dshow::record_func()
	{
		AVPacket *packet = (AVPacket*)av_malloc(sizeof(AVPacket));
		AVFrame *frame = av_frame_alloc();

		int ret = 0;

		int got_pic = 0;
		while (_running == true) {
			ret = av_read_frame(_fmt_ctx, packet);

			if (ret < 0) {
				if (_on_error) _on_error(AE_FFMPEG_READ_FRAME_FAILED);

				al_fatal("read frame failed:%d", ret);
				break;
			}

			if (packet->stream_index == _stream_index) {

				ret = avcodec_decode_video2(_codec_ctx, frame, &got_pic, packet);
				if (ret < 0) {
					if (_on_error) _on_error(AE_FFMPEG_DECODE_FRAME_FAILED);
					al_fatal("decode desktop frame failed");
					break;
				}

				if (got_pic) {
					if (_on_data) _on_data(frame);
				}
			}

			av_free_packet(packet);
		}

		av_free(frame);
	}

}