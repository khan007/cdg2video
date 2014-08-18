#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

extern "C" {
  #include "libavcodec/avcodec.h"
  #include "libavformat/avformat.h"
  #include "libswscale/swscale.h"
  #include "libavutil/fifo.h"
  #include "libavutil/mathematics.h"
  #include "libavutil/channel_layout.h"
  #include "libavutil/frame.h"
  #include "libavutil/imgutils.h"
  #include "libswresample/swresample.h"
  #include "libavutil/audio_fifo.h"

}



