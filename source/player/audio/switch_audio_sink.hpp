class AudioSink {
public:
  bool open(int sampleRate, int channels);
  void push(const uint8_t* pcm, size_t size);
  void pause(bool value);
  void close();
};