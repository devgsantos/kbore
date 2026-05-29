avformat_network_init();
avformat_open_input(...);
avformat_find_stream_info(...);
av_find_best_stream(...);

avcodec_find_decoder(...);
avcodec_alloc_context3(...);
avcodec_parameters_to_context(...);

// aqui entra a criação do hw device/hw frames
// av_hwdevice_ctx_create(...)

avcodec_open2(...);

while (...) {
  av_read_frame(...);
  avcodec_send_packet(...);
  avcodec_receive_frame(...);
}