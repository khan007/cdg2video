cdg2video is a command line tool for converting karaoke CDG+MP3/OGG/FLAC files to a video.

**How to compile**

Compiling and installing from the source code goes as follows : (replacing X.Y by the correct version number)

tar -xvzf cdg2video-X.Y.tar.gz<br>
cd cdg2video-X.Y<br>
cmake .<br>
make<br>
sudo make install<br>

<b>Compiling and installing from SVN</b>
<pre><code>svn checkout http://cdg2video.googlecode.com/svn/trunk/ cdg2video-svn<br>
cd cdg2video-svn<br>
cmake .<br>
make<br>
sudo make install<br>
</code></pre>
<b>Requirements</b>

- ffmpeg shall be installed (libavcodec, libavformat, libavutil, libswscale, libswresample are required as shared libraries)<br>
- libzip - support for zipped cdg files.<br>
- cmake version 2.6 or newer<br>
- for ubuntu/kubuntu the following packages are required: libavcodec-dev, libavformat-dev, libavutil-dev, libswscale-dev, libswresample-dev and libzip-dev<br>

<br><br>
<img src='http://cdg2video.googlecode.com/files/cdg2video.png'>