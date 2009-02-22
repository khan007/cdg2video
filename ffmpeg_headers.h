#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

extern "C" {
#ifdef OLD_FFMPEG_INCLUDES
  #include "ffmpeg/avcodec.h"
  #include "ffmpeg/avformat.h"
  #include "ffmpeg/swscale.h"
  #include "ffmpeg/fifo.h"
#else
  #include "libavcodec/avcodec.h"
  #include "libavformat/avformat.h"
  #include "libswscale/swscale.h"
  #include "libavutil/fifo.h"
#endif
}



